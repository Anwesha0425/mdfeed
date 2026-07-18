#include <iostream>
#include <cassert>
#include <thread>
#include <vector>
#include <cmath>
#include "../include/order_book.hpp"

using namespace std;

void test_basic_bid_ask() {
    OrderBook book("TEST");

    book.update_bid(100.0, 500);
    book.update_ask(101.0, 300);

    assert(book.best_bid() == 100.0);
    assert(book.best_ask() == 101.0);
    assert(book.spread() == 1.0);
    assert(book.mid_price() == 100.5);

    cout << "PASS  test_basic_bid_ask\n";
}

void test_best_levels_with_multiple_prices() {
    OrderBook book("TEST");

    book.update_bid(99.0,  100);
    book.update_bid(100.0, 200);   // best bid
    book.update_bid(98.0,  300);

    book.update_ask(101.0, 100);   // best ask
    book.update_ask(102.0, 200);
    book.update_ask(103.0, 300);

    assert(book.best_bid() == 100.0);
    assert(book.best_ask() == 101.0);

    cout << "PASS  test_best_levels_with_multiple_prices\n";
}

void test_remove_level() {
    OrderBook book("TEST");

    book.update_bid(100.0, 500);
    book.update_bid(99.0,  300);

    book.update_bid(100.0, 0);   // qty=0 removes the level

    assert(book.best_bid() == 99.0);

    cout << "PASS  test_remove_level\n";
}

void test_empty_book_returns_zero() {
    OrderBook book("TEST");

    assert(book.best_bid()  == 0.0);
    assert(book.best_ask()  == 0.0);
    assert(book.mid_price() == 0.0);
    assert(book.spread()    == 0.0);

    cout << "PASS  test_empty_book_returns_zero\n";
}

void test_concurrent_writes() {
    OrderBook book("TEST");

    const int num_threads        = 8;
    const int updates_per_thread = 2000;
    vector<thread> threads;

    for (int i = 0; i < num_threads; i++) {
        threads.emplace_back([&book, i]() {
            for (int j = 0; j < updates_per_thread; j++) {
                double price = 100.0 + (i * 1.0) + (j * 0.001);
                book.update_bid(price, 100 + j);
                book.update_ask(price + 1.0, 100 + j);

                double bid = book.best_bid();
                double ask = book.best_ask();
                (void)bid;
                (void)ask;
            }
        });
    }

    for (auto& t : threads)
        t.join();

    double bid = book.best_bid();
    assert(bid > 0.0);

    cout << "PASS  test_concurrent_writes  (" << num_threads
         << " threads x " << updates_per_thread << " updates)\n";
}

void test_snapshot_not_empty() {
    OrderBook book("AAPL");

    book.update_bid(184.50, 300);
    book.update_ask(184.60, 200);

    string snap = book.snapshot();
    assert(!snap.empty());
    assert(snap.find("184.50") != string::npos);
    assert(snap.find("184.60") != string::npos);

    cout << "PASS  test_snapshot_not_empty\n";
}

int main() {
    cout << "running order book tests\n\n";

    test_empty_book_returns_zero();
    test_basic_bid_ask();
    test_best_levels_with_multiple_prices();
    test_remove_level();
    test_snapshot_not_empty();
    test_concurrent_writes();

    cout << "\nall tests passed\n";
    return 0;
}
