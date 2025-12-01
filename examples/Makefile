# Makefile for compiling buttons.cpp with optimizations for release

CXX := g++
CXXFLAGS := -std=c++17 -O3 -Wall -Wextra -march=native -flto
LDFLAGS := -flto
OUT_DIR := ./../out

all: keylistener emulator

keylistener: keylistener.cpp
	$(CXX) $(CXXFLAGS) keylistener.cpp -o $(OUT_DIR)/keylistener $(LDFLAGS)

emulator: emulator.cpp
	$(CXX) $(CXXFLAGS) emulator.cpp -o $(OUT_DIR)/emulator $(LDFLAGS) -llgpio

clean:
	rm -f $(OUT_DIR)/keylistener $(OUT_DIR)/emulator