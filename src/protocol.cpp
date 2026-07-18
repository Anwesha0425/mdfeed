#include "protocol.hpp"
#include <sstream>
#include <vector>

using namespace std;

string msg_type_to_str(MsgType t) {
    switch (t) {
        case MsgType::LOGON:       return "LOGON";
        case MsgType::LOGOUT:      return "LOGOUT";
        case MsgType::SUBSCRIBE:   return "SUBSCRIBE";
        case MsgType::UNSUBSCRIBE: return "UNSUBSCRIBE";
        case MsgType::SNAPSHOT:    return "SNAPSHOT";
        case MsgType::UPDATE:      return "UPDATE";
        case MsgType::HEARTBEAT:   return "HEARTBEAT";
        case MsgType::ERROR_MSG:   return "ERROR";
        default:                   return "UNKNOWN";
    }
}

MsgType str_to_msg_type(string s) {
    if (s == "LOGON")       return MsgType::LOGON;
    if (s == "LOGOUT")      return MsgType::LOGOUT;
    if (s == "SUBSCRIBE")   return MsgType::SUBSCRIBE;
    if (s == "UNSUBSCRIBE") return MsgType::UNSUBSCRIBE;
    if (s == "SNAPSHOT")    return MsgType::SNAPSHOT;
    if (s == "UPDATE")      return MsgType::UPDATE;
    if (s == "HEARTBEAT")   return MsgType::HEARTBEAT;
    if (s == "ERROR")       return MsgType::ERROR_MSG;
    return MsgType::UNKNOWN;
}

string encode_message(Message msg) {
    ostringstream oss;
    oss << msg_type_to_str(msg.type) << "|"
        << msg.symbol << "|"
        << msg.data   << "|"
        << msg.seq_num << "\n";
    return oss.str();
}

Message decode_message(string raw) {
    Message msg;
    vector<string> parts;
    istringstream iss(raw);
    string token;

    while (getline(iss, token, '|'))
        parts.push_back(token);

    if (parts.size() < 4) {
        msg.type = MsgType::UNKNOWN;
        return msg;
    }

    msg.type    = str_to_msg_type(parts[0]);
    msg.symbol  = parts[1];
    msg.data    = parts[2];

    try {
        msg.seq_num = stoi(parts[3]);
    } catch (...) {
        msg.seq_num = 0;
    }

    return msg;
}
