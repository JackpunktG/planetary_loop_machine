# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -g -Ilib/arena_memory -Ilib/mini_audio -Isrc/planetary_loop_machine
LDFLAGS = -lpthread -lm -ldl

# Directories
SRC_DIR = src
LIB_DIR = lib
OBJ_DIR = obj

# Target executable
TARGET = planetary_loop_machine

# Source files
SRCS = $(SRC_DIR)/main.c \
       $(SRC_DIR)/planetary_loop_machine/planetary_loop_machine.c \
       $(LIB_DIR)/arena_memory/arena_memory.c \
       $(LIB_DIR)/mini_audio/miniaudio.c

# Object files
OBJS = $(OBJ_DIR)/main.o \
       $(OBJ_DIR)/planetary_loop_machine.o \
       $(OBJ_DIR)/arena_memory.o \
       $(OBJ_DIR)/miniaudio.o

# Default target
all: $(TARGET)

# Link
$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LDFLAGS)
	@echo "Build complete: $(TARGET)"

# Compile source files
$(OBJ_DIR)/main.o: $(SRC_DIR)/main.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/planetary_loop_machine.o: $(SRC_DIR)/planetary_loop_machine/planetary_loop_machine.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/arena_memory.o: $(LIB_DIR)/arena_memory/arena_memory.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/miniaudio.o: $(LIB_DIR)/mini_audio/miniaudio.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Create obj directory
$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

# Clean
clean:
	rm -rf $(OBJ_DIR) $(TARGET)
	@echo "Cleaned build artifacts"

# Run
run: $(TARGET)
	./$(TARGET)

.PHONY: all clean run
