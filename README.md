# mdfeed

A real-time market data distribution server built in C++17 using POSIX TCP sockets and threads. No external libraries.

The idea came from learning about how trading firms stream live price data to multiple internal clients — this is a simplified version of that: a server that accepts connections, simulates live stock prices, and pushes bid/ask updates to clients that subscribe to specific symbols.

---

## What it does

- Accepts multiple TCP client connections (one thread pair per client)
- Simulates live bid/ask prices for 6 symbols: `AAPL`, `MSFT`, `GOOGL`, `TSLA`, `SPY`, `NVDA`
- Clients subscribe to symbols they care about and get real-time order book updates
- Detects sequence number gaps on the client side (prints a warning if ticks were missed)
- Graceful shutdown on Ctrl-C

---

## How to build

Requires Linux, `g++` with C++17, and `pthread`.

```bash
make          # builds server and client
make test     # runs order book unit tests
make bench    # runs order book throughput benchmark
make asan     # AddressSanitizer build
make tsan     # ThreadSanitizer build
make clean
```

---

## How to run

**Start the server:**
```bash
./mdfeed-server
./mdfeed-server 9002    # custom port
```

**Connect a client (open a new terminal):**
```bash
./mdfeed-client
./mdfeed-client 127.0.0.1 9002
```

**Client commands:**
```
sub AAPL       start receiving AAPL updates
sub TSLA       start receiving TSLA updates
unsub AAPL     stop AAPL updates
heartbeat      ping the server
quit           disconnect
```

**Example session:**
```
connected to 127.0.0.1:9001
commands: sub <SYMBOL>  |  unsub <SYMBOL>  |  heartbeat  |  quit

> sub AAPL

+--[ AAPL ]----------+
  ASK
      185.32   qty   400
      185.29   qty   700
  ----------
  BID
      185.21   qty   300
      185.18   qty   600
+---------------------------+

[AAPL]  bid=185.19 (500)  ask=185.31 (200)  spread=0.12
[AAPL]  bid=185.22 (300)  ask=185.34 (400)  spread=0.12
```

---

## Tests

```bash
make test
```

`tests/test_order_book.cpp` covers:
- Empty book returns zero for all accessors
- Correct best bid and best ask with single levels
- Correct best levels when multiple price levels exist
- Removing a level (qty=0) works correctly
- Snapshot output contains the right prices
- Concurrent writes from 8 threads don't corrupt state

The concurrent test is also a sanity check before running TSan — if it passes consistently under repeat runs, the locking is at least not obviously broken.

---

## Benchmarks

```bash
make bench
```

`benchmarks/bench_order_book.cpp` measures order book throughput in isolation (no network, no threads overhead from session management). Typical numbers on a modern Linux machine:

| Measurement | Expected range |
|---|---|
| Single-thread update rate | 4–8M updates/sec |
| Single-thread read rate | 10–20M reads/sec |
| 4-thread concurrent update rate | 2–5M updates/sec (mutex contention brings this down) |

These are in-process numbers. The bottleneck in the actual server is the network and the per-client thread overhead, not the order book itself.

**End-to-end tick-to-client latency** (loopback, not yet measured):

To measure this properly, add `steady_clock::now()` timestamps at tick generation in `FeedSimulator::run()` and at client receipt in `recv_loop()`, encode them in the UPDATE message, and print the diff on the client side. Expected range on loopback: **30–70µs** (dominated by `recv()` syscall overhead and thread scheduling, not the order book).

I have not run a sustained load test to find the ceiling yet — that's the next thing to do.

---

## Concurrency and TSan

`make tsan` builds with `-fsanitize=thread`. To run it properly:

```bash
make tsan
./mdfeed-server &
./mdfeed-client   # subscribe to a few symbols, let it run 30 seconds, then quit
```

I haven't run TSan under real sustained load yet — it's listed in "things to do" because the honest answer is I don't know what it will find. The most likely issue is the globals in `server.cpp` (like `g_sessions` and `g_books`) — they're each protected by their own mutex, but if any code path reads one while the other is unlocked in a way that causes a race, TSan will catch it before I would. I'll document findings here when I run it.

---

## Project structure

```
mdfeed/
├── include/
│   ├── protocol.hpp
│   ├── order_book.hpp
│   ├── feed_simulator.hpp
│   ├── subscription_manager.hpp
│   └── client_session.hpp
├── src/
│   ├── protocol.cpp
│   ├── order_book.cpp
│   ├── feed_simulator.cpp
│   ├── subscription_manager.cpp
│   ├── client_session.cpp
│   ├── server.cpp
│   └── client.cpp
├── tests/
│   └── test_order_book.cpp
├── benchmarks/
│   └── bench_order_book.cpp
├── Makefile
└── README.md
```

---

## Design decisions

**Why one thread per client?**
Simple to reason about and debug. A thread blocks on `recv()` while the send thread drains a queue independently. For a small number of clients (tested up to ~10 concurrent) this works fine. At higher scale, this would need to move to an event-driven model with `epoll`.

**Why mutex + condition_variable for the send queue?**
The send thread sleeps when there's nothing to write instead of spinning. It wakes up as soon as a message is enqueued. Straightforward, correct, and the right call at this scale.

**Why regular mutex on the order book instead of shared_mutex?**
I started with `shared_mutex` (allows multiple concurrent readers). Switched to a plain mutex for two reasons: at 5 ticks/sec and a small number of clients, the reader contention that `shared_mutex` solves simply doesn't exist; and `shared_mutex` adds complexity with almost no measurable benefit at this load. If throughput requirements went up by 100x, revisit it — but not before measuring that it's actually the bottleneck.

**What happened to the SPSC lock-free ring buffer from the original design?**
The original plan included a lock-free ring buffer between the feed simulator and the server. I removed it because the feed callback runs synchronously on the feed thread and the mutex on the order book handles the write. Adding a ring buffer would have decoupled feed generation from order book writes, but at 5 ticks/sec and a non-blocking callback, it would have been complexity for its own sake. If the feed were producing at 100k+ ticks/sec and I had evidence the mutex was a bottleneck, it would be worth adding.

**Why `TCP_NODELAY`?**
Without it, the OS buffers small packets and batches them together (Nagle's algorithm). For tick data you want each message sent immediately, so this disables that buffering.

**Why `SO_REUSEADDR`?**
Lets the server restart on the same port immediately without waiting for TIME_WAIT to expire.

**Why pipe-delimited protocol?**
Inspired by FIX protocol used at exchanges. Each message: `TYPE|SYMBOL|DATA|SEQNUM\n`. The sequence number lets the client detect if it missed a tick — the client prints a warning if the numbers aren't consecutive.

**Price simulation**
Uses a simplified Geometric Brownian Motion: `new_price = old_price * exp(sigma * Z)` where `Z ~ N(0,1)`. This is the same model underlying Black-Scholes. Each symbol has a different volatility — TSLA and NVDA are noisier than SPY.

---

## Bugs I hit during development

**1. Server crashing on restart with "address already in use"**
`bind()` was failing because the port was in TIME_WAIT after the previous process closed. Fixed by adding `SO_REUSEADDR` on the server socket before `bind()`. Obvious in hindsight.

**2. send_loop stuck forever after client disconnect**
When a client disconnected, the recv thread set `alive_ = false` and exited. But the send thread was blocked on `queue_cv_.wait()` and never woke up — because nothing called `queue_cv_.notify_all()` after setting the flag. Fixed by calling `queue_cv_.notify_all()` from `recv_loop` before the disconnect callback. The condition variable's predicate checks `!alive_`, so once notified it exits cleanly.

**3. Destructor ordering with detached threads**
If `on_disconnect` ran and removed the session from `g_sessions`, the `shared_ptr` refcount hit zero and the destructor ran from inside `recv_loop`'s thread. The destructor called `detach()` on `recv_thread_` — which is valid (a thread can detach itself) — but calling `join()` on a detached thread is UB. Had to make sure the destructor always uses `detach()` and that `recv_loop` doesn't touch any member variables after the disconnect callback fires.

---

## Things to do next

**Immediate:**
- Run TSan under sustained load (multiple clients, 60 seconds) and fix what it finds. Document findings here.
- Measure actual end-to-end latency and add real numbers to the benchmarks table above.
- Find the tick rate at which the server starts dropping or delaying messages.

**Larger additions:**
- Per-symbol stats exposed to clients (tick rate, high/low, last price)
- Historical tick replay: write ticks to a file, add `--replay <file>` mode so the feed replays real data instead of simulated GBM
- Config file for symbols, port, tick rate instead of hardcoded values

**Scaling beyond one machine:**
## Architecture & Demo

```text
connected to 127.0.0.1:9001
commands: sub <SYMBOL>  |  unsub <SYMBOL>  |  heartbeat  |  quit

[server] connected 
> sub AAPL
[AAPL]  bid=192.96 (1000)  ask=193.27 (100)  spread=0.31
[AAPL]  bid=193.84 (200)  ask=194.15 (200)  spread=0.31
[AAPL]  bid=192.73 (600)  ask=193.04 (100)  spread=0.31
> sub TSLA
[AAPL]  bid=189.46 (400)  ask=189.76 (900)  spread=0.30
[AAPL]  bid=189.65 (300)  ask=189.96 (100)  spread=0.30
[TSLA]  bid=249.77 (600)  ask=250.52 (600)  spread=0.75
[AAPL]  bid=186.44 (900)  ask=186.74 (100)  spread=0.30
[TSLA]  bid=244.21 (700)  ask=244.94 (600)  spread=0.73
> unsub AAPL
[TSLA]  bid=254.31 (300)  ask=255.07 (1000)  spread=0.76
[TSLA]  bid=249.75 (700)  ask=250.50 (400)  spread=0.75
[TSLA]  bid=253.91 (500)  ask=254.67 (400)  spread=0.76
> quit
[client] server closed the connection
```

The core structure is intentionally minimal to limit locking overhead and thread contention. The thread-per-client model works for tens of clients. To handle hundreds or thousands, it would need to move to an event-driven model using `epoll` with non-blocking I/O. Real exchange feed handlers at HFT firms bypass the kernel network stack entirely with DPDK or RDMA to get latencies below 1µs, but that's a very different engineering problem from what this project demonstrates.
