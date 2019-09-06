SOURCES = $(wildcard *.cc)
EXES = $(patsubst %.cc, %, $(SOURCES))

# CXX = g++
CXX = clang++

LDFLAGS = -L/usr/local/lib -lpthread -ldl
CXXFLAGS = -I/usr/local/include -Wall -std=c++11 -DLOGURU_WITH_STREAMS=1

targets : redis_test

%: %.cc
	$(CXX) $<  ../proxy/loguru.cc ../proxy/logging.cc $(CXXFLAGS) $(LDFLAGS) -o $@

clean:
	rm -fv $(EXES)
