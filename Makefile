CC = gcc

INCLUDES = -Iinclude -I/usr/include/mysql

PKG_XML = $(shell pkg-config --cflags --libs libxml-2.0)

LIBS_COMMON = -lcurl -ljson-c -lmysqlclient

BUILD_DIR = build

TARGETS = $(BUILD_DIR)/youtube_fetcher $(BUILD_DIR)/telegram_listener $(BUILD_DIR)/telegram_writer $(BUILD_DIR)/notifier

all: build

build: $(BUILD_DIR) $(TARGETS)

$(BUILD_DIR)/notifier: src/programs/notifier.c src/core/telegram_core.c src/core/db_core.c src/core/rss_parser.c src/utils/logger.c | $(BUILD_DIR)
	$(CC) $^ -o $@ $(INCLUDES) $(LIBS_COMMON) $(PKG_XML)

$(BUILD_DIR)/telegram_writer: src/programs/telegram_db_writer.c src/core/telegram_core.c src/core/db_core.c src/utils/logger.c | $(BUILD_DIR)
	$(CC) $^ -o $@ $(INCLUDES) $(PKG_XML) -lmysqlclient -ljson-c -lcurl -pthread -lrt

$(BUILD_DIR)/telegram_listener: src/programs/telegram_listener.c src/core/telegram_core.c src/utils/logger.c src/core/db_core.c | $(BUILD_DIR)
	$(CC) $^ -o $@ $(INCLUDES) -lcurl -ljson-c -lmysqlclient $(PKG_XML)

$(BUILD_DIR)/youtube_fetcher: src/programs/youtube_fetcher.c src/core/telegram_core.c src/core/db_core.c src/utils/logger.c src/core/rss_parser.c | $(BUILD_DIR)
	$(CC) $^ -o $@ $(INCLUDES) $(PKG_XML) -lmysqlclient -ljson-c -lcurl -pthread -lrt

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

clean:
	-rm -f $(TARGETS)

.PHONY: run run-youtube run-listener run-writer run-notifier all build clean

run: build
	@echo "Running all programs..."
	@echo "Starting youtube_fetcher..."
	./$(BUILD_DIR)/youtube_fetcher &
	@echo "Starting telegram_listener..."
	./$(BUILD_DIR)/telegram_listener &
	@echo "Starting telegram_writer..."
	./$(BUILD_DIR)/telegram_writer &
	@echo "Starting notifier..."
	./$(BUILD_DIR)/notifier &
	@echo "All programs started in background"

run-youtube: $(BUILD_DIR)/youtube_fetcher
	./$(BUILD_DIR)/youtube_fetcher

run-listener: $(BUILD_DIR)/telegram_listener
	./$(BUILD_DIR)/telegram_listener

run-writer: $(BUILD_DIR)/telegram_writer
	./$(BUILD_DIR)/telegram_writer

run-notifier: $(BUILD_DIR)/notifier
	./$(BUILD_DIR)/notifier
