# mdfeed

A real-time market data distribution server built in C++17 using POSIX TCP sockets and threads. No external libraries.

The idea came from learning about how trading firms stream live price data to multiple internal clients — this is a simplified version of that: a server that accepts connections, simulates live stock prices, and pushes bid/ask updates to clients that subscribe to specific symbols.

---

## What it does

- Accepts multiple TCP client connections (one thread pair per client)
- Simulates live bid/ask prices for 6 symbols: `AAPL`, `MSFT`, `GOOGL`, `TSLA`, `SPY`, `NVDA`
- Clients subscribe to symbols they care about and get real-time order book updates
- Detects sequence number gaps on the client side (shows if any ticks were missed)
- Graceful shutdown on Ctrl-C

## How to build

Requires Linux, `g++` with C++17, and `pthread`.

```bash
make          # builds server and client
make asan     # build with AddressSanitizer (memory bugs)
make tsan     # build with ThreadSanitizer (data races)
make clean
```

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

## Example session

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

## Project structure

```
mdfeed/
├── include/
│   ├── protocol.hpp              message types and wire format
│   ├── order_book.hpp            thread-safe bid/ask order book
│   ├── feed_simulator.hpp        market data generator
│   ├── subscription_manager.hpp  tracks who subscribed to what
│   └── client_session.hpp        per-client recv/send threads
├── src/
│   ├── protocol.cpp
│   ├── order_book.cpp
│   ├── feed_simulator.cpp
│   ├── subscription_manager.cpp
│   ├── client_session.cpp
│   ├── server.cpp
│   └── client.cpp
├── Makefile
└── README.md
```

---

## Design decisions

**Why one thread per client?**
Simple to reason about and debug. A thread blocks on `recv()` while the send thread drains a queue independently. For a small number of clients this works fine.

**Why mutex + condition_variable for the send queue?**
The send thread sleeps when there's nothing to write, instead of spinning. It wakes up as soon as a message is enqueued. Straightforward and correct.

**Why a separate mutex on the order book instead of shared_mutex?**
I started with `shared_mutex` (allows multiple concurrent readers) but switched to a regular `mutex` — at this tick rate and client count, the simpler option is the right call. Premature optimization.

**Why `TCP_NODELAY`?**
Without it, the OS buffers small packets and sends them together (Nagle's algorithm). For tick data you want each message sent immediately, so this disables that buffering.

**Why `SO_REUSEADDR`?**
Lets the server restart on the same port immediately without waiting for TIME_WAIT to expire.

**Why pipe-delimited protocol?**
Inspired by FIX protocol used at exchanges. Each message has: `TYPE|SYMBOL|DATA|SEQNUM\n`. The sequence number lets the client detect if it missed a tick.

**Price simulation**
Uses a simplified Geometric Brownian Motion: `new_price = old_price * exp(sigma * Z)` where `Z` is a standard normal random variable. This is the same model underlying Black-Scholes. Each symbol has a different volatility — TSLA and NVDA are noisier than SPY.

---

## Bugs I hit during development

**1. Client getting disconnected immediately on server restart**
`bind()` was failing with "address already in use" because the port was in TIME_WAIT. Fixed by adding `SO_REUSEADDR` on the server socket. Obvious in hindsight.

**2. send_loop not exiting on disconnect**
When a client disconnected, the recv thread set `alive_ = false` but the send thread was stuck waiting on the condition variable forever. Fixed by calling `queue_cv_.notify_all()` from recv_loop before calling the disconnect callback.

**3. Destructor calling detach on threads that already exited**
If the session cleaned itself up before the destructor ran, calling `detach()` on an already-finished thread is fine — but calling `join()` on a detached thread is UB. Took a minute to get the ownership sequencing right.

---

## Things I'd add with more time

- Historical tick replay (`--replay ticks.log` mode)
- Per-symbol stats: tick rate, last price, high/low
- A proper config file instead of hardcoded symbols
- Running TSan under sustained load and fixing whatever it finds
