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
