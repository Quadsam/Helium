CC        := gcc
TARGET    := main

# Directories
SRC_DIR   := src
OBJ_DIR   := build
BIN_DIR   := bin

# Build Mode Logic
MODE ?= debug

ifeq ($(MODE), release)
	CFLAGS  := -std=gnu23 -Wall -Wextra -Werror -O3 -DNDEBUG
	LDFLAGS :=
	MSG     := "RELEASE MODE (Optimized)"
else
	CFLAGS  := -std=gnu23 -Wall -Wextra -Werror -ggdb -O0 -fsanitize=address
	LDFLAGS := -fsanitize=address
	MSG     := "DEBUG MODE (ASan enabled)"
endif

SRCS      := $(wildcard $(SRC_DIR)/*.c)
OBJS      := $(SRCS:$(SRC_DIR)/%.c=$(OBJ_DIR)/$(MODE)/%.o)
DEPS      := $(OBJS:.o=.d)
CPPFLAGS  := -Iinclude -MMD -MP

.PHONY: all clean check directories run info help

## all: Default target, builds the project in debug mode
all: info directories $(BIN_DIR)/$(TARGET)

## info: Prints the current build configuration
info:
	@echo 'Building in $(MSG)'

directories:
	@mkdir -p $(OBJ_DIR)/$(MODE) $(BIN_DIR)

$(BIN_DIR)/$(TARGET): $(OBJS)
	@echo "  LD	$@"
	@$(CC) $(LDFLAGS) -o $@ $^

$(OBJ_DIR)/$(MODE)/%.o: $(SRC_DIR)/%.c
	@echo "  CC	$@"
	@$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

## run: Builds and executes the binary
run: all
	@./$(BIN_DIR)/$(TARGET)

## check: Runs static analysis using cppcheck
check:
	@echo "  LINT"
	@cppcheck --enable=all --suppress=missingIncludeSystem $(SRC_DIR)/*.c

## clean: Removes all build artifacts
clean:
	@echo "  CLEAN"
	@$(RM) -rf $(OBJ_DIR) $(BIN_DIR)

## help: Shows this help message
help:
	@echo 'Usage: make [target] [MODE=release|debug]'
	@echo ''
	@echo 'Targets:'
	@sed -n 's/^##//p' $(MAKEFILE_LIST) | column -t -s ':' |  sed -e 's/^/ /'

-include $(DEPS)