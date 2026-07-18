#pragma once

#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <vector>
#include <string>

class SubscriptionManager {
public:
    void subscribe(int client_id, const std::string& symbol);
    void unsubscribe(int client_id, const std::string& symbol);
    void remove_client(int client_id);

    std::vector<int>    get_subscribers(const std::string& symbol);
    std::vector<std::string> get_subscriptions(int client_id);

private:
    std::mutex mtx_;
    std::unordered_map<std::string, std::unordered_set<int>>    symbol_to_clients_;
    std::unordered_map<int,    std::unordered_set<std::string>> client_to_symbols_;
};
