#pragma once
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <unordered_map>
#include <nlohmann/json.hpp>
#include <lepus/utils/string_utils.hpp>

namespace lepus::protocol::agent {

using json = nlohmann::json;

// 1. 现代化强类型 AST 节点定义
enum class NodeType {
    Text,
    Mention,
    Image,
    Audio,
    File,
    Reply,
    Markdown,
    ToolCall,
    ToolResult,
    ReasoningTrace, // 思考推理过程（Agent思维链）
    InteractiveCard // 交互卡片/按钮
};

struct AgentNode {
    NodeType type;
    json payload;

    static AgentNode text(const std::string& content) {
        return {NodeType::Text, {{"content", content}}};
    }

    static AgentNode mention(const std::string& target_uin, const std::string& display_name = "") {
        return {NodeType::Mention, {{"uin", target_uin}, {"name", display_name}}};
    }

    static AgentNode image(const std::string& url_or_path, const std::string& summary = "") {
        return {NodeType::Image, {{"source", url_or_path}, {"summary", summary}}};
    }

    static AgentNode reasoning(const std::string& trace) {
        return {NodeType::ReasoningTrace, {{"thought", trace}}};
    }

    static AgentNode tool_call(const std::string& call_id, const std::string& name, const json& arguments) {
        return {NodeType::ToolCall, {{"call_id", call_id}, {"name", name}, {"arguments", arguments}}};
    }

    static AgentNode tool_result(const std::string& call_id, const std::string& name, const json& result) {
        return {NodeType::ToolResult, {{"call_id", call_id}, {"name", name}, {"result", result}}};
    }

    static AgentNode markdown(const std::string& md_text) {
        return {NodeType::Markdown, {{"content", md_text}}};
    }
};

// 2. 会话上下文与意图元数据（供 Agent 快速理解对话背景）
struct SessionContext {
    std::string session_id;       // 全局唯一会话 ID
    std::string bot_uin;          // 机器人 UIN
    std::string channel_type;     // "private", "group", "guild"
    std::string channel_id;       // 好友 UIN 或群号
    std::string sender_uin;       // 发送者 UIN
    std::string sender_role;      // "owner", "admin", "member"
    std::string sender_nickname;  // 发送者昵称
    int64_t timestamp;            // 消息时间戳
    bool is_bot_mentioned{false}; // 是否 @ 了机器人
    std::vector<json> recent_history; // 最近上下文历史快照
};

// 3. Agent 现代化请求与响应封装
struct AgentEnvelope {
    std::string trace_id;          // 全局链路追踪 ID
    std::string version = "2.0";   // 协议版本
    SessionContext context;
    std::vector<AgentNode> nodes;
    std::unordered_map<std::string, std::string> metadata;

    json to_json() const {
        json j;
        j["trace_id"] = trace_id.empty() ? utils::generate_uuid() : trace_id;
        j["version"] = version;
        
        j["context"] = {
            {"session_id", context.session_id},
            {"bot_uin", context.bot_uin},
            {"channel_type", context.channel_type},
            {"channel_id", context.channel_id},
            {"sender_uin", context.sender_uin},
            {"sender_role", context.sender_role},
            {"sender_nickname", context.sender_nickname},
            {"timestamp", context.timestamp},
            {"is_bot_mentioned", context.is_bot_mentioned},
            {"recent_history", context.recent_history}
        };

        json nodes_arr = json::array();
        for (const auto& n : nodes) {
            std::string type_str;
            switch (n.type) {
                case NodeType::Text: type_str = "text"; break;
                case NodeType::Mention: type_str = "mention"; break;
                case NodeType::Image: type_str = "image"; break;
                case NodeType::Audio: type_str = "audio"; break;
                case NodeType::File: type_str = "file"; break;
                case NodeType::Reply: type_str = "reply"; break;
                case NodeType::Markdown: type_str = "markdown"; break;
                case NodeType::ToolCall: type_str = "tool_call"; break;
                case NodeType::ToolResult: type_str = "tool_result"; break;
                case NodeType::ReasoningTrace: type_str = "reasoning_trace"; break;
                case NodeType::InteractiveCard: type_str = "interactive_card"; break;
            }
            nodes_arr.push_back({
                {"type", type_str},
                {"payload", n.payload}
            });
        }
        j["nodes"] = nodes_arr;
        j["metadata"] = metadata;
        return j;
    }

    static AgentEnvelope from_json(const json& j) {
        AgentEnvelope env;
        env.trace_id = j.value("trace_id", utils::generate_uuid());
        env.version = j.value("version", "2.0");

        if (j.contains("context")) {
            const auto& ctx = j["context"];
            env.context.session_id = ctx.value("session_id", "");
            env.context.bot_uin = ctx.value("bot_uin", "");
            env.context.channel_type = ctx.value("channel_type", "private");
            env.context.channel_id = ctx.value("channel_id", "");
            env.context.sender_uin = ctx.value("sender_uin", "");
            env.context.sender_role = ctx.value("sender_role", "member");
            env.context.sender_nickname = ctx.value("sender_nickname", "");
            env.context.timestamp = ctx.value("timestamp", 0LL);
            env.context.is_bot_mentioned = ctx.value("is_bot_mentioned", false);
            if (ctx.contains("recent_history") && ctx["recent_history"].is_array()) {
                env.context.recent_history = ctx["recent_history"].get<std::vector<json>>();
            }
        }

        if (j.contains("nodes") && j["nodes"].is_array()) {
            for (const auto& item : j["nodes"]) {
                std::string t = item.value("type", "text");
                NodeType type = NodeType::Text;
                if (t == "mention") type = NodeType::Mention;
                else if (t == "image") type = NodeType::Image;
                else if (t == "audio") type = NodeType::Audio;
                else if (t == "file") type = NodeType::File;
                else if (t == "reply") type = NodeType::Reply;
                else if (t == "markdown") type = NodeType::Markdown;
                else if (t == "tool_call") type = NodeType::ToolCall;
                else if (t == "tool_result") type = NodeType::ToolResult;
                else if (t == "reasoning_trace") type = NodeType::ReasoningTrace;
                else if (t == "interactive_card") type = NodeType::InteractiveCard;

                env.nodes.push_back({type, item.value("payload", json::object())});
            }
        }

        if (j.contains("metadata") && j["metadata"].is_object()) {
            for (auto& [k, v] : j["metadata"].items()) {
                if (v.is_string()) env.metadata[k] = v.get<std::string>();
            }
        }
        return env;
    }
};

// 4. 流式传输增量包（针对 Agent 流式输出）
struct StreamDeltaPacket {
    std::string trace_id;
    std::string session_id;
    int32_t chunk_index{0};
    bool is_final{false};
    std::string delta_text;
    std::string delta_reasoning;

    json to_json() const {
        return {
            {"trace_id", trace_id},
            {"session_id", session_id},
            {"chunk_index", chunk_index},
            {"is_final", is_final},
            {"delta_text", delta_text},
            {"delta_reasoning", delta_reasoning}
        };
    }
};

} // namespace lepus::protocol::agent


