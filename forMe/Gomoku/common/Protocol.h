// ============================================================================
// Gomoku 通信协议定义（服务端与客户端共享）
//
// 传输方式：TCP 上的 UTF-8 文本行协议，一行一条消息，以 '\n' 分隔。
//           （服务端解析时会自动剥除可能存在的 '\r'，因此 telnet 也可手工调试）
//
// 设计取舍：
//   * 纯文本、可读、可用 nc/telnet 手工测试，方便排错；
//   * 命令全部大写，参数以空格分隔；聊天正文取 "CHAT " 之后的全部内容；
//   * 不做二进制封包，不追求极致吞吐（本项目关注 Reactor 结构本身）。
// ============================================================================
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace gomoku {

// ---------------------------- 棋盘尺寸 ----------------------------
// 标准五子棋棋盘为 15x15，坐标范围 1..15（x 为列，y 为行）。
constexpr int kBoardSize = 15;
constexpr int kCellCount = kBoardSize * kBoardSize;  // 225

// ---------------------------- 协议上限 ----------------------------
// 单行最大字节数：超过视为恶意/异常输入，服务端会直接断开连接。
constexpr std::size_t kMaxLineBytes = 1024;
// 聊天正文最大字节数。
constexpr std::size_t kMaxChatBytes = 512;
// 昵称：2~16 字节，[A-Za-z0-9_]。
constexpr std::size_t kMinNickLen = 2;
constexpr std::size_t kMaxNickLen = 16;
// 密码：4~32 字节，可打印 ASCII、不含空白（因为协议以空格分词）。
constexpr std::size_t kMinPassLen = 4;
constexpr std::size_t kMaxPassLen = 32;
// 房间号：1~16 字节，[0-9A-Za-z]。
constexpr std::size_t kMaxRoomIdLen = 16;
// 单个房间成员上限（含观战者），防止单房间异常膨胀。
constexpr std::size_t kMaxRoomMembers = 64;

// ------------------- 客户端 -> 服务端（命令） -------------------
constexpr const char* kCmdRegister = "REGISTER";  // REGISTER <nick> <pass>
constexpr const char* kCmdLogin    = "LOGIN";     // LOGIN <nick> <pass>
constexpr const char* kCmdJoin     = "JOIN";      // JOIN <roomId>
constexpr const char* kCmdLeave    = "LEAVE";     // LEAVE
constexpr const char* kCmdSit      = "SIT";       // SIT
constexpr const char* kCmdStand    = "STAND";     // STAND
constexpr const char* kCmdReady    = "READY";     // READY
constexpr const char* kCmdMove     = "MOVE";      // MOVE <x> <y>   x=列 1..15, y=行 1..15
constexpr const char* kCmdChat     = "CHAT";      // CHAT <text...>
constexpr const char* kCmdBoard    = "BOARD";     // BOARD
constexpr const char* kCmdWho      = "WHO";       // WHO
constexpr const char* kCmdRooms    = "ROOMS";     // ROOMS
constexpr const char* kCmdStats    = "STATS";     // STATS
constexpr const char* kCmdPing     = "PING";      // PING
constexpr const char* kCmdQuit     = "QUIT";      // QUIT

// ------------------- 服务端 -> 客户端（事件） -------------------
// 通用应答
constexpr const char* kEvOk         = "OK";        // OK <msg>
constexpr const char* kEvErr        = "ERR";       // ERR <CODE> <msg>
// 认证
constexpr const char* kEvAuth       = "AUTH";      // AUTH <nick> <games> <wins> <rate>
// 房间
constexpr const char* kEvRoom       = "ROOM";      // ROOM <roomId> <state> <black> <white> <you>
constexpr const char* kEvUsers      = "USERS";     // USERS <n1,n2,...>
constexpr const char* kEvRooms      = "ROOMS";     // ROOMS <room1|room2|...> 或 ROOMS -
constexpr const char* kEvJoined     = "JOINED";    // JOINED <nick>
constexpr const char* kEvLeft       = "LEFT";      // LEFT <nick>
constexpr const char* kEvSeats      = "SEATS";     // SEATS <black> <white> <state>
// 对局
constexpr const char* kEvStart      = "START";     // START <black> <white>
constexpr const char* kEvTurn       = "TURN";      // TURN <nick>
constexpr const char* kEvPlaced     = "PLACED";    // PLACED <x> <y> <nick> <B|W>
constexpr const char* kEvBoard      = "BOARD";     // BOARD <225 chars> <lastX> <lastY>
constexpr const char* kEvOver       = "OVER";      // OVER <WON|DRAW> <winner> <reason>
constexpr const char* kEvInfo       = "INFO";      // INFO <games> <wins> <rate>
// 聊天 / 系统
constexpr const char* kEvChat       = "CHAT";      // CHAT <nick> <text...>
constexpr const char* kEvSys        = "SYS";       // SYS <text...>
constexpr const char* kEvPong       = "PONG";      // PONG

// ---------------------------- 房间/对局状态 ----------------------------
// 与 ROOM/SEATS 消息中的 <state> 字段一一对应。
constexpr const char* kStateIdle      = "IDLE";       // 空闲：可以上座
constexpr const char* kStateWaitReady = "WAITREADY";  // 上一局结束，等双方 READY
constexpr const char* kStatePlaying   = "PLAYING";    // 对局进行中

// ---------------------------- 结束原因 ----------------------------
constexpr const char* kReasonFive        = "FIVE";          // 五连
constexpr const char* kReasonFull        = "FULL";          // 满盘和棋
constexpr const char* kReasonForfeitStand= "FORFEIT_STAND"; // 起身认输
constexpr const char* kReasonForfeitLeft = "FORFEIT_LEFT";  // 断线判负

// ---------------------------- 错误码 ----------------------------
constexpr const char* kErrNotLogin     = "NOT_LOGIN";
constexpr const char* kErrBadCmd       = "BAD_CMD";
constexpr const char* kErrBadArg       = "BAD_ARG";
constexpr const char* kErrNameTaken    = "NAME_TAKEN";
constexpr const char* kErrNoSuchUser   = "NO_SUCH_USER";
constexpr const char* kErrBadPassword  = "BAD_PASSWORD";
constexpr const char* kErrAlreadyOnline= "ALREADY_ONLINE";
constexpr const char* kErrAlreadyAuthed= "ALREADY_AUTHED";
constexpr const char* kErrNotInRoom    = "NOT_IN_ROOM";
constexpr const char* kErrAlreadyInRoom= "ALREADY_IN_ROOM";
constexpr const char* kErrGameRunning  = "GAME_RUNNING";
constexpr const char* kErrSeatFull     = "SEAT_FULL";
constexpr const char* kErrNotSeated    = "NOT_SEATED";
constexpr const char* kErrNotYourTurn  = "NOT_YOUR_TURN";
constexpr const char* kErrIllegalMove  = "ILLEGAL_MOVE";
constexpr const char* kErrChatLocked   = "CHAT_LOCKED";
constexpr const char* kErrDbUnavailable= "DB_UNAVAILABLE";
constexpr const char* kErrLineTooLong  = "LINE_TOO_LONG";
constexpr const char* kErrRoomFull     = "ROOM_FULL";
constexpr const char* kErrNotReady     = "NOT_READY";

// ---------------------------- 工具函数 ----------------------------

// 去掉首尾空白（空格、\t、\r、\n）。
std::string trim(const std::string& s);

// 按空白字符切分，忽略连续空白。例如 "MOVE  8 9" -> {"MOVE","8","9"}。
std::vector<std::string> splitWhitespace(const std::string& s);

// 是否以 prefix 开头。
bool startsWith(const std::string& s, const char* prefix);

// 忽略大小写比较（命令与错误码统一按大写发送，这里容忍小写输入）。
bool equalsIgnoreCase(const std::string& s, const char* other);

// 昵称合法性：[A-Za-z0-9_] 组成，长度 2~16，首字符必须是字母或数字。
bool isValidNickname(const std::string& nick);

// 密码合法性：可打印 ASCII(33~126)，长度 4~32，不含空白。
bool isValidPassword(const std::string& pwd);

// 房间号合法性：[0-9A-Za-z] 组成，长度 1~16。
bool isValidRoomId(const std::string& roomId);

// 胜率格式化：games 为 0 时返回 "0.0000"，否则返回 4 位小数。
std::string formatWinRate(int wins, int games);

// 解析两个 1..15 的坐标；成功返回 true。
bool parseCoordinates(const std::string& sx, const std::string& sy, int* x, int* y);

}  // namespace gomoku
