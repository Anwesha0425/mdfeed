#include "client_session.hpp"
#include "protocol.hpp"

#include <iostream>
#include <sys/socket.h>
#include <unistd.h>

using namespace std;

ClientSession::ClientSession(int id, int sockfd, DisconnectCb on_disconnect, CommandCb on_command)
    : id_(id)
    , sockfd_(sockfd)
    , alive_(false)
    , on_disconnect_(on_disconnect)
    , on_command_(on_command)
{}

ClientSession::~ClientSession() {
    alive_ = false;
    queue_cv_.notify_all();
    close(sockfd_);
    
    if (recv_thread_.joinable()) {
        if (std::this_thread::get_id() == recv_thread_.get_id()) {
            recv_thread_.detach();
        } else {
            recv_thread_.join();
        }
    }
    
    if (send_thread_.joinable()) {
        if (std::this_thread::get_id() == send_thread_.get_id()) {
            send_thread_.detach();
        } else {
            send_thread_.join();
        }
    }
}

void ClientSession::start() {
    alive_       = true;
    recv_thread_ = thread(&ClientSession::recv_loop, this);
    send_thread_ = thread(&ClientSession::send_loop, this);
}

void ClientSession::send_message(string msg) {
    if (!alive_) return;
    {
        lock_guard<mutex> lock(queue_mtx_);
        send_queue_.push(msg);
    }
    queue_cv_.notify_one();
}

void ClientSession::recv_loop() {
    char buf[4096];
    string leftover;

    while (alive_) {
        int n = recv(sockfd_, buf, sizeof(buf) - 1, 0);
        if (n <= 0) break;

        buf[n] = '\0';
        leftover += buf;

        size_t pos;
        while ((pos = leftover.find('\n')) != string::npos) {
            string line = leftover.substr(0, pos);
            leftover    = leftover.substr(pos + 1);
            if (line.empty()) continue;

            Message msg = decode_message(line);

            if (msg.type == MsgType::SUBSCRIBE || msg.type == MsgType::UNSUBSCRIBE || msg.type == MsgType::REPLAY_REQUEST) {
                on_command_(id_, msg);
            } else if (msg.type == MsgType::HEARTBEAT) {
                on_command_(id_, msg);
            } else if (msg.type == MsgType::LOGOUT) {
                alive_ = false;
            }
        }
    }

    alive_ = false;
    queue_cv_.notify_all();

    DisconnectCb cb = on_disconnect_;
    int cid = id_;
    cb(cid);
}

void ClientSession::send_loop() {
    while (alive_) {
        unique_lock<mutex> lock(queue_mtx_);
        queue_cv_.wait(lock, [this] {
            return !send_queue_.empty() || !alive_;
        });

        while (!send_queue_.empty()) {
            string msg = send_queue_.front();
            send_queue_.pop();
            lock.unlock();

            int sent = send(sockfd_, msg.c_str(), msg.size(), 0);
            if (sent < 0) {
                alive_ = false;
                return;
            }

            lock.lock();
        }
    }
}
