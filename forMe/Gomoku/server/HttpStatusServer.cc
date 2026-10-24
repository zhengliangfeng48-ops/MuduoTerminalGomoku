#include "server/HttpStatusServer.h"

#include <cstdio>

#include "muduo/base/Logging.h"

namespace gomoku {

namespace {

// 组装一个完整的 HTTP 响应。
// keepAlive 固定为 false：webbench 读不到 EOF 会一直等到超时，必须主动关闭。
std::string buildResponse(int code, const char* status, const std::string& contentType,
                          const std::string& body, bool headOnly) {
  char header[256];
  std::snprintf(header, sizeof header,
                "HTTP/1.1 %d %s\r\n"
                "Content-Type: %s\r\n"
                "Content-Length: %zu\r\n"
                "Connection: close\r\n"
                "Server: gomoku-muduo\r\n"
                "\r\n",
                code, status, contentType.c_str(), body.size());
  std::string out(header);
  if (!headOnly) {
    out += body;
  }
  return out;
}

}  // namespace

HttpStatusServer::HttpStatusServer(muduo::net::EventLoop* loop,
                                   const muduo::net::InetAddress& listenAddr,
                                   const std::string& name, StatsProvider stats)
    : server_(loop, listenAddr, name), stats_(stats) {
  server_.setConnectionCallback(
      std::bind(&HttpStatusServer::onConnection, this, std::placeholders::_1));
  server_.setMessageCallback(std::bind(&HttpStatusServer::onMessage, this, std::placeholders::_1,
                                       std::placeholders::_2, std::placeholders::_3));
  // 单线程足够：每个请求只是拼一个很短的字符串，压测瓶颈在 TCP 建连/关闭。
  server_.setThreadNum(0);
}

void HttpStatusServer::start() {
  server_.start();
}

void HttpStatusServer::onConnection(const muduo::net::TcpConnectionPtr& conn) {
  if (!conn->connected()) {
    conn->shutdown();
  }
}

void HttpStatusServer::onMessage(const muduo::net::TcpConnectionPtr& conn, muduo::net::Buffer* buf,
                                 muduo::Timestamp) {
  // 只解析请求行，不解析请求头/请求体：webbench 的请求非常简单。
  const char* crlf = buf->findCRLF();
  if (crlf == NULL) {
    // 请求行还没收全；防御性上限，避免被无换行的垃圾数据撑爆内存。
    if (buf->readableBytes() > 8192) {
      conn->shutdown();
    }
    return;
  }

  const std::string requestLine(buf->peek(), crlf);
  buf->retrieveUntil(crlf + 2);

  // 形如 "GET /stats HTTP/1.0"
  std::string method;
  std::string path;
  {
    std::size_t sp1 = requestLine.find(' ');
    if (sp1 != std::string::npos) {
      method = requestLine.substr(0, sp1);
      std::size_t sp2 = requestLine.find(' ', sp1 + 1);
      if (sp2 != std::string::npos) {
        path = requestLine.substr(sp1 + 1, sp2 - sp1 - 1);
      }
    }
  }

  const bool headOnly = (method == "HEAD");
  std::string body;
  std::string content;
  int code = 200;
  const char* status = "OK";

  if (path == "/" || path.empty()) {
    content = "text/html; charset=utf-8";
    body =
        "<!doctype html><html><head><meta charset=\"utf-8\"><title>Gomoku Server</title></head>"
        "<body><h1>Gomoku Server (muduo)</h1>"
        "<p>status endpoint for webbench</p>"
        "<ul><li><a href=\"/health\">/health</a></li>"
        "<li><a href=\"/stats\">/stats</a></li></ul></body></html>\n";
  } else if (path == "/health") {
    content = "application/json";
    body = "{\"status\":\"ok\"}\n";
  } else if (path == "/stats") {
    content = "application/json";
    body = stats_ ? stats_() : std::string("{}\n");
  } else if (path == "/favicon.ico") {
    code = 204;
    status = "No Content";
    content = "text/plain";
  } else {
    code = 404;
    status = "Not Found";
    content = "text/plain; charset=utf-8";
    body = "404 Not Found\n";
  }

  conn->send(buildResponse(code, status, content, body, headOnly));
  // 发送完毕后关闭写端；TcpConnection 会在发送缓冲区排空后真正 close。
  conn->shutdown();
}

}  // namespace gomoku
