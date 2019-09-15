SOURCES = $(wildcard *.cc)
EXES = $(patsubst %.cc, %, $(SOURCES))

# CXX = g++
CXX = clang++

LDFLAGS = -L/usr/local/lib -lpthread -ldl
CXXFLAGS = -I/usr/local/include -I.. -Wall -std=c++11 -DLOGURU_WITH_STREAMS=1

targets : redis_protocol_test

%: %.cc
	$(CXX) $<  ../base/loguru.cc ../base/logging.cc $(CXXFLAGS) $(LDFLAGS) -o $@

clean:
	rm -fv $(EXES)
