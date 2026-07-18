CXX      = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -pthread
INCLUDES = -Iinclude

SRC = src

SERVER_SRCS = $(SRC)/protocol.cpp        \
              $(SRC)/order_book.cpp       \
              $(SRC)/feed_simulator.cpp   \
              $(SRC)/subscription_manager.cpp \
              $(SRC)/client_session.cpp   \
              $(SRC)/server.cpp

CLIENT_SRCS = $(SRC)/protocol.cpp \
              $(SRC)/client.cpp

SERVER_BIN = mdfeed-server
CLIENT_BIN = mdfeed-client

.PHONY: all server client clean asan tsan

all: server client

server: $(SERVER_SRCS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $(SERVER_BIN) $(SERVER_SRCS)

client: $(CLIENT_SRCS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $(CLIENT_BIN) $(CLIENT_SRCS)

asan: CXXFLAGS += -fsanitize=address -fno-omit-frame-pointer -g
asan: all

tsan: CXXFLAGS += -fsanitize=thread -fno-omit-frame-pointer -g
tsan: all

clean:
	rm -f $(SERVER_BIN) $(CLIENT_BIN)
