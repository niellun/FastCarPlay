# Compiler
CXX := g++
SRC_DIR := ./src
OUT_DIR := ./out

# File lists
SRCS := $(wildcard $(SRC_DIR)/*.cpp)

# Targets
TARGET_NAME := app

# Build types
.PHONY: all debug release clean build

all: debug

LDOPTIONS := -lSDL2 -lavformat -lavcodec -lavutil -lswscale
LDFLAGS := -static-libstdc++
CXXCOMMON := -Wall

debug: BUILD_TYPE := debug
debug: CXXFLAGS := -g -O0 
debug: TARGET := $(TARGET_NAME)-debug
debug: build

release: BUILD_TYPE := release
release: CXXFLAGS := -O2
release: TARGET := $(TARGET_NAME)
release: build

build:
	$(MAKE) BUILD_DIR=$(OUT_DIR)/$(BUILD_TYPE) OBJS="$(patsubst $(SRC_DIR)/%.cpp,$(OUT_DIR)/$(BUILD_TYPE)/%.o,$(SRCS))" TARGET=$(OUT_DIR)/$(TARGET) do_build

do_build: $(OBJS)
	@mkdir -p $(OUT_DIR)
	$(CXX) $(LDFLAGS) $(OBJS) -o $(TARGET) $(LDOPTIONS)
	@echo "Build complete: $(TARGET)"

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXCOMMON) $(CXXFLAGS) -c $< -o $@

clean:
	@rm -rf $(OUT_DIR)
	@echo "Clean complete"