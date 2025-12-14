CC = clang
CFLAGS = -Wall -Wextra -std=c99 -O3
INCLUDES = -I/opt/homebrew/include -I/usr/local/include
LDFLAGS = -L/opt/homebrew/lib -L/usr/local/lib -lraylib -framework OpenGL -framework Cocoa -framework IOKit -framework CoreVideo

BUILD_DIR = build
TARGET = $(BUILD_DIR)/gog

# Auto-discover all .c files
SRCS = $(wildcard *.c)
OBJS = $(SRCS:%.c=$(BUILD_DIR)/%.o)
DEPS = $(OBJS:.o=.d)

all: $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Compile .c to .o with automatic dependency generation
$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

# Link all object files
$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LDFLAGS)

# Include dependency files (if they exist)
-include $(DEPS)

clean:
	rm -rf $(BUILD_DIR)

run: $(TARGET)
	./$(TARGET)

# Debug build with symbols and no optimization
debug: CFLAGS = -Wall -Wextra -std=c99 -g -O0
debug: clean all

rebuild: clean all

.PHONY: all clean run debug rebuild
