#include <iostream>
#include <string>
#include <sstream>
#include <iomanip>
#include <thread>
#include <atomic>
#include <map>
#include <algorithm>
#include <chrono>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "protocol.hpp"

using namespace std;

atomic<bool> g_running{true};

map<string, uint64_t> last_seq;

void recv_loop(int sockfd) {
    char buf[4096];
    string leftover;

    while (g_running) {
        int n = recv(sockfd, buf, sizeof(buf) - 1, 0);
        if (n <= 0) {
            cout << "\n[client] server closed the connection\n";
            g_running = false;
            break;
        }
        buf[n] = '\0';
        leftover += buf;

        size_t pos;
        while ((pos = leftover.find('\n')) != string::npos) {
            string line = leftover.substr(0, pos);
            leftover    = leftover.substr(pos + 1);
            if (line.empty()) continue;

            Message msg = decode_message(line);

            if (msg.type == MsgType::UPDATE && msg.seq_num > 0) {
                uint64_t prev = last_seq[msg.symbol];
                if (prev > 0 && msg.seq_num != prev + 1) {
                    cout << "[gap] " << msg.symbol << " missed seq "
                         << prev + 1 << " to " << msg.seq_num - 1 << ". Requesting replay...\n";
                    Message req;
                    req.type = MsgType::REPLAY_REQUEST;
                    req.symbol = msg.symbol;
                    req.data = to_string(prev + 1) + ":" + to_string(msg.seq_num - 1);
                    string enc = encode_message(req);
                    send(sockfd, enc.c_str(), enc.size(), 0);
                }
                last_seq[msg.symbol] = msg.seq_num;
            }

            if (msg.type == MsgType::LOGON) {
                cout << "[server] " << msg.data << "\n\n";

            } else if (msg.type == MsgType::SNAPSHOT) {
                cout << "\n+--[ " << msg.symbol << " ]----------+\n";
                cout << msg.data;
                cout << "+---------------------------+\n\n";

            } else if (msg.type == MsgType::UPDATE) {
                uint64_t now = chrono::duration_cast<chrono::microseconds>(
                                   chrono::system_clock::now().time_since_epoch()
                               ).count();
                double latency_ms = 0.0;
                if (msg.timestamp > 0 && now >= msg.timestamp) {
                    latency_ms = (now - msg.timestamp) / 1000.0;
                }

                istringstream iss(msg.data);
                string bp, bq, ap, aq;
                getline(iss, bp, ':');
                getline(iss, bq, ':');
                getline(iss, ap, ':');
                getline(iss, aq, ':');

                try {
                    double bid = stod(bp);
                    double ask = stod(ap);
                    int bqv = (int)stod(bq);
                    int aqv = (int)stod(aq);
                    cout << "[" << msg.symbol << "]"
                         << "  bid=" << fixed << setprecision(2) << bid << " (" << bqv << ")"
                         << "  ask=" << ask << " (" << aqv << ")"
                         << "  spread=" << (ask - bid);
                    if (latency_ms > 0) {
                        cout << "  latency=" << latency_ms << "ms";
                    }
                    cout << "\n";
                } catch (...) {
                    cout << "[" << msg.symbol << "] " << msg.data << "\n";
                }

            } else if (msg.type == MsgType::HEARTBEAT) {
                cout << "[pong]\n";

            } else if (msg.type == MsgType::ERROR_MSG) {
                cout << "[error] " << msg.data << "\n";
            }
        }
    }
}

int main(int argc, char* argv[]) {
    const char* host = "127.0.0.1";
    int port = 9001;
    if (argc > 1) host = argv[1];
    if (argc > 2) port = atoi(argv[2]);

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return 1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    inet_pton(AF_INET, host, &addr.sin_addr);

    if (connect(sockfd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect");
        return 1;
    }

    cout << "connected to " << host << ":" << port << "\n";
    cout << "commands: sub <SYMBOL>  |  unsub <SYMBOL>  |  heartbeat  |  quit\n\n";

    thread recv_t(recv_loop, sockfd);

    string line;
    while (g_running && getline(cin, line)) {
        if (line.empty()) continue;

        if (line == "quit") {
            Message msg;
            msg.type = MsgType::LOGOUT;
            string enc = encode_message(msg);
            send(sockfd, enc.c_str(), enc.size(), 0);
            g_running = false;
            break;
        }

        if (line == "heartbeat") {
            Message msg;
            msg.type = MsgType::HEARTBEAT;
            string enc = encode_message(msg);
            send(sockfd, enc.c_str(), enc.size(), 0);
            continue;
        }

        istringstream iss(line);
        string cmd, sym;
        iss >> cmd >> sym;

        if (sym.empty()) {
            cout << "usage: sub <SYMBOL>  |  unsub <SYMBOL>\n";
            continue;
        }

        transform(sym.begin(), sym.end(), sym.begin(), ::toupper);

        Message msg;
        if (cmd == "sub") {
            msg.type   = MsgType::SUBSCRIBE;
            msg.symbol = sym;
        } else if (cmd == "unsub") {
            msg.type   = MsgType::UNSUBSCRIBE;
            msg.symbol = sym;
        } else {
            cout << "unknown command\n";
            continue;
        }

        string enc = encode_message(msg);
        send(sockfd, enc.c_str(), enc.size(), 0);
    }

    shutdown(sockfd, SHUT_RDWR);
    if (recv_t.joinable()) recv_t.join();
    close(sockfd);
    return 0;
}
