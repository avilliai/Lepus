#pragma once

#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <atomic>
#include <chrono>
#include <httplib/httplib.h>
#include <nlohmann/json.hpp>
#include <lepus/utils/logger.hpp>
#include <lepus/ntqq/ntqq_engine.hpp>
#include <lepus/protocol/onebot_v11.hpp>
#include <lepus/protocol/onebot_dispatcher.hpp>
#include <lepus/protocol/agent_protocol.hpp>
#include <lepus/core/conversation_memory.hpp>
#include <lepus/core/agent_tool_registry.hpp>
#include <lepus/network/websocket_server.hpp>
#include <lepus/network/websocket_client.hpp>

namespace lepus::network {

using json = nlohmann::json;

class ProtocolServer {
public:
    ProtocolServer() : running_(false), http_port_(3000), ws_port_(3001) {}

    ~ProtocolServer() {
        stop();
    }

    bool start(int http_port = 3000, int ws_port = 3001) {
        // ?? NTQQ ????????? WebSocket ????
        ntqq::NTQQEngine::instance().set_on_message([this](const json& event_json) {
            std::string text = event_json.dump();
            LEPUS_LOG_INFO("[Server] Broadcasting Event to WebSocket clients: {}", text.substr(0, std::min<size_t>(text.size(), 120)));
            ws_server_.broadcast(text);
        });

        http_port_ = http_port;
        ws_port_ = ws_port;
        running_ = true;

        setup_routes();

        // 1. Start Forward WebSocket Server
        ws_server_.start(ws_port_);
        ws_server_.on_connect([this](SOCKET client_sock) {
            // Send OneBot lifecycle connect meta_event
            auto ev = protocol::OneBotEventHelper::make_lifecycle_event(10001, "connect");
            ws_server_.send_to(client_sock, ev.dump());
        });

        ws_server_.on_message([this](SOCKET client_sock, const std::string& msg) {
            handle_ws_request(client_sock, msg);
        });

        // 2. Start HTTP REST Server in background thread
        http_thread_ = std::thread([this]() {
            LEPUS_INFO("[ProtocolServer] Starting HTTP Server on 0.0.0.0:{}", http_port_);
            svr_.listen("0.0.0.0", http_port_);
        });

        // 3. Start Heartbeat Thread (OneBot meta_event: heartbeat)
        heartbeat_thread_ = std::thread(&ProtocolServer::heartbeat_loop, this);

        return true;
    }

    void enable_reverse_websocket(const std::string& host, int port, const std::string& path = "/") {
        reverse_ws_.set_target(host, port, path);
        reverse_ws_.on_connect([this]() {
            auto ev = protocol::OneBotEventHelper::make_lifecycle_event(10001, "connect");
            reverse_ws_.send_message(ev.dump());
        });
        reverse_ws_.on_message([this](const std::string& msg) {
            try {
                auto req = json::parse(msg);
                std::string action = req.value("action", "");
                json params = req.value("params", json::object());
                std::string echo = req.value("echo", "");

                auto resp = protocol::OneBotDispatcher::instance().handle_action(action, params);
                if (!echo.empty()) resp["echo"] = echo;
                reverse_ws_.send_message(resp.dump());
            } catch (const std::exception& e) {
                LEPUS_ERROR("[ReverseWS] Failed to parse message: {}", e.what());
            }
        });
        reverse_ws_.start();
    }

    void broadcast_event(const json& event_json) {
        std::string payload = event_json.dump();
        ws_server_.broadcast(payload);
        if (reverse_ws_.is_connected()) {
            reverse_ws_.send_message(payload);
        }
    }

    void stop() {
        if (!running_) return;
        running_ = false;

        reverse_ws_.stop();
        ws_server_.stop();
        svr_.stop();

        if (http_thread_.joinable()) http_thread_.join();
        if (heartbeat_thread_.joinable()) heartbeat_thread_.join();

        LEPUS_INFO("[ProtocolServer] Stopped Protocol Gateway");
    }

private:
    std::atomic<bool> running_;
    int http_port_;
    int ws_port_;
    httplib::Server svr_;
    WebSocketServer ws_server_;
    WebSocketClient reverse_ws_;
    std::thread http_thread_;
    std::thread heartbeat_thread_;

    void heartbeat_loop() {
        while (running_) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            if (!running_) break;
            auto hb = protocol::OneBotEventHelper::make_heartbeat_event(10001, 5000);
            broadcast_event(hb);
        }
    }

    void handle_ws_request(SOCKET client_sock, const std::string& msg) {
        try {
            auto req = json::parse(msg);
            std::string action = req.value("action", "");
            json params = req.value("params", json::object());
            std::string echo = req.value("echo", "");

            LEPUS_INFO("\033[33m[WS Request]\033[0m Action: \033[32m{}\033[0m Params: {}", action, params.dump());

            auto resp = protocol::OneBotDispatcher::instance().handle_action(action, params);
            LEPUS_INFO("\033[34m[WS Response]\033[0m Action: {} RetCode: {}", action, resp.value("retcode", -1));
            if (!echo.empty()) resp["echo"] = echo;
            ws_server_.send_to(client_sock, resp.dump());
        } catch (const std::exception& e) {
            LEPUS_ERROR("[WS Server] Error parsing json request: {}", e.what());
        }
    }

    void setup_routes() {
        // ==================== Health Check ====================
        svr_.Get("/status", [](const httplib::Request& req, httplib::Response& res) {
            json resp;
            resp["status"] = "ok";
            resp["version"] = "Lepus-1.0.0-Modern";
            resp["framework"] = "Lepus C++20 Core";
            resp["ntqq_connected"] = true;
            resp["websocket_port"] = 3001;
            res.set_content(resp.dump(), "application/json");
        });

        // ==================== OneBot v11 HTTP API Gateway ====================
        // Direct Post handler for any OneBot v11 action (/action_name)
        auto onebot_handler = [](const httplib::Request& req, httplib::Response& res) {
            std::string action = req.path;
            if (action.rfind("/", 0) == 0) {
                action = action.substr(1);
            }

            json params = json::object();
            if (!req.body.empty()) {
                try {
                    params = json::parse(req.body);
                } catch (...) {}
            }
            for (auto& param : req.params) {
                params[param.first] = param.second;
            }

            auto resp = protocol::OneBotDispatcher::instance().handle_action(action, params);
            res.set_content(resp.dump(), "application/json");
        };

        // Static routes & Catch-all route for OneBot v11 standard actions
        static const std::vector<std::string> standard_apis = {
            "/send_private_msg", "/send_group_msg", "/send_msg", "/delete_msg",
            "/get_msg", "/get_forward_msg", "/send_like", "/set_group_kick",
            "/set_group_ban", "/set_group_anonymous_ban", "/set_group_whole_ban",
            "/set_group_admin", "/set_group_anonymous", "/set_group_card",
            "/set_group_name", "/set_group_leave", "/set_group_special_title",
            "/set_friend_add_request", "/set_group_add_request", "/get_login_info",
            "/get_stranger_info", "/get_friend_list", "/get_group_info",
            "/get_group_list", "/get_group_member_info", "/get_group_member_list",
            "/get_group_honor_info", "/get_cookies", "/get_csrf_token",
            "/get_credentials", "/get_record", "/get_image", "/can_send_image",
            "/can_send_record", "/get_status", "/get_version_info",
            "/set_restart", "/clean_cache"
        };

        for (const auto& endpoint : standard_apis) {
            svr_.Post(endpoint, onebot_handler);
            svr_.Get(endpoint, onebot_handler);
        }

        // ==================== Modern Agent API Gateway ====================
        // 1. Agent Tools Manifest (/api/agent/tools)
        svr_.Get("/api/agent/tools", [](const httplib::Request& req, httplib::Response& res) {
            auto& reg = core::AgentToolRegistry::instance();
            json resp;
            resp["status"] = "ok";
            resp["tools"] = reg.get_tools_manifest();
            res.set_content(resp.dump(), "application/json");
        });

        // 2. Direct Agent Tool Call (/api/agent/tool/call)
        svr_.Post("/api/agent/tool/call", [](const httplib::Request& req, httplib::Response& res) {
            try {
                auto body = json::parse(req.body);
                std::string tool_name = body.value("name", "");
                json args = body.value("arguments", json::object());

                auto& reg = core::AgentToolRegistry::instance();
                auto exec_result = reg.execute_tool(tool_name, args);

                json resp;
                resp["status"] = "ok";
                resp["tool_name"] = tool_name;
                resp["result"] = exec_result;
                res.set_content(resp.dump(), "application/json");
            } catch (const std::exception& e) {
                json err;
                err["status"] = "error";
                err["message"] = e.what();
                res.set_content(err.dump(), "application/json");
            }
        });

        // 3. Agent Context Query (/api/agent/context)
        svr_.Get("/api/agent/context", [](const httplib::Request& req, httplib::Response& res) {
            std::string conv_id = req.get_param_value("id");
            size_t max_rounds = 10;
            if (req.has_param("limit")) {
                max_rounds = std::stoul(req.get_param_value("limit"));
            }

            auto& mem = core::ConversationMemory::instance();
            auto history = mem.get_history(conv_id, max_rounds);

            json resp;
            resp["status"] = "ok";
            resp["conversation_id"] = conv_id;
            resp["history"] = history;
            res.set_content(resp.dump(), "application/json");
        });
    }
};

} // namespace lepus::network