#include <random>
#include "lepus/utils/media_analyzer.hpp"
#include "lepus/ntqq/msg_decoder.hpp"
#include "lepus/ntqq/oidb_media.hpp"
namespace lepus::ntqq {

using json = nlohmann::json;

#pragma pack(push, 1)
struct MojoPipeControlHeader {
    uint32_t magic = 0x31504851; // 'QHP1'
    uint16_t version = 1;
    uint16_t op = 2;             // 2 = sendRequest
    uint32_t request_id = 0;
    int32_t  status = 0;
    uint32_t flags = 1;          // 1 = WantReply
    uint32_t cmd_len = 0;
    uint32_t msg_len = 0;
    uint32_t body_len = 0;
    uint64_t value0 = 0;
};
#pragma pack(pop)

class MojoPipeClient {
public:
    static MojoPipeClient& instance() {
        static MojoPipeClient s_inst;
        return s_inst;
    }

    static void write_varint(std::vector<uint8_t>& buf, uint64_t val) {
        while (val >= 0x80) {
            buf.push_back((uint8_t)((val & 0x7F) | 0x80));
            val >>= 7;
        }
        buf.push_back((uint8_t)val);
    }

    static void write_tag(std::vector<uint8_t>& buf, uint32_t tag, uint32_t wire) {
        write_varint(buf, ((uint64_t)tag << 3) | wire);
    }

    static void write_string(std::vector<uint8_t>& buf, uint32_t tag, const std::string& str) {
        write_tag(buf, tag, 2);
        write_varint(buf, str.size());
        buf.insert(buf.end(), str.begin(), str.end());
    }

    static void write_bytes(std::vector<uint8_t>& buf, uint32_t tag, const std::vector<uint8_t>& b) {
        write_tag(buf, tag, 2);
        write_varint(buf, b.size());
        buf.insert(buf.end(), b.begin(), b.end());
    }

    static void write_varint_field(std::vector<uint8_t>& buf, uint32_t tag, uint64_t val) {
        write_tag(buf, tag, 0);
        write_varint(buf, val);
    }

    // Build rich text element list from JSON elements or raw string
    static std::string clean_file_path(std::string path) {
        if (path.rfind("file://", 0) == 0) {
            path = path.substr(7);
        }
        if (path.size() > 2 && path[0] == '/' && path[2] == ':') {
            path = path.substr(1);
        }
        // URL percent-decoding (e.g. %20 -> space)
        std::string res;
        for (size_t i = 0; i < path.size(); ++i) {
            if (path[i] == '%' && i + 2 < path.size()) {
                auto hex_val = [](char c) -> int {
                    if (c >= '0' && c <= '9') return c - '0';
                    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                    return -1;
                };
                int hi = hex_val(path[i+1]);
                int lo = hex_val(path[i+2]);
                if (hi != -1 && lo != -1) {
                    res.push_back((char)((hi << 4) | lo));
                    i += 2;
                    continue;
                }
            }
            res.push_back(path[i]);
        }
        return res;
    }

    std::vector<uint8_t> build_elements_pb(bool is_group, uint64_t peer_id, const json& elements) {
        std::vector<uint8_t> richText;

        if (elements.is_string()) {
            std::string str = elements.get<std::string>();
            if (str.empty()) str = " ";
            std::vector<uint8_t> textElem;
            write_string(textElem, 1, str);
            std::vector<uint8_t> elem;
            write_bytes(elem, 1, textElem);
            write_bytes(richText, 2, elem);
            return richText;
        }

        if (elements.is_array()) {
            for (const auto& item : elements) {
                std::string type = item.value("type", "");
                int elemType = item.value("elementType", 0);

                if (type == "text" || elemType == 1) {
                    std::string text = "";
                    if (item.contains("data") && item["data"].contains("text")) {
                        text = item["data"]["text"].get<std::string>();
                    } else if (item.contains("textElement") && item["textElement"].contains("content")) {
                        text = item["textElement"]["content"].get<std::string>();
                    }

                    if (!text.empty()) {
                        std::vector<uint8_t> textElem;
                        write_string(textElem, 1, text);
                        std::vector<uint8_t> elem;
                        write_bytes(elem, 1, textElem);
                        write_bytes(richText, 2, elem);
                    }
                } else if (type == "at") {
                    std::string qq_str = "";
                    if (item.contains("data") && item["data"].contains("qq")) {
                        if (item["data"]["qq"].is_number()) {
                            qq_str = std::to_string(item["data"]["qq"].get<uint64_t>());
                        } else {
                            qq_str = item["data"]["qq"].get<std::string>();
                        }
                    }
                    std::string at_text = "@" + qq_str + " ";
                    std::vector<uint8_t> textElem;
                    write_string(textElem, 1, at_text);
                    std::vector<uint8_t> elem;
                    write_bytes(elem, 1, textElem);
                    write_bytes(richText, 2, elem);
                } else if (type == "image" || elemType == 2) {
                    std::string file_str = "";
                    if (item.contains("data") && item["data"].contains("file")) {
                        file_str = item["data"]["file"].get<std::string>();
                    }
                    file_str = clean_file_path(file_str);
                    LEPUS_LOG_INFO("[MojoPipeClient] clean_file_path result: {}", file_str);

                    media::MediaInfo minfo; media::MediaAnalyzer::analyze_file(file_str, minfo);
                    LEPUS_LOG_INFO("[MojoPipeClient] analyze_file result: size={}, w={}, h={}", minfo.file_size, minfo.width, minfo.height);
                    if (minfo.file_size > 0) {
                        uint64_t bot_uin = GroupSessionCache::instance().bot_uin();
                        if (bot_uin == 0) bot_uin = 3377428814ULL;
                        std::vector<uint8_t> msg_info = OidbMediaHelper::upload_rich_media(*this, is_group, peer_id, bot_uin, minfo, false);
                        if (!msg_info.empty()) {
                            // commonElem: serviceType=48, pbElem=msg_info, businessType=(is_group ? 20 : 10)
                            std::vector<uint8_t> common_elem;
                            write_varint_field(common_elem, 1, 48);
                            write_bytes(common_elem, 2, msg_info);
                            write_varint_field(common_elem, 3, is_group ? 20 : 10);

                            std::vector<uint8_t> elem;
                            write_bytes(elem, 53, common_elem);
                            write_bytes(richText, 2, elem);
                            LEPUS_LOG_INFO("[MojoPipeClient] Successfully encoded commonElem for image: {}", file_str);
                            continue;
                        }
                    }
                    // Fallback to text if upload failed
                    std::string img_desc = "[图片]";
                    std::vector<uint8_t> textElem;
                    write_string(textElem, 1, img_desc);
                    std::vector<uint8_t> elem;
                    write_bytes(elem, 1, textElem);
                    write_bytes(richText, 2, elem);
                } else if (type == "record" || type == "voice" || elemType == 3) {
                    std::string file_str = "";
                    if (item.contains("data") && item["data"].contains("file")) {
                        file_str = item["data"]["file"].get<std::string>();
                    }
                    file_str = clean_file_path(file_str);

                    media::MediaInfo minfo; media::MediaAnalyzer::analyze_file(file_str, minfo);
                    if (minfo.file_size > 0) {
                        uint64_t bot_uin = GroupSessionCache::instance().bot_uin();
                        if (bot_uin == 0) bot_uin = 3377428814ULL;
                        std::vector<uint8_t> msg_info = OidbMediaHelper::upload_rich_media(*this, is_group, peer_id, bot_uin, minfo, true);
                        if (!msg_info.empty()) {
                            // commonElem: serviceType=48, pbElem=msg_info, businessType=(is_group ? 22 : 12)
                            std::vector<uint8_t> common_elem;
                            write_varint_field(common_elem, 1, 48);
                            write_bytes(common_elem, 2, msg_info);
                            write_varint_field(common_elem, 3, is_group ? 22 : 12);

                            std::vector<uint8_t> elem;
                            write_bytes(elem, 53, common_elem);
                            write_bytes(richText, 2, elem);
                            LEPUS_LOG_INFO("[MojoPipeClient] Successfully encoded commonElem for voice: {}", file_str);
                            continue;
                        }
                    }
                    // Fallback
                    std::string voice_desc = "[语音]";
                    std::vector<uint8_t> textElem;
                    write_string(textElem, 1, voice_desc);
                    std::vector<uint8_t> elem;
                    write_bytes(elem, 1, textElem);
                    write_bytes(richText, 2, elem);
                }
            }
        }

        if (richText.empty()) {
            std::vector<uint8_t> textElem;
            write_string(textElem, 1, " ");
            std::vector<uint8_t> elem;
            write_bytes(elem, 1, textElem);
            write_bytes(richText, 2, elem);
        }

        return richText;
    }

    // Build Protobuf for MessageSvc.PbSendMsg for Group
    std::vector<uint8_t> build_group_send_pb(uint64_t group_code, const json& elements) {
        std::vector<uint8_t> routingHead;
        {
            std::vector<uint8_t> grp;
            write_varint_field(grp, 1, group_code);
            write_bytes(routingHead, 2, grp);
        }

        std::vector<uint8_t> contentHead;
        {
            write_varint_field(contentHead, 1, 1); // pkgNum = 1
            write_varint_field(contentHead, 2, 0); // pkgIndex = 0
            write_varint_field(contentHead, 3, 0); // divSeq = 0
        }

        std::vector<uint8_t> msgBody;
        {
            std::vector<uint8_t> richText = build_elements_pb(true, group_code, elements);
            write_bytes(msgBody, 1, richText);
        }

        std::mt19937 rng((uint32_t)std::chrono::system_clock::now().time_since_epoch().count());
        uint32_t clientSeq = 10000 + (rng() % 89999);
        uint32_t randomVal = (uint32_t)rng();

        std::vector<uint8_t> pb;
        write_bytes(pb, 1, routingHead);
        write_bytes(pb, 2, contentHead);
        write_bytes(pb, 3, msgBody);
        write_varint_field(pb, 4, clientSeq);
        write_varint_field(pb, 5, randomVal);

        return pb;
    }

    // Build Protobuf for MessageSvc.PbSendMsg for Private (C2C)
    std::vector<uint8_t> build_private_send_pb(uint64_t user_id, const json& elements) {
        std::vector<uint8_t> routingHead;
        {
            std::vector<uint8_t> c2c;
            write_varint_field(c2c, 1, user_id);
            write_bytes(routingHead, 1, c2c);
        }

        std::vector<uint8_t> contentHead;
        {
            write_varint_field(contentHead, 1, 1);
            write_varint_field(contentHead, 2, 0);
            write_varint_field(contentHead, 3, 0);
        }

        std::vector<uint8_t> msgBody;
        {
            std::vector<uint8_t> richText = build_elements_pb(false, user_id, elements);
            write_bytes(msgBody, 1, richText);
        }

        std::mt19937 rng((uint32_t)std::chrono::system_clock::now().time_since_epoch().count());
        uint32_t clientSeq = 10000 + (rng() % 89999);
        uint32_t randomVal = (uint32_t)rng();

        std::vector<uint8_t> pb;
        write_bytes(pb, 1, routingHead);
        write_bytes(pb, 2, contentHead);
        write_bytes(pb, 3, msgBody);
        write_varint_field(pb, 4, clientSeq);
        write_varint_field(pb, 5, randomVal);

        return pb;
    }

    bool ensure_control_pipe() {
        if (hPipe_ != INVALID_HANDLE_VALUE) {
            return true;
        }

        DWORD pid = GetCurrentProcessId();
        std::string pipeName = "\\\\.\\pipe\\mojo." + std::to_string(pid) + ".control";

        for (int retry = 0; retry < 15; ++retry) {
            hPipe_ = CreateFileA(
                pipeName.c_str(),
                GENERIC_READ | GENERIC_WRITE,
                0,
                NULL,
                OPEN_EXISTING,
                0,
                NULL
            );
            if (hPipe_ != INVALID_HANDLE_VALUE) break;

            if (GetLastError() == ERROR_PIPE_BUSY) {
                WaitNamedPipeA(pipeName.c_str(), 1000);
            } else {
                Sleep(200);
            }
        }

        if (hPipe_ == INVALID_HANDLE_VALUE) {
            LEPUS_LOG_ERROR("[MojoPipeClient] Failed to open control pipe: error {}", GetLastError());
            return false;
        }

        // SnowLuma control pipe immediately pushes Hello (op=1) and LoginState (op=7)
        uint8_t init_buf[4096];
        DWORD bytesRead = 0;
        for (int i = 0; i < 2; ++i) {
            if (ReadFile(hPipe_, init_buf, sizeof(init_buf), &bytesRead, NULL)) {
                // Read hello / login state
            } else {
                break;
            }
        }

        LEPUS_LOG_INFO("[MojoPipeClient] Established persistent control pipe connection!");
        return true;
    }

    bool send_sso_with_reply(const std::string& cmd, const std::vector<uint8_t>& body, std::vector<uint8_t>& reply_body, uint32_t timeout_ms = 5000) {
        std::lock_guard<std::mutex> lock(send_mutex_);

        if (!ensure_control_pipe()) {
            return false;
        }

        uint32_t reqId = ++req_id_;

        MojoPipeControlHeader hdr;
        hdr.magic = 0x31504851; // "QHP1"
        hdr.version = 1;
        hdr.op = 2;             // 2 = sendRequest
        hdr.request_id = reqId;
        hdr.status = 0;
        hdr.flags = 1;          // 1 = WantReply
        hdr.cmd_len = (uint32_t)cmd.size();
        hdr.msg_len = 0;
        hdr.body_len = (uint32_t)body.size();
        hdr.value0 = 0;

        std::vector<uint8_t> packet;
        packet.resize(sizeof(hdr));
        std::memcpy(packet.data(), &hdr, sizeof(hdr));
        packet.insert(packet.end(), cmd.begin(), cmd.end());
        packet.insert(packet.end(), body.begin(), body.end());

        DWORD written = 0;
        BOOL ok = WriteFile(hPipe_, packet.data(), (DWORD)packet.size(), &written, NULL);
        if (!ok || written != packet.size()) {
            LEPUS_LOG_ERROR("[MojoPipeClient] WriteFile failed: error {}", GetLastError());
            CloseHandle(hPipe_);
            hPipe_ = INVALID_HANDLE_VALUE;
            return false;
        }

        // Wait for reply frame (op = 4, sendReply)
        auto start_time = std::chrono::steady_clock::now();
        while (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time).count() < timeout_ms) {
            DWORD avail = 0;
            if (PeekNamedPipe(hPipe_, NULL, 0, NULL, &avail, NULL) && avail >= sizeof(MojoPipeControlHeader)) {
                MojoPipeControlHeader in_hdr;
                DWORD read_bytes = 0;
                if (ReadFile(hPipe_, &in_hdr, sizeof(in_hdr), &read_bytes, NULL) && read_bytes == sizeof(in_hdr)) {
                    if (in_hdr.magic == 0x31504851) {
                        uint32_t total_payload = in_hdr.cmd_len + in_hdr.msg_len + in_hdr.body_len;
                        std::vector<uint8_t> payload(total_payload);
                        DWORD payload_read = 0;
                        if (total_payload > 0) {
                            ReadFile(hPipe_, payload.data(), total_payload, &payload_read, NULL);
                        }

                        if (in_hdr.op == 4 && in_hdr.request_id == reqId) { // sendReply
                            size_t body_offset = in_hdr.cmd_len + in_hdr.msg_len;
                            if (body_offset + in_hdr.body_len <= payload.size()) {
                                reply_body.assign(payload.begin() + body_offset, payload.begin() + body_offset + in_hdr.body_len);
                                return true;
                            }
                        }
                    }
                }
            }
            Sleep(10);
        }
        LEPUS_LOG_WARN("[MojoPipeClient] send_sso_with_reply timed out for cmd {}", cmd);
        return false;
    }

    bool send_sso_packet(const std::string& cmd, const std::vector<uint8_t>& body) {
        std::lock_guard<std::mutex> lock(send_mutex_);

        if (!ensure_control_pipe()) {
            return false;
        }

        uint32_t reqId = ++req_id_;

        MojoPipeControlHeader hdr;
        hdr.magic = 0x31504851; // "QHP1"
        hdr.version = 1;
        hdr.op = 2;             // 2 = sendRequest
        hdr.request_id = reqId;
        hdr.status = 0;
        hdr.flags = 1;          // 1 = WantReply
        hdr.cmd_len = (uint32_t)cmd.size();
        hdr.msg_len = 0;
        hdr.body_len = (uint32_t)body.size();
        hdr.value0 = 0;

        std::vector<uint8_t> packet;
        packet.resize(sizeof(hdr));
        std::memcpy(packet.data(), &hdr, sizeof(hdr));
        packet.insert(packet.end(), cmd.begin(), cmd.end());
        packet.insert(packet.end(), body.begin(), body.end());

        DWORD written = 0;
        BOOL ok = WriteFile(hPipe_, packet.data(), (DWORD)packet.size(), &written, NULL);
        if (!ok || written != packet.size()) {
            LEPUS_LOG_ERROR("[MojoPipeClient] WriteFile failed: error {}", GetLastError());
            CloseHandle(hPipe_);
            hPipe_ = INVALID_HANDLE_VALUE;
            return false;
        }

        // Drain the Ack / Reply from control pipe to keep pipe buffer clear
        uint8_t resp_buf[4096];
        DWORD respBytes = 0;
        if (ReadFile(hPipe_, resp_buf, sizeof(resp_buf), &respBytes, NULL)) {
            // Got Ack (op=3) or Reply (op=4)
        }

        LEPUS_LOG_INFO("[MojoPipeClient] Successfully sent {} ({} bytes) to NTQQ via control pipe!", cmd, body.size());
        return true;
    }

    bool send_group_msg(uint64_t group_code, const json& elements) {
        auto pb = build_group_send_pb(group_code, elements);
        return send_sso_packet("MessageSvc.PbSendMsg", pb);
    }

    bool send_private_msg(uint64_t user_id, const json& elements) {
        auto pb = build_private_send_pb(user_id, elements);
        return send_sso_packet("MessageSvc.PbSendMsg", pb);
    }

private:
    std::mutex send_mutex_;
    uint32_t req_id_{100};
    HANDLE hPipe_{INVALID_HANDLE_VALUE};
};

} // namespace lepus::ntqq
