#pragma once

#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <functional>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <lepus/network/websocket_codec.hpp>
#include <lepus/utils/logger.hpp>

namespace lepus::network {

class WebSocketClient {
public:
    using MessageHandler = std::function<void(const std::string& message)>;
    using ConnectionHandler = std::function<void()>;

    WebSocketClient() : running_(false), sock_(INVALID_SOCKET), port_(0) {}
    ~WebSocketClient() { stop(); }

    void set_target(const std::string& host, int port, const std::string& path = "/") {
        host_ = host;
        port_ = port;
        path_ = path;
    }

    void start() {
        running_ = true;
        worker_thread_ = std::thread(&WebSocketClient::reconnect_loop, this);
    }

    void stop() {
        running_ = false;
        if (sock_ != INVALID_SOCKET) {
            closesocket(sock_);
            sock_ = INVALID_SOCKET;
        }
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
    }

    void on_message(MessageHandler handler) { on_message_ = handler; }
    void on_connect(ConnectionHandler handler) { on_connect_ = handler; }
    void on_disconnect(ConnectionHandler handler) { on_disconnect_ = handler; }

    bool send_message(const std::string& payload) {
        if (sock_ == INVALID_SOCKET) return false;
        std::string frame = WebSocketCodec::build_frame(payload, WSOpCode::Text, true); // masked client -> server
        int sent = send(sock_, frame.data(), (int)frame.size(), 0);
        return sent != SOCKET_ERROR;
    }

    bool is_connected() const {
        return sock_ != INVALID_SOCKET;
    }

private:
    std::atomic<bool> running_;
    SOCKET sock_;
    std::string host_;
    int port_;
    std::string path_;
    std::thread worker_thread_;

    MessageHandler on_message_;
    ConnectionHandler on_connect_;
    ConnectionHandler on_disconnect_;

    void reconnect_loop() {
        while (running_) {
            if (connect_server()) {
                if (on_connect_) on_connect_();
                read_loop();
                if (on_disconnect_) on_disconnect_();
            }
            if (running_) {
                std::this_thread::sleep_for(std::chrono::seconds(3));
            }
        }
    }

    bool connect_server() {
        sock_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock_ == INVALID_SOCKET) return false;

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons((u_short)port_);
        inet_pton(AF_INET, host_.c_str(), &addr.sin_addr);

        if (connect(sock_, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
            closesocket(sock_);
            sock_ = INVALID_SOCKET;
            return false;
        }

        // Send handshake
        std::string key = WebSocketCodec::base64_encode((const unsigned char*)"lepus_random_key", 16);
        std::ostringstream ss;
        ss << "GET " << path_ << " HTTP/1.1\r\n"
           << "Host: " << host_ << ":" << port_ << "\r\n"
           << "Upgrade: websocket\r\n"
           << "Connection: Upgrade\r\n"
           << "Sec-WebSocket-Key: " << key << "\r\n"
           << "Sec-WebSocket-Version: 13\r\n\r\n";
        
        std::string handshake = ss.str();
        send(sock_, handshake.data(), (int)handshake.size(), 0);

        char buf[2048];
        int len = recv(sock_, buf, sizeof(buf) - 1, 0);
        if (len <= 0) {
            closesocket(sock_);
            sock_ = INVALID_SOCKET;
            return false;
        }
        buf[len] = '\0';
        std::string response(buf, len);
        if (response.find("101 Switching Protocols") == std::string::npos) {
            closesocket(sock_);
            sock_ = INVALID_SOCKET;
            return false;
        }

        LEPUS_INFO("[WSClient] Reverse WebSocket successfully connected to {}:{}{}", host_, port_, path_);
        return true;
    }

    void read_loop() {
        std::string buffer;
        while (running_ && sock_ != INVALID_SOCKET) {
            char chunk[2048];
            int r = recv(sock_, chunk, sizeof(chunk), 0);
            if (r <= 0) break;
            buffer.append(chunk, r);

            while (!buffer.empty()) {
                WSFrame frame;
                size_t consumed = 0;
                if (WebSocketCodec::parse_frame(buffer, frame, consumed)) {
                    buffer.erase(0, consumed);
                    if (frame.opcode == WSOpCode::Ping) {
                        std::string pong = WebSocketCodec::build_frame(frame.payload, WSOpCode::Pong, true);
                        send(sock_, pong.data(), (int)pong.size(), 0);
                    } else if (frame.opcode == WSOpCode::Text || frame.opcode == WSOpCode::Binary) {
                        if (on_message_) {
                            on_message_(frame.payload);
                        }
                    } else if (frame.opcode == WSOpCode::Close) {
                        break;
                    }
                } else {
                    break;
                }
            }
        }
        if (sock_ != INVALID_SOCKET) {
            closesocket(sock_);
            sock_ = INVALID_SOCKET;
        }
    }
};

} // namespace lepus::network
