#include "order_book.hpp"
#include <sstream>
#include <iomanip>
#include <vector>

using namespace std;

OrderBook::OrderBook(string symbol) : symbol_(symbol) {}

void OrderBook::update_bid(double price, double qty) {
    unique_lock<shared_mutex> lock(mtx_);
    if (qty <= 0.0)
        bids_.erase(price);
    else
        bids_[price] = qty;
}

void OrderBook::update_ask(double price, double qty) {
    unique_lock<shared_mutex> lock(mtx_);
    if (qty <= 0.0)
        asks_.erase(price);
    else
        asks_[price] = qty;
}

double OrderBook::best_bid() const {
    shared_lock<shared_mutex> lock(mtx_);
    if (bids_.empty()) return 0.0;
    return bids_.rbegin()->first;
}

double OrderBook::best_ask() const {
    shared_lock<shared_mutex> lock(mtx_);
    if (asks_.empty()) return 0.0;
    return asks_.begin()->first;
}

double OrderBook::mid_price() const {
    double bid = best_bid();
    double ask = best_ask();
    if (bid == 0.0 || ask == 0.0) return 0.0;
    return (bid + ask) / 2.0;
}

double OrderBook::spread() const {
    double bid = best_bid();
    double ask = best_ask();
    if (bid == 0.0 || ask == 0.0) return 0.0;
    return ask - bid;
}

string OrderBook::snapshot() const {
    shared_lock<shared_mutex> lock(mtx_);
    ostringstream oss;
    oss << fixed << setprecision(2);

    oss << "  ASK\n";
    vector<pair<double, double>> top_asks;
    int cnt = 0;
    for (auto it = asks_.begin(); it != asks_.end() && cnt < 10; ++it, ++cnt) {
        top_asks.push_back(*it);
    }
    for (auto it = top_asks.rbegin(); it != top_asks.rend(); ++it) {
        oss << "  " << setw(10) << it->first << "   qty " << setw(5) << it->second << "\n";
    }

    oss << "  ----------\n";

    oss << "  BID\n";
    cnt = 0;
    for (auto it = bids_.rbegin(); it != bids_.rend() && cnt < 10; ++it, ++cnt) {
        oss << "  " << setw(10) << it->first << "   qty " << setw(5) << it->second << "\n";
    }

    return oss.str();
}
