#pragma once

#include <thread>
#include <atomic>
#include <functional>
#include <vector>
#include <string>
#include <random>

struct MarketTick {
    std::string symbol;
    double bid_price;
    double bid_qty;
    double ask_price;
    double ask_qty;
};

using TickCallback = std::function<void(MarketTick)>;

class FeedSimulator {
public:
    FeedSimulator(int ticks_per_second = 5);
    ~FeedSimulator();

    void start(TickCallback cb);
    void stop();

private:
    void   run();
    double next_price(double current, double sigma);

    int               ticks_per_sec_;
    std::atomic<bool> running_;
    std::thread       thread_;
    TickCallback      callback_;

    struct SymbolState {
        std::string name;
        double price;
        double sigma;
        double spread_pct;
    };

    std::vector<SymbolState>         symbols_;
    std::mt19937                     rng_;
    std::normal_distribution<double> dist_;
};
