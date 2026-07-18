#pragma once

#include <map>
#include <mutex>
#include <string>
#include <sstream>
#include <iomanip>

class OrderBook {
public:
    OrderBook(std::string symbol);

    void update_bid(double price, double qty);
    void update_ask(double price, double qty);

    double best_bid()  const;
    double best_ask()  const;
    double mid_price() const;
    double spread()    const;

    std::string snapshot() const;
    std::string symbol()   const { return symbol_; }

private:
    std::string symbol_;
    std::map<double, double> bids_;
    std::map<double, double> asks_;
    mutable std::mutex mtx_;
};
