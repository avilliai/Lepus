#pragma once
#include <string>
#include <vector>
#include <variant>
#include <optional>
#include <nlohmann/json.hpp>

namespace lepus::protocol {

using json = nlohmann::json;

// OneBot v11 消息段定义
struct CQSegment {
    std::string type; // "text", "image", "at", "face", "reply", "record", "video"
    json data;
};

// 消息解析与构造器
class CQMessageHelper {
public:
    // 将字符串形式的 CQ 码转换成 Segment 列表
        static std::string segments_to_cq_string(const std::vector<CQSegment>& segments) {
        std::string res;
        for (const auto& seg : segments) {
            if (seg.type == "text") {
                res += seg.data.value("text", "");
            } else {
                res += "[CQ:" + seg.type;
                for (auto& [k, v] : seg.data.items()) {
                    if (v.is_string()) {
                        res += "," + k + "=" + v.get<std::string>();
                    } else {
                        res += "," + k + "=" + v.dump();
                    }
                }
                res += "]";
            }
        }
        return res;
    }

static std::vector<CQSegment> parse_string_to_segments(const std::string& raw) {
        std::vector<CQSegment> segments;
        size_t cursor = 0;
        while (cursor < raw.length()) {
            size_t cq_start = raw.find("[CQ:", cursor);
            if (cq_start == std::string::npos) {
                std::string plain = raw.substr(cursor);
                if (!plain.empty()) {
                    segments.push_back({"text", {{"text", plain}}});
                }
                break;
            }
            if (cq_start > cursor) {
                std::string plain = raw.substr(cursor, cq_start - cursor);
                segments.push_back({"text", {{"text", plain}}});
            }

            size_t cq_end = raw.find(']', cq_start);
            if (cq_end == std::string::npos) {
                // 没有闭合标签，当普通文本处理
                segments.push_back({"text", {{"text", raw.substr(cq_start)}}});
                break;
            }

            // 提取 CQ 码内容: [CQ:type,k1=v1,k2=v2]
            std::string cq_body = raw.substr(cq_start + 4, cq_end - cq_start - 4);
            size_t comma = cq_body.find(',');
            std::string type = (comma != std::string::npos) ? cq_body.substr(0, comma) : cq_body;
            json data = json::object();

            if (comma != std::string::npos) {
                size_t param_start = comma + 1;
                while (param_start < cq_body.length()) {
                    size_t next_comma = cq_body.find(',', param_start);
                    std::string pair = (next_comma != std::string::npos) 
                        ? cq_body.substr(param_start, next_comma - param_start)
                        : cq_body.substr(param_start);

                    size_t eq = pair.find('=');
                    if (eq != std::string::npos) {
                        std::string k = pair.substr(0, eq);
                        std::string v = pair.substr(eq + 1);
                        data[k] = v;
                    }
                    if (next_comma == std::string::npos) break;
                    param_start = next_comma + 1;
                }
            }

            segments.push_back({type, data});
            cursor = cq_end + 1;
        }
        return segments;
    }

    // 将 Segment 数组转为标准 JSON 消息
    static json segments_to_json(const std::vector<CQSegment>& segments) {
        json j = json::array();
        for (const auto& seg : segments) {
            j.push_back({
                {"type", seg.type},
                {"data", seg.data}
            });
        }
        return j;
    }

    // 从 JSON (可以是纯 string 也可以是 segment array) 解析
    static std::vector<CQSegment> from_message_field(const json& msg_field) {
        if (msg_field.is_string()) {
            return parse_string_to_segments(msg_field.get<std::string>());
        }
        if (msg_field.is_array()) {
            std::vector<CQSegment> segments;
            for (const auto& item : msg_field) {
                if (item.contains("type")) {
                    CQSegment seg;
                    seg.type = item["type"].get<std::string>();
                    seg.data = item.value("data", json::object());
                    segments.push_back(seg);
                }
            }
            return segments;
        }
        return {};
    }

    // 转换为 NTQQ 内核 elements 数组
    static json to_ntqq_elements(const std::vector<CQSegment>& segments) {
        json elements = json::array();
        for (const auto& seg : segments) {
            if (seg.type == "text") {
                elements.push_back({
                    {"elementType", 1},
                    {"textElement", {
                        {"content", seg.data.value("text", "")}
                    }}
                });
            } else if (seg.type == "at") {
                std::string qq_str = "";
                if (seg.data.contains("qq")) {
                    if (seg.data["qq"].is_number()) {
                        qq_str = std::to_string(seg.data["qq"].get<uint64_t>());
                    } else if (seg.data["qq"].is_string()) {
                        qq_str = seg.data["qq"].get<std::string>();
                    }
                }
                elements.push_back({
                    {"elementType", 1},
                    {"textElement", {
                        {"atType", qq_str == "all" ? 1 : 2},
                        {"atNtUid", qq_str},
                        {"content", "@" + qq_str}
                    }}
                });
            } else if (seg.type == "image") {
                elements.push_back({
                    {"elementType", 2},
                    {"picElement", {
                        {"fileName", seg.data.value("file", "image.png")},
                        {"sourcePath", seg.data.value("file", "")},
                        {"picWidth", 640},
                        {"picHeight", 480}
                    }}
                });
            } else if (seg.type == "reply") {
                elements.push_back({
                    {"elementType", 7},
                    {"replyElement", {
                        {"sourceMsgIdInRecords", seg.data.value("id", "0")}
                    }}
                });
            } else if (seg.type == "face") {
                elements.push_back({
                    {"elementType", 6},
                    {"faceElement", {
                        {"faceIndex", std::stoi(seg.data.value("id", "0"))}
                    }}
                });
            }
        }
        return elements;
    }
};


// ==================== OneBot v11 Events Helper ====================
class OneBotEventHelper {
public:
    static json make_base_event(const std::string& post_type, int64_t self_id) {
        int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
        return json{
            {"time", now},
            {"self_id", self_id},
            {"post_type", post_type}
        };
    }

    // 1. Meta Events
    static json make_lifecycle_event(int64_t self_id, const std::string& sub_type = "connect") {
        auto ev = make_base_event("meta_event", self_id);
        ev["meta_event_type"] = "lifecycle";
        ev["sub_type"] = sub_type;
        return ev;
    }

    static json make_heartbeat_event(int64_t self_id, int64_t interval_ms = 5000) {
        auto ev = make_base_event("meta_event", self_id);
        ev["meta_event_type"] = "heartbeat";
        ev["interval"] = interval_ms;
        ev["status"] = json{
            {"app_initialized", true},
            {"app_enabled", true},
            {"app_good", true},
            {"online", true},
            {"good", true}
        };
        return ev;
    }

    // 2. Message Events
    static json make_private_message_event(
        int64_t self_id, int64_t user_id, int32_t message_id,
        const std::string& raw_message, const json& message_elements = json::array(), const std::string& sub_type = "friend"
    ) {
        auto ev = make_base_event("message", self_id);
        ev["message_type"] = "private";
        ev["sub_type"] = sub_type;
        ev["message_id"] = message_id;
        ev["user_id"] = user_id;
        if (message_elements.is_array() && !message_elements.empty()) {
            ev["message"] = message_elements;
        } else {
            ev["message"] = json::array({
                json{{"type", "text"}, {"data", json{{"text", raw_message}}}}
            });
        }
        ev["raw_message"] = raw_message;
        ev["font"] = 0;
        ev["sender"] = json{
            {"user_id", user_id},
            {"nickname", "User_" + std::to_string(user_id)},
            {"sex", "unknown"},
            {"age", 0}
        };
        return ev;
    }

    static json make_group_message_event(
        int64_t self_id, int64_t group_id, int64_t user_id, int32_t message_id,
        const std::string& raw_message, const json& message_elements = json::array(), const std::string& sub_type = "normal"
    ) {
        auto ev = make_base_event("message", self_id);
        ev["message_type"] = "group";
        ev["sub_type"] = sub_type;
        ev["message_id"] = message_id;
        ev["group_id"] = group_id;
        ev["user_id"] = user_id;
        ev["anonymous"] = nullptr;
        if (message_elements.is_array() && !message_elements.empty()) {
            ev["message"] = message_elements;
        } else {
            ev["message"] = json::array({
                json{{"type", "text"}, {"data", json{{"text", raw_message}}}}
            });
        }
        ev["raw_message"] = raw_message;
        ev["font"] = 0;
        ev["sender"] = json{
            {"user_id", user_id},
            {"nickname", "Member_" + std::to_string(user_id)},
            {"card", ""},
            {"role", "member"},
            {"title", ""}
        };
        return ev;
    }

    // 3. Notice Events
    static json make_group_upload_notice(int64_t self_id, int64_t group_id, int64_t user_id, const json& file_info) {
        auto ev = make_base_event("notice", self_id);
        ev["notice_type"] = "group_upload";
        ev["group_id"] = group_id;
        ev["user_id"] = user_id;
        ev["file"] = file_info;
        return ev;
    }

    static json make_group_admin_notice(int64_t self_id, int64_t group_id, int64_t user_id, const std::string& sub_type) {
        auto ev = make_base_event("notice", self_id);
        ev["notice_type"] = "group_admin";
        ev["sub_type"] = sub_type; // set or unset
        ev["group_id"] = group_id;
        ev["user_id"] = user_id;
        return ev;
    }

    static json make_group_decrease_notice(int64_t self_id, int64_t group_id, int64_t user_id, int64_t operator_id, const std::string& sub_type) {
        auto ev = make_base_event("notice", self_id);
        ev["notice_type"] = "group_decrease";
        ev["sub_type"] = sub_type; // leave, kick, kick_me
        ev["group_id"] = group_id;
        ev["operator_id"] = operator_id;
        ev["user_id"] = user_id;
        return ev;
    }

    static json make_group_increase_notice(int64_t self_id, int64_t group_id, int64_t user_id, int64_t operator_id, const std::string& sub_type) {
        auto ev = make_base_event("notice", self_id);
        ev["notice_type"] = "group_increase";
        ev["sub_type"] = sub_type; // approve, invite
        ev["group_id"] = group_id;
        ev["operator_id"] = operator_id;
        ev["user_id"] = user_id;
        return ev;
    }

    static json make_group_ban_notice(int64_t self_id, int64_t group_id, int64_t user_id, int64_t operator_id, int64_t duration) {
        auto ev = make_base_event("notice", self_id);
        ev["notice_type"] = "group_ban";
        ev["sub_type"] = duration > 0 ? "ban" : "lift_ban";
        ev["group_id"] = group_id;
        ev["operator_id"] = operator_id;
        ev["user_id"] = user_id;
        ev["duration"] = duration;
        return ev;
    }

    static json make_friend_add_notice(int64_t self_id, int64_t user_id) {
        auto ev = make_base_event("notice", self_id);
        ev["notice_type"] = "friend_add";
        ev["user_id"] = user_id;
        return ev;
    }

    static json make_group_recall_notice(int64_t self_id, int64_t group_id, int64_t user_id, int64_t operator_id, int32_t message_id) {
        auto ev = make_base_event("notice", self_id);
        ev["notice_type"] = "group_recall";
        ev["group_id"] = group_id;
        ev["user_id"] = user_id;
        ev["operator_id"] = operator_id;
        ev["message_id"] = message_id;
        return ev;
    }

    static json make_friend_recall_notice(int64_t self_id, int64_t user_id, int32_t message_id) {
        auto ev = make_base_event("notice", self_id);
        ev["notice_type"] = "friend_recall";
        ev["user_id"] = user_id;
        ev["message_id"] = message_id;
        return ev;
    }

    static json make_notify_poke_notice(int64_t self_id, int64_t group_id, int64_t user_id, int64_t target_id) {
        auto ev = make_base_event("notice", self_id);
        ev["notice_type"] = "notify";
        ev["sub_type"] = "poke";
        ev["group_id"] = group_id;
        ev["user_id"] = user_id;
        ev["target_id"] = target_id;
        return ev;
    }

    // 4. Request Events
    static json make_friend_request(int64_t self_id, int64_t user_id, const std::string& comment, const std::string& flag) {
        auto ev = make_base_event("request", self_id);
        ev["request_type"] = "friend";
        ev["user_id"] = user_id;
        ev["comment"] = comment;
        ev["flag"] = flag;
        return ev;
    }

    static json make_group_request(int64_t self_id, const std::string& sub_type, int64_t group_id, int64_t user_id, const std::string& comment, const std::string& flag) {
        auto ev = make_base_event("request", self_id);
        ev["request_type"] = "group";
        ev["sub_type"] = sub_type; // add, invite
        ev["group_id"] = group_id;
        ev["user_id"] = user_id;
        ev["comment"] = comment;
        ev["flag"] = flag;
        return ev;
    }
};

} // namespace lepus::protocol
