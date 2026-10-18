# ============================================================================
# webbench 1.5 构建脚本（项目自带的精简版，等价于上游 Makefile 的核心部分）
#   make          -> 生成可执行文件 webbench
#   make clean
# ============================================================================
CC      ?= gcc
CFLAGS  ?= -Wall -O2 -g
LDFLAGS ?=
LIBS    ?=

all: webbench

webbench: webbench.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ webbench.o $(LIBS)

# webbench.c 通过 #include "socket.c" 把 socket 辅助函数编进来，
# 所以只有一个编译单元。
webbench.o: webbench.c socket.c
	$(CC) $(CFLAGS) -c webbench.c -o $@

clean:
	rm -f *.o webbench

.PHONY: all clean
