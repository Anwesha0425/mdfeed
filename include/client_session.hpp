#pragma once

#include <thread>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <atomic>
#include <string>
#include <functional>

using DisconnectCb = function<void(int)>;
using CommandCb    = function<void(int, string, string)>;

class ClientSession {
public:
    ClientSession(int id, int sockfd, DisconnectCb on_disconnect, CommandCb on_command);
    ~ClientSession();

    void start();
    void send_message(string msg);

    int  id()       { return id_; }
    bool is_alive() { return alive_.load(); }

private:
    void recv_loop();
    void send_loop();

    int          id_;
    int          sockfd_;
    atomic<bool> alive_;

    queue<string>           send_queue_;
    mutex                   queue_mtx_;
    condition_variable      queue_cv_;

    thread recv_thread_;
    thread send_thread_;

    DisconnectCb on_disconnect_;
    CommandCb    on_command_;
};
