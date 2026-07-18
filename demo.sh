#!/bin/bash
./mdfeed-server &
SERVER_PID=$!
sleep 1

(
  echo "sub AAPL"
  sleep 3
  echo "sub TSLA"
  sleep 3
  echo "unsub AAPL"
  sleep 2
  echo "quit"
) | ./mdfeed-client

kill $SERVER_PID
wait $SERVER_PID 2>/dev/null
