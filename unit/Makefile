SOURCES = $(wildcard *.cc)
EXES = $(patsubst %.cc, %, $(SOURCES))

# CXX = g++
CXX = clang++

LDFLAGS = -L/usr/local/lib -lpthread -ldl
CXXFLAGS = -I/usr/local/include -I.. -Wall -std=c++11 -DLOGURU_WITH_STREAMS=1

targets : redis_protocol_test config_test

%: %.cc
	$(CXX) $<  ../proxy/logging.cc $(CXXFLAGS) $(LDFLAGS) -o $@

config_test : config_test.cc ../proxy/config.cc
	$(CXX) $<  ../proxy/config.cc ../proxy/logging.cc -I../proxy $(CXXFLAGS) $(LDFLAGS) -lboost_system -o $@

clean:
	rm -fv $(EXES)
