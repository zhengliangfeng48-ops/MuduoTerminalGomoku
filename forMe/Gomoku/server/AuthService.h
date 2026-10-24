// ============================================================================
// 用户认证与战绩服务（注册 / 登录 / 战绩读写）
//
// 表结构见 Gomoku/sql/schema.sql：
//   users(id, nickname UNIQUE, password, games, wins, win_rate, created_at, last_login_at)
//
// 说明：
//   * 按需求，密码目前**明文存储**（不做哈希/加盐），后续可在此单一入口替换；
//   * 昵称唯一性由 DB 的 UNIQUE 索引最终保证，代码里先查一次只是为了给出更友好的
//     错误码（并发注册时仍可能撞索引，捕获 1062 后同样返回 kNameTaken）；
//   * 所有方法都在内部加锁，可被多个 IO 线程安全调用（实际串行执行）。
// ============================================================================
#pragma once

#include <mutex>
#include <string>

#include "server/Db.h"
#include "server/GomokuGame.h"

namespace gomoku {

// 认证结果
enum class AuthStatus {
  kOk = 0,
  kNameTaken,     // 注册时昵称已存在
  kNoSuchUser,    // 登录时用户不存在
  kBadPassword,   // 密码错误
  kDbError,       // 数据库不可用/执行失败
};

// 用户战绩快照
struct UserStats {
  long long id;
  std::string nickname;
  int games;
  int wins;
  std::string winRate;  // 形如 "0.6667"

  UserStats() : id(-1), games(0), wins(0), winRate("0.0000") {}
};

class AuthService {
 public:
  explicit AuthService(Db* db);

  // 数据库是否已连接（未连接时注册/登录直接返回 kDbError）。
  bool available() const;

  // 注册并返回初始战绩（games=wins=0）。
  AuthStatus registerUser(const std::string& nickname, const std::string& password,
                          UserStats* out, std::string* err);

  // 登录：校验密码，成功后刷新 last_login_at 并返回战绩。
  AuthStatus login(const std::string& nickname, const std::string& password,
                   UserStats* out, std::string* err);

  // 读取战绩。
  bool loadStats(const std::string& nickname, UserStats* out, std::string* err);

  // 一局结束后写入战绩：
  //   * 双方 games+1；
  //   * 胜者 wins+1；负者 wins 不变；和棋双方都不加；
  //   * 重算 win_rate = wins / games；
  //   * 追加一条 match_history。
  // 整个过程在一个事务里，失败则回滚。
  bool recordResult(const std::string& roomId, const std::string& blackNick,
                    const std::string& whiteNick, GomokuGame::Result result,
                    const std::string& reason, int moves, std::string* err);

  // 统计注册用户数（供 /stats 端点使用）。
  long long userCount(std::string* err);

 private:
  bool queryIdLocked(const std::string& nickname, long long* id, std::string* err);

  std::mutex mutex_;
  Db* db_;
};

}  // namespace gomoku
