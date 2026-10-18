# muduo 核心库（本项目内置版本）

本目录是 [chenshuo/muduo](https://github.com/chenshuo/muduo) **核心源码的逐字副本**，
用于支撑 `../Gomoku` 的终端五子棋服务端与客户端。

> 目标：**不做任何改进与优化**，保持与上游一致的 API、实现与注释。

## 1. 来源与许可

| 项 | 值 |
|---|---|
| 上游仓库 | https://github.com/chenshuo/muduo |
| 获取方式 | `https://codeload.github.com/chenshuo/muduo/tar.gz/refs/heads/master`（master 分支快照） |
| 获取日期 | 2025-10（以实际拉取时的 master 为准） |
| 许可证 | BSD 3-Clause，见本目录 `License` 文件，版权归 Shuo Chen 所有 |

源码中原作者的中文/英文注释、版权头一律保留，未做修改。

## 2. 目录结构

```
muduo/
├── License                 # 上游 BSD 3-Clause 许可证（原样）
├── CMakeLists.txt          # 本项目新增的精简构建脚本（唯一新增的构建描述）
└── muduo/
    ├── base/               # 42 个 .h/.cc：日志、线程、时间、文件、阻塞队列等
    │   └── CMakeLists.txt  # 新增
    └── net/                # 35 个 .h/.cc：Reactor 网络核心
        ├── CMakeLists.txt  # 新增
        └── poller/         # 5 个文件：EPollPoller / PollPoller / DefaultPoller
```

## 3. 包含的模块

**muduo_base**（15 个 .cc + 27 个头文件）

- 日志：`Logging`、`LogStream`、`LogFile`、`AsyncLogging`
- 线程：`Thread`、`ThreadPool`、`ThreadLocal`、`ThreadLocalSingleton`、`Mutex`、`Condition`、`CountDownLatch`、`BlockingQueue`、`BoundedBlockingQueue`
- 时间：`Timestamp`、`Date`、`TimeZone`
- 工具：`Exception`、`FileUtil`、`ProcessInfo`、`StringPiece`、`Atomic`、`Singleton`、`WeakCallback`、`Types`、`copyable`/`noncopyable`

**muduo_net**（16 个 .cc + poller 3 个 .cc + 19 个头文件）

- Reactor：`EventLoop`、`Channel`、`Poller`、`EPollPoller`、`PollPoller`、`Timer`、`TimerQueue`、`TimerId`
- TCP：`Acceptor`、`Connector`、`TcpConnection`、`TcpServer`、`TcpClient`、`Socket`、`SocketsOps`
- 辅助：`Buffer`、`InetAddress`、`Callbacks`、`Endian`、`EventLoopThread`、`EventLoopThreadPool`

## 4. 明确排除的部分（对应需求“不包括测试、参考、示例等其他部分”）

| 排除路径 | 原因 |
|---|---|
| `net/http/` | HTTP 示例库，本项目自带极简 HTTP 服务 |
| `net/inspect/` | 内省/调试接口，示例性质 |
| `net/protobuf/`、`net/protorpc/` | RPC 扩展，依赖 protobuf |
| `net/tests/`、`base/tests/` | 单元测试与示例程序 |
| `examples/`、`contrib/` | 上游示例与贡献代码 |
| `base/GzipFile.h` | 只有声明无实现（实现已移入 examples），且依赖 zlib |
| `net/ZlibStream.h` | zlib 压缩流，不属于网络核心 |
| `net/boilerplate.{h,cc}` | 上游占位示例文件（文件本身注明 “you should not include this”） |

因此本项目只需依赖 **Boost 头文件**（`boost::any` / `boost::circular_buffer` / `boost::operators`）与 `pthread`。
上游根 `CMakeLists.txt` 会因为强制查找 Protobuf / CURL / ZLIB / c-ares / libmicrohttpd 而未采用。

## 5. 与上游构建脚本的差异

仅有一份新增的 `CMakeLists.txt`（上游那份依赖太多、且默认编译 examples），差异只有三点编译选项：

| 上游 | 本项目 | 原因 |
|---|---|---|
| `-Werror` | 去掉 | gcc 11.3 下上游遗留告警会直接中断编译 |
| `-Wconversion` | 去掉 | 同上 |
| `-Wold-style-cast` | 去掉 | 同上 |

`-g -Wall -Wextra -Wno-unused-parameter -Wpointer-arith -Wshadow -Wwrite-strings`、
`-DCHECK_PTHREAD_RETURN_VALUE`、`-D_FILE_OFFSET_BITS=64`、`-std=c++11` 全部保留。
**源码零修改**。
（`-march=native` 也未沿用，避免二进制绑定到构建机的 CPU 指令集。）

## 6. 单独编译本库

```bash
cd /forDsh/muduo
cmake -S . -B build
cmake --build build -j$(nproc)
# 产物：build/muduo/base/libmuduo_base.a、build/muduo/net/libmuduo_net.a
```

## 7. 在项目中引用

```cmake
add_subdirectory(muduo)
target_link_libraries(your_target PRIVATE muduo_net)  # 自动带上 muduo_base / pthread
```

```cpp
#include "muduo/net/EventLoop.h"
#include "muduo/net/TcpServer.h"
#include "muduo/base/Logging.h"
```

## 8. 核心调用链速查（阅读源码时先看这里）

```
EventLoop::loop()
  └─ Poller::poll()            -> 得到活跃 Channel 列表
       └─ Channel::handleEvent()
            ├─ TcpConnection::handleRead()   -> Buffer::readFd() -> MessageCallback
            ├─ TcpConnection::handleWrite()  -> 继续发 sendBuffer_
            ├─ Acceptor::handleRead()        -> accept4 -> TcpServer::newConnection()
            └─ TimerQueue::handleRead()      -> timerfd 到期 -> run timer callback

TcpServer::start()  -> 创建 Acceptor 并 listen
TcpServer::newConnection()
  └─ EventLoopThreadPool::getNextLoop()   -> 轮转选 IO 线程
       └─ TcpConnection(ioLoop, ...)      -> 注册 Channel 到 ioLoop
            └─ ioLoop->runInLoop(TcpConnection::connectEstablished)
```

要点：
- `EventLoop::runInLoop()` / `queueInLoop()` 是**跨线程**投递任务的唯一正确入口；
- `TcpConnection::send()` 线程安全，内部会 `runInLoop` 到所属 loop；
- 一个 `TcpServer` 的 base loop 负责 accept，连接句柄分布在 `EventLoopThreadPool` 的 IO 线程上。
