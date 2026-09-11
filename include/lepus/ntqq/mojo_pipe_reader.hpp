#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <functional>
#include <iostream>
#include <lepus/ntqq/msg_decoder.hpp>
#include <lepus/protocol/onebot_v11.hpp>

namespace lepus::ntqq {

#pragma pack(push, 1)
struct MojoPipeHeader {
    uint32_t magic;      // 0x31504851 ("QHP1")
    uint32_t op;         // Operation opcode
    uint32_t request_id;
    int32_t  status;
    uint32_t flags;
    uint32_t cmd_len;    // Length of cmd string
    uint32_t msg_len;    // Length of msg string
    uint32_t body_len;   // Length of protobuf payload
    uint64_t val0;
};
#pragma pack(pop)

using MessageCallback = std::function<void(const ParsedMessage& msg, const nlohmann::json& ob11_event)>;

class MojoPipeReader {
public:
    static MojoPipeReader& instance() {
        static MojoPipeReader s_instance;
        return s_instance;
    }

    void set_callback(MessageCallback cb) {
        callback_ = cb;
    }

    void start() {
        if (running_) return;
        running_ = true;
        worker_thread_ = std::thread(&MojoPipeReader::run_loop, this);
    }

    void stop() {
        running_ = false;
        if (pipe_handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(pipe_handle_);
            pipe_handle_ = INVALID_HANDLE_VALUE;
        }
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
    }

private:
    MojoPipeReader() = default;
    ~MojoPipeReader() { stop(); }

    std::atomic<bool> running_{false};
    std::thread worker_thread_;
    HANDLE pipe_handle_{INVALID_HANDLE_VALUE};
    MessageCallback callback_;

    void run_loop() {
        DWORD pid = GetCurrentProcessId();
        std::string pipe_name = "\\\\.\\pipe\\mojo." + std::to_string(pid) + ".recv";

        while (running_) {
            // Wait for pipe to be available
            pipe_handle_ = CreateFileA(
                pipe_name.c_str(),
                GENERIC_READ | GENERIC_WRITE,
                0,
                NULL,
                OPEN_EXISTING,
                0,
                NULL
            );

            if (pipe_handle_ == INVALID_HANDLE_VALUE) {
                // Sleep and retry (snowluma hook may initialize shortly)
                Sleep(1000);
                continue;
            }

            // Set message mode
            DWORD mode = PIPE_READMODE_MESSAGE;
            SetNamedPipeHandleState(pipe_handle_, &mode, NULL, NULL);

            std::cout << "\033[32m[MojoPipeReader] Connected to named pipe: " << pipe_name << "\033[0m" << std::endl;

            // Read loop
            std::vector<uint8_t> buffer(65536);
            while (running_) {
                DWORD bytes_read = 0;
                BOOL success = ReadFile(
                    pipe_handle_,
                    buffer.data(),
                    (DWORD)buffer.size(),
                    &bytes_read,
                    NULL
                );

                if (!success || bytes_read == 0) {
                    DWORD err = GetLastError();
                    if (err == ERROR_MORE_DATA) {
                        // Grow buffer and read rest
                        size_t prev_size = buffer.size();
                        buffer.resize(prev_size * 2);
                        DWORD additional_read = 0;
                        ReadFile(pipe_handle_, buffer.data() + prev_size, (DWORD)prev_size, &additional_read, NULL);
                        bytes_read += additional_read;
                    } else {
                        // Pipe broken or disconnected
                        std::cout << "[MojoPipeReader] Pipe disconnected (err: " << err << "), reconnecting..." << std::endl;
                        CloseHandle(pipe_handle_);
                        pipe_handle_ = INVALID_HANDLE_VALUE;
                        break;
                    }
                }

                handle_packet(buffer.data(), bytes_read);
            }
        }
    }

    void handle_packet(const uint8_t* data, size_t size) {
        if (size < sizeof(MojoPipeHeader)) return;

        const auto* hdr = reinterpret_cast<const MojoPipeHeader*>(data);
        if (hdr->magic != 0x31504851) {
            return;
        }

        size_t offset = sizeof(MojoPipeHeader);
        if (offset + hdr->cmd_len > size) return;
        std::string cmd((const char*)data + offset, hdr->cmd_len);
        offset += hdr->cmd_len;

        if (offset + hdr->msg_len > size) return;
        std::string msg_desc((const char*)data + offset, hdr->msg_len);
        offset += hdr->msg_len;

        if (offset + hdr->body_len > size) return;
        const uint8_t* body = data + offset;
        size_t body_len = hdr->body_len;

        // Filter OlPush MsgPush
        if (cmd == "trpc.msg.olpush.OlPushService.MsgPush") {
            ParsedMessage parsed;
            if (ProtobufReader::parse_msg_push(body, body_len, parsed)) {
                // Print to console
                if (parsed.is_group) {
                    std::cout << "\033[36m[NTQQ Msg] [Group " << parsed.peer_uin << "] "
                              << parsed.sender_uin << " (" << parsed.sender_uid << "): "
                              << parsed.text << "\033[0m" << std::endl;
                } else {
                    std::cout << "\033[35m[NTQQ Msg] [Private] "
                              << parsed.sender_uin << ": "
                              << parsed.text << "\033[0m" << std::endl;
                }

                // Construct OneBot v11 event
                nlohmann::json ob11_event;
                int64_t self_id = (parsed.self_uin > 0) ? (int64_t)parsed.self_uin : 10001;

                if (parsed.is_group) {
                    ob11_event = protocol::OneBotEventHelper::make_group_message_event(
                        self_id,
                        parsed.peer_uin,
                        parsed.sender_uin,
                        (int32_t)parsed.msg_seq,
                        parsed.text
                    );
                    if (!parsed.message_elements.empty()) {
                        ob11_event["message"] = parsed.message_elements;
                    }
                    if (!parsed.sender_nick.empty()) {
                        ob11_event["sender"]["nickname"] = parsed.sender_nick;
                    }
                    ob11_event["sender"]["user_id"] = parsed.sender_uin;
                    std::string displayName = !parsed.sender_card.empty() ? parsed.sender_card : 
                                              (!parsed.sender_nick.empty() ? parsed.sender_nick : "User_" + std::to_string(parsed.sender_uin));
                    ob11_event["sender"]["card"] = parsed.sender_card;
                    ob11_event["sender"]["nickname"] = displayName;
                    ob11_event["sender"]["role"] = parsed.sender_role;
                } else {
                    ob11_event = protocol::OneBotEventHelper::make_private_message_event(
                        self_id,
                        parsed.sender_uin,
                        (int32_t)parsed.msg_seq,
                        parsed.text
                    );
                    if (!parsed.message_elements.empty()) {
                        ob11_event["message"] = parsed.message_elements;
                    }
                }

                if (callback_) {
                    callback_(parsed, ob11_event);
                }
            }
        }
    }
};

} // namespace lepus::ntqq
