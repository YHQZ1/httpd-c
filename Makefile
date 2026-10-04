CC := cc
CFLAGS := -std=c11 -D_XOPEN_SOURCE=700 -O2 -Wall -Wextra -Wpedantic -Iinclude -Isrc -MMD -MP
LDFLAGS :=

SRC_DIR := src
BUILD_DIR := build
BIN := httpd-c

SRCS := $(wildcard $(SRC_DIR)/*.c)
OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))

.PHONY: all clean run test

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

run: all
	./$(BIN)

test: all
	./tests/smoke.sh ./$(BIN)

clean:
	rm -rf $(BUILD_DIR) $(BIN)

-include $(OBJS:.o=.d)
