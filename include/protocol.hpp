#pragma once

#include <string>

enum class MsgType {
    LOGON,
    LOGOUT,
    SUBSCRIBE,
    UNSUBSCRIBE,
    SNAPSHOT,
    UPDATE,
    HEARTBEAT,
    ERROR_MSG,
    UNKNOWN
};

struct Message {
    MsgType type   = MsgType::UNKNOWN;
    std::string  symbol;
    std::string  data;
    int     seq_num = 0;
};

std::string  encode_message(Message msg);
Message decode_message(std::string raw);
std::string  msg_type_to_str(MsgType t);
MsgType str_to_msg_type(std::string s);
