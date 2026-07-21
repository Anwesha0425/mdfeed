#pragma once

#include <thread>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <atomic>
#include <string>
#include <functional>

#include "protocol.hpp"

using DisconnectCb = std::function<void(int)>;
using CommandCb    = std::function<void(int, Message)>;

class ClientSession {
public:
    ClientSession(int id, int sockfd, DisconnectCb on_disconnect, CommandCb on_command);
    ~ClientSession();

    void start();
    void send_message(std::string msg);

    int  id()       { return id_; }
    bool is_alive() { return alive_.load(); }

private:
    void recv_loop();
    void send_loop();

    int          id_;
    int          sockfd_;
    std::atomic<bool> alive_;

    std::queue<std::string> send_queue_;
    std::mutex                   queue_mtx_;
    std::condition_variable      queue_cv_;

    std::thread recv_thread_;
    std::thread send_thread_;

    DisconnectCb on_disconnect_;
    CommandCb    on_command_;
};
