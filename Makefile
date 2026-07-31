CC := cc
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -Iinclude -Isrc
LDFLAGS :=

SRC_DIR := src
BUILD_DIR := build
BIN := httpd-c

SRCS := $(wildcard $(SRC_DIR)/*.c)
OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))

.PHONY: all clean run

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

run: all
	./$(BIN)

clean:
	rm -rf $(BUILD_DIR) $(BIN)