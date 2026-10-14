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
  seen_survivor_guide BOOLEAN NOT NULL DEFAULT FALSE,
  seen_mannequin_guide BOOLEAN NOT NULL DEFAULT FALSE,
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

CREATE TABLE IF NOT EXISTS service_control
(
  id TINYINT UNSIGNED NOT NULL,
  maintenance_enabled BOOLEAN NOT NULL DEFAULT FALSE,
  announcement VARCHAR(512) NOT NULL DEFAULT '',
  maintenance_start_at_utc DATETIME(6) NULL,
  shutdown_at_utc DATETIME(6) NULL,
  revision BIGINT UNSIGNED NOT NULL DEFAULT 1,
  updated_at_utc DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (id)
) ENGINE=InnoDB;

INSERT IGNORE INTO service_control
  (id, maintenance_enabled, announcement, shutdown_at_utc, revision)
VALUES (1, FALSE, '', NULL, 1);

CREATE TABLE IF NOT EXISTS maintenance_schedules
(
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  starts_at_utc DATETIME(6) NOT NULL,
  ends_at_utc DATETIME(6) NOT NULL,
  announcement VARCHAR(512) NOT NULL DEFAULT '',
  created_at_utc DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (id),
  KEY ix_maintenance_schedules_window (starts_at_utc, ends_at_utc),
  CONSTRAINT ck_maintenance_schedules_window CHECK (ends_at_utc > starts_at_utc)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS game_rooms
(
  id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  join_code CHAR(8) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  name VARCHAR(48) NOT NULL,
  host_user_id BIGINT UNSIGNED NOT NULL,
  password_hash VARCHAR(512) CHARACTER SET ascii COLLATE ascii_bin NULL,
  is_public BOOLEAN NOT NULL DEFAULT TRUE,
  status VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT 'Waiting',
  travel_url VARCHAR(512) NULL,
  match_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NULL,
  created_at_utc DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  started_at_utc DATETIME(6) NULL,
  PRIMARY KEY (id),
  UNIQUE KEY uq_game_rooms_join_code (join_code),
  KEY ix_game_rooms_status_created (status, created_at_utc),
  CONSTRAINT fk_game_rooms_host
    FOREIGN KEY (host_user_id) REFERENCES users(id) ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS game_join_tickets
(
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  ticket_hash BINARY(32) NOT NULL,
  room_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  match_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  user_id BIGINT UNSIGNED NOT NULL,
  role VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  expires_at_utc DATETIME(6) NOT NULL,
  consumed_at_utc DATETIME(6) NULL,
  created_at_utc DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (id),
  UNIQUE KEY uq_game_join_tickets_hash (ticket_hash),
  KEY ix_game_join_tickets_user_expiry (user_id, expires_at_utc),
  CONSTRAINT fk_game_join_tickets_room
    FOREIGN KEY (room_id) REFERENCES game_rooms(id) ON DELETE CASCADE,
  CONSTRAINT fk_game_join_tickets_user
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS verified_match_results
(
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  match_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  room_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  user_id BIGINT UNSIGNED NOT NULL,
  role VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  success BOOLEAN NOT NULL,
  capture_count INT UNSIGNED NULL,
  first_capture_seconds DECIMAL(12,3) NULL,
  all_captured_seconds DECIMAL(12,3) NULL,
  rescue_count INT UNSIGNED NULL,
  escape_seconds DECIMAL(12,3) NULL,
  verified_at_utc DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  published_at_utc DATETIME(6) NULL,
  PRIMARY KEY (id),
  UNIQUE KEY uq_verified_match_user (match_id, user_id),
  KEY ix_verified_match_room (room_id),
  CONSTRAINT fk_verified_match_user
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS security_audit_events
(
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  user_id BIGINT UNSIGNED NULL,
  event_type VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  outcome VARCHAR(32) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  remote_address VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NULL,
  details VARCHAR(512) NULL,
  created_at_utc DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (id),
  KEY ix_security_audit_created (created_at_utc),
  KEY ix_security_audit_user_created (user_id, created_at_utc),
  CONSTRAINT fk_security_audit_user
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE SET NULL
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS room_members
(
  room_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  user_id BIGINT UNSIGNED NOT NULL,
  is_ready BOOLEAN NOT NULL DEFAULT FALSE,
  assigned_role VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NULL,
  returned_to_room BOOLEAN NOT NULL DEFAULT FALSE,
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

CREATE TABLE IF NOT EXISTS leaderboard_records
(
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  match_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  user_id BIGINT UNSIGNED NOT NULL,
  room_name VARCHAR(48) NOT NULL DEFAULT '알 수 없는 방',
  role VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  capture_count INT UNSIGNED NULL,
  first_capture_seconds DECIMAL(12,3) NULL,
  all_captured_seconds DECIMAL(12,3) NULL,
  rescue_count INT UNSIGNED NULL,
  escape_seconds DECIMAL(12,3) NULL,
  created_at_utc DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (id),
  UNIQUE KEY uq_leaderboard_match_user (match_id, user_id),
  KEY ix_leaderboard_role_capture (role, capture_count, all_captured_seconds),
  KEY ix_leaderboard_role_first_capture (role, first_capture_seconds),
  KEY ix_leaderboard_role_all_captured (role, all_captured_seconds),
  KEY ix_leaderboard_role_rescue (role, rescue_count, escape_seconds),
  KEY ix_leaderboard_role_escape (role, escape_seconds),
  CONSTRAINT fk_leaderboard_user
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS leaderboard_survivor_team_records
(
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  match_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  room_name VARCHAR(48) NOT NULL,
  survivor_count INT UNSIGNED NOT NULL,
  average_rescue_count DECIMAL(12,3) NOT NULL,
  average_escape_seconds DECIMAL(12,3) NOT NULL,
  submitted_by_user_id BIGINT UNSIGNED NOT NULL,
  created_at_utc DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at_utc DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (id),
  UNIQUE KEY uq_survivor_team_match (match_id),
  KEY ix_survivor_team_rescue (average_rescue_count, average_escape_seconds),
  KEY ix_survivor_team_escape (average_escape_seconds, average_rescue_count),
  CONSTRAINT fk_survivor_team_submitter
    FOREIGN KEY (submitted_by_user_id) REFERENCES users(id) ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS crash_reports
(
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  report_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  detected_at_utc DATETIME(6) NOT NULL,
  started_at_utc DATETIME(6) NOT NULL,
  last_updated_at_utc DATETIME(6) NOT NULL,
  build_version VARCHAR(128) NOT NULL,
  map_name VARCHAR(128) NOT NULL,
  game_mode VARCHAR(128) NOT NULL,
  process_role VARCHAR(32) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  player_role VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NULL,
  last_log_path VARCHAR(256) NOT NULL,
  diagnostic_run_id VARCHAR(128) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  match_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NULL,
  created_at_utc DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (id),
  UNIQUE KEY uq_crash_reports_report_id (report_id),
  KEY ix_crash_reports_detected (detected_at_utc),
  KEY ix_crash_reports_role_detected (process_role, detected_at_utc),
  KEY ix_crash_reports_match (match_id)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS player_surveys
(
  id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  title VARCHAR(128) NOT NULL,
  description VARCHAR(512) NOT NULL DEFAULT '',
  questions_json JSON NOT NULL,
  is_active BOOLEAN NOT NULL DEFAULT FALSE,
  created_at_utc DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  activated_at_utc DATETIME(6) NULL,
  closed_at_utc DATETIME(6) NULL,
  PRIMARY KEY (id),
  KEY ix_player_surveys_active (is_active, activated_at_utc)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS player_survey_responses
(
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  response_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  survey_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  user_id BIGINT UNSIGNED NULL,
  submitted_at_utc DATETIME(6) NOT NULL,
  answers_json JSON NOT NULL,
  build_version VARCHAR(128) NOT NULL,
  map_name VARCHAR(128) NOT NULL,
  process_role VARCHAR(32) NOT NULL,
  player_role VARCHAR(32) NULL,
  created_at_utc DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (id),
  UNIQUE KEY uq_player_survey_response_id (response_id),
  UNIQUE KEY uq_player_survey_user (survey_id, user_id),
  KEY ix_player_survey_responses_submitted (survey_id, submitted_at_utc),
  CONSTRAINT fk_player_survey_response_survey
    FOREIGN KEY (survey_id) REFERENCES player_surveys(id) ON DELETE CASCADE,
  CONSTRAINT fk_player_survey_response_user
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE SET NULL
) ENGINE=InnoDB;
