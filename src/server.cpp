#include <iostream>
#include <map>
#include <mutex>
#include <memory>
#include <vector>
#include <string>
#include <atomic>
#include <csignal>
#include <deque>
#include <unordered_map>
#include <thread>
#include <chrono>

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/select.h>

#include "protocol.hpp"
#include "order_book.hpp"
#include "feed_simulator.hpp"
#include "subscription_manager.hpp"
#include "client_session.hpp"

using namespace std;

atomic<bool> g_running{true};

map<string, shared_ptr<OrderBook>> g_books;
mutex g_books_mtx;

SubscriptionManager g_sub_mgr;

map<int, shared_ptr<ClientSession>> g_sessions;
mutex g_sessions_mtx;

atomic<int> g_next_id{1};

unordered_map<string, uint64_t> g_seq_nums;
unordered_map<string, deque<Message>> g_replay_buffer;
mutex g_replay_mtx;

struct Metrics {
    atomic<uint64_t> updates{0};
    atomic<uint64_t> messages{0};
    atomic<uint64_t> clients{0};
    atomic<uint64_t> subscriptions{0};
};
Metrics g_metrics;

void handle_sigint(int) {
    cout << "\n[server] shutting down...\n";
    g_running = false;
}

void on_tick(MarketTick tick) {
    g_metrics.updates++;

    {
        lock_guard<mutex> lock(g_books_mtx);
        auto it = g_books.find(tick.symbol);
        if (it == g_books.end()) return;
        it->second->update_bid(tick.bid_price, tick.bid_qty);
        it->second->update_ask(tick.ask_price, tick.ask_qty);
    }

    vector<int> subs = g_sub_mgr.get_subscribers(tick.symbol);
    if (subs.empty()) return;

    Message msg;
    msg.type   = MsgType::UPDATE;
    msg.symbol = tick.symbol;
    msg.data   = to_string(tick.bid_price) + ":"
               + to_string(tick.bid_qty)   + ":"
               + to_string(tick.ask_price) + ":"
               + to_string(tick.ask_qty);
    msg.timestamp = chrono::duration_cast<chrono::microseconds>(
                        chrono::system_clock::now().time_since_epoch()
                    ).count();

    {
        lock_guard<mutex> lock(g_replay_mtx);
        msg.seq_num = ++g_seq_nums[tick.symbol];
        auto& dq = g_replay_buffer[tick.symbol];
        dq.push_back(msg);
        if (dq.size() > 1000) {
            dq.pop_front();
        }
    }

    string encoded = encode_message(msg);

    lock_guard<mutex> lock(g_sessions_mtx);
    for (int cid : subs) {
        auto it = g_sessions.find(cid);
        if (it != g_sessions.end() && it->second->is_alive()) {
            it->second->send_message(encoded);
            g_metrics.messages++;
        }
    }
}

void on_command(int client_id, Message msg) {
    if (msg.type == MsgType::SUBSCRIBE) {
        g_sub_mgr.subscribe(client_id, msg.symbol);
        g_metrics.subscriptions++;

        shared_ptr<OrderBook> book;
        {
            lock_guard<mutex> lock(g_books_mtx);
            auto it = g_books.find(msg.symbol);
            if (it != g_books.end())
                book = it->second;
        }

        Message reply;
        if (book) {
            reply.type   = MsgType::SNAPSHOT;
            reply.symbol = msg.symbol;
            reply.data   = book->snapshot();
        } else {
            reply.type   = MsgType::ERROR_MSG;
            reply.symbol = msg.symbol;
            reply.data   = "unknown symbol";
        }

        lock_guard<mutex> lock(g_sessions_mtx);
        auto it = g_sessions.find(client_id);
        if (it != g_sessions.end()) {
            it->second->send_message(encode_message(reply));
            g_metrics.messages++;
        }

    } else if (msg.type == MsgType::UNSUBSCRIBE) {
        g_sub_mgr.unsubscribe(client_id, msg.symbol);
        g_metrics.subscriptions--;

    } else if (msg.type == MsgType::HEARTBEAT) {
        Message pong;
        pong.type = MsgType::HEARTBEAT;

        lock_guard<mutex> lock(g_sessions_mtx);
        auto it = g_sessions.find(client_id);
        if (it != g_sessions.end()) {
            it->second->send_message(encode_message(pong));
            g_metrics.messages++;
        }
    } else if (msg.type == MsgType::REPLAY_REQUEST) {
        uint64_t start = 0, end = 0;
        size_t colon = msg.data.find(':');
        if (colon != string::npos) {
            try {
                start = stoull(msg.data.substr(0, colon));
                end = stoull(msg.data.substr(colon + 1));
            } catch (...) {}
        }
        
        vector<Message> replays;
        {
            lock_guard<mutex> lock(g_replay_mtx);
            auto it = g_replay_buffer.find(msg.symbol);
            if (it != g_replay_buffer.end()) {
                for (const auto& m : it->second) {
                    if (m.seq_num >= start && m.seq_num <= end) {
                        replays.push_back(m);
                    }
                }
            }
        }
        
        lock_guard<mutex> lock(g_sessions_mtx);
        auto it = g_sessions.find(client_id);
        if (it != g_sessions.end() && it->second->is_alive()) {
            for (const auto& m : replays) {
                it->second->send_message(encode_message(m));
                g_metrics.messages++;
            }
        }
    }
}

void on_disconnect(int client_id) {
    vector<string> subs = g_sub_mgr.get_subscriptions(client_id);
    g_metrics.subscriptions -= subs.size();
    g_sub_mgr.remove_client(client_id);
    
    {
        lock_guard<mutex> lock(g_sessions_mtx);
        g_sessions.erase(client_id);
    }
    g_metrics.clients--;
    cout << "[server] client " << client_id << " disconnected\n";
}

int main(int argc, char* argv[]) {
    int port = 9001;
    if (argc > 1)
        port = atoi(argv[1]);

    signal(SIGINT, handle_sigint);

    vector<string> symbols = {"AAPL", "MSFT", "GOOGL", "TSLA", "SPY", "NVDA"};
    for (string s : symbols) {
        g_books[s] = make_shared<OrderBook>(s);
        g_seq_nums[s] = 0;
    }

    FeedSimulator feed(5);
    feed.start(on_tick);

    thread metrics_t([]() {
        while (g_running) {
            this_thread::sleep_for(chrono::seconds(1));
            if (!g_running) break;
            uint64_t u = g_metrics.updates.exchange(0);
            uint64_t m = g_metrics.messages.exchange(0);
            uint64_t c = g_metrics.clients.load();
            uint64_t s = g_metrics.subscriptions.load();
            cout << "----------------------\n";
            cout << "Clients: " << c << "\n";
            cout << "Updates/sec: " << u << "\n";
            cout << "Messages/sec: " << m << "\n";
            cout << "Subscriptions: " << s << "\n";
            cout << "----------------------\n";
        }
    });

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);

    if (bind(server_fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }

    if (listen(server_fd, 10) < 0) {
        perror("listen");
        return 1;
    }

    cout << "[server] port " << port << " | symbols: ";
    for (string s : symbols) cout << s << " ";
    cout << "\n[server] Ctrl-C to stop\n\n";

    while (g_running) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(server_fd, &rfds);
        timeval tv{1, 0};

        if (select(server_fd + 1, &rfds, nullptr, nullptr, &tv) <= 0)
            continue;

        sockaddr_in client_addr{};
        socklen_t len = sizeof(client_addr);
        int client_fd = accept(server_fd, (sockaddr*)&client_addr, &len);
        if (client_fd < 0) continue;

        int flag = 1;
        setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

        int cid = g_next_id++;
        cout << "[server] client " << cid
             << " connected from " << inet_ntoa(client_addr.sin_addr) << "\n";

        g_metrics.clients++;

        auto session = make_shared<ClientSession>(cid, client_fd, on_disconnect, on_command);
        {
            lock_guard<mutex> lock(g_sessions_mtx);
            g_sessions[cid] = session;
        }
        session->start();

        Message logon;
        logon.type = MsgType::LOGON;
        logon.data = "connected | symbols: AAPL MSFT GOOGL TSLA SPY NVDA";
        session->send_message(encode_message(logon));
        g_metrics.messages++;
    }

    feed.stop();
    if (metrics_t.joinable()) metrics_t.join();
    close(server_fd);
    this_thread::sleep_for(chrono::milliseconds(300));
    cout << "[server] stopped\n";
    return 0;
}
