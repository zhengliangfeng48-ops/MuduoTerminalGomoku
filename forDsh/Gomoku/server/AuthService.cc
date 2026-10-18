#include "server/AuthService.h"

#include <cstdlib>

#include "muduo/base/Logging.h"

namespace gomoku {

AuthService::AuthService(Db* db) : db_(db) {}

bool AuthService::available() const {
  return db_ != NULL && db_->connected();
}

bool AuthService::queryIdLocked(const std::string& nickname, long long* id, std::string* err) {
  std::vector<std::vector<std::string> > rows;
  const std::string sql =
      "SELECT id FROM users WHERE nickname='" + db_->escape(nickname) + "' LIMIT 1";
  if (!db_->query(sql, &rows, err)) {
    return false;
  }
  if (rows.empty() || rows[0].empty()) {
    if (err != NULL) {
      *err = "no such user";
    }
    return false;
  }
  *id = std::atoll(rows[0][0].c_str());
  return true;
}

AuthStatus AuthService::registerUser(const std::string& nickname, const std::string& password,
                                     UserStats* out, std::string* err) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!available()) {
    if (err != NULL) {
      *err = "database is not connected";
    }
    return AuthStatus::kDbError;
  }

  // 1) 先查重：给出明确的 NAME_TAKEN 而不是让客户端看到 1062 错误。
  {
    std::vector<std::vector<std::string> > rows;
    const std::string sql =
        "SELECT id FROM users WHERE nickname='" + db_->escape(nickname) + "' LIMIT 1";
    if (!db_->query(sql, &rows, err)) {
      return AuthStatus::kDbError;
    }
    if (!rows.empty()) {
      return AuthStatus::kNameTaken;
    }
  }

  // 2) 插入。即使上面查过重，并发注册仍可能撞 UNIQUE 索引，需要兜底。
  const std::string sql =
      "INSERT INTO users (nickname, password, games, wins, win_rate) VALUES ('" +
      db_->escape(nickname) + "', '" + db_->escape(password) + "', 0, 0, 0)";
  if (!db_->execute(sql, err)) {
    // 1062 = ER_DUP_ENTRY
    if (err != NULL && err->find("Duplicate entry") != std::string::npos) {
      return AuthStatus::kNameTaken;
    }
    return AuthStatus::kDbError;
  }

  // 3) 读回 id（AUTO_INCREMENT 值）。
  if (!queryIdLocked(nickname, &out->id, err)) {
    return AuthStatus::kDbError;
  }
  out->nickname = nickname;
  out->games = 0;
  out->wins = 0;
  out->winRate = "0.0000";
  LOG_INFO << "new user registered: " << nickname << " (id=" << out->id << ")";
  return AuthStatus::kOk;
}

AuthStatus AuthService::login(const std::string& nickname, const std::string& password,
                              UserStats* out, std::string* err) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!available()) {
    if (err != NULL) {
      *err = "database is not connected";
    }
    return AuthStatus::kDbError;
  }

  std::vector<std::vector<std::string> > rows;
  const std::string sql = "SELECT id, password, games, wins FROM users WHERE nickname='" +
                          db_->escape(nickname) + "' LIMIT 1";
  if (!db_->query(sql, &rows, err)) {
    return AuthStatus::kDbError;
  }
  if (rows.empty()) {
    return AuthStatus::kNoSuchUser;
  }
  const std::vector<std::string>& row = rows[0];
  if (row.size() < 4) {
    if (err != NULL) {
      *err = "unexpected result set";
    }
    return AuthStatus::kDbError;
  }
  if (row[1] != password) {
    return AuthStatus::kBadPassword;
  }

  out->id = std::atoll(row[0].c_str());
  out->nickname = nickname;
  out->games = std::atoi(row[2].c_str());
  out->wins = std::atoi(row[3].c_str());
  out->winRate = formatWinRate(out->wins, out->games);

  // 刷新最后登录时间（失败不影响登录本身）。
  std::string updateErr;
  if (!db_->execute("UPDATE users SET last_login_at=NOW() WHERE id=" +
                        std::to_string(out->id), &updateErr)) {
    LOG_WARN << "update last_login_at failed: " << updateErr;
  }
  return AuthStatus::kOk;
}

bool AuthService::loadStats(const std::string& nickname, UserStats* out, std::string* err) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!available()) {
    if (err != NULL) {
      *err = "database is not connected";
    }
    return false;
  }
  std::vector<std::vector<std::string> > rows;
  const std::string sql = "SELECT id, games, wins FROM users WHERE nickname='" +
                          db_->escape(nickname) + "' LIMIT 1";
  if (!db_->query(sql, &rows, err)) {
    return false;
  }
  if (rows.empty() || rows[0].size() < 3) {
    if (err != NULL) {
      *err = "no such user";
    }
    return false;
  }
  out->id = std::atoll(rows[0][0].c_str());
  out->nickname = nickname;
  out->games = std::atoi(rows[0][1].c_str());
  out->wins = std::atoi(rows[0][2].c_str());
  out->winRate = formatWinRate(out->wins, out->games);
  return true;
}

bool AuthService::recordResult(const std::string& roomId, const std::string& blackNick,
                               const std::string& whiteNick, GomokuGame::Result result,
                               const std::string& reason, int moves, std::string* err) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!available()) {
    if (err != NULL) {
      *err = "database is not connected";
    }
    return false;
  }

  long long blackId = -1;
  long long whiteId = -1;
  if (!queryIdLocked(blackNick, &blackId, err)) {
    return false;
  }
  if (!queryIdLocked(whiteNick, &whiteId, err)) {
    return false;
  }

  // 事务：要么双方的场次/胜率与历史表一起更新，要么全部回滚。
  if (!db_->execute("START TRANSACTION", err)) {
    return false;
  }

  // 两个容易踩的坑：
  //   1) MySQL 的整数除法：必须写 games + 1.0 才会得到小数结果；
  //   2) MySQL 的 SET 子句是**从左到右**求值的，后面的表达式会看到前面已经被
  //      更新的列值。所以 win_rate 必须写在 games / wins 之前，它才能用到
  //      更新前的 wins/games（否则胜率会算成 (wins+W)/(games+2)，明显偏大）。
  const int blackWin = (result == GomokuGame::kBlackWin) ? 1 : 0;
  const int whiteWin = (result == GomokuGame::kWhiteWin) ? 1 : 0;
  const std::string updateBlack =
      "UPDATE users SET win_rate = (wins + " + std::to_string(blackWin) +
      ") / (games + 1.0), games = games + 1, wins = wins + " + std::to_string(blackWin) +
      " WHERE id = " + std::to_string(blackId);
  const std::string updateWhite =
      "UPDATE users SET win_rate = (wins + " + std::to_string(whiteWin) +
      ") / (games + 1.0), games = games + 1, wins = wins + " + std::to_string(whiteWin) +
      " WHERE id = " + std::to_string(whiteId);

  if (!db_->execute(updateBlack, err) || !db_->execute(updateWhite, err)) {
    std::string ignored;
    db_->execute("ROLLBACK", &ignored);
    return false;
  }

  const char* resultText = "draw";
  if (result == GomokuGame::kBlackWin) {
    resultText = "black";
  } else if (result == GomokuGame::kWhiteWin) {
    resultText = "white";
  }
  const std::string insertHistory =
      "INSERT INTO match_history (room_id, black_nick, white_nick, result, reason, moves) VALUES ('" +
      db_->escape(roomId) + "', '" + db_->escape(blackNick) + "', '" +
      db_->escape(whiteNick) + "', '" + resultText + "', '" + db_->escape(reason) + "', " +
      std::to_string(moves) + ")";

  if (!db_->execute(insertHistory, err)) {
    std::string ignored;
    db_->execute("ROLLBACK", &ignored);
    return false;
  }

  if (!db_->execute("COMMIT", err)) {
    std::string ignored;
    db_->execute("ROLLBACK", &ignored);
    return false;
  }
  return true;
}

long long AuthService::userCount(std::string* err) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!available()) {
    if (err != NULL) {
      *err = "database is not connected";
    }
    return -1;
  }
  std::vector<std::vector<std::string> > rows;
  if (!db_->query("SELECT COUNT(*) FROM users", &rows, err)) {
    return -1;
  }
  if (rows.empty() || rows[0].empty()) {
    return 0;
  }
  return std::atoll(rows[0][0].c_str());
}

}  // namespace gomoku
