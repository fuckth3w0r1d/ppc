# ===== 基本配置 =====
CXX := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2 -g
INCLUDES := -Iinclude

SRC_DIR := src
BUILD_DIR := build
TARGET := lexer

# ===== 自动收集源文件 =====
SRCS := $(wildcard $(SRC_DIR)/*.cpp)
OBJS := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(SRCS))

# ===== 默认目标 =====
all: $(TARGET)

# ===== 链接 =====
$(TARGET): $(OBJS)
	$(CXX) $^ -o $@

# ===== 编译 =====
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# ===== 清理 =====
clean:
	rm -rf $(BUILD_DIR) $(TARGET)

# ===== 伪目标 =====
.PHONY: all clean
