# webbench（内置版）

本目录是 [EZLippi/WebBench](https://github.com/EZLippi/WebBench)（经典 webbench 1.5 的中文注释版）的副本，
用于对 Gomoku 服务端的 HTTP 压测端点做压力/性能测试。

## 1. 许可证

GPL v3，见同目录 `LICENSE`。上游作者：Radim Kolar（1997-2004），中文注释分支由 EZLippi 维护。

## 2. 与上游的唯一差异

```diff
  #include <sys/param.h>
- #include <rpc/types.h>
  #include <getopt.h>
```

原因：Ubuntu 22.04（glibc 2.35）起不再随 libc 提供 `<rpc/types.h>`，
而该头在本文件中**并未被使用**。删除后即可在新发行版上直接编译。
除此之外 `webbench.c`、`socket.c` 与上游完全一致。

另外本目录的 `Makefile` 是项目自建的极简版本（上游 Makefile 里包含 tags/install/tar 等无关目标）。

## 3. 编译

```bash
cd /forDsh/tools/webbench
make
# 产物：./webbench
```

## 4. 用法

```bash
./webbench [选项] URL

  -c|--clients <n>     并发客户端数（默认 1）
  -t|--time <sec>      压测时长秒数（默认 30）
  -1|--http10          HTTP/1.0（默认就是这个，**本项目必须用它**）
  -2|--http11          HTTP/1.1（本服务端只实现 Connection: close，不要用）
  -f|--force           不等待服务器响应（只测发送，不推荐）
  -?|--help            帮助
```

注意：
- URL **必须以 `/` 结尾**（webbench 的 URL 解析要求形如 `http://host:port/path`）；
- 请使用默认的 HTTP/1.0：服务端应答后会主动关闭连接，webbench 读到 EOF 才会计数，
  换成 `-2` 会因为服务端不保持长连接而出现大量失败；
- 高并发前先 `ulimit -n 65535`，否则会报 "Cannot fork" / socket 打开失败。
