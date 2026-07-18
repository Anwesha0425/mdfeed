#pragma once

#include <map>
#include <mutex>
#include <string>
#include <sstream>
#include <iomanip>

class OrderBook {
public:
    OrderBook(string symbol);

    void update_bid(double price, double qty);
    void update_ask(double price, double qty);

    double best_bid()  const;
    double best_ask()  const;
    double mid_price() const;
    double spread()    const;

    string snapshot() const;
    string symbol()   const { return symbol_; }

private:
    string symbol_;
    map<double, double> bids_;
    map<double, double> asks_;
    mutable mutex mtx_;
};
