#include "server/Db.h"

#include "muduo/base/Logging.h"

namespace gomoku {

namespace {
// 这两个错误码表示“连接已失效”，遇到后重连一次再试。
bool isLostConnectionError(unsigned int code) {
  return code == CR_SERVER_GONE_ERROR || code == CR_SERVER_LOST ||
         code == CR_CONN_HOST_ERROR || code == CR_CONNECTION_ERROR;
}
}  // namespace

Db::Db() : conn_(NULL) {}

Db::~Db() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (conn_ != NULL) {
    mysql_close(conn_);
    conn_ = NULL;
  }
}

bool Db::connect(const DbParams& params, std::string* err) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (conn_ != NULL) {
    mysql_close(conn_);
    conn_ = NULL;
  }
  params_ = params;

  conn_ = mysql_init(NULL);
  if (conn_ == NULL) {
    if (err != NULL) {
      *err = "mysql_init failed (out of memory)";
    }
    return false;
  }

  // 关键：给连接设置超时。否则 MySQL 不可达时，connect 会阻塞很久，
  // 而调用方可能正处在 IO 线程的回调里。
  unsigned int timeout = params_.connectTimeoutSec;
  mysql_options(conn_, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
  timeout = params_.readTimeoutSec;
  mysql_options(conn_, MYSQL_OPT_READ_TIMEOUT, &timeout);
  mysql_options(conn_, MYSQL_OPT_WRITE_TIMEOUT, &timeout);

  if (mysql_real_connect(conn_, params_.host.c_str(), params_.user.c_str(),
                         params_.password.c_str(), params_.database.c_str(),
                         params_.port, NULL, 0) == NULL) {
    if (err != NULL) {
      *err = std::string(mysql_error(conn_));
    }
    mysql_close(conn_);
    conn_ = NULL;
    return false;
  }

  // 昵称允许中文时用 utf8mb4；本项目的昵称限制为 ASCII，但聊天内容可能是中文，
  // 统一按 utf8mb4 处理可以避免乱码。
  mysql_set_character_set(conn_, "utf8mb4");
  LOG_INFO << "MySQL connected: " << params_.user << "@" << params_.host << ":" << params_.port
           << "/" << params_.database;
  return true;
}

bool Db::connected() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return conn_ != NULL;
}

bool Db::ensureConnectedLocked(std::string* err) {
  if (conn_ == NULL) {
    if (err != NULL) {
      *err = "database is not connected";
    }
    return false;
  }
  // mysql_ping 会自行尝试重连（需要 MYSQL_OPT_RECONNECT 或 5.x 的默认行为）。
  // 这里显式判断返回值：非 0 表示连接已不可用，直接走重连流程。
  if (mysql_ping(conn_) != 0) {
    LOG_WARN << "MySQL ping failed: " << mysql_error(conn_) << ", reconnecting...";
    mysql_close(conn_);
    conn_ = NULL;
    DbParams params = params_;
    // 复用 connect() 的逻辑，但当前已持有锁，因此这里手工重建连接。
    conn_ = mysql_init(NULL);
    if (conn_ == NULL) {
      if (err != NULL) {
        *err = "mysql_init failed";
      }
      return false;
    }
    unsigned int timeout = params.connectTimeoutSec;
    mysql_options(conn_, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
    timeout = params.readTimeoutSec;
    mysql_options(conn_, MYSQL_OPT_READ_TIMEOUT, &timeout);
    mysql_options(conn_, MYSQL_OPT_WRITE_TIMEOUT, &timeout);
    if (mysql_real_connect(conn_, params.host.c_str(), params.user.c_str(),
                           params.password.c_str(), params.database.c_str(),
                           params.port, NULL, 0) == NULL) {
      if (err != NULL) {
        *err = std::string(mysql_error(conn_));
      }
      mysql_close(conn_);
      conn_ = NULL;
      return false;
    }
    mysql_set_character_set(conn_, "utf8mb4");
    LOG_INFO << "MySQL reconnected";
  }
  return true;
}

bool Db::executeLocked(const std::string& sql, std::string* err, bool allowRetry) {
  if (!ensureConnectedLocked(err)) {
    return false;
  }
  if (mysql_query(conn_, sql.c_str()) != 0) {
    const unsigned int code = mysql_errno(conn_);
    if (allowRetry && isLostConnectionError(code)) {
      LOG_WARN << "MySQL lost connection during query, retrying once: " << mysql_error(conn_);
      // 丢弃失效连接，下一次 ensureConnectedLocked 会重建
      mysql_close(conn_);
      conn_ = NULL;
      return executeLocked(sql, err, false);
    }
    if (err != NULL) {
      *err = std::string(mysql_error(conn_));
    }
    return false;
  }
  return true;
}

bool Db::execute(const std::string& sql, std::string* err) {
  std::lock_guard<std::mutex> lock(mutex_);
  return executeLocked(sql, err, true);
}

bool Db::queryLocked(const std::string& sql, std::vector<std::vector<std::string> >* rows,
                     std::string* err, bool allowRetry) {
  if (!ensureConnectedLocked(err)) {
    return false;
  }
  if (mysql_query(conn_, sql.c_str()) != 0) {
    const unsigned int code = mysql_errno(conn_);
    if (allowRetry && isLostConnectionError(code)) {
      LOG_WARN << "MySQL lost connection during query, retrying once: " << mysql_error(conn_);
      mysql_close(conn_);
      conn_ = NULL;
      return queryLocked(sql, rows, err, false);
    }
    if (err != NULL) {
      *err = std::string(mysql_error(conn_));
    }
    return false;
  }

  MYSQL_RES* res = mysql_store_result(conn_);
  if (res == NULL) {
    // 可能是无结果集的语句，或读取结果失败
    if (mysql_field_count(conn_) == 0) {
      return true;
    }
    if (err != NULL) {
      *err = std::string(mysql_error(conn_));
    }
    return false;
  }

  if (rows != NULL) {
    rows->clear();
    MYSQL_ROW row;
    const unsigned int fields = mysql_num_fields(res);
    while ((row = mysql_fetch_row(res)) != NULL) {
      std::vector<std::string> one;
      one.reserve(fields);
      for (unsigned int i = 0; i < fields; ++i) {
        one.push_back(row[i] != NULL ? std::string(row[i]) : std::string());
      }
      rows->push_back(one);
    }
  }
  mysql_free_result(res);
  return true;
}

bool Db::query(const std::string& sql, std::vector<std::vector<std::string> >* rows, std::string* err) {
  std::lock_guard<std::mutex> lock(mutex_);
  return queryLocked(sql, rows, err, true);
}

std::string Db::escape(const std::string& s) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::string err;
  if (!ensureConnectedLocked(&err)) {
    // 没有连接时无法调用 mysql_real_escape_string；
    // 此时 SQL 也无法执行，返回原串即可（调用方随后会收到 DB 错误）。
    return s;
  }
  std::vector<char> buf(s.size() * 2 + 1);
  const unsigned long len = mysql_real_escape_string(conn_, &buf[0], s.data(),
                                                     static_cast<unsigned long>(s.size()));
  return std::string(&buf[0], len);
}

}  // namespace gomoku
