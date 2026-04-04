# Git GUI 客户端 Makefile
# 使用 MinGW-w64 GCC 编译

CC = gcc
CFLAGS = -Wall -Os -flto -fdata-sections -ffunction-sections -DUNICODE -D_UNICODE -D__USE_MINGW_SECURE_API
LDFLAGS = -mwindows -s -flto -Wl,--gc-sections -lcomctl32 -lcomdlg32 -lshell32 -lole32 -luuid

# 源文件
SRCS = main.c git_operations.c ui_callbacks.c
OBJS = $(SRCS:.c=.o)

# 目标文件
TARGET = git-gui.exe

# 默认目标
all: $(TARGET)

# 编译目标
$(TARGET): $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)

# 编译 C 文件
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# 清理
clean:
	del /Q *.o $(TARGET) 2>nul

# 重新编译
rebuild: clean all

# 运行 
run: $(TARGET)
	./$(TARGET)

.PHONY: all clean rebuild run
