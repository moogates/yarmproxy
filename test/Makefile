CXX = g++
#CXX = clang++
CXXFLAGS = -I/usr/local/include -Wall -Wextra -std=c++11

all: yarmnc slow_server sink_server

yarmnc : yarmnc.cc
	$(CXX) $< -o $@ $(CXXFLAGS) $(LDFLAGS)

slow_server : slow_server.cc
	$(CXX) $< -o $@ $(CXXFLAGS) $(LDFLAGS) -lboost_system -lpthread

sink_server : sink_server.cc
	$(CXX) $< -o $@ $(CXXFLAGS) $(LDFLAGS) -lboost_system -lpthread

test : yarmnc
	cd redis && make && make test
	cd memcached && make && make test

clean:
	rm -fv yarmnc slow_server sink_server

.PHONY : all test clean 

