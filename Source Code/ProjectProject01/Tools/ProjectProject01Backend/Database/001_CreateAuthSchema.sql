-- File: Tools/ProjectProject01Backend/Database/001_CreateAuthSchema.sql
-- Target: MySQL Server 8.0

CREATE DATABASE IF NOT EXISTS projectproject01
  CHARACTER SET utf8mb4
  COLLATE utf8mb4_0900_ai_ci;

USE projectproject01;

CREATE TABLE IF NOT EXISTS users
(
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  account_id VARCHAR(32) NOT NULL,
  display_name VARCHAR(32) NOT NULL,
  password_hash VARCHAR(512) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  failed_login_count INT UNSIGNED NOT NULL DEFAULT 0,
  locked_until_utc DATETIME(6) NULL,
  created_at_utc DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at_utc DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (id),
  UNIQUE KEY uq_users_account_id (account_id)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS auth_sessions
(
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  user_id BIGINT UNSIGNED NOT NULL,
  access_token_hash BINARY(32) NOT NULL,
  refresh_token_hash BINARY(32) NOT NULL,
  access_expires_at_utc DATETIME(6) NOT NULL,
  refresh_expires_at_utc DATETIME(6) NOT NULL,
  created_at_utc DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  revoked_at_utc DATETIME(6) NULL,
  PRIMARY KEY (id),
  UNIQUE KEY uq_auth_sessions_access_token_hash (access_token_hash),
  UNIQUE KEY uq_auth_sessions_refresh_token_hash (refresh_token_hash),
  KEY ix_auth_sessions_user_id_refresh_expiry (user_id, refresh_expires_at_utc),
  CONSTRAINT fk_auth_sessions_user
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS lobby_state
(
  id TINYINT UNSIGNED NOT NULL,
  max_rooms INT UNSIGNED NOT NULL DEFAULT 30,
  PRIMARY KEY (id)
) ENGINE=InnoDB;

INSERT IGNORE INTO lobby_state (id, max_rooms) VALUES (1, 30);

CREATE TABLE IF NOT EXISTS game_rooms
(
  id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  name VARCHAR(48) NOT NULL,
  host_user_id BIGINT UNSIGNED NOT NULL,
  password_hash VARCHAR(512) CHARACTER SET ascii COLLATE ascii_bin NULL,
  is_public BOOLEAN NOT NULL DEFAULT TRUE,
  status VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT 'Waiting',
  travel_url VARCHAR(512) NULL,
  created_at_utc DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  started_at_utc DATETIME(6) NULL,
  PRIMARY KEY (id),
  KEY ix_game_rooms_status_created (status, created_at_utc),
  CONSTRAINT fk_game_rooms_host
    FOREIGN KEY (host_user_id) REFERENCES users(id) ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS room_members
(
  room_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  user_id BIGINT UNSIGNED NOT NULL,
  is_ready BOOLEAN NOT NULL DEFAULT FALSE,
  assigned_role VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NULL,
  joined_at_utc DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  last_seen_at_utc DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (room_id, user_id),
  UNIQUE KEY uq_room_members_user (user_id),
  KEY ix_room_members_room_joined (room_id, joined_at_utc),
  CONSTRAINT fk_room_members_room
    FOREIGN KEY (room_id) REFERENCES game_rooms(id) ON DELETE CASCADE,
  CONSTRAINT fk_room_members_user
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS room_chat_messages
(
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  room_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  user_id BIGINT UNSIGNED NOT NULL,
  message VARCHAR(300) NOT NULL,
  created_at_utc DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (id),
  KEY ix_room_chat_room_id (room_id, id),
  CONSTRAINT fk_room_chat_room
    FOREIGN KEY (room_id) REFERENCES game_rooms(id) ON DELETE CASCADE,
  CONSTRAINT fk_room_chat_user
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
) ENGINE=InnoDB;
