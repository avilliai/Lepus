#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <mutex>

namespace lepus::ntqq {

class GroupSessionCache {
public:
    static GroupSessionCache& instance() {
        static GroupSessionCache inst;
        return inst;
    }

    void register_mapping(uint64_t internal_peer_id, uint64_t true_group_code, const std::string& group_name = "") {
        std::lock_guard<std::mutex> lock(mutex_);
        if (internal_peer_id > 0 && true_group_code > 0) {
            peer_to_group_[internal_peer_id] = true_group_code;
            group_to_peer_[true_group_code] = internal_peer_id;
        }
        if (!group_name.empty() && true_group_code > 0) {
            group_names_[true_group_code] = group_name;
        }
    }

    uint64_t get_group_code(uint64_t internal_peer_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = peer_to_group_.find(internal_peer_id);
        if (it != peer_to_group_.end()) {
            return it->second;
        }
        return internal_peer_id;
    }

    uint64_t get_internal_peer_id(uint64_t group_code) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = group_to_peer_.find(group_code);
        if (it != group_to_peer_.end()) {
            return it->second;
        }
        return group_code;
    }

    std::string get_group_name(uint64_t group_code) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = group_names_.find(group_code);
        if (it != group_names_.end()) {
            return it->second;
        }
        return "";
    }

    void set_bot_uin(uint64_t uin) {
        std::lock_guard<std::mutex> lock(mutex_);
        bot_uin_ = uin;
    }

        void register_user_uid(uint64_t uin, const std::string& uid) {
        if (uin == 0 || uid.empty()) return;
        std::lock_guard<std::mutex> lock(mutex_);
        uin_to_uid_[uin] = uid;
    }

        std::string get_user_uid(uint64_t uin) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = uin_to_uid_.find(uin);
        if (it != uin_to_uid_.end()) return it->second;
        return "";
    }

std::string get_or_create_uid(uint64_t uin) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = uin_to_uid_.find(uin);
        if (it != uin_to_uid_.end() && !it->second.empty()) return it->second;
        return "u_" + std::to_string(uin);
    }

    uint64_t bot_uin() {
        std::lock_guard<std::mutex> lock(mutex_);
        return bot_uin_ ? bot_uin_ : 3377428814ULL;
    }

private:
    std::mutex mutex_;
    uint64_t bot_uin_ = 3377428814ULL;
    std::unordered_map<uint64_t, uint64_t> peer_to_group_;
    std::unordered_map<uint64_t, uint64_t> group_to_peer_;
    std::unordered_map<uint64_t, std::string> group_names_;
    std::unordered_map<uint64_t, std::string> uin_to_uid_;
};

} // namespace lepus::ntqq
