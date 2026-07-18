#include "subscription_manager.hpp"

using namespace std;

void SubscriptionManager::subscribe(int client_id, const string& symbol) {
    lock_guard<mutex> lock(mtx_);
    symbol_to_clients_[symbol].insert(client_id);
    client_to_symbols_[client_id].insert(symbol);
}

void SubscriptionManager::unsubscribe(int client_id, const string& symbol) {
    lock_guard<mutex> lock(mtx_);
    symbol_to_clients_[symbol].erase(client_id);
    client_to_symbols_[client_id].erase(symbol);
}

void SubscriptionManager::remove_client(int client_id) {
    lock_guard<mutex> lock(mtx_);
    auto it = client_to_symbols_.find(client_id);
    if (it != client_to_symbols_.end()) {
        for (const string& sym : it->second)
            symbol_to_clients_[sym].erase(client_id);
        client_to_symbols_.erase(it);
    }
}

vector<int> SubscriptionManager::get_subscribers(const string& symbol) {
    lock_guard<mutex> lock(mtx_);
    vector<int> result;
    auto it = symbol_to_clients_.find(symbol);
    if (it != symbol_to_clients_.end())
        result.assign(it->second.begin(), it->second.end());
    return result;
}

vector<string> SubscriptionManager::get_subscriptions(int client_id) {
    lock_guard<mutex> lock(mtx_);
    vector<string> result;
    auto it = client_to_symbols_.find(client_id);
    if (it != client_to_symbols_.end())
        result.assign(it->second.begin(), it->second.end());
    return result;
}
