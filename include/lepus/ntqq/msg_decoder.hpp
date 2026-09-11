#pragma once
#include "group_session_cache.hpp"
#include <cstdint>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace lepus::ntqq {

using json = nlohmann::json;

struct ParsedMessage {
    uint64_t sender_uin = 0;
    std::string sender_uid;
    uint64_t peer_uin = 0;
    uint64_t internal_peer_id = 0;
    uint64_t self_uin = 0;
    uint32_t chat_type = 0;      // 82 = group, 166 = c2c, etc.
    uint64_t to_uin = 0;
    bool has_c2c = false;
    bool has_group = false;
    uint64_t msg_seq = 0;
    uint32_t msg_time = 0;
    std::string text;
    json message_elements = json::array();
    bool is_group = false;

    // Rich group & sender metadata from tag 8
    std::string sender_card;
    std::string sender_nick;
    std::string sender_title;
    uint32_t sub_type = 0;
    std::string sender_role = "member";
    std::string group_name;
};

class ProtobufReader {
public:
    static uint64_t read_varint(const uint8_t*& p, const uint8_t* end) {
        uint64_t res = 0;
        int shift = 0;
        while (p < end) {
            uint8_t b = *p++;
            res |= (uint64_t)(b & 0x7F) << shift;
            if ((b & 0x80) == 0) break;
            shift += 7;
        }
        return res;
    }

    static void skip_field(uint32_t wire_type, const uint8_t*& p, const uint8_t* end) {
        if (wire_type == 0) {
            read_varint(p, end);
        } else if (wire_type == 1) {
            p += 8;
        } else if (wire_type == 2) {
            uint64_t len = read_varint(p, end);
            p += len;
        } else if (wire_type == 5) {
            p += 4;
        }
    }

    static bool parse_msg_push(const uint8_t* data, size_t size, ParsedMessage& out_msg) {
        if (!data || size == 0) return false;
        const uint8_t* p = data;
        const uint8_t* end = data + size;

        try {
            // Root MsgPush
            // Field 1: length-delimited (MsgRecord)
            while (p < end) {
                uint64_t tw = read_varint(p, end);
                uint32_t tag = tw >> 3;
                uint32_t wire = tw & 7;

                if (wire == 2) {
                    uint64_t len = read_varint(p, end);
                    const uint8_t* field_end = p + len;
                    if (field_end > end) break;

                    if (tag == 1) {
                        parse_msg_record(p, field_end, out_msg);
                    }
                    p = field_end;
                } else if (wire == 0) {
                    read_varint(p, end);
                } else {
                    skip_field(wire, p, end);
                }
            }

            // Validate message type: ignore non-chat notifications like 0x210 (528) / 0x2DC (732)
            if (out_msg.chat_type != 0 &&
                out_msg.chat_type != 82 &&
                out_msg.chat_type != 166 &&
                out_msg.chat_type != 167 &&
                out_msg.chat_type != 141 &&
                out_msg.chat_type != 208 &&
                out_msg.chat_type != 529) {
                return false;
            }

            if (out_msg.chat_type == 82 || out_msg.has_group) {
                out_msg.is_group = true;
            } else if (out_msg.chat_type == 166 || out_msg.chat_type == 167 || out_msg.chat_type == 141 || out_msg.chat_type == 208 || out_msg.chat_type == 529 || out_msg.has_c2c) {
                out_msg.is_group = false;
            } else {
                out_msg.is_group = (out_msg.peer_uin != 0 && out_msg.peer_uin != out_msg.sender_uin);
            }

            // Must have sender and actual message content or elements (prevent spurious empty messages)
            if (out_msg.sender_uin == 0 && out_msg.sender_uid.empty()) return false;
            if (out_msg.text.empty() && out_msg.message_elements.empty()) return false;

            return true;
        } catch (...) {
            return false;
        }
    }

private:
    static void parse_msg_record(const uint8_t* p, const uint8_t* end, ParsedMessage& msg) {
        while (p < end) {
            uint64_t tw = read_varint(p, end);
            uint32_t tag = tw >> 3;
            uint32_t wire = tw & 7;
            if (wire == 2) {
                uint64_t len = read_varint(p, end);
                const uint8_t* field_end = p + len;
                if (field_end > end) break;

                if (tag == 1) {
                    parse_msg_head(p, field_end, msg);
                } else if (tag == 2) {
                    parse_msg_content(p, field_end, msg);
                } else if (tag == 3) {
                    parse_msg_body_root(p, field_end, msg);
                }
                p = field_end;
            } else if (wire == 0) {
                read_varint(p, end);
            } else {
                skip_field(wire, p, end);
            }
        }
    }

    static void parse_group_meta(const uint8_t* p, const uint8_t* end, ParsedMessage& msg) {
        while (p < end) {
            uint64_t tw = read_varint(p, end);
            uint32_t tag = tw >> 3;
            uint32_t wire = tw & 7;
            if (wire == 0) {
                uint64_t val = read_varint(p, end);
                if (tag == 1) {
                    // True QQ group code!
                    msg.peer_uin = val;
                    msg.is_group = true;
                    // Register mapping from internal peer (tag 4) to true group code
                    GroupSessionCache::instance().register_mapping(msg.internal_peer_id, val, msg.group_name);
                } else if (tag == 5) {
                    // Role: 1=owner, 2=admin, 3=member
                    if (val == 1) msg.sender_role = "owner";
                    else if (val == 2) msg.sender_role = "admin";
                    else msg.sender_role = "member";
                }
            } else if (wire == 2) {
                uint64_t len = read_varint(p, end);
                const uint8_t* sub_end = p + len;
                if (sub_end <= end) {
                    if (tag == 4) {
                        msg.sender_card = std::string((const char*)p, len);
                    } else if (tag == 7) {
                        msg.group_name = std::string((const char*)p, len);
                    }
                }
                p += len;
            } else {
                skip_field(wire, p, end);
            }
        }
    }

    static void parse_msg_head(const uint8_t* p, const uint8_t* end, ParsedMessage& msg) {
        while (p < end) {
            uint64_t tw = read_varint(p, end);
            uint32_t tag = tw >> 3;
            uint32_t wire = tw & 7;
            if (wire == 0) {
                uint64_t val = read_varint(p, end);
                if (tag == 1) {
                    msg.sender_uin = val; // fromUin
                } else if (tag == 5) {
                    msg.to_uin = val;     // toUin (if private, usually bot self_uin)
                }
            } else if (wire == 2) {
                uint64_t len = read_varint(p, end);
                const uint8_t* sub_end = p + len;
                if (sub_end <= end) {
                    if (tag == 2) {
                        msg.sender_uid = std::string((const char*)p, len);
                        if (msg.sender_uin != 0) {
                            GroupSessionCache::instance().register_user_uid(msg.sender_uin, msg.sender_uid);
                        }
                    } else if (tag == 7) {
                        // C2C (Private) routing info
                        msg.has_c2c = true;
                    } else if (tag == 8) {
                        // Group routing info (tag 8 in routingHead)
                        msg.has_group = true;
                        parse_group_meta(p, sub_end, msg);
                    }
                }
                p += len;
            } else {
                skip_field(wire, p, end);
            }
        }
    }

    static void parse_msg_content(const uint8_t* p, const uint8_t* end, ParsedMessage& msg) {
        while (p < end) {
            uint64_t tw = read_varint(p, end);
            uint32_t tag = tw >> 3;
            uint32_t wire = tw & 7;
            if (wire == 0) {
                uint64_t val = read_varint(p, end);
                if (tag == 1) msg.chat_type = (uint32_t)val;
                else if (tag == 4) {
                    if (msg.msg_seq == 0) msg.msg_seq = val;
                } else if (tag == 5) {
                    msg.msg_seq = val;
                } else if (tag == 6) {
                    msg.msg_time = (uint32_t)val;
                }
            } else if (wire == 2) {
                uint64_t len = read_varint(p, end);
                p += len;
            } else {
                skip_field(wire, p, end);
            }
        }
    }

    static void parse_msg_body_root(const uint8_t* p, const uint8_t* end, ParsedMessage& msg) {
        while (p < end) {
            uint64_t tw = read_varint(p, end);
            uint32_t tag = tw >> 3;
            uint32_t wire = tw & 7;
            if (wire == 2) {
                uint64_t len = read_varint(p, end);
                if (tag == 1) {
                    parse_elements_list(p, p + len, msg);
                }
                p += len;
            } else if (wire == 0) {
                read_varint(p, end);
            } else {
                skip_field(wire, p, end);
            }
        }
    }

    static void parse_elements_list(const uint8_t* p, const uint8_t* end, ParsedMessage& msg) {
        while (p < end) {
            uint64_t tw = read_varint(p, end);
            uint32_t tag = tw >> 3;
            uint32_t wire = tw & 7;
            if (wire == 2) {
                uint64_t len = read_varint(p, end);
                if (tag == 2) {
                    parse_single_elem(p, p + len, msg);
                }
                p += len;
            } else if (wire == 0) {
                read_varint(p, end);
            } else {
                skip_field(wire, p, end);
            }
        }
    }

    static void parse_single_elem(const uint8_t* p, const uint8_t* end, ParsedMessage& msg) {
        while (p < end) {
            uint64_t tw = read_varint(p, end);
            uint32_t tag = tw >> 3;
            uint32_t wire = tw & 7;
            if (wire == 2) {
                uint64_t len = read_varint(p, end);
                if (tag == 1) { // TextElem
                    parse_text_elem(p, p + len, msg);
                } else if (tag == 2) { // FaceElem
                    parse_face_elem(p, p + len, msg);
                } else if (tag == 8) { // CustomFace / PicElem
                    parse_pic_elem(p, p + len, msg);
                } else if (tag == 16) { // ExtraInfo (nick, title)
                    parse_extra_info(p, p + len, msg);
                } else if (tag == 53) { // CommonElem (NTV2 rich media)
                    parse_common_elem(p, p + len, msg);
                }
                p += len;
            } else if (wire == 0) {
                read_varint(p, end);
            } else {
                skip_field(wire, p, end);
            }
        }
    }

    static void parse_extra_info(const uint8_t* p, const uint8_t* end, ParsedMessage& msg) {
        while (p < end) {
            uint64_t tw = read_varint(p, end);
            uint32_t tag = tw >> 3;
            uint32_t wire = tw & 7;
            if (wire == 2) {
                uint64_t len = read_varint(p, end);
                const uint8_t* sub_end = p + len;
                if (sub_end <= end) {
                    if (tag == 1) {
                        msg.sender_nick = std::string((const char*)p, len);
                    } else if (tag == 2 && msg.sender_card.empty()) {
                        msg.sender_card = std::string((const char*)p, len);
                    } else if (tag == 7) {
                        msg.sender_title = std::string((const char*)p, len);
                    }
                }
                p = sub_end;
            } else if (wire == 0) {
                read_varint(p, end);
            } else {
                skip_field(wire, p, end);
            }
        }
    }

    static void parse_common_elem(const uint8_t* p, const uint8_t* end, ParsedMessage& msg) {
        uint32_t service_type = 0;
        uint32_t business_type = 0;
        std::vector<uint8_t> pb_elem;

        while (p < end) {
            uint64_t tw = read_varint(p, end);
            uint32_t tag = tw >> 3;
            uint32_t wire = tw & 7;
            if (wire == 0) {
                uint64_t val = read_varint(p, end);
                if (tag == 1) service_type = (uint32_t)val;
                else if (tag == 3) business_type = (uint32_t)val;
            } else if (wire == 2) {
                uint64_t len = read_varint(p, end);
                const uint8_t* sub_end = p + len;
                if (sub_end <= end) {
                    if (tag == 2) {
                        pb_elem.assign(p, sub_end);
                    }
                }
                p = sub_end;
            } else {
                skip_field(wire, p, end);
            }
        }

        if (service_type == 48 && !pb_elem.empty()) {
            // Parse MsgInfo
            // bizType: 10/20 = image, 12/22 = voice (ptt)
            if (business_type == 10 || business_type == 20) {
                std::string file_uuid;
                std::string file_url;
                // scan pb_elem for file info
                msg.message_elements.push_back({
                    {"type", "image"},
                    {"data", {
                        {"file", "image_common_elem"},
                        {"url", ""}
                    }}
                });
            } else if (business_type == 12 || business_type == 22) {
                msg.message_elements.push_back({
                    {"type", "record"},
                    {"data", {
                        {"file", "record_common_elem"}
                    }}
                });
            }
        }
    }

    static void parse_text_elem(const uint8_t* p, const uint8_t* end, ParsedMessage& msg) {
        std::string content;
        std::string at_uid;
        uint64_t at_uin = 0;
        while (p < end) {
            uint64_t tw = read_varint(p, end);
            uint32_t tag = tw >> 3;
            uint32_t wire = tw & 7;
            if (wire == 2) {
                uint64_t len = read_varint(p, end);
                if (tag == 1) {
                    content = std::string((const char*)p, len);
                } else if (tag == 3) {
                    // Tag 3 has 13 bytes: 00 01 00 00 00 02 00 [4-byte big-endian UIN] 00 00
                    if (len >= 11 && p[0] == 0 && p[1] == 1) {
                        uint32_t uin_be = ((uint32_t)p[7] << 24) |
                                          ((uint32_t)p[8] << 16) |
                                          ((uint32_t)p[9] << 8)  |
                                          ((uint32_t)p[10]);
                        if (uin_be != 0) {
                            at_uin = uin_be;
                        }
                    }
                } else if (tag == 12) {
                    const uint8_t* ap = p;
                    const uint8_t* aend = p + len;
                    while (ap < aend) {
                        uint64_t atw = read_varint(ap, aend);
                        uint32_t atag = atw >> 3;
                        uint32_t awire = atw & 7;
                        if (awire == 2) {
                            uint64_t alen = read_varint(ap, aend);
                            if (atag == 9) {
                                at_uid = std::string((const char*)ap, alen);
                            }
                            ap += alen;
                        } else if (awire == 0) {
                            uint64_t v = read_varint(ap, aend);
                            if (atag == 4 && v != 0 && at_uin == 0) {
                                at_uin = v;
                            }
                        } else break;
                    }
                }
                p += len;
            } else if (wire == 0) {
                read_varint(p, end);
            } else {
                skip_field(wire, p, end);
            }
        }

        if (!content.empty()) {
            msg.text += content;
            if (!at_uid.empty() || at_uin != 0) {
                // OneBot v11 standard requires at.data.qq to be int or numeric string or 'all'
                json at_data = json::object();
                if (at_uin != 0) {
                    at_data["qq"] = at_uin;
                } else {
                    at_data["qq"] = 0;
                }
                at_data["text"] = content;
                msg.message_elements.push_back({
                    {"type", "at"},
                    {"data", at_data}
                });
            } else {
                msg.message_elements.push_back({
                    {"type", "text"},
                    {"data", {{"text", content}}}
                });
            }
        }
    }

    static void parse_face_elem(const uint8_t* p, const uint8_t* end, ParsedMessage& msg) {
        while (p < end) {
            uint64_t tw = read_varint(p, end);
            uint32_t tag = tw >> 3;
            uint32_t wire = tw & 7;
            if (wire == 0) {
                uint64_t val = read_varint(p, end);
                if (tag == 1) {
                    msg.text += "[动画表情]";
                    msg.message_elements.push_back({
                        {"type", "face"},
                        {"data", {{"id", std::to_string(val)}}}
                    });
                }
            } else if (wire == 2) {
                uint64_t len = read_varint(p, end);
                p += len;
            } else {
                skip_field(wire, p, end);
            }
        }
    }

    static void parse_pic_elem(const uint8_t* p, const uint8_t* end, ParsedMessage& msg) {
        msg.text += "[图片]";
        msg.message_elements.push_back({
            {"type", "image"},
            {"data", {{"file", "pic"}}}
        });
    }
};

} // namespace lepus::ntqq
