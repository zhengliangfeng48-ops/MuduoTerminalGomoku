// ============================================================================
// gomoku_client 入口
//
// 启动流程：
//   1. 解析参数（--host/--port/--nick/--password/--register/--no-color）
//   2. 缺少昵称或密码时交互式询问（密码用 termios 关闭回显）
//   3. 创建 EventLoop + GameClient，发起连接
//   4. 启动 stdin 线程，进入事件循环
//
// 命令行直接给全昵称密码，便于脚本化冒烟测试：
//   ./gomoku_client --nick alice --password alice123 --register
// ============================================================================
#include <termios.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

#include "muduo/base/Logging.h"
#include "muduo/net/EventLoop.h"

#include "client/GameClient.h"
#include "client/StdinLoop.h"
#include "common/Protocol.h"

namespace {

void printUsage(const char* prog) {
  std::printf(
      "用法: %s [选项]\n"
      "\n"
      "  --host <ip>        服务端地址，默认 127.0.0.1\n"
      "  --port <port>      服务端端口，默认 9527\n"
      "  --nick <昵称>      账号昵称（缺省则交互输入）\n"
      "  --password <密码>  账号密码（缺省则交互输入，不回显）\n"
      "  --register         本次为注册（缺省则交互式选择注册/登录）\n"
      "  --login            本次为登录\n"
      "  --no-color         关闭 ANSI 颜色\n"
      "  -h, --help         显示帮助\n",
      prog);
}

std::string readLine(const std::string& prompt) {
  std::printf("%s", prompt.c_str());
  std::fflush(stdout);
  std::string line;
  std::getline(std::cin, line);
  return gomoku::trim(line);
}

std::string readPassword(const std::string& prompt) {
  std::printf("%s", prompt.c_str());
  std::fflush(stdout);
  struct termios oldt;
  bool changed = false;
  if (::tcgetattr(STDIN_FILENO, &oldt) == 0) {
    struct termios newt = oldt;
    newt.c_lflag &= ~static_cast<tcflag_t>(ECHO);  // 关闭回显
    if (::tcsetattr(STDIN_FILENO, TCSANOW, &newt) == 0) {
      changed = true;
    }
  }
  std::string line;
  std::getline(std::cin, line);
  if (changed) {
    ::tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
  }
  std::printf("\n");
  return gomoku::trim(line);
}

}  // namespace

int main(int argc, char** argv) {
  gomoku::GameClient::Options options;
  bool interactiveMode = true;  // 未显式指定 --register / --login

  for (int i = 1; i < argc; ++i) {
    const std::string arg(argv[i]);
    if (arg == "-h" || arg == "--help") {
      printUsage(argv[0]);
      return 0;
    } else if (arg == "--register") {
      options.doRegister = true;
      interactiveMode = false;
    } else if (arg == "--login") {
      options.doRegister = false;
      interactiveMode = false;
    } else if (arg == "--no-color") {
      options.color = false;
    } else if (arg == "--host" || arg == "--port" || arg == "--nick" || arg == "--password") {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "参数 %s 缺少取值\n", arg.c_str());
        return 1;
      }
      const std::string value(argv[++i]);
      if (arg == "--host") {
        options.host = value;
      } else if (arg == "--port") {
        options.port = static_cast<unsigned short>(std::atoi(value.c_str()));
        if (options.port == 0) {
          std::fprintf(stderr, "端口非法: %s\n", value.c_str());
          return 1;
        }
      } else if (arg == "--nick") {
        options.nickname = value;
      } else {
        options.password = value;
      }
    } else {
      std::fprintf(stderr, "未知参数: %s\n", arg.c_str());
      printUsage(argv[0]);
      return 1;
    }
  }

  // 交互式引导：注册 / 登录
  if (options.nickname.empty() || options.password.empty()) {
    gomoku::TerminalUI banner;
    banner.printBanner();
  }
  if (interactiveMode && (options.nickname.empty() || options.password.empty())) {
    const std::string choice = readLine("1) 注册新账号   2) 登录已有账号   [1/2]: ");
    options.doRegister = (choice.empty() || choice == "1");
  }
  if (options.nickname.empty()) {
    options.nickname = readLine(options.doRegister ? "请输入昵称（注册后不可重复）: " : "请输入昵称: ");
  }
  if (options.password.empty()) {
    options.password = readPassword("请输入密码: ");
  }

  if (!gomoku::isValidNickname(options.nickname)) {
    std::fprintf(stderr, "昵称不合法：需为 2~16 个字母/数字/下划线，且以字母或数字开头\n");
    return 1;
  }
  if (!gomoku::isValidPassword(options.password)) {
    std::fprintf(stderr, "密码不合法：需为 4~32 个可打印字符且不含空格\n");
    return 1;
  }

  // 客户端不需要服务器端的 INFO 日志，调成 WARN 以免干扰界面
  muduo::Logger::setLogLevel(muduo::Logger::WARN);

  muduo::net::EventLoop loop;
  gomoku::GameClient client(&loop, options);
  client.ui()->printBanner();
  client.ui()->printSystem("正在连接 " + options.host + ":" + std::to_string(options.port) + " ...");

  gomoku::StdinLoop stdinLoop(
      &loop, std::bind(&gomoku::GameClient::handleInput, &client, std::placeholders::_1));
  client.connect();
  stdinLoop.start();
  loop.loop();

  std::printf("\n客户端已退出。\n");
  return 0;
}
