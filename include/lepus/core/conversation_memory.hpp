#pragma once
#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <nlohmann/json.hpp>
#include <lepus/protocol/agent_protocol.hpp>

namespace lepus::core {

using json = nlohmann::json;

// 针对 Agent 的短时记忆管理器
class ConversationMemory {
public:
    static ConversationMemory& instance() {
        static ConversationMemory inst;
        return inst;
    }

    void append_message(const std::string& session_id, const std::string& role, const json& content) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& history = sessions_[session_id];
        history.push_back({
            {"role", role},
            {"content", content},
            {"time", std::time(nullptr)}
        });
        if (history.size() > max_history_size_) {
            history.pop_front();
        }
    }

    std::vector<json> get_history(const std::string& session_id, size_t limit = 10) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessions_.find(session_id);
        if (it == sessions_.end()) return {};

        std::vector<json> res;
        size_t start = (it->second.size() > limit) ? (it->second.size() - limit) : 0;
        for (size_t i = start; i < it->second.size(); ++i) {
            res.push_back(it->second[i]);
        }
        return res;
    }

    void clear(const std::string& session_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        sessions_.erase(session_id);
    }

    void set_max_history_size(size_t max_size) {
        max_history_size_ = max_size;
    }

private:
    ConversationMemory() : max_history_size_(20) {}
    std::unordered_map<std::string, std::deque<json>> sessions_;
    size_t max_history_size_{20};
    std::mutex mutex_;
};

} // namespace lepus::core
