#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <atomic>
#include <iomanip>
#include "../include/order_book.hpp"

using namespace std;
using namespace chrono;

void bench_single_thread_update_rate() {
    OrderBook book("BENCH");

    const int N = 1000000;
    double price = 100.0;

    auto start = steady_clock::now();
    for (int i = 0; i < N; i++) {
        book.update_bid(price + (i % 10) * 0.01, 100);
        book.update_ask(price + (i % 10) * 0.01 + 0.50, 100);
    }
    auto end = steady_clock::now();

    double ms = duration_cast<microseconds>(end - start).count() / 1000.0;
    double ops_per_sec = (N * 2.0) / (ms / 1000.0);

    cout << fixed << setprecision(0);
    cout << "single-thread update rate:  " << ops_per_sec / 1e6 << "M updates/sec";
    cout << "  (" << N * 2 << " ops in " << ms << "ms)\n";
}

void bench_concurrent_update_rate() {
    OrderBook book("BENCH");

    const int num_threads = 4;
    const int N           = 500000;
    atomic<long long> total_ops{0};

    auto start = steady_clock::now();

    vector<thread> threads;
    for (int i = 0; i < num_threads; i++) {
        threads.emplace_back([&book, &total_ops, i, N]() {
            double base = 100.0 + i * 5.0;
            for (int j = 0; j < N; j++) {
                book.update_bid(base + (j % 10) * 0.01, 100);
                book.update_ask(base + (j % 10) * 0.01 + 0.50, 100);
            }
            total_ops += N * 2;
        });
    }

    for (auto& t : threads)
        t.join();

    auto end = steady_clock::now();
    double ms = duration_cast<microseconds>(end - start).count() / 1000.0;
    double ops_per_sec = total_ops.load() / (ms / 1000.0);

    cout << num_threads << "-thread concurrent rate:   " << ops_per_sec / 1e6 << "M updates/sec";
    cout << "  (" << total_ops << " ops in " << ms << "ms)\n";
}

void bench_read_rate() {
    OrderBook book("BENCH");

    for (int i = 0; i < 20; i++) {
        book.update_bid(100.0 + i * 0.01, 100);
        book.update_ask(101.0 + i * 0.01, 100);
    }

    const int N = 2000000;

    auto start = steady_clock::now();
    double sink = 0.0;
    for (int i = 0; i < N; i++) {
        sink += book.best_bid();
        sink += book.best_ask();
    }
    auto end = steady_clock::now();

    double ms = duration_cast<microseconds>(end - start).count() / 1000.0;
    double reads_per_sec = (N * 2.0) / (ms / 1000.0);
    (void)sink;

    cout << "single-thread read rate:    " << reads_per_sec / 1e6 << "M reads/sec";
    cout << "  (" << N * 2 << " reads in " << ms << "ms)\n";
}

int main() {
    cout << "\norder book throughput benchmark\n";
    cout << "--------------------------------\n";

    bench_single_thread_update_rate();
    bench_read_rate();
    bench_concurrent_update_rate();

    cout << "\nnote: this measures the order book in isolation (no network).\n";
    cout << "for end-to-end latency, run the server and client separately\n";
    cout << "and compare timestamps in the feed vs client output.\n";

    return 0;
}
