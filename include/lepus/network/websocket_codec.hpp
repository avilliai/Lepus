#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <winsock2.h>
#include <wincrypt.h>
#pragma comment(lib, "crypt32.lib")

namespace lepus::network {

enum class WSOpCode : uint8_t {
    Continuation = 0x0,
    Text = 0x1,
    Binary = 0x2,
    Close = 0x8,
    Ping = 0x9,
    Pong = 0xA
};

struct WSFrame {
    bool fin{true};
    WSOpCode opcode{WSOpCode::Text};
    std::string payload;
};

class WebSocketCodec {
public:
    static std::string base64_encode(const unsigned char* data, size_t len) {
        static const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string res;
        res.reserve(((len + 2) / 3) * 4);
        for (size_t i = 0; i < len; i += 3) {
            unsigned int val = (data[i] << 16) | ((i + 1 < len ? data[i + 1] : 0) << 8) | (i + 2 < len ? data[i + 2] : 0);
            res.push_back(chars[(val >> 18) & 0x3F]);
            res.push_back(chars[(val >> 12) & 0x3F]);
            res.push_back((i + 1 < len) ? chars[(val >> 6) & 0x3F] : '=');
            res.push_back((i + 2 < len) ? chars[val & 0x3F] : '=');
        }
        return res;
    }

    static std::string compute_accept_key(const std::string& sec_key) {
        std::string combined = sec_key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
        HCRYPTPROV hProv = 0;
        HCRYPTHASH hHash = 0;
        unsigned char sha1_res[20] = {0};
        DWORD hash_len = 20;

        if (CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
            if (CryptCreateHash(hProv, CALG_SHA1, 0, 0, &hHash)) {
                CryptHashData(hHash, (const BYTE*)combined.data(), (DWORD)combined.size(), 0);
                CryptGetHashParam(hHash, HP_HASHVAL, sha1_res, &hash_len, 0);
                CryptDestroyHash(hHash);
            }
            CryptReleaseContext(hProv, 0);
        }
        return base64_encode(sha1_res, 20);
    }

    static std::string build_frame(const std::string& payload, WSOpCode opcode = WSOpCode::Text, bool mask = false) {
        std::string frame;
        unsigned char b1 = 0x80 | static_cast<uint8_t>(opcode);
        frame.push_back((char)b1);

        size_t len = payload.size();
        unsigned char mask_bit = mask ? 0x80 : 0x00;

        if (len <= 125) {
            frame.push_back((char)(mask_bit | (unsigned char)len));
        } else if (len <= 65535) {
            frame.push_back((char)(mask_bit | 126));
            frame.push_back((char)((len >> 8) & 0xFF));
            frame.push_back((char)(len & 0xFF));
        } else {
            frame.push_back((char)(mask_bit | 127));
            for (int i = 7; i >= 0; --i) {
                frame.push_back((char)((len >> (8 * i)) & 0xFF));
            }
        }

        if (mask) {
            unsigned char mask_key[4] = { 0x12, 0x34, 0x56, 0x78 };
            frame.append((char*)mask_key, 4);
            std::string masked = payload;
            for (size_t i = 0; i < len; ++i) {
                masked[i] ^= mask_key[i % 4];
            }
            frame.append(masked);
        } else {
            frame.append(payload);
        }
        return frame;
    }

    static std::string build_pong(const std::string& ping_payload) {
        return build_frame(ping_payload, WSOpCode::Pong, false);
    }

    static std::string build_close() {
        return build_frame("", WSOpCode::Close, false);
    }

    static bool parse_frame(const std::string& buffer, WSFrame& out_frame, size_t& consumed_bytes) {
        if (buffer.size() < 2) return false;
        unsigned char b1 = (unsigned char)buffer[0];
        unsigned char b2 = (unsigned char)buffer[1];

        out_frame.fin = (b1 & 0x80) != 0;
        out_frame.opcode = static_cast<WSOpCode>(b1 & 0x0F);

        bool is_masked = (b2 & 0x80) != 0;
        uint64_t payload_len = b2 & 0x7F;
        size_t offset = 2;

        if (payload_len == 126) {
            if (buffer.size() < 4) return false;
            payload_len = ((unsigned char)buffer[2] << 8) | (unsigned char)buffer[3];
            offset = 4;
        } else if (payload_len == 127) {
            if (buffer.size() < 10) return false;
            payload_len = 0;
            for (int i = 0; i < 8; ++i) {
                payload_len = (payload_len << 8) | (unsigned char)buffer[offset + i];
            }
            offset = 10;
        }

        if (is_masked) {
            if (buffer.size() < offset + 4 + payload_len) return false;
            unsigned char mask_key[4];
            memcpy(mask_key, &buffer[offset], 4);
            offset += 4;

            out_frame.payload.resize(payload_len);
            for (size_t i = 0; i < payload_len; ++i) {
                out_frame.payload[i] = buffer[offset + i] ^ mask_key[i % 4];
            }
            consumed_bytes = offset + payload_len;
            return true;
        } else {
            if (buffer.size() < offset + payload_len) return false;
            out_frame.payload = buffer.substr(offset, payload_len);
            consumed_bytes = offset + payload_len;
            return true;
        }
    }
};

} // namespace lepus::network
