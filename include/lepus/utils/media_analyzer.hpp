#pragma once
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstdint>
#include <cstring>
#include <windows.h>
#include <wincrypt.h>

#pragma comment(lib, "advapi32.lib")

namespace lepus::media {

struct MediaInfo {
    std::string path;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t duration = 1; // For voice
    uint64_t file_size = 0;
    std::string ext = "jpg";
    std::string md5_hex;
    std::string sha1_hex;
    bool is_silk = false;
};

class MediaAnalyzer {
public:
    static std::string to_hex(const uint8_t* data, size_t len) {
        static const char hex_chars[] = "0123456789abcdef";
        std::string res;
        res.reserve(len * 2);
        for (size_t i = 0; i < len; ++i) {
            res.push_back(hex_chars[(data[i] >> 4) & 0x0F]);
            res.push_back(hex_chars[data[i] & 0x0F]);
        }
        return res;
    }

    static bool calc_hashes(const std::vector<uint8_t>& buf, std::string& md5_hex, std::string& sha1_hex) {
        HCRYPTPROV hProv = 0;
        if (!CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
            return false;
        }

        // MD5
        HCRYPTHASH hHash = 0;
        if (CryptCreateHash(hProv, CALG_MD5, 0, 0, &hHash)) {
            CryptHashData(hHash, buf.data(), (DWORD)buf.size(), 0);
            uint8_t hash[16];
            DWORD hashLen = sizeof(hash);
            CryptGetHashParam(hHash, HP_HASHVAL, hash, &hashLen, 0);
            CryptDestroyHash(hHash);
            md5_hex = to_hex(hash, 16);
        }

        // SHA1
        if (CryptCreateHash(hProv, CALG_SHA1, 0, 0, &hHash)) {
            CryptHashData(hHash, buf.data(), (DWORD)buf.size(), 0);
            uint8_t hash[20];
            DWORD hashLen = sizeof(hash);
            CryptGetHashParam(hHash, HP_HASHVAL, hash, &hashLen, 0);
            CryptDestroyHash(hHash);
            sha1_hex = to_hex(hash, 20);
        }

        CryptReleaseContext(hProv, 0);
        return (!md5_hex.empty() && !sha1_hex.empty());
    }

    static bool parse_image_dimensions(const std::vector<uint8_t>& buf, uint32_t& width, uint32_t& height, std::string& ext) {
        if (buf.size() < 16) return false;

        // PNG check: 89 50 4E 47 0D 0A 1A 0A
        if (buf[0] == 0x89 && buf[1] == 'P' && buf[2] == 'N' && buf[3] == 'G') {
            ext = "png";
            if (buf.size() >= 24) {
                width = (buf[16] << 24) | (buf[17] << 16) | (buf[18] << 8) | buf[19];
                height = (buf[20] << 24) | (buf[21] << 16) | (buf[22] << 8) | buf[23];
                return true;
            }
        }

        // GIF check: 'G' 'I' 'F'
        if (buf[0] == 'G' && buf[1] == 'I' && buf[2] == 'F') {
            ext = "gif";
            if (buf.size() >= 10) {
                width = buf[6] | (buf[7] << 8);
                height = buf[8] | (buf[9] << 8);
                return true;
            }
        }

        // JPEG check: FF D8 FF
        if (buf[0] == 0xFF && buf[1] == 0xD8 && buf[2] == 0xFF) {
            ext = "jpg";
            size_t pos = 2;
            while (pos + 4 < buf.size()) {
                if (buf[pos] != 0xFF) { ++pos; continue; }
                uint8_t marker = buf[pos + 1];
                uint16_t len = (buf[pos + 2] << 8) | buf[pos + 3];
                // SOF0 (0xC0) or SOF2 (0xC2)
                if (marker == 0xC0 || marker == 0xC2) {
                    if (pos + 9 < buf.size()) {
                        height = (buf[pos + 5] << 8) | buf[pos + 6];
                        width = (buf[pos + 7] << 8) | buf[pos + 8];
                        return true;
                    }
                }
                pos += 2 + len;
            }
        }

        // Fallback default
        width = 640;
        height = 480;
        ext = "jpg";
        return true;
    }

    static bool analyze_file(const std::string& path, MediaInfo& out) {
        out.path = path;
        
        // Use CreateFileW to support UTF-8 Chinese characters and long paths
        int size_needed = MultiByteToWideChar(CP_UTF8, 0, path.data(), (int)path.size(), NULL, 0);
        std::wstring wpath(size_needed, 0);
        MultiByteToWideChar(CP_UTF8, 0, path.data(), (int)path.size(), &wpath[0], size_needed);

        HANDLE hFile = CreateFileW(wpath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) {
            // Also try system default ANSI codepage fallback
            int ansi_len = MultiByteToWideChar(CP_ACP, 0, path.data(), (int)path.size(), NULL, 0);
            std::wstring wpath_ansi(ansi_len, 0);
            MultiByteToWideChar(CP_ACP, 0, path.data(), (int)path.size(), &wpath_ansi[0], ansi_len);
            hFile = CreateFileW(wpath_ansi.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (hFile == INVALID_HANDLE_VALUE) return false;
        }

        DWORD fsize = GetFileSize(hFile, NULL);
        if (fsize == INVALID_FILE_SIZE || fsize == 0) {
            CloseHandle(hFile);
            return false;
        }

        std::vector<uint8_t> buffer(fsize);
        DWORD bytesRead = 0;
        BOOL readRes = ReadFile(hFile, buffer.data(), fsize, &bytesRead, NULL);
        CloseHandle(hFile);

        if (!readRes || bytesRead == 0) return false;
        buffer.resize(bytesRead);
        out.file_size = buffer.size();

        calc_hashes(buffer, out.md5_hex, out.sha1_hex);

        // Check Silk v3 audio (#!SILK_V3 or \x02#!SILK_V3)
        if (buffer.size() >= 9) {
            std::string header((char*)buffer.data(), std::min((size_t)12, buffer.size()));
            if (header.find("#!SILK_V3") != std::string::npos) {
                out.is_silk = true;
                out.ext = "amr";
                out.duration = std::max((uint32_t)1, (uint32_t)(buffer.size() / 3200)); // Rough estimate if header not exact
                return true;
            }
        }

        parse_image_dimensions(buffer, out.width, out.height, out.ext);
        return true;
    }
};

} // namespace lepus::media
