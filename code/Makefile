CC = gcc
CFLAGS = -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror -std=c11 -Iinclude
LDFLAGS =

BIN_DIR = bin
BUILD_DIR = build
SRC_DIR = src
RUNTIME_DIR = runtime

CONTROLLER_SRCS = \
	$(SRC_DIR)/controller/main.c \
	$(SRC_DIR)/controller/controller.c \
	$(SRC_DIR)/controller/repl.c \
	$(SRC_DIR)/controller/parser.c \
	$(SRC_DIR)/controller/commands.c \
	$(SRC_DIR)/core/device_common.c \
	$(SRC_DIR)/core/registry.c \
	$(SRC_DIR)/core/routing.c \
	$(SRC_DIR)/core/hierarchy.c \
	$(SRC_DIR)/core/serialization.c \
	$(SRC_DIR)/core/errors.c \
	$(SRC_DIR)/ipc/ipc_common.c \
	$(SRC_DIR)/ipc/fifo.c \
	$(SRC_DIR)/ipc/message.c \
	$(SRC_DIR)/ipc/request_reply.c \
	$(SRC_DIR)/ipc/event_loop.c \
	$(SRC_DIR)/devices/controller_device.c \
	$(SRC_DIR)/devices/hub.c \
	$(SRC_DIR)/devices/timer.c \
	$(SRC_DIR)/devices/bulb.c \
	$(SRC_DIR)/devices/window.c \
	$(SRC_DIR)/devices/fridge.c \
	$(SRC_DIR)/utils/logging.c \
	$(SRC_DIR)/utils/time_utils.c \
	$(SRC_DIR)/utils/random_delay.c \
	$(SRC_DIR)/utils/cleanup.c

MANUAL_SRCS = \
	$(SRC_DIR)/manual/manual_client.c \
	$(SRC_DIR)/ipc/ipc_common.c \
	$(SRC_DIR)/ipc/fifo.c \
	$(SRC_DIR)/ipc/message.c \
	$(SRC_DIR)/core/errors.c \
	$(SRC_DIR)/utils/logging.c

CONTROLLER_BIN = $(BIN_DIR)/domotics_controller
MANUAL_BIN = $(BIN_DIR)/manual_client

CONTROLLER_OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(CONTROLLER_SRCS))
MANUAL_OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(MANUAL_SRCS))

.PHONY: all build clean run test dirs

all: build

build: dirs $(CONTROLLER_BIN) $(MANUAL_BIN)

dirs:
	mkdir -p $(BIN_DIR)
	mkdir -p $(BUILD_DIR)/controller
	mkdir -p $(BUILD_DIR)/core
	mkdir -p $(BUILD_DIR)/ipc
	mkdir -p $(BUILD_DIR)/devices
	mkdir -p $(BUILD_DIR)/manual
	mkdir -p $(BUILD_DIR)/utils
	mkdir -p $(RUNTIME_DIR)/fifos
	mkdir -p $(RUNTIME_DIR)/logs
	mkdir -p $(RUNTIME_DIR)/pids
	mkdir -p $(RUNTIME_DIR)/registry

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(CONTROLLER_BIN): $(CONTROLLER_OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(MANUAL_BIN): $(MANUAL_OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

run: build
	bash scripts/run_demo.sh $(ARGS)

test: build
	bash tests/test_linking.sh
	bash tests/test_override.sh
	bash tests/test_cycle_detection.sh
	bash tests/test_crash_handling.sh
	bash tests/test_cascade_delete.sh
	bash tests/test_timer_validation.sh

clean:
	-bash scripts/cleanup_ipc.sh
	rm -rf $(BIN_DIR)
	rm -rf $(BUILD_DIR)
	rm -rf $(RUNTIME_DIR)/fifos/*
	rm -rf $(RUNTIME_DIR)/logs/*
	rm -rf $(RUNTIME_DIR)/pids/*
	rm -rf $(RUNTIME_DIR)/registry/*