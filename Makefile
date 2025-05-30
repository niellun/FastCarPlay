# Compiler
CXX := g++
SRC_DIR := ./src
OUT_DIR := ./out
RES_DIR := $(SRC_DIR)/resource
GEN_DIR := $(SRC_DIR)/autogen
BUILD_DIR := $(OUT_DIR)/$(BUILD_TYPE)

# File lists
SRCS := $(shell find $(SRC_DIR) -type f -name '*.cpp')
OBJS=$(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(SRCS))

RES := $(shell find $(RES_DIR) -type f ! -name '*.h' ! -name '.*' -name '*.*')
RES_SRC := $(patsubst $(RES_DIR)/%,$(GEN_DIR)/%.cpp,$(RES))

# Targets
TARGET_NAME := app

# Build types
.PHONY: all debug release clean build

all: debug

LDOPTIONS := -lSDL2 -lSDL2_ttf -lavformat -lavcodec -lavutil -lswscale -lusb-1.0 -lssl -lcrypto
LDFLAGS := 
CXXCOMMON := -Wall -Isrc

debug: BUILD_TYPE := debug
debug: CXXFLAGS := -g -O0 -DPROTOCOL_DEBUG -fsanitize=address -fno-omit-frame-pointer 
debug: LDFLAGS += -fsanitize=address -fno-omit-frame-pointer
debug: TARGET := $(TARGET_NAME)-debug
debug: prepare

release: BUILD_TYPE := release
release: CXXFLAGS := -Ofast -march=native -fno-plt -fno-rtti -flto -fdata-sections -ffunction-sections -DNDEBUG
release: LDFLAGS += -Ofast -march=native -Wl,--gc-sections -flto
release: TARGET := $(TARGET_NAME)
release: prepare

prepare: $(RES_SRC)
	$(MAKE) BUILD_TYPE=$(BUILD_TYPE) TARGET=$(OUT_DIR)/$(TARGET) CXXFLAGS="$(CXXFLAGS)" LDFLAGS="$(LDFLAGS)" build

build: $(TARGET)

$(GEN_DIR)/%.cpp: $(RES_DIR)/%
	@mkdir -p $(GEN_DIR)
	xxd -i -n $(basename $(notdir $<)) $< > $@

$(TARGET): $(OBJS)
	@mkdir -p $(OUT_DIR)
	$(CXX) $(LDFLAGS) $(OBJS) -o $(TARGET) $(LDOPTIONS)
	@echo "Build complete: $(TARGET)"

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXCOMMON) $(CXXFLAGS) -c $< -o $@

clean:
	@rm -rf $(OUT_DIR)
	@rm -rf $(GEN_DIR)
	@echo "Clean complete"