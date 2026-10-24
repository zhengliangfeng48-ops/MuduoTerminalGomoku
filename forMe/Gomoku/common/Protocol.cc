#include "common/Protocol.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace gomoku {

namespace {

bool isSpaceChar(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}

bool isAlphaNum(char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

}  // namespace

std::string trim(const std::string& s) {
  std::size_t begin = 0;
  std::size_t end = s.size();
  while (begin < end && isSpaceChar(s[begin])) {
    ++begin;
  }
  while (end > begin && isSpaceChar(s[end - 1])) {
    --end;
  }
  return s.substr(begin, end - begin);
}

std::vector<std::string> splitWhitespace(const std::string& s) {
  std::vector<std::string> tokens;
  std::string cur;
  for (std::size_t i = 0; i < s.size(); ++i) {
    if (isSpaceChar(s[i])) {
      if (!cur.empty()) {
        tokens.push_back(cur);
        cur.clear();
      }
    } else {
      cur.push_back(s[i]);
    }
  }
  if (!cur.empty()) {
    tokens.push_back(cur);
  }
  return tokens;
}

bool startsWith(const std::string& s, const char* prefix) {
  const std::size_t n = std::strlen(prefix);
  if (s.size() < n) {
    return false;
  }
  return std::memcmp(s.data(), prefix, n) == 0;
}

bool equalsIgnoreCase(const std::string& s, const char* other) {
  const std::size_t n = std::strlen(other);
  if (s.size() != n) {
    return false;
  }
  for (std::size_t i = 0; i < n; ++i) {
    char a = s[i];
    char b = other[i];
    if (a >= 'a' && a <= 'z') {
      a = static_cast<char>(a - 'a' + 'A');
    }
    if (b >= 'a' && b <= 'z') {
      b = static_cast<char>(b - 'a' + 'A');
    }
    if (a != b) {
      return false;
    }
  }
  return true;
}

bool isValidNickname(const std::string& nick) {
  if (nick.size() < kMinNickLen || nick.size() > kMaxNickLen) {
    return false;
  }
  if (!isAlphaNum(nick[0])) {
    return false;
  }
  for (std::size_t i = 0; i < nick.size(); ++i) {
    if (!isAlphaNum(nick[i]) && nick[i] != '_') {
      return false;
    }
  }
  return true;
}

bool isValidPassword(const std::string& pwd) {
  if (pwd.size() < kMinPassLen || pwd.size() > kMaxPassLen) {
    return false;
  }
  for (std::size_t i = 0; i < pwd.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(pwd[i]);
    if (c < 33 || c > 126) {
      return false;
    }
  }
  return true;
}

bool isValidRoomId(const std::string& roomId) {
  if (roomId.empty() || roomId.size() > kMaxRoomIdLen) {
    return false;
  }
  for (std::size_t i = 0; i < roomId.size(); ++i) {
    if (!isAlphaNum(roomId[i])) {
      return false;
    }
  }
  return true;
}

std::string formatWinRate(int wins, int games) {
  char buf[32];
  const double rate = games <= 0 ? 0.0 : static_cast<double>(wins) / static_cast<double>(games);
  std::snprintf(buf, sizeof buf, "%.4f", rate);
  return std::string(buf);
}

bool parseCoordinates(const std::string& sx, const std::string& sy, int* x, int* y) {
  if (sx.empty() || sy.empty()) {
    return false;
  }
  // 只接受纯数字，长度 <= 2，避免 std::stoi 抛异常与越界输入。
  if (sx.size() > 2 || sy.size() > 2) {
    return false;
  }
  for (std::size_t i = 0; i < sx.size(); ++i) {
    if (sx[i] < '0' || sx[i] > '9') {
      return false;
    }
  }
  for (std::size_t i = 0; i < sy.size(); ++i) {
    if (sy[i] < '0' || sy[i] > '9') {
      return false;
    }
  }
  const int vx = std::atoi(sx.c_str());
  const int vy = std::atoi(sy.c_str());
  if (vx < 1 || vx > kBoardSize || vy < 1 || vy > kBoardSize) {
    return false;
  }
  *x = vx;
  *y = vy;
  return true;
}

}  // namespace gomoku
