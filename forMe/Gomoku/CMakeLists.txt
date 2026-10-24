# ============================================================================
# Gomoku：基于内置 muduo 的终端五子棋 CS 系统
#
# 目标：
#   gomoku_server      服务端（游戏协议 + 压测 HTTP 端点 + MySQL 战绩）
#   gomoku_client      终端客户端
#   gomoku_board_test  棋盘/协议单元测试
# ============================================================================
cmake_minimum_required(VERSION 3.10)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# MySQL C API
#   优先 pkg-config（不同发行版路径不一样），失败则回退到 find_path/find_library。
# ---------------------------------------------------------------------------
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(MYSQLCLIENT QUIET mysqlclient)
endif()

if(MYSQLCLIENT_FOUND)
  message(STATUS "Gomoku: MySQL 通过 pkg-config 找到 (mysqlclient ${MYSQLCLIENT_VERSION})")
  set(GOMOKU_MYSQL_INCLUDE_DIRS ${MYSQLCLIENT_INCLUDE_DIRS})
  set(GOMOKU_MYSQL_LIBRARIES ${MYSQLCLIENT_LIBRARIES})
else()
  find_path(MYSQL_INCLUDE_DIR NAMES mysql/mysql.h)
  find_library(MYSQL_LIBRARY NAMES mysqlclient)
  if(NOT MYSQL_INCLUDE_DIR OR NOT MYSQL_LIBRARY)
    message(FATAL_ERROR "找不到 MySQL 客户端库，请安装：apt install libmysqlclient-dev")
  endif()
  message(STATUS "Gomoku: MySQL 头文件 ${MYSQL_INCLUDE_DIR}，库 ${MYSQL_LIBRARY}")
  set(GOMOKU_MYSQL_INCLUDE_DIRS ${MYSQL_INCLUDE_DIR})
  set(GOMOKU_MYSQL_LIBRARIES ${MYSQL_LIBRARY})
endif()

# ---------------------------------------------------------------------------
# gomoku_common：协议常量与工具（服务端/客户端共用）
# ---------------------------------------------------------------------------
add_library(gomoku_common STATIC common/Protocol.cc)
target_include_directories(gomoku_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(gomoku_common PRIVATE -Wall -Wextra -Wno-unused-parameter -g)

# ---------------------------------------------------------------------------
# 服务端
# ---------------------------------------------------------------------------
set(SERVER_SRCS
  server/main.cc
  server/Config.cc
  server/Db.cc
  server/AuthService.cc
  server/GomokuGame.cc
  server/Session.cc
  server/Room.cc
  server/RoomManager.cc
  server/ServerApp.cc
  server/HttpStatusServer.cc
  )

add_executable(gomoku_server ${SERVER_SRCS})
target_include_directories(gomoku_server PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${GOMOKU_MYSQL_INCLUDE_DIRS})
target_compile_options(gomoku_server PRIVATE -Wall -Wextra -Wno-unused-parameter -g)
# muduo_net 会传递 muduo_base / pthread / muduo 头文件路径
target_link_libraries(gomoku_server PRIVATE muduo_net gomoku_common ${GOMOKU_MYSQL_LIBRARIES})

# ---------------------------------------------------------------------------
# 终端客户端
# ---------------------------------------------------------------------------
set(CLIENT_SRCS
  client/main.cc
  client/GameClient.cc
  client/TerminalUI.cc
  client/StdinLoop.cc
  )

add_executable(gomoku_client ${CLIENT_SRCS})
target_include_directories(gomoku_client PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(gomoku_client PRIVATE -Wall -Wextra -Wno-unused-parameter -g)
target_link_libraries(gomoku_client PRIVATE muduo_net gomoku_common)

# ---------------------------------------------------------------------------
# 单元测试（棋盘规则 + 协议工具），不依赖网络与数据库
# ---------------------------------------------------------------------------
add_executable(gomoku_board_test tests/board_test.cc server/GomokuGame.cc)
target_include_directories(gomoku_board_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(gomoku_board_test PRIVATE -Wall -Wextra -Wno-unused-parameter -g)
target_link_libraries(gomoku_board_test PRIVATE gomoku_common)
