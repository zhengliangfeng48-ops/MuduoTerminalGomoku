-- ============================================================================
-- Gomoku 服务端数据库初始化脚本
--
-- 用法：
--   sudo mysql < schema.sql
-- 或在 MySQL 客户端里：  source /forDsh/Gomoku/sql/schema.sql;
--
-- 说明：
--   * 密码按需求先明文存储，生产环境应改为哈希（如 bcrypt/argon2）；
--   * win_rate 用 DECIMAL(5,4) 存储，范围 0.0000 ~ 1.0000，避免浮点误差；
--   * nickname 上的 UNIQUE 索引是“昵称不能重复”的最终保证。
-- ============================================================================

-- 1. 数据库
CREATE DATABASE IF NOT EXISTS gomoku DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_general_ci;

-- 2. 专用账号（服务端用它连接；密码请自行修改，并同步到启动参数 --db-pass）
CREATE USER IF NOT EXISTS 'gomoku'@'localhost' IDENTIFIED BY 'gomoku123';
CREATE USER IF NOT EXISTS 'gomoku'@'127.0.0.1' IDENTIFIED BY 'gomoku123';
GRANT ALL PRIVILEGES ON gomoku.* TO 'gomoku'@'localhost';
GRANT ALL PRIVILEGES ON gomoku.* TO 'gomoku'@'127.0.0.1';
FLUSH PRIVILEGES;

USE gomoku;

-- 3. 用户表：昵称（唯一）、密码（明文）、场次、胜场、胜率
CREATE TABLE IF NOT EXISTS users (
  id            BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  nickname      VARCHAR(32)     NOT NULL COMMENT '昵称，全局唯一',
  password      VARCHAR(64)     NOT NULL COMMENT '密码（当前为明文，按需求暂不哈希）',
  games         INT UNSIGNED    NOT NULL DEFAULT 0 COMMENT '总场次',
  wins          INT UNSIGNED    NOT NULL DEFAULT 0 COMMENT '胜场数',
  win_rate      DECIMAL(5,4)    NOT NULL DEFAULT 0 COMMENT '胜率 wins/games，0.0000~1.0000',
  created_at    TIMESTAMP       NOT NULL DEFAULT CURRENT_TIMESTAMP,
  last_login_at TIMESTAMP       NULL DEFAULT NULL,
  PRIMARY KEY (id),
  UNIQUE KEY uk_nickname (nickname)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='用户账号与战绩';

-- 4. 对局历史：便于核对战绩、复盘与统计
CREATE TABLE IF NOT EXISTS match_history (
  id         BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  room_id    VARCHAR(16)     NOT NULL COMMENT '房间号',
  black_nick VARCHAR(32)     NOT NULL COMMENT '黑方昵称（快照）',
  white_nick VARCHAR(32)     NOT NULL COMMENT '白方昵称（快照）',
  result     ENUM('black','white','draw') NOT NULL COMMENT '结果',
  reason     VARCHAR(24)     NOT NULL COMMENT 'FIVE/FULL/FORFEIT_STAND/FORFEIT_LEFT',
  moves      SMALLINT UNSIGNED NOT NULL COMMENT '总手数',
  ended_at   TIMESTAMP       NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (id),
  KEY idx_black (black_nick),
  KEY idx_white (white_nick),
  KEY idx_ended (ended_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='对局历史';

-- 5. 自检
SELECT 'database gomoku ready' AS status;
SHOW TABLES;
