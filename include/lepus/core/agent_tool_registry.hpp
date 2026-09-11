#pragma once
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include <mutex>
#include <nlohmann/json.hpp>
#include <lepus/utils/logger.hpp>

namespace lepus::core {

using json = nlohmann::json;

using ToolExecutor = std::function<json(const json& params)>;

struct ToolDefinition {
    std::string name;
    std::string description;
    json parameters_schema;
    ToolExecutor executor;
};

// Agent Function Calling 注册与管理中心
class AgentToolRegistry {
public:
    static AgentToolRegistry& instance() {
        static AgentToolRegistry inst;
        return inst;
    }

    void register_tool(const std::string& name,
                       const std::string& description,
                       const json& schema,
                       ToolExecutor executor) {
        std::lock_guard<std::mutex> lock(mutex_);
        tools_[name] = {name, description, schema, executor};
        LEPUS_LOG_INFO("[AgentToolRegistry] Registered tool: {}", name);
    }

    json get_tools_manifest() {
        std::lock_guard<std::mutex> lock(mutex_);
        json manifest = json::array();
        for (const auto& [name, tool] : tools_) {
            manifest.push_back({
                {"type", "function"},
                {"function", {
                    {"name", tool.name},
                    {"description", tool.description},
                    {"parameters", tool.parameters_schema}
                }}
            });
        }
        return manifest;
    }

    json execute_tool(const std::string& name, const json& arguments) {
        ToolExecutor exec = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = tools_.find(name);
            if (it != tools_.end()) {
                exec = it->second.executor;
            }
        }
        if (!exec) {
            LEPUS_LOG_WARN("[AgentToolRegistry] Tool {} not found", name);
            return {{"error", "Tool not found"}, {"status", "error"}};
        }
        try {
            return exec(arguments);
        } catch (const std::exception& e) {
            LEPUS_LOG_ERROR("[AgentToolRegistry] Error executing {}: {}", name, e.what());
            return {{"error", e.what()}, {"status", "exception"}};
        }
    }

private:
    AgentToolRegistry() {
        register_default_tools();
    }

    void register_default_tools() {
        tools_["get_weather"] = {
            "get_weather",
            "???????????",
            {
                {"type", "object"},
                {"properties", {
                    {"city", {{"type", "string"}, {"description", "????"}}}
                }},
                {"required", json::array({"city"})}
            },
            [](const json& params) -> json {
                std::string city = params.value("city", "??");
                return {
                    {"status", "ok"},
                    {"city", city},
                    {"temperature", "22?C"},
                    {"condition", "Sunny"}
                };
            }
        };
    }
    std::unordered_map<std::string, ToolDefinition> tools_;
    std::mutex mutex_;
};

} // namespace lepus::core
