#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <chrono>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <windows.h>
#include <winhttp.h>
#include "lepus/utils/media_analyzer.hpp"
#include "lepus/utils/logger.hpp"

namespace lepus::ntqq {

class MojoPipeClient;
using PipeClient = MojoPipeClient;
using MediaFileInfo = ::lepus::media::MediaInfo;

class ProtoWriter {
public:
    static void write_varint(std::vector<uint8_t>& buf, uint64_t val) {
        while (val >= 0x80) {
            buf.push_back((uint8_t)((val & 0x7F) | 0x80));
            val >>= 7;
        }
        buf.push_back((uint8_t)val);
    }

    static void write_tag(std::vector<uint8_t>& buf, uint32_t tag, uint32_t wire_type) {
        write_varint(buf, ((uint64_t)tag << 3) | wire_type);
    }

    static void write_uint32(std::vector<uint8_t>& buf, uint32_t tag, uint32_t val) {
        if (val != 0) {
            write_tag(buf, tag, 0);
            write_varint(buf, val);
        }
    }

    static void write_uint64(std::vector<uint8_t>& buf, uint32_t tag, uint64_t val) {
        if (val != 0) {
            write_tag(buf, tag, 0);
            write_varint(buf, val);
        }
    }

    static void write_int32(std::vector<uint8_t>& buf, uint32_t tag, int32_t val) {
        if (val != 0) {
            write_tag(buf, tag, 0);
            write_varint(buf, (uint64_t)val);
        }
    }

    static void write_string(std::vector<uint8_t>& buf, uint32_t tag, const std::string& str) {
        if (!str.empty()) {
            write_tag(buf, tag, 2);
            write_varint(buf, str.size());
            buf.insert(buf.end(), str.begin(), str.end());
        }
    }

    static void write_bytes(std::vector<uint8_t>& buf, uint32_t tag, const std::vector<uint8_t>& data) {
        if (!data.empty()) {
            write_tag(buf, tag, 2);
            write_varint(buf, data.size());
            buf.insert(buf.end(), data.begin(), data.end());
        }
    }

    static void write_sub(std::vector<uint8_t>& buf, uint32_t tag, const std::vector<uint8_t>& sub) {
        write_bytes(buf, tag, sub);
    }
};

class ProtoReader {
public:
    const uint8_t* p;
    const uint8_t* end;

    ProtoReader(const uint8_t* start, const uint8_t* stop) : p(start), end(stop) {}
    ProtoReader(const std::vector<uint8_t>& buf) : p(buf.data()), end(buf.data() + buf.size()) {}

    bool has_more() const { return p < end; }

    bool read_varint(uint64_t& val) {
        val = 0;
        int shift = 0;
        while (p < end) {
            uint8_t b = *p++;
            val |= (uint64_t)(b & 0x7F) << shift;
            if ((b & 0x80) == 0) return true;
            shift += 7;
            if (shift >= 64) return false;
        }
        return false;
    }

    bool read_tag(uint32_t& tag, uint32_t& wire) {
        uint64_t tw = 0;
        if (!read_varint(tw)) return false;
        tag = (uint32_t)(tw >> 3);
        wire = (uint32_t)(tw & 7);
        return true;
    }

    bool read_bytes(std::vector<uint8_t>& bytes_val) {
        uint64_t len = 0;
        if (!read_varint(len)) return false;
        if (p + len > end) return false;
        bytes_val.assign(p, p + len);
        p += len;
        return true;
    }

    bool read_string(std::string& str) {
        uint64_t len = 0;
        if (!read_varint(len)) return false;
        if (p + len > end) return false;
        str.assign((const char*)p, len);
        p += len;
        return true;
    }

    bool skip_field(uint32_t wire) {
        if (wire == 0) {
            uint64_t dummy;
            return read_varint(dummy);
        } else if (wire == 1) {
            if (p + 8 <= end) { p += 8; return true; }
            return false;
        } else if (wire == 2) {
            uint64_t len = 0;
            if (!read_varint(len)) return false;
            if (p + len <= end) { p += len; return true; }
            return false;
        } else if (wire == 5) {
            if (p + 4 <= end) { p += 4; return true; }
            return false;
        }
        return false;
    }
};

class HighwayClient {
public:
    static bool upload_highway_data(
        const std::string& host,
        uint32_t port,
        uint64_t uin,
        uint32_t command_id,
        const std::vector<uint8_t>& ticket,
        const std::vector<uint8_t>& ext_info,
        const std::vector<uint8_t>& file_data)
    {
        if (host.empty() || port == 0) {
            LEPUS_LOG_WARN("[Highway] No server host/port provided");
            return false;
        }

        // Build Highway HTTP request
        // URL: /cgi-bin/httpconn?htcmd=0x6FF0087&uin=UIN
        HINTERNET hSession = WinHttpOpen(L"HighwayClient/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) return false;

        std::wstring whost(host.begin(), host.end());
        HINTERNET hConnect = WinHttpConnect(hSession, whost.c_str(), (INTERNET_PORT)port, 0);
        if (!hConnect) {
            WinHttpCloseHandle(hSession);
            return false;
        }

        std::wstring path = L"/cgi-bin/httpconn?htcmd=0x6FF0087&uin=" + std::to_wstring(uin);
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", path.c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
        if (!hRequest) {
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return false;
        }

        // Compute MD5 of file
        std::string md5_hex, sha1_hex; media::MediaAnalyzer::calc_hashes(file_data, md5_hex, sha1_hex);
        auto hex_to_bytes = [](const std::string& hex) {
            std::vector<uint8_t> bytes;
            for (size_t i = 0; i < hex.length(); i += 2) {
                uint8_t byte = (uint8_t)strtol(hex.substr(i, 2).c_str(), nullptr, 16);
                bytes.push_back(byte);
            }
            return bytes;
        };
        std::vector<uint8_t> file_md5 = hex_to_bytes(md5_hex);

        // Build ReqDataHighwayHead (Media.ReqDataHighwayHead)
        std::vector<uint8_t> msgBaseHead;
        ProtoWriter::write_uint32(msgBaseHead, 1, 1); // version = 1
        ProtoWriter::write_string(msgBaseHead, 2, std::to_string(uin));
        ProtoWriter::write_string(msgBaseHead, 3, "PicUp.DataUp");
        ProtoWriter::write_uint32(msgBaseHead, 4, 1); // seq
        ProtoWriter::write_uint32(msgBaseHead, 5, 0); // retryTimes
        ProtoWriter::write_uint32(msgBaseHead, 6, 1600001615); // appId
        ProtoWriter::write_uint32(msgBaseHead, 7, 16); // dataFlag
        ProtoWriter::write_uint32(msgBaseHead, 8, command_id);

        std::vector<uint8_t> msgSegHead;
        ProtoWriter::write_uint32(msgSegHead, 1, 0); // serviceId
        ProtoWriter::write_uint32(msgSegHead, 2, (uint32_t)file_data.size()); // filesize
        ProtoWriter::write_uint32(msgSegHead, 3, 0); // offset = 0
        ProtoWriter::write_uint32(msgSegHead, 4, (uint32_t)file_data.size()); // dataLength
        if (!ticket.empty()) ProtoWriter::write_bytes(msgSegHead, 5, ticket);
        if (!file_md5.empty()) {
            ProtoWriter::write_bytes(msgSegHead, 6, file_md5); // body md5
            ProtoWriter::write_bytes(msgSegHead, 7, file_md5); // file md5
        }

        std::vector<uint8_t> highwayHead;
        ProtoWriter::write_bytes(highwayHead, 1, msgBaseHead);
        ProtoWriter::write_bytes(highwayHead, 2, msgSegHead);
        if (!ext_info.empty()) {
            ProtoWriter::write_bytes(highwayHead, 3, ext_info);
        }
        ProtoWriter::write_uint64(highwayHead, 4, (uint64_t)std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());

        // Frame structure: 0x28 (1 byte) + head_len (4 bytes BE) + body_len (4 bytes BE) + head + body + 0x29 (1 byte)
        std::vector<uint8_t> frame;
        frame.push_back(0x28);
        uint32_t head_len = (uint32_t)highwayHead.size();
        uint32_t body_len = (uint32_t)file_data.size();
        frame.push_back((head_len >> 24) & 0xFF);
        frame.push_back((head_len >> 16) & 0xFF);
        frame.push_back((head_len >> 8) & 0xFF);
        frame.push_back(head_len & 0xFF);

        frame.push_back((body_len >> 24) & 0xFF);
        frame.push_back((body_len >> 16) & 0xFF);
        frame.push_back((body_len >> 8) & 0xFF);
        frame.push_back(body_len & 0xFF);

        frame.insert(frame.end(), highwayHead.begin(), highwayHead.end());
        frame.insert(frame.end(), file_data.begin(), file_data.end());
        frame.push_back(0x29);

        std::wstring headers = L"Content-Type: application/octet-stream\r\n";
        BOOL bResults = WinHttpSendRequest(
            hRequest,
            headers.c_str(),
            (DWORD)headers.length(),
            (LPVOID)frame.data(),
            (DWORD)frame.size(),
            (DWORD)frame.size(),
            0
        );
        bool success = false;
        if (bResults) {
            bResults = WinHttpReceiveResponse(hRequest, NULL);
            if (bResults) {
                DWORD dwStatusCode = 0;
                DWORD dwSize = sizeof(dwStatusCode);
                WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &dwStatusCode, &dwSize, WINHTTP_NO_HEADER_INDEX);
                if (dwStatusCode == 200) {
                    success = true;
                }
            }
        }

        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return success;
    }
};

struct RichMediaUploadResult {
    bool success = false;
    std::vector<uint8_t> msg_info;
    std::vector<uint8_t> ukey;
    std::vector<uint8_t> ticket;
    std::string server_ip;
    uint32_t server_port = 0;
};

class OidbMediaHelper {
public:
    static std::vector<uint8_t> wrap_oidb_base(uint32_t command, uint32_t sub_command, const std::vector<uint8_t>& body) {
        std::vector<uint8_t> oidb;
        ProtoWriter::write_uint32(oidb, 1, command);
        ProtoWriter::write_uint32(oidb, 2, sub_command);
        ProtoWriter::write_bytes(oidb, 4, body);
        return oidb;
    }

    static std::vector<uint8_t> unwrap_oidb_base(const uint8_t* p, const uint8_t* end) {
        ProtoReader reader(p, end);
        uint32_t tag = 0, wire = 0;
        while (reader.has_more()) {
            if (!reader.read_tag(tag, wire)) break;
            if (tag == 4 && wire == 2) {
                std::vector<uint8_t> body;
                if (reader.read_bytes(body)) return body;
            } else {
                reader.skip_field(wire);
            }
        }
        return {};
    }

    static std::vector<uint8_t> hex_to_bytes(const std::string& hex) {
        std::vector<uint8_t> bytes;
        for (size_t i = 0; i + 1 < hex.length(); i += 2) {
            std::string byteString = hex.substr(i, 2);
            uint8_t byte = (uint8_t)strtol(byteString.c_str(), nullptr, 16);
            bytes.push_back(byte);
        }
        return bytes;
    }

    static std::string bytes_to_hex(const std::vector<uint8_t>& bytes) {
        std::ostringstream oss;
        for (uint8_t b : bytes) {
            oss << std::hex << std::setw(2) << std::setfill('0') << (int)b;
        }
        return oss.str();
    }

    // Build NTV2RichMediaReq protobuf
    static std::vector<uint8_t> build_rich_media_upload_req(
        bool is_group,
        uint64_t peer_id,
        uint64_t bot_uin,
        const MediaFileInfo& file_info,
        bool is_ptt = false)
    {
        // Reference: tests/ntv2RichMedia.ts -> buildUploadReq & tests/proto_media.ts -> NTV2RichMediaReq
        // 1. reqHead (tag 1)
        std::vector<uint8_t> reqHead;
        {
            // common (tag 1)
            std::vector<uint8_t> common;
            ProtoWriter::write_uint32(common, 1, 1);   // requestId: 1
            ProtoWriter::write_uint32(common, 2, 100); // command: 100
            ProtoWriter::write_sub(reqHead, 1, common);

            // scene (tag 2)
            std::vector<uint8_t> scene;
            ProtoWriter::write_uint32(scene, 101, 2); // requestType: 2
            uint32_t businessType = is_ptt ? 3 : 1;   // 1 for image, 3 for voice
            ProtoWriter::write_uint32(scene, 102, businessType);
            ProtoWriter::write_uint32(scene, 200, is_group ? 2 : 1); // sceneType: 2 for group, 1 for c2c

            if (is_group) {
                std::vector<uint8_t> group;
                ProtoWriter::write_uint32(group, 1, static_cast<uint32_t>(peer_id));
                ProtoWriter::write_sub(scene, 202, group);
            } else {
                std::vector<uint8_t> c2c;
                ProtoWriter::write_uint32(c2c, 1, 2); // accountType: 2
                std::string target_uid = GroupSessionCache::instance().get_or_create_uid(peer_id);
                ProtoWriter::write_string(c2c, 2, target_uid);
                ProtoWriter::write_sub(scene, 201, c2c);
            }
            ProtoWriter::write_sub(reqHead, 2, scene);

            // client (tag 3)
            std::vector<uint8_t> client;
            ProtoWriter::write_uint32(client, 1, 2); // agentType: 2
            ProtoWriter::write_sub(reqHead, 3, client);
        }

        // 2. upload
        std::vector<uint8_t> upload;
        {
            // uploadInfo (tag 1, repeated)
            std::vector<uint8_t> uploadInfo;
            {
                // fileInfo (tag 1)
                std::vector<uint8_t> fileInfo;
                ProtoWriter::write_uint32(fileInfo, 1, static_cast<uint32_t>(file_info.file_size));
                ProtoWriter::write_string(fileInfo, 2, file_info.md5_hex);
                ProtoWriter::write_string(fileInfo, 3, file_info.sha1_hex);

                // fileName (tag 4): md5_hex + '.' + ext
                std::string ext = is_ptt ? "amr" : "jpg";
                if (!file_info.path.empty()) {
                    size_t dot = file_info.path.rfind('.');
                    if (dot != std::string::npos) {
                        ext = file_info.path.substr(dot + 1);
                    }
                }
                std::string file_name = file_info.md5_hex + "." + ext;
                ProtoWriter::write_string(fileInfo, 4, file_name);

                // fileType (tag 5)
                std::vector<uint8_t> fileType;
                if (is_ptt) {
                    ProtoWriter::write_uint32(fileType, 1, 3);    // ptt
                    ProtoWriter::write_uint32(fileType, 4, 1000); // pttFormat
                } else {
                    ProtoWriter::write_uint32(fileType, 1, 1);    // pic
                    uint32_t picFormat = (ext == "gif" || ext == "GIF") ? 2000 : 1000;
                    ProtoWriter::write_uint32(fileType, 2, picFormat);
                }
                ProtoWriter::write_sub(fileInfo, 5, fileType);

                // width, height, time
                if (is_ptt) {
                    ProtoWriter::write_uint32(fileInfo, 6, 0);
                    ProtoWriter::write_uint32(fileInfo, 7, 0);
                    ProtoWriter::write_uint32(fileInfo, 8, file_info.duration > 0 ? file_info.duration : 1);
                } else {
                    ProtoWriter::write_uint32(fileInfo, 6, file_info.width > 0 ? file_info.width : 100);
                    ProtoWriter::write_uint32(fileInfo, 7, file_info.height > 0 ? file_info.height : 100);
                    ProtoWriter::write_uint32(fileInfo, 8, 0);
                }
                ProtoWriter::write_uint32(fileInfo, 9, 1); // original: 1

                ProtoWriter::write_sub(uploadInfo, 1, fileInfo);
                ProtoWriter::write_uint32(uploadInfo, 2, 0); // subFileType: 0
            }
            ProtoWriter::write_sub(upload, 1, uploadInfo);

            // tryFastUploadCompleted (tag 2)
            ProtoWriter::write_uint32(upload, 2, 1); // true
            // srvSendMsg (tag 3)
            ProtoWriter::write_uint32(upload, 3, 0); // false
            // clientRandomId (tag 4)
            uint32_t clientRandomId = static_cast<uint32_t>(rand() & 0x7FFFFFFF);
            ProtoWriter::write_uint32(upload, 4, clientRandomId);
            // compatQMsgSceneType (tag 5)
            ProtoWriter::write_uint32(upload, 5, is_group ? 2 : 1);

            // extBizInfo (tag 6)
            std::vector<uint8_t> extBizInfo;
            if (is_ptt) {
                std::vector<uint8_t> ptt;
                ProtoWriter::write_uint32(ptt, 1, 0); // bizType
                ProtoWriter::write_sub(extBizInfo, 2, ptt);
            } else {
                std::vector<uint8_t> pic;
                ProtoWriter::write_uint32(pic, 1, 0); // bizType
                ProtoWriter::ProtoWriter::write_string(pic, 2, u8"[??]"); // summary
                ProtoWriter::write_sub(extBizInfo, 1, pic);
            }
            ProtoWriter::write_sub(upload, 6, extBizInfo);

            // clientSeq (tag 7)
            ProtoWriter::write_uint32(upload, 7, 10);
            // noNeedCompatMsg (tag 8)
            ProtoWriter::write_uint32(upload, 8, 0); // false
        }

        // 3. NTV2RichMediaReq root
        std::vector<uint8_t> req;
        ProtoWriter::write_sub(req, 1, reqHead);
        ProtoWriter::write_sub(req, 2, upload);

        return req;
    }

    static RichMediaUploadResult parse_rich_media_resp(const std::vector<uint8_t>& pb) {
        RichMediaUploadResult res;
        ProtoReader r(pb);
        while (r.has_more()) {
            uint32_t tag = 0, wire = 0;
            if (!r.read_tag(tag, wire)) break;

            if (tag == 2 && wire == 2) { // upload
                std::vector<uint8_t> upload_pb;
                r.read_bytes(upload_pb);
                ProtoReader ur(upload_pb);
                while (ur.has_more()) {
                    uint32_t utag = 0, uwire = 0;
                    if (!ur.read_tag(utag, uwire)) break;

                    if (utag == 1) { // uKey (string)
                        if (uwire == 2) {
                            std::string ukey_str;
                            ur.read_string(ukey_str);
                            res.ukey = std::vector<uint8_t>(ukey_str.begin(), ukey_str.end());
                        } else {
                            ur.skip_field(uwire);
                        }
                    } else if (utag == 3 && uwire == 2) { // ipv4s (repeated IPv4)
                        std::vector<uint8_t> ip_pb;
                        ur.read_bytes(ip_pb);
                        ProtoReader ip_r(ip_pb);
                        uint32_t outIP = 0, outPort = 0;
                        while (ip_r.has_more()) {
                            uint32_t iptag = 0, ipwire = 0;
                            if (!ip_r.read_tag(iptag, ipwire)) break;
                            if (iptag == 1) {
                                uint64_t v = 0;
                                ip_r.read_varint(v);
                                outIP = static_cast<uint32_t>(v);
                            } else if (iptag == 2) {
                                uint64_t v = 0;
                                ip_r.read_varint(v);
                                outPort = static_cast<uint32_t>(v);
                            } else {
                                ip_r.skip_field(ipwire);
                            }
                        }
                        if (res.server_ip.empty() && outIP != 0) {
                            // Convert uint32 outIP (network order) to string
                            res.server_ip = std::to_string(outIP & 0xFF) + "." +
                                            std::to_string((outIP >> 8) & 0xFF) + "." +
                                            std::to_string((outIP >> 16) & 0xFF) + "." +
                                            std::to_string((outIP >> 24) & 0xFF);
                            res.server_port = outPort ? outPort : 80;
                            LEPUS_LOG_INFO("[OIDB] Rich media server IP: {}:{}", res.server_ip, res.server_port);
                        }
                    } else if (utag == 6 && uwire == 2) { // msgInfo (raw bytes)
                        ur.read_bytes(res.msg_info);
                        res.success = true;
                        LEPUS_LOG_INFO("[OIDB] Got msgInfo bytes (size={})", res.msg_info.size());
                    } else {
                        ur.skip_field(uwire);
                    }
                }
            } else {
                r.skip_field(wire);
            }
        }
        return res;
    }

    template <typename PipeClientType>
    static std::vector<uint8_t> upload_rich_media(
        PipeClientType& pipe,
        bool is_group,
        uint64_t peer_id,
        uint64_t bot_uin,
        const MediaFileInfo& file_info,
        bool is_ptt
    ) {
                if (!is_group) {
            std::string known_uid = GroupSessionCache::instance().get_user_uid(peer_id);
            if (known_uid.empty()) {
                // Fetch UID via 0xfe1_2
                std::vector<uint8_t> fe1_req;
                ProtoWriter::write_uint32(fe1_req, 1, (uint32_t)peer_id); // uin
                ProtoWriter::write_uint32(fe1_req, 2, 1);
                
                auto wrapped_fe1 = wrap_oidb_base(0xfe1, 2, fe1_req);
                std::vector<uint8_t> fe1_reply;
                if (pipe.send_sso_with_reply("OidbSvcTrpcTcp.0xfe1_2", wrapped_fe1, fe1_reply, 3000)) {
                    auto unwrapped = unwrap_oidb_base(fe1_reply.data(), fe1_reply.data() + fe1_reply.size());
                    // Parse UID if present in properties
                    // Any string starting with u_
                    std::string unwrapped_str((const char*)unwrapped.data(), unwrapped.size());
                    auto pos = unwrapped_str.find("u_");
                    if (pos != std::string::npos && pos + 24 <= unwrapped_str.size()) {
                        std::string parsed_uid = unwrapped_str.substr(pos, 24);
                        GroupSessionCache::instance().register_user_uid(peer_id, parsed_uid);
                        LEPUS_LOG_INFO("[OIDB] Discovered UID for {}: {}", peer_id, parsed_uid);
                    }
                }
            }
        }
        auto req = build_rich_media_upload_req(is_group, peer_id, bot_uin, file_info, is_ptt);
        uint32_t cmd_id = is_ptt ? (is_group ? 0x126e : 0x126d) : (is_group ? 0x11c4 : 0x11c5);
        
        char cmd_buf[64];
        snprintf(cmd_buf, sizeof(cmd_buf), "OidbSvcTrpcTcp.0x%x_100", cmd_id);
        std::string sso_cmd = cmd_buf;

        auto oidb_req = wrap_oidb_base(cmd_id, 100, req);
        std::vector<uint8_t> reply_body;
        if (!pipe.send_sso_with_reply(sso_cmd, oidb_req, reply_body, 5000)) {
            LEPUS_LOG_ERROR("[OIDB] Failed to send pre-upload RPC for {}", sso_cmd);
            return {};
        }

        LEPUS_LOG_INFO("[OIDB] Pre-upload raw reply_body bytes: {}", reply_body.size());
        {
            std::string hex;
            for (size_t i = 0; i < std::min<size_t>(reply_body.size(), 128); ++i) {
                char b[4]; snprintf(b, sizeof(b), "%02x", reply_body[i]); hex += b;
            }
            LEPUS_LOG_INFO("[OIDB] reply_body hex (first 128): {}", hex);
        }
        auto parsed_oidb = unwrap_oidb_base(reply_body.data(), reply_body.data() + reply_body.size());
        if (parsed_oidb.empty()) {
            LEPUS_LOG_WARN("[OIDB] unwrap_oidb_base returned empty, trying raw reply_body directly");
            parsed_oidb = reply_body;
        }

        auto upload_res = parse_rich_media_resp(parsed_oidb);
        if (!upload_res.success || upload_res.msg_info.empty()) {
            LEPUS_LOG_ERROR("[OIDB] Pre-upload returned no msg_info");
            return {};
        }

        // If fast upload succeeded (no ukey), msg_info is already ready!
        if (upload_res.ukey.empty()) {
            LEPUS_LOG_INFO("[OIDB] Fast upload succeeded for {}", file_info.path);
            return upload_res.msg_info;
        }

        // Otherwise, Highway HTTP block upload is needed
        std::wstring wpath;
        int len = MultiByteToWideChar(CP_UTF8, 0, file_info.path.c_str(), -1, NULL, 0);
        if (len > 0) {
            wpath.resize(len - 1);
            MultiByteToWideChar(CP_UTF8, 0, file_info.path.c_str(), -1, &wpath[0], len);
        } else {
            wpath = std::wstring(file_info.path.begin(), file_info.path.end());
        }

        HANDLE hFile = CreateFileW(wpath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) {
            LEPUS_LOG_ERROR("[OIDB] Failed to open file for highway upload: {}", file_info.path);
            return upload_res.msg_info;
        }

        DWORD fsize = GetFileSize(hFile, NULL);
        std::vector<uint8_t> file_bytes(fsize);
        DWORD bytesRead = 0;
        if (!ReadFile(hFile, file_bytes.data(), fsize, &bytesRead, NULL) || bytesRead != fsize) {
            CloseHandle(hFile);
            LEPUS_LOG_ERROR("[OIDB] Failed to read file for highway upload: {}", file_info.path);
            return upload_res.msg_info;
        }
        CloseHandle(hFile);

        uint32_t highway_cmd = is_ptt ? (is_group ? 1008 : 1007) : (is_group ? 1004 : 1003);
        if (HighwayClient::upload_highway_data(upload_res.server_ip, upload_res.server_port, bot_uin, highway_cmd, upload_res.ticket, upload_res.ukey, file_bytes)) {
            LEPUS_LOG_INFO("[OIDB] Highway upload completed successfully for {}", file_info.path);
            return upload_res.msg_info;
        }

        LEPUS_LOG_WARN("[OIDB] Highway upload failed, falling back to msg_info");
        return upload_res.msg_info;
    }
};

} // namespace lepus::ntqq
