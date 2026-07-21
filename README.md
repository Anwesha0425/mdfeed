# mdfeed

A real-time market data distribution server in C++17 using POSIX TCP sockets 
and threads. No external libraries.

## What it does
- Multi-client TCP server; one recv/send thread pair per client
- Simulates bid/ask prices for 6 symbols via Geometric Brownian Motion
- Clients subscribe to symbols and receive live order book updates
- Per-tick microsecond latency reported client-side
- Sequence-number gap detection with automatic replay request
- Server retains a 1,000-message sliding replay buffer per symbol
- Thread-safe order book via `std::shared_mutex` (concurrent reads, exclusive writes)
- Server-side console telemetry: updates/sec, messages/sec, active clients, subscriptions
- Graceful shutdown on Ctrl-C

## Build & Run
**Prerequisites:** The project utilizes raw POSIX sockets (`<sys/socket.h>`) and requires a Linux environment or Windows Subsystem for Linux (WSL).

1.  **Clone & Build:**
    ```bash
    git clone https://github.com/Anwesha0425/mdfeed.git
    cd mdfeed
    make
    ```

2.  **Start the Server:**
    ```bash
    ./mdfeed-server
    ```

3.  **Start the Client (in a separate terminal):**
    ```bash
    ./mdfeed-client
    ```

## Benchmarks

Run with `make bench`. Actual output on WSL Ubuntu 22.04:

```
order book throughput benchmark
--------------------------------
single-thread update rate:  26M updates/sec  (2000000 ops in 77ms)
single-thread read rate:    37M reads/sec  (4000000 reads in 107ms)
4-thread concurrent rate:   5M updates/sec  (4000000 ops in 820ms)

note: this measures the order book in isolation (no network).
for end-to-end latency, run the server and client separately
and compare timestamps in the feed vs client output.
```

## ThreadSanitizer

Ran with 4 concurrent clients over 60 seconds, each subscribing/unsubscribing 
repeatedly during the run. Found a use-after-free race condition in the 
destructor (since fixed):

```
WARNING: ThreadSanitizer: data race (pid=565)
  Write of size 8 at 0x7244000003d8 by thread T3:
    #17 std::map::erase /usr/include/c++/15/bits/stl_map.h:1159
    #18 on_disconnect(int) src/server.cpp:191

  Previous atomic read of size 1 at 0x7244000003d8 by thread T4:
    #0 std::__atomic_base<bool>::load /usr/include/c++/15/bits/atomic_base.h:501
    #1 ClientSession::send_loop() src/client_session.cpp:79

SUMMARY: ThreadSanitizer: data race src/server.cpp:191 in on_disconnect(int)
```

Fixed by joining the detached send thread before destroying the session. Subsequent runs exit cleanly with no warnings.

## Concurrency Design
- **Order book: `shared_mutex`.** Multiple clients can read concurrently 
  (`shared_lock`); updates take exclusive access (`unique_lock`). Verified 
  correct via the concurrent order book test and TSan.
- **Send queue: `mutex` + `condition_variable`** per client, so idle send 
  threads sleep instead of spin.
- **Subscription manager: plain `mutex`** — no read/write split needed here; 
  subscribe/unsubscribe are infrequent relative to the tick rate.

## Known Limitations
**Replay/live interleaving can cause false-positive gap detection.** Replayed 
historical messages use the same `UPDATE` type as live ticks, and delivery 
order between the feed thread and the replay-request handler isn't 
synchronized — so a replay burst can arrive interleaved with newer live ticks, 
and the client's naive sequence check can misinterpret this as a second gap. 
Found this by tracing the replay path by hand, not via TSan (it's a protocol 
logic issue, not a data race). Fix would be tagging replay messages distinctly 
so the client skips gap-checking on them.

## Bugs Found During Development
1. **"Address already in use" on Server Restart:** `bind()` failed because the port was stuck in `TIME_WAIT` after closing. Fixed by setting `SO_REUSEADDR` on the socket.
2. **The Deadlocked Send Thread:** When a client disconnected, the send thread slept forever on `queue_cv_.wait()`. Fixed by calling `notify_all()` during disconnect.
3. **The UB Detached Destructor Race:** When a client disconnected, the session destructor was destroying the object while the background threads were still running. Fixed by carefully joining all threads and checking thread IDs to prevent self-join deadlocks.

## Things to add next
- Fix the replay/live interleaving issue above
- Real end-to-end latency numbers under sustained multi-client load (not just 
  in-process order book benchmarks)
- Event-driven (epoll) model for higher client counts
- UDP multicast transport for authentic exchange-style dissemination
