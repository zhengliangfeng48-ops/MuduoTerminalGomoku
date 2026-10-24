#include "server/Config.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace gomoku {

namespace {

// 读取环境变量，不存在则返回空串。
std::string envOr(const char* name, const std::string& fallback) {
  const char* v = std::getenv(name);
  if (v == NULL || v[0] == '\0') {
    return fallback;
  }
  return std::string(v);
}

int envIntOr(const char* name, int fallback) {
  const char* v = std::getenv(name);
  if (v == NULL || v[0] == '\0') {
    return fallback;
  }
  return std::atoi(v);
}

// 严格解析非负整数，避免 atoi 把 "abc" 解析成 0。
bool parseInt(const std::string& s, int minV, int maxV, int* out) {
  if (s.empty()) {
    return false;
  }
  for (std::size_t i = 0; i < s.size(); ++i) {
    if (s[i] < '0' || s[i] > '9') {
      return false;
    }
  }
  const long v = std::strtol(s.c_str(), NULL, 10);
  if (v < minV || v > maxV) {
    return false;
  }
  *out = static_cast<int>(v);
  return true;
}

// 支持 "--key value" 与 "--key=value" 两种写法。
bool splitFlag(const std::string& arg, std::string* key, std::string* value, bool* hasValue) {
  if (arg.size() < 3 || arg[0] != '-' || arg[1] != '-') {
    return false;
  }
  const std::string body = arg.substr(2);
  const std::size_t eq = body.find('=');
  if (eq == std::string::npos) {
    *key = body;
    *hasValue = false;
    value->clear();
  } else {
    *key = body.substr(0, eq);
    *value = body.substr(eq + 1);
    *hasValue = true;
  }
  return true;
}

}  // namespace

Config::Config()
    : listenAddr("0.0.0.0"),
      port(9527),
      httpPort(8080),
      ioThreads(4),
      logDir("logs") {
  // 环境变量默认值
  listenAddr = envOr("GOMOKU_LISTEN", listenAddr);
  port = static_cast<unsigned short>(envIntOr("GOMOKU_PORT", port));
  httpPort = static_cast<unsigned short>(envIntOr("GOMOKU_HTTP_PORT", httpPort));
  ioThreads = envIntOr("GOMOKU_IO_THREADS", ioThreads);
  logDir = envOr("GOMOKU_LOG_DIR", logDir);
  db.host = envOr("GOMOKU_DB_HOST", db.host);
  db.port = static_cast<unsigned int>(envIntOr("GOMOKU_DB_PORT", static_cast<int>(db.port)));
  db.user = envOr("GOMOKU_DB_USER", db.user);
  db.password = envOr("GOMOKU_DB_PASS", db.password);
  db.database = envOr("GOMOKU_DB_NAME", db.database);
}

Config::ParseResult Config::parse(int argc, char** argv, std::string* err) {
  for (int i = 1; i < argc; ++i) {
    const std::string arg(argv[i]);
    if (arg == "-h" || arg == "--help") {
      return kHelp;
    }
    std::string key;
    std::string value;
    bool hasValue = false;
    if (!splitFlag(arg, &key, &value, &hasValue)) {
      *err = "无法识别的参数: " + arg;
      return kError;
    }
    if (!hasValue) {
      // 形如 --port 9527
      if (i + 1 >= argc) {
        *err = "参数缺少取值: " + arg;
        return kError;
      }
      value = argv[++i];
    }

    if (key == "listen") {
      listenAddr = value;
    } else if (key == "port") {
      int v = 0;
      if (!parseInt(value, 1, 65535, &v)) {
        *err = "--port 取值非法: " + value;
        return kError;
      }
      port = static_cast<unsigned short>(v);
    } else if (key == "http-port") {
      int v = 0;
      if (!parseInt(value, 1, 65535, &v)) {
        *err = "--http-port 取值非法: " + value;
        return kError;
      }
      httpPort = static_cast<unsigned short>(v);
    } else if (key == "io-threads") {
      int v = 0;
      if (!parseInt(value, 0, 256, &v)) {
        *err = "--io-threads 取值非法: " + value;
        return kError;
      }
      ioThreads = v;
    } else if (key == "log-dir") {
      logDir = value;
    } else if (key == "db-host") {
      db.host = value;
    } else if (key == "db-port") {
      int v = 0;
      if (!parseInt(value, 1, 65535, &v)) {
        *err = "--db-port 取值非法: " + value;
        return kError;
      }
      db.port = static_cast<unsigned int>(v);
    } else if (key == "db-user") {
      db.user = value;
    } else if (key == "db-pass") {
      db.password = value;
    } else if (key == "db-name") {
      db.database = value;
    } else {
      *err = "未知参数: --" + key;
      return kError;
    }
  }

  if (port == httpPort) {
    *err = "--port 与 --http-port 不能相同";
    return kError;
  }
  return kOk;
}

void Config::printUsage(const char* prog) {
  std::printf(
      "用法: %s [选项]\n"
      "\n"
      "  --listen <ip>        监听地址，默认 0.0.0.0\n"
      "  --port <port>        游戏协议端口，默认 9527\n"
      "  --http-port <port>   压测 HTTP 端口（webbench 用），默认 8080\n"
      "  --io-threads <n>     TcpServer IO 线程数，默认 4；0 表示只用 base loop\n"
      "  --log-dir <dir>      日志目录，默认 logs；传空串则只输出到终端\n"
      "  --db-host <host>     MySQL 地址，默认 127.0.0.1\n"
      "  --db-port <port>     MySQL 端口，默认 3306\n"
      "  --db-user <user>     MySQL 用户，默认 gomoku\n"
      "  --db-pass <pass>     MySQL 密码，默认 gomoku123\n"
      "  --db-name <name>     MySQL 库名，默认 gomoku\n"
      "  -h, --help           显示本帮助\n"
      "\n"
      "对应的环境变量：GOMOKU_LISTEN / GOMOKU_PORT / GOMOKU_HTTP_PORT /\n"
      "  GOMOKU_IO_THREADS / GOMOKU_LOG_DIR / GOMOKU_DB_HOST / GOMOKU_DB_PORT /\n"
      "  GOMOKU_DB_USER / GOMOKU_DB_PASS / GOMOKU_DB_NAME（命令行优先级更高）\n",
      prog);
}

}  // namespace gomoku
