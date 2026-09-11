#pragma once

#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <unordered_set>
#include <functional>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <lepus/network/websocket_codec.hpp>
#include <lepus/utils/logger.hpp>

namespace lepus::network {

class WebSocketServer {
public:
    using MessageHandler = std::function<void(SOCKET client_sock, const std::string& message)>;
    using ConnectionHandler = std::function<void(SOCKET client_sock)>;

    WebSocketServer() : running_(false), server_fd_(INVALID_SOCKET), port_(0) {}
    ~WebSocketServer() { stop(); }

    bool start(int port) {
        port_ = port;
        server_fd_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (server_fd_ == INVALID_SOCKET) {
            LEPUS_ERROR("[WSServer] Failed to create socket: {}", WSAGetLastError());
            return false;
        }

        int opt = 1;
        setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons((u_short)port);

        if (bind(server_fd_, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
            LEPUS_ERROR("[WSServer] Failed to bind port {}: {}", port, WSAGetLastError());
            closesocket(server_fd_);
            server_fd_ = INVALID_SOCKET;
            return false;
        }

        if (listen(server_fd_, SOMAXCONN) == SOCKET_ERROR) {
            LEPUS_ERROR("[WSServer] Failed to listen: {}", WSAGetLastError());
            closesocket(server_fd_);
            server_fd_ = INVALID_SOCKET;
            return false;
        }

        running_ = true;
        accept_thread_ = std::thread(&WebSocketServer::accept_loop, this);
        LEPUS_INFO("[WSServer] Forward WebSocket Server listening on port {}", port);
        return true;
    }

    void stop() {
        if (!running_) return;
        running_ = false;
        if (server_fd_ != INVALID_SOCKET) {
            closesocket(server_fd_);
            server_fd_ = INVALID_SOCKET;
        }
        if (accept_thread_.joinable()) {
            accept_thread_.join();
        }

        std::lock_guard<std::mutex> lock(clients_mutex_);
        for (SOCKET s : clients_) {
            closesocket(s);
        }
        clients_.clear();
    }

    void on_message(MessageHandler handler) { on_message_ = handler; }
    void on_connection(ConnectionHandler handler) { on_connect_ = handler; }
    void on_connect(ConnectionHandler handler) { on_connect_ = handler; }
    void on_disconnection(ConnectionHandler handler) { on_disconnect_ = handler; }
    void on_disconnect(ConnectionHandler handler) { on_disconnect_ = handler; }

    void broadcast(const std::string& message) {
        std::string frame = WebSocketCodec::build_frame(message, WSOpCode::Text, false);
        std::lock_guard<std::mutex> lock(clients_mutex_);
        std::vector<SOCKET> dead_sockets;
        for (SOCKET s : clients_) {
            int sent = send(s, frame.data(), (int)frame.size(), 0);
            if (sent == SOCKET_ERROR) {
                dead_sockets.push_back(s);
            }
        }
        for (SOCKET s : dead_sockets) {
            closesocket(s);
            clients_.erase(s);
            if (on_disconnect_) on_disconnect_(s);
        }
    }

    void send_to(SOCKET s, const std::string& message) {
        std::string frame = WebSocketCodec::build_frame(message, WSOpCode::Text, false);
        send(s, frame.data(), (int)frame.size(), 0);
    }

    size_t client_count() {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        return clients_.size();
    }

private:
    std::atomic<bool> running_;
    SOCKET server_fd_;
    int port_;
    std::thread accept_thread_;
    std::mutex clients_mutex_;
    std::unordered_set<SOCKET> clients_;

    MessageHandler on_message_;
    ConnectionHandler on_connect_;
    ConnectionHandler on_disconnect_;

    void accept_loop() {
        while (running_) {
            sockaddr_in client_addr{};
            int addr_len = sizeof(client_addr);
            SOCKET client_sock = accept(server_fd_, (sockaddr*)&client_addr, &addr_len);
            if (client_sock == INVALID_SOCKET) {
                if (!running_) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }

            std::thread(&WebSocketServer::handle_client, this, client_sock).detach();
        }
    }

    void handle_client(SOCKET s) {
        char buf[4096];
        int len = recv(s, buf, sizeof(buf) - 1, 0);
        if (len <= 0) {
            closesocket(s);
            return;
        }
        buf[len] = '\0';
        std::string request(buf, len);

        // Check for WebSocket upgrade
        size_t key_pos = request.find("Sec-WebSocket-Key: ");
        if (key_pos == std::string::npos) {
            closesocket(s);
            return;
        }
        key_pos += 19;
        size_t end_pos = request.find("\r\n", key_pos);
        if (end_pos == std::string::npos) {
            closesocket(s);
            return;
        }
        std::string client_key = request.substr(key_pos, end_pos - key_pos);
        std::string accept_key = WebSocketCodec::compute_accept_key(client_key);

        std::string response = 
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Accept: " + accept_key + "\r\n\r\n";

        send(s, response.data(), (int)response.size(), 0);

        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            clients_.insert(s);
        }
        LEPUS_INFO("[WSServer] New WebSocket client connected! (Total active: {})", client_count());
        if (on_connect_) on_connect_(s);

        std::string buffer;
        while (running_) {
            char chunk[4096];
            int r = recv(s, chunk, sizeof(chunk), 0);
            if (r <= 0) break;
            buffer.append(chunk, r);

            while (!buffer.empty()) {
                WSFrame frame;
                size_t consumed = 0;
                if (WebSocketCodec::parse_frame(buffer, frame, consumed)) {
                    buffer.erase(0, consumed);

                    if (frame.opcode == WSOpCode::Ping) {
                        // Immediate RFC 6455 Pong reply to prevent keepalive timeout
                        std::string pong = WebSocketCodec::build_pong(frame.payload);
                        send(s, pong.data(), (int)pong.size(), 0);
                        LEPUS_TRACE("[WSServer] Ping received, Pong replied to client.");
                    } else if (frame.opcode == WSOpCode::Close) {
                        std::string close_frame = WebSocketCodec::build_close();
                        send(s, close_frame.data(), (int)close_frame.size(), 0);
                        goto client_exit;
                    } else if (frame.opcode == WSOpCode::Text || frame.opcode == WSOpCode::Binary) {
                        if (on_message_) {
                            on_message_(s, frame.payload);
                        }
                    }
                } else {
                    break;
                }
            }
        }

    client_exit:
        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            clients_.erase(s);
        }
        closesocket(s);
        LEPUS_INFO("[WSServer] WebSocket client disconnected. (Remaining: {})", client_count());
        if (on_disconnect_) on_disconnect_(s);
    }
};

} // namespace lepus::network
