# Makefile for compiling buttons.cpp with optimizations for release

CXX := g++
CXXFLAGS := -std=c++17 -O3 -Wall -Wextra -march=native -flto
LDFLAGS := -flto

all: keylistener emulator

keylistener: keylistener.cpp
	$(CXX) $(CXXFLAGS) keylistener.cpp -o keylistener $(LDFLAGS)

emulator: emulator.cpp
	$(CXX) $(CXXFLAGS) emulator.cpp -o emulator $(LDFLAGS)

clean:
	rm -f keylistener emulator