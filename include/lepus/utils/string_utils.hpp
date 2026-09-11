#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <sstream>
#include <iomanip>
#include <random>

namespace lepus::utils {

inline std::string to_hex(const uint8_t* data, size_t length, bool uppercase = false) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    if (uppercase) oss << std::uppercase;
    for (size_t i = 0; i < length; ++i) {
        oss << std::setw(2) << static_cast<int>(data[i]);
    }
    return oss.str();
}

inline std::string to_hex(const std::vector<uint8_t>& data, bool uppercase = false) {
    return to_hex(data.data(), data.size(), uppercase);
}

inline std::vector<uint8_t> from_hex(const std::string& hex) {
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i < hex.length(); i += 2) {
        std::string byteString = hex.substr(i, 2);
        uint8_t byte = static_cast<uint8_t>(strtol(byteString.c_str(), nullptr, 16));
        bytes.push_back(byte);
    }
    return bytes;
}

inline std::string generate_uuid() {
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    static std::uniform_int_distribution<uint64_t> dis;

    uint64_t part1 = dis(gen);
    uint64_t part2 = dis(gen);

    // RFC 4122 v4
    part1 = (part1 & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
    part2 = (part2 & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;

    char buf[37];
    snprintf(buf, sizeof(buf),
             "%08x-%04x-%04x-%02x%02x-%012llx",
             static_cast<uint32_t>(part1 >> 32),
             static_cast<uint16_t>((part1 >> 16) & 0xFFFF),
             static_cast<uint16_t>(part1 & 0xFFFF),
             static_cast<uint8_t>(part2 >> 56),
             static_cast<uint8_t>((part2 >> 48) & 0xFF),
             static_cast<unsigned long long>(part2 & 0xFFFFFFFFFFFFULL));
    return std::string(buf);
}

inline int64_t current_timestamp_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

inline int64_t current_timestamp_s() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

} // namespace lepus::utils
