#CXX = g++
CXX = clang++
CXXFLAGS = -I/usr/local/include -Wall -Wextra -std=c++11

yarmnc : yarmnc.cc
	$(CXX) $< -o $@ $(CXXFLAGS) $(LDFLAGS)

clean:
	rm -fv yarmnc
