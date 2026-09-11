#pragma once
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <functional>
#include <atomic>
#include <nlohmann/json.hpp>
#include <lepus/utils/logger.hpp>
#include "mojo_pipe_client.hpp"

namespace lepus::ntqq {

using json = nlohmann::json;

// NTQQ 核心通信引擎（桥接 In-Process Hook 与包装层）
class NTQQEngine {
public:
    static NTQQEngine& instance() {
        static NTQQEngine s_instance;
        return s_instance;
    }

    bool is_initialized() const {
        return initialized_.load();
    }

    void initialize(uint64_t self_uin = 10001) {
        self_uin_ = self_uin;
        initialized_.store(true);
        LEPUS_LOG_INFO("[NTQQEngine] Initialized with self_uin: {}", self_uin_);
    }

    // 从 OneBot 消息段抽取纯文本或合并内容用于日志展示
    static std::string extract_text(const json& elements) {
        if (elements.is_string()) {
            return elements.get<std::string>();
        }
        std::string result;
        if (elements.is_array()) {
            for (const auto& item : elements) {
                if (item.contains("type") && item["type"] == "text") {
                    if (item.contains("data") && item["data"].contains("text")) {
                        result += item["data"]["text"].get<std::string>();
                    }
                } else if (item.contains("type") && item["type"] == "image") {
                    result += "[图片]";
                } else if (item.contains("type") && item["type"] == "at") {
                    result += "@" + item["data"].value("qq", "") + " ";
                }
            }
        }
        return result;
    }

    // 发送私聊消息
    std::string send_private_message(uint64_t peer_uin, const json& elements) {
        std::string text_preview = extract_text(elements);
        LEPUS_LOG_INFO("[NTQQEngine] Native send_private_message to {}: {}", peer_uin, text_preview);
        MojoPipeClient::instance().send_private_msg(peer_uin, elements);
        uint64_t msg_id = ++message_seq_;
        return std::to_string(msg_id);
    }

    // 发送群聊消息
    std::string send_group_message(uint64_t group_code, const json& elements) {
        std::string text_preview = extract_text(elements);
        LEPUS_LOG_INFO("[NTQQEngine] Native send_group_message to group {}: {}", group_code, text_preview);
        MojoPipeClient::instance().send_group_msg(group_code, elements);
        uint64_t msg_id = ++message_seq_;
        return std::to_string(msg_id);
    }

    // 撤回消息
    bool recall_message(uint64_t peer_id, int chat_type, const std::string& msg_id) {
        LEPUS_LOG_INFO("[NTQQEngine] Native recall_message: chat_type={}, peer={}, msg_id={}", chat_type, peer_id, msg_id);
        return true;
    }

    uint64_t get_self_uin() const {
        return self_uin_;
    }

    using MessageCallback = std::function<void(const json&)>;

    void set_on_message(MessageCallback cb) {
        on_message_cb_ = cb;
    }

    void emit_incoming_message(const json& onebot_msg) {
        if (on_message_cb_) {
            on_message_cb_(onebot_msg);
        }
    }

private:
    NTQQEngine() : initialized_(true), self_uin_(10001), message_seq_(100000) {}

    std::atomic<bool> initialized_;
    uint64_t self_uin_;
    std::atomic<uint64_t> message_seq_;
    MessageCallback on_message_cb_;
};

} // namespace lepus::ntqq
