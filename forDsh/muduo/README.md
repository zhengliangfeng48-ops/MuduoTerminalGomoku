# muduo 核心库（上游副本）

本目录是 [chenshuo/muduo](https://github.com/chenshuo/muduo) **核心源码的逐字副本**，
用于支撑 `../Gomoku`。目标：**不做任何改进与优化**，API、实现、注释与上游一致。

## 1. 来源与许可

| 项 | 值 |
|---|---|
| 上游 | https://github.com/chenshuo/muduo （master 快照，`codeload` tar.gz） |
| 许可 | BSD 3-Clause，见 `License`，著作权归 Shuo Chen |
| 规模 | 82 个源文件：`base/` 42（15 `.cc` + 27 头）、`net/` 40（19 `.cc` + 21 头，含 `poller/` 5 个） |

## 2. 结构

```
muduo/
├── License / CMakeLists.txt        # 3 份 CMake 为本项目新增，源码零修改
└── muduo/
    ├── base/   日志 Logging LogStream LogFile AsyncLogging；线程 Thread ThreadPool ThreadLocal*
    │           Mutex Condition CountDownLatch BlockingQueue*；时间 Timestamp Date TimeZone；
    │           工具 Exception FileUtil ProcessInfo StringPiece Atomic Singleton WeakCallback Types
    └── net/    Reactor：EventLoop Channel Poller Timer TimerQueue；TCP：Acceptor Connector
                TcpConnection TcpServer TcpClient Socket SocketsOps；辅助：Buffer InetAddress
                Callbacks Endian EventLoopThread(Pool)
        └── poller/   EPollPoller / PollPoller / DefaultPoller
```

## 3. 排除的部分

`net/http/`、`net/inspect/`、`net/protobuf/`、`net/protorpc/`、`net/tests/`、`base/tests/`、
`examples/`、`contrib/`、`base/GzipFile.h`（只有声明且依赖 zlib）、`net/ZlibStream.h`、
`net/boilerplate.*`（上游占位示例）。

因此只依赖 **Boost 头文件**（`any` / `circular_buffer` / `operators`）与 `pthread`。

## 4. 与上游构建的差异

只有三点：去掉 `-Werror`、`-Wconversion`、`-Wold-style-cast`（gcc 11 下上游遗留告警会中断编译）。
`-g -Wall -Wextra -Wno-unused-parameter -Wpointer-arith -Wshadow -Wwrite-strings`、
`-DCHECK_PTHREAD_RETURN_VALUE`、`-D_FILE_OFFSET_BITS=64`、`-std=c++11` 全部保留；
未沿用 `-march=native`，避免二进制绑定构建机 CPU。

## 5. 单独编译与引用

```bash
cd /forDsh/muduo && cmake -S . -B build && cmake --build build -j$(nproc)
# 产物：build/muduo/base/libmuduo_base.a、build/muduo/net/libmuduo_net.a
```

```cmake
add_subdirectory(muduo)
target_link_libraries(your_target PRIVATE muduo_net)   # 自动带上 muduo_base / pthread
```

## 6. 调用链速查

```
EventLoop::loop()
  └─ Poller::poll()                 → 活跃 Channel 列表
       └─ Channel::handleEvent()
            ├─ TcpConnection::handleRead()   → Buffer::readFd() → MessageCallback
            ├─ TcpConnection::handleWrite()  → 继续发 outputBuffer_
            ├─ Acceptor::handleRead()        → accept4 → TcpServer::newConnection()
            └─ TimerQueue::handleRead()      → timerfd 到期 → 定时器回调

TcpServer::newConnection()
  └─ EventLoopThreadPool::getNextLoop()  → 轮转选 IO 线程
       └─ TcpConnection(ioLoop, ...)
            └─ ioLoop->runInLoop(TcpConnection::connectEstablished)
```

要点：`runInLoop` / `queueInLoop` 是跨线程投递的唯一正确入口；`TcpConnection::send()` 线程安全
（内部投递到所属 loop）；base loop 负责 accept，连接句柄分布在 `EventLoopThreadPool` 的 IO 线程上。
