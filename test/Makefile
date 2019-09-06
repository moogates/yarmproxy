SOURCES = $(wildcard *.cc)
EXES = $(patsubst %.cc, %, $(SOURCES))

# CXX = g++
CXX = clang++

LDFLAGS = -L/usr/local/lib 
CXXFLAGS = -I/usr/local/include -Wall -std=c++11 -DLOGURU_WITH_STREAMS=1

%: %.cc
	$(CXX) $<  ../proxy/loguru.cc ../proxy/logging.cc $(CXXFLAGS) $(LDFLAGS) -o $@

clean:
	rm -fv $(EXES)
