// ============================================================================
// MySQL 连接的最薄一层封装（基于 MySQL C API）
//
// 为什么需要这一层？
//   * MySQL C API 的 MYSQL* 不是线程安全的，而本服务是多 IO 线程的，
//     所以这里用一把 mutex 把所有访问串行化；
//   * 长连接可能被服务端/网络断开（wait_timeout、重启等），
//     所以每次操作前 ping，失败则自动重连一次；
//   * 把“拼接 SQL 时必须转义”这件事集中到 escape() 一个入口，
//     避免各处手写转义遗漏导致 SQL 注入。
//
// 说明：本项目的数据库访问都是“低频、短小”的事务（注册/登录/结束一局），
// 直接使用同步阻塞调用是可以接受的；若要极致性能，可改为“专用的 DB 线程 +
// 任务队列”或连接池，但那属于优化范畴，本项目刻意不做。
// ============================================================================
#pragma once

#include <mutex>
#include <string>
#include <vector>

#include <mysql/mysql.h>

namespace gomoku {

// 数据库连接参数（来自命令行/环境变量）
struct DbParams {
  std::string host = "127.0.0.1";
  unsigned int port = 3306;
  std::string user = "gomoku";
  std::string password = "gomoku123";
  std::string database = "gomoku";
  unsigned int connectTimeoutSec = 3;  // 连接超时，避免 DB 挂掉时长时间阻塞 IO 线程
  unsigned int readTimeoutSec = 5;     // 读超时
};

class Db {
 public:
  Db();
  ~Db();

  // 建立连接。失败时 err 带出原因，对象仍可继续尝试后续 connect。
  bool connect(const DbParams& params, std::string* err);

  bool connected() const;

  // 执行无结果集的 SQL（INSERT / UPDATE / DELETE / BEGIN / COMMIT ...）。
  bool execute(const std::string& sql, std::string* err);

  // 执行 SELECT，rows 按行保存每列的字符串值（NULL 返回空串）。
  bool query(const std::string& sql, std::vector<std::vector<std::string> >* rows, std::string* err);

  // SQL 字符串转义：返回值不含外层引号，调用方需自己写成 '...'。
  // 例：sql = "INSERT ... VALUES ('" + db.escape(nick) + "')";
  std::string escape(const std::string& s);

 private:
  // 持有 mutex_ 的前提下确保连接可用（必要时重连）。
  bool ensureConnectedLocked(std::string* err);
  bool executeLocked(const std::string& sql, std::string* err, bool allowRetry);
  bool queryLocked(const std::string& sql, std::vector<std::vector<std::string> >* rows,
                   std::string* err, bool allowRetry);

  mutable std::mutex mutex_;
  MYSQL* conn_;
  DbParams params_;
};

}  // namespace gomoku
