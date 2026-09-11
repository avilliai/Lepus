#pragma once

#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include <nlohmann/json.hpp>
#include <lepus/ntqq/ntqq_engine.hpp>
#include <lepus/protocol/onebot_v11.hpp>
#include <lepus/utils/logger.hpp>

namespace lepus::protocol {

using json = nlohmann::json;

class OneBotDispatcher {
public:
    static OneBotDispatcher& instance() {
        static OneBotDispatcher inst;
        return inst;
    }

    OneBotDispatcher() {
        register_handlers();
    }

    json handle_action(const std::string& action, const json& params) {
        auto it = handlers_.find(action);
        if (it != handlers_.end()) {
            try {
                return it->second(params);
            } catch (const std::exception& e) {
                LEPUS_ERROR("[OneBotDispatcher] Exception handling action {}: {}", action, e.what());
                return make_response("failed", -1, nullptr, e.what());
            }
        }
        LEPUS_WARN("[OneBotDispatcher] Action not found: {}", action);
        return make_response("failed", 1404, nullptr, "Action not found: " + action);
    }

    static json make_response(const std::string& status, int retcode, const json& data, const std::string& message = "") {
        json resp;
        resp["status"] = status;
        resp["retcode"] = retcode;
        resp["data"] = data.is_null() ? json::object() : data;
        resp["message"] = message;
        return resp;
    }

private:
    std::unordered_map<std::string, std::function<json(const json&)>> handlers_;

    void register_handlers() {
        auto& engine = ntqq::NTQQEngine::instance();

        // 1. send_private_msg
        handlers_["send_private_msg"] = [&engine](const json& p) -> json {
            int64_t user_id = p.value("user_id", 0LL);
            json message_payload = p.value("message", json::array());
            int64_t msg_id = std::stoll(engine.send_private_message(user_id, message_payload));
            return make_response("ok", 0, json{{"message_id", msg_id}});
        };

        // 2. send_group_msg
        handlers_["send_group_msg"] = [&engine](const json& p) -> json {
            int64_t group_id = p.value("group_id", 0LL);
            json message_payload = p.value("message", json::array());
            int64_t msg_id = std::stoll(engine.send_group_message(group_id, message_payload));
            return make_response("ok", 0, json{{"message_id", msg_id}});
        };

        // 3. send_msg (unified)
        handlers_["send_msg"] = [this](const json& p) -> json {
            std::string msg_type = p.value("message_type", "");
            if (msg_type == "private" || (msg_type.empty() && p.contains("user_id") && !p.contains("group_id"))) {
                return handlers_["send_private_msg"](p);
            } else {
                return handlers_["send_group_msg"](p);
            }
        };

        // 4. delete_msg
        handlers_["delete_msg"] = [](const json& p) -> json {
            int32_t msg_id = p.value("message_id", 0);
            LEPUS_INFO("[OneBot] Recall/delete msg: {}", msg_id);
            return make_response("ok", 0, nullptr);
        };

        // 5. get_msg
        handlers_["get_msg"] = [](const json& p) -> json {
            int32_t msg_id = p.value("message_id", 0);
            return make_response("ok", 0, json{
                {"time", 1726000000},
                {"message_type", "private"},
                {"message_id", msg_id},
                {"real_id", msg_id},
                {"sender", {{"user_id", 123456}, {"nickname", "User"}}},
                {"message", "Hello"},
                {"raw_message", "Hello"}
            });
        };

        // 6. get_forward_msg
        handlers_["get_forward_msg"] = [](const json& p) -> json {
            return make_response("ok", 0, json{
                {"messages", json::array({
                    json{{"content", "Forwarded sub message"}, {"sender", {{"nickname", "Bot"}, {"user_id", 10001}}}}
                })}
            });
        };

        // 7. send_like
        handlers_["send_like"] = [](const json& p) -> json {
            int64_t user_id = p.value("user_id", 0LL);
            int32_t times = p.value("times", 1);
            LEPUS_INFO("[OneBot] Send like to {} times: {}", user_id, times);
            return make_response("ok", 0, nullptr);
        };

        // 8. set_group_kick
        handlers_["set_group_kick"] = [](const json& p) -> json {
            int64_t group_id = p.value("group_id", 0LL);
            int64_t user_id = p.value("user_id", 0LL);
            LEPUS_INFO("[OneBot] Group {} kick user {}", group_id, user_id);
            return make_response("ok", 0, nullptr);
        };

        // 9. set_group_ban
        handlers_["set_group_ban"] = [](const json& p) -> json {
            int64_t group_id = p.value("group_id", 0LL);
            int64_t user_id = p.value("user_id", 0LL);
            int64_t duration = p.value("duration", 1800LL);
            LEPUS_INFO("[OneBot] Group {} ban user {} duration {}", group_id, user_id, duration);
            return make_response("ok", 0, nullptr);
        };

        // 10. set_group_anonymous_ban
        handlers_["set_group_anonymous_ban"] = [](const json& p) -> json {
            return make_response("ok", 0, nullptr);
        };

        // 11. set_group_whole_ban
        handlers_["set_group_whole_ban"] = [](const json& p) -> json {
            bool enable = p.value("enable", true);
            LEPUS_INFO("[OneBot] Group whole ban: {}", enable);
            return make_response("ok", 0, nullptr);
        };

        // 12. set_group_admin
        handlers_["set_group_admin"] = [](const json& p) -> json {
            return make_response("ok", 0, nullptr);
        };

        // 13. set_group_anonymous
        handlers_["set_group_anonymous"] = [](const json& p) -> json {
            return make_response("ok", 0, nullptr);
        };

        // 14. set_group_card
        handlers_["set_group_card"] = [](const json& p) -> json {
            return make_response("ok", 0, nullptr);
        };

        // 15. set_group_name
        handlers_["set_group_name"] = [](const json& p) -> json {
            return make_response("ok", 0, nullptr);
        };

        // 16. set_group_leave
        handlers_["set_group_leave"] = [](const json& p) -> json {
            return make_response("ok", 0, nullptr);
        };

        // 17. set_group_special_title
        handlers_["set_group_special_title"] = [](const json& p) -> json {
            return make_response("ok", 0, nullptr);
        };

        // 18. set_friend_add_request
        handlers_["set_friend_add_request"] = [](const json& p) -> json {
            return make_response("ok", 0, nullptr);
        };

        // 19. set_group_add_request
        handlers_["set_group_add_request"] = [](const json& p) -> json {
            return make_response("ok", 0, nullptr);
        };

        // 20. get_login_info
        handlers_["get_login_info"] = [](const json& p) -> json {
            return make_response("ok", 0, json{
                {"user_id", 10001},
                {"nickname", "Lepus-NTQQ-Bot"}
            });
        };

        // 21. get_stranger_info
        handlers_["get_stranger_info"] = [](const json& p) -> json {
            int64_t user_id = p.value("user_id", 0LL);
            return make_response("ok", 0, json{
                {"user_id", user_id},
                {"nickname", "QQUser_" + std::to_string(user_id)},
                {"sex", "unknown"},
                {"age", 18},
                {"qid", ""},
                {"level", 30},
                {"login_days", 1000}
            });
        };

        // 22. get_friend_list
        handlers_["get_friend_list"] = [](const json& p) -> json {
            return make_response("ok", 0, json::array({
                json{{"user_id", 12345678}, {"nickname", "Alice"}, {"remark", "Friend Alice"}},
                json{{"user_id", 87654321}, {"nickname", "Bob"}, {"remark", "Friend Bob"}}
            }));
        };

        // 23. get_group_info
        handlers_["get_group_info"] = [](const json& p) -> json {
            int64_t group_id = p.value("group_id", 0LL);
            return make_response("ok", 0, json{
                {"group_id", group_id},
                {"group_name", "Lepus Community Group"},
                {"group_memo", "Welcome to Lepus NTQQ framework"},
                {"group_create_time", 1600000000},
                {"group_level", 5},
                {"member_count", 50},
                {"max_member_count", 500}
            });
        };

        // 24. get_group_list
        handlers_["get_group_list"] = [](const json& p) -> json {
            return make_response("ok", 0, json::array({
                json{
                    {"group_id", 888888},
                    {"group_name", "NTQQ Bot Dev Group"},
                    {"member_count", 120},
                    {"max_member_count", 500}
                }
            }));
        };

        // 25. get_group_member_info
        handlers_["get_group_member_info"] = [](const json& p) -> json {
            int64_t group_id = p.value("group_id", 0LL);
            int64_t user_id = p.value("user_id", 0LL);
            return make_response("ok", 0, json{
                {"group_id", group_id},
                {"user_id", user_id},
                {"nickname", "Member_" + std::to_string(user_id)},
                {"card", ""},
                {"sex", "unknown"},
                {"age", 0},
                {"area", ""},
                {"join_time", 1650000000},
                {"last_sent_time", 1726000000},
                {"level", "1"},
                {"role", "member"},
                {"unfriendly", false},
                {"title", ""},
                {"title_expire_time", 0},
                {"card_changeable", false}
            });
        };

        // 26. get_group_member_list
        handlers_["get_group_member_list"] = [](const json& p) -> json {
            int64_t group_id = p.value("group_id", 0LL);
            return make_response("ok", 0, json::array({
                json{{"group_id", group_id}, {"user_id", 10001}, {"nickname", "Bot"}, {"role", "admin"}},
                json{{"group_id", group_id}, {"user_id", 123456}, {"nickname", "User"}, {"role", "member"}}
            }));
        };

        // 27. get_group_honor_info
        handlers_["get_group_honor_info"] = [](const json& p) -> json {
            int64_t group_id = p.value("group_id", 0LL);
            return make_response("ok", 0, json{
                {"group_id", group_id},
                {"current_talkative", {{"user_id", 123456}, {"nickname", "ChatMaster"}, {"avatar", ""}, {"day_count", 5}}}
            });
        };

        // 28. get_cookies
        handlers_["get_cookies"] = [](const json& p) -> json {
            return make_response("ok", 0, json{{"cookies", "uin=o00010001; skey=@LepusFakeSkey;"}});
        };

        // 29. get_csrf_token
        handlers_["get_csrf_token"] = [](const json& p) -> json {
            return make_response("ok", 0, json{{"token", 123456789}});
        };

        // 30. get_credentials
        handlers_["get_credentials"] = [](const json& p) -> json {
            return make_response("ok", 0, json{
                {"cookies", "uin=o00010001; skey=@LepusFakeSkey;"},
                {"csrf_token", 123456789}
            });
        };

        // 31. get_record
        handlers_["get_record"] = [](const json& p) -> json {
            return make_response("ok", 0, json{{"file", "cache/record/sample.silk"}});
        };

        // 32. get_image
        handlers_["get_image"] = [](const json& p) -> json {
            return make_response("ok", 0, json{{"file", "cache/image/sample.png"}});
        };

        // 33. can_send_image
        handlers_["can_send_image"] = [](const json& p) -> json {
            return make_response("ok", 0, json{{"yes", true}});
        };

        // 34. can_send_record
        handlers_["can_send_record"] = [](const json& p) -> json {
            return make_response("ok", 0, json{{"yes", true}});
        };

        // 35. get_status
        handlers_["get_status"] = [](const json& p) -> json {
            return make_response("ok", 0, json{
                {"app_initialized", true},
                {"app_enabled", true},
                {"app_good", true},
                {"online", true},
                {"good", true},
                {"stat", {
                    {"packet_received", 1024},
                    {"packet_sent", 512},
                    {"packet_lost", 0},
                    {"message_received", 256},
                    {"message_sent", 128},
                    {"disconnect_times", 0},
                    {"lost_times", 0}
                }}
            });
        };

        // 36. get_version_info
        handlers_["get_version_info"] = [](const json& p) -> json {
            return make_response("ok", 0, json{
                {"app_name", "Lepus"},
                {"app_version", "1.0.0"},
                {"protocol_version", "v11"}
            });
        };

        // 37. set_restart
        handlers_["set_restart"] = [](const json& p) -> json {
            LEPUS_INFO("[OneBot] Requesting framework restart...");
            return make_response("ok", 0, nullptr);
        };

        // 38. clean_cache
        handlers_["clean_cache"] = [](const json& p) -> json {
            LEPUS_INFO("[OneBot] Cleaning cache files...");
            return make_response("ok", 0, nullptr);
        };
    }
};

} // namespace lepus::protocol