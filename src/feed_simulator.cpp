#include "feed_simulator.hpp"
#include <cmath>
#include <thread>
#include <chrono>

using namespace std;

FeedSimulator::FeedSimulator(int ticks_per_second)
    : ticks_per_sec_(ticks_per_second)
    , running_(false)
    , rng_(chrono::steady_clock::now().time_since_epoch().count())
    , dist_(0.0, 1.0)
{
    symbols_ = {
        {"AAPL",  185.00, 0.012, 0.0008},
        {"MSFT",  375.00, 0.011, 0.0007},
        {"GOOGL", 165.00, 0.013, 0.0009},
        {"TSLA",  245.00, 0.030, 0.0015},
        {"SPY",   520.00, 0.007, 0.0003},
        {"NVDA",  800.00, 0.028, 0.0012},
    };
}

FeedSimulator::~FeedSimulator() {
    stop();
}

void FeedSimulator::start(TickCallback cb) {
    callback_ = cb;
    running_  = true;
    thread_   = thread(&FeedSimulator::run, this);
}

void FeedSimulator::stop() {
    running_ = false;
    if (thread_.joinable())
        thread_.join();
}

double FeedSimulator::next_price(double current, double sigma) {
    return current * exp(sigma * dist_(rng_));
}

void FeedSimulator::run() {
    auto interval = chrono::milliseconds(1000 / ticks_per_sec_);

    while (running_) {
        auto tick_start = chrono::steady_clock::now();

        for (auto& sym : symbols_) {
            sym.price = next_price(sym.price, sym.sigma);

            double half_spread = sym.price * sym.spread_pct;

            MarketTick tick;
            tick.symbol    = sym.name;
            tick.bid_price = sym.price - half_spread;
            tick.bid_qty   = 100.0 * (1 + rng_() % 10);
            tick.ask_price = sym.price + half_spread;
            tick.ask_qty   = 100.0 * (1 + rng_() % 10);

            if (callback_)
                callback_(tick);
        }

        auto elapsed    = chrono::steady_clock::now() - tick_start;
        auto sleep_time = interval - elapsed;
        if (sleep_time.count() > 0)
            this_thread::sleep_for(sleep_time);
    }
}
