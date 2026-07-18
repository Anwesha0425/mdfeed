#pragma once

#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <vector>
#include <string>

class SubscriptionManager {
public:
    void subscribe(int client_id, string symbol);
    void unsubscribe(int client_id, string symbol);
    void remove_client(int client_id);

    vector<int>    get_subscribers(string symbol);
    vector<string> get_subscriptions(int client_id);

private:
    mutex mtx_;
    unordered_map<string, unordered_set<int>>    symbol_to_clients_;
    unordered_map<int,    unordered_set<string>> client_to_symbols_;
};
