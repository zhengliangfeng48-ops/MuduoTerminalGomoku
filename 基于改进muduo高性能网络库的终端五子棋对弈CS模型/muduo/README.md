# muduo（自研实现）

这份 muduo 是**照着 `../../forDsh/muduo` 自己写出来的**，用来学 Reactor 网络库。
接口、目录结构、头文件路径都与参考实现一一对应（`Gomoku/` 的 `#include "muduo/net/TcpServer.h"` 依赖这一点），
内部实现由你自己完成。

> 参考实现是上游 chenshuo/muduo master 的逐字副本，许可证见 `License`（BSD 3-Clause，著作权归 Shuo Chen）。
> 学习目标是"读懂并重写"，所以**不要把参考实现的源码拷进来**（`scripts/progress.sh` 会统计逐字相同的文件数）。

## 1. 规模与进度

| 目录 | 目标文件 | .h | .cc |
|---|---:|---:|---:|
| `muduo/base/` | 42 | 27 | 15 |
| `muduo/net/`（含 `poller/`） | 40 | 21 | 19 |
| **合计** | **82** | **48** | **34** |

```bash
./scripts/progress.sh      # 已实现 N/82、缺失清单、逐字相同文件数
```

## 2. 增量编译

`base/` 与 `net/` 的 CMake 用 GLOB 收集 `.cc`，**写完一个文件就能编译一次**：

```bash
cd muduo && cmake -S . -B build && cmake --build build -j$(nproc)   # 只编 muduo
cd ..     && ./scripts/build.sh                                     # 编整个工程
```

一个 `.cc` 都没有时是 INTERFACE 占位库，configure 不会失败。
Gomoku 在 muduo 头文件缺失时会编译失败——**这是预期的进度提示，不是配置错误**。

## 3. 文件清单（按建议实现顺序）

### 3.1 muduo/base（L0→L7，共 42 个）

| 文件 | 角色 | 实现要点 |
|---|---|---|
| **L0 零依赖** | | |
| `copyable.h` | 「可值拷贝」空基类 | protected 构造/析构 |
| `noncopyable.h` | 「禁止拷贝」空基类 | 拷贝构造/赋值 `= delete` |
| `Types.h` | 公共类型与转型工具 | `implicit_cast` / `down_cast` / `memZero` |
| `WeakCallback.h` | weak_ptr 安全回调 | 持 `weak_ptr` + `std::function<void(CLASS*,ARGS...)>` |
| **L1 值类型/工具** | | |
| `StringPiece.h` | 零拷贝字符串视图 | `ptr_+length_`；4 个比较运算符宏 |
| `Atomic.h` | `__sync` 原子整数模板 | `get()` 用 CAS(0,0)；分清 `getAndAdd`/`addAndGet` |
| `Singleton.h` | 进程单例模板 | `pthread_once` + `atexit` 析构 |
| `ThreadLocalSingleton.h` | 线程内单例模板 | `__thread` 指针 + `pthread_key` 两套状态 |
| `Timestamp.h/.cc` | 微秒时间戳值类型 | `int64` 微秒；`now()` 用 `gettimeofday`；`%06` 补零 |
| `Date.h/.cc` | 儒略日日期 | JDN 正反算；`weekDay` |
| `TimeZone.h/.cc` | TZif 时区换算 | 解析 TZif v1/v2；`upper_bound` 找转换点 |
| **L2 线程身份/同步** | | |
| `CurrentThread.h/.cc` | tid 与线程名缓存 | `__thread` 懒加载；`tidString` 格式 `"%5d "`（长度 6） |
| `Mutex.h` | mutex + holder 记录 | `MCHECK` 包 pthread 返回值；`assertLocked` |
| `Condition.h/.cc` | 条件变量 | `wait()` 里用 `UnassignGuard` 暂清 holder |
| `CountDownLatch.h/.cc` | 一次性门闩 | `wait()` 用 `while`；归零时 `notifyAll` |
| **L3 日志** | | |
| `LogStream.h/.cc` | 定长缓冲日志流 | `FixedBuffer`；`kMaxNumericSize=48`；`Fmt` 显式实例化 |
| `Logging.h/.cc` | `LOG_*` 宏与 Logger | `SourceFile` 编译期取 basename；FATAL 才 `abort` |
| **L4 线程/文件/异常** | | |
| `Thread.h/.cc` | pthread 封装 | `started_/joined_`；latch 同步 tid；**定义 `cacheTid`** |
| `Exception.h/.cc` | 带栈回溯的异常 | 构造时就抓 `stackTrace(false)` |
| `FileUtil.h/.cc` | 文件读写 | `fwrite_unlocked` + 64K buffer；`-D_FILE_OFFSET_BITS=64` |
| `ProcessInfo.h/.cc` | 进程/线程自省 | 读 `/proc`；`scanDir` 靠 filter 副作用计数 |
| `LogFile.h/.cc` | 按大小/日期滚动 | `rollSize` + 跨天；每 1024 条才查跨天 |
| **L5 并发容器** | | |
| `BlockingQueue.h` | 无界阻塞队列 | `deque` + 单条件变量；`take` 用 `while` |
| `BoundedBlockingQueue.h` | 有界阻塞队列 | `boost::circular_buffer` + 双条件变量 |
| `ThreadPool.h/.cc` | 固定线程池 | 停止时锁外 `join`；空池退化为直调 |
| `ThreadLocal.h` | 每线程对象 | `pthread_key` + 析构器 |
| **L6 异步日志** | | |
| `AsyncLogging.h/.cc` | 异步日志后端 | 双缓冲 + 16 槽队列；积压超 25 个 buffer 丢日志 |

### 3.2 muduo/net（L0→L6，共 40 个，含 `poller/` 5 个）

| 文件 | 角色 | 实现要点 |
|---|---|---|
| **L0 裸封装/类型** | | |
| `Endian.h` | 字节序转换 | 全 inline，6 个 `htobe*`/`be*toh` |
| `Callbacks.h` | 回调类型集合 | 6 个 `std::function`；`MessageCallback` 带 `Timestamp` |
| `TimerId.h` | 定时器句柄 | `Timer*` + `sequence_` 双键 |
| `SocketsOps.h/.cc` | syscall 封装 | `accept4` vs `NO_ACCEPT4`；地址统一按 `sockaddr_in6` 存 |
| **L1 fd 对象化** | | |
| `InetAddress.h/.cc` | sockaddr 值语义 | `union addr_/addr6_`；`resolve` 用 `__thread` 64K 缓冲 |
| `Socket.h/.cc` | fd 的 RAII | `const int sockfd_`；只有析构函数 close；sockopt 封装 |
| **L2 缓冲区** | | |
| `Buffer.h/.cc` | 可读写字节缓冲 | 双索引；`readv` 两段 iovec；`kCheapPrepend=8` |
| **L3 Reactor 核心** | | |
| `Channel.h/.cc` | fd 事件分发单元 | `events_/revents_/index_`；`tie_` 防 UAF；`handleEventWithGuard` |
| `Poller.h/.cc` | 多路复用抽象 | `map<int,Channel*>` 只记账；工厂 `newDefaultPoller` |
| `poller/EPollPoller.h/.cc` | epoll 实现 | `index` 三态；`data.ptr` 存 `Channel*`；events_ 满则翻倍 |
| `poller/PollPoller.h/.cc` | poll 实现 | index 是数组下标；删除用 swap-back |
| `poller/DefaultPoller.cc` | 选择 poller | 读 `MUDUO_USE_POLL` 环境变量 |
| `EventLoop.h/.cc` | Reactor 主循环 | eventfd 唤醒；`doPendingFunctors` 先 swap；poll 超时 10s |
| **L4 定时器** | | |
| `Timer.h/.cc` | 定时器实体 | `sequence_` 原子自增；`restart` 只对 repeat 生效 |
| `TimerQueue.h/.cc` | timerfd + 有序 set | 两个 set 互为镜像；只在最早变化时 `settime` |
| **L5 线程模型/accept** | | |
| `EventLoopThread.h/.cc` | one loop per thread | `cond_` 等 `loop_ != NULL`；loop 是栈对象 |
| `EventLoopThreadPool.h/.cc` | 子 loop 池 | `unique_ptr` 持线程、裸指针持 loop；round-robin |
| `Acceptor.h/.cc` | 监听与 accept | `idleFd_` 扛 EMFILE；**一次只 accept 一个** |
| **L6 连接/装配** | | |
| `TcpConnection.h/.cc` | 连接状态机 | 四态；send 先直写；`outputBuffer_` + 64MB 水位；close 回调最后执行 |
| `TcpServer.h/.cc` | 服务端装配 | `unique_ptr` 藏 Acceptor；`map<name,conn>` 保活；remove 回投 base loop |
| `Connector.h/.cc` | 主动连接与重试 | EPOLLOUT 后查 `SO_ERROR` + `isSelfConnect`；指数退避 |
| `TcpClient.h/.cc` | 客户端装配 | `connection_` 受 mutex 保护；retry 逻辑 |

## 4. 依赖顺序（写的时候照着来）

```
base:  copyable/noncopyable/Types
       → StringPiece/Atomic/Timestamp/Date/TimeZone
       → CurrentThread → Mutex → Condition → CountDownLatch
       → LogStream → Logging → Exception → Thread
       → FileUtil → ProcessInfo → LogFile → 队列 → AsyncLogging

net:   Endian/SocketsOps → InetAddress/Socket → Buffer
       → Channel → EventLoop → Poller → EPollPoller/PollPoller → DefaultPoller
       → Timer/TimerQueue → EventLoopThread(Pool) → Acceptor
       → TcpConnection → TcpServer → Connector → TcpClient
```

两条容易卡住的规则：

1. **`EventLoop.h` ↔ `Poller.h` 是唯一一处头文件循环**：让 `Poller.h` include `EventLoop.h`，
   `EventLoop.h` 只前置声明 `Poller`/`Channel`/`TimerQueue` 并用 `unique_ptr`——
   因此 `EventLoop` 的析构函数必须在 `.cc` 里定义（out-line）。
2. **`cacheTid()` 在参考实现里定义在 `Thread.cc`**，而 `Logging.cc` 要用它
   → 只编 `Logging.cc` + `CurrentThread.cc` 会 undefined reference。
   自己写时把 `cacheTid/isMainThread/sleepUsec` 放进 `CurrentThread.cc`，依赖就变成单向的。

## 5. 参考实现里的两处"悬空声明"（照抄会链接失败）

- `StringPiece.h` 声明了 `operator<<(std::ostream&, const StringPiece&)`，但没有 `StringPiece.cc`；
- `TimeZone.h` 声明了 `static TimeZone China()`，`TimeZone.cc` 里没有实现。

Gomoku 没用到这两个，所以不影响跑通。想用到时自己补（正好检验依赖表读没读明白）。

## 6. 写完怎么验

```bash
./scripts/progress.sh                     # 82/82 且逐字相同数应该很低
cd muduo && cmake -S . -B build && cmake --build build -j$(nproc)
cd .. && ./scripts/build.sh               # 整个工程（含 Gomoku）
./build/bin/gomoku_board_test             # 期望 PASS（591 项）
python3 Gomoku/tests/e2e_test.py          # 期望 38 checks, 0 failures
```

再往下就是性能基线测量与优化，见仓库根 `AGENTS.md` 与 `benchmarks/RESULTS.md`。
