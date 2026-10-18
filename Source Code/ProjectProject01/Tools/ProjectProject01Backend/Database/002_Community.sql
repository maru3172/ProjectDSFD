USE projectproject01;
CREATE TABLE IF NOT EXISTS auth_session_liveness(
 session_id BIGINT UNSIGNED NOT NULL PRIMARY KEY,deadline_utc DATETIME(6) NOT NULL,
 KEY ix_session_deadline(deadline_utc),
 FOREIGN KEY(session_id) REFERENCES auth_sessions(id) ON DELETE CASCADE
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS social_conversation_state(
 owner_id BIGINT UNSIGNED NOT NULL,target_id BIGINT UNSIGNED NOT NULL,
 deleted_through_id BIGINT UNSIGNED NOT NULL DEFAULT 0,read_through_id BIGINT UNSIGNED NOT NULL DEFAULT 0,
 PRIMARY KEY(owner_id,target_id),
 FOREIGN KEY(owner_id) REFERENCES users(id) ON DELETE CASCADE,
 FOREIGN KEY(target_id) REFERENCES users(id) ON DELETE CASCADE
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS social_profiles(
 user_id BIGINT UNSIGNED NOT NULL PRIMARY KEY,
 status_message VARCHAR(160) NOT NULL DEFAULT '',bio VARCHAR(500) NOT NULL DEFAULT '',
 accent VARCHAR(16) NOT NULL DEFAULT 'Slate',title VARCHAR(24) NOT NULL DEFAULT 'Rookie',
 updated_at_utc DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
 status_updated_at_utc DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
 FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS social_room_invites(
 id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL PRIMARY KEY,
 room_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 sender_id BIGINT UNSIGNED NOT NULL,recipient_id BIGINT UNSIGNED NOT NULL,
 expires_at_utc DATETIME(6) NOT NULL,created_at_utc DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
 UNIQUE KEY uq_invite_room_user(room_id,recipient_id),KEY ix_invite_recipient(recipient_id,expires_at_utc),
 FOREIGN KEY(room_id) REFERENCES game_rooms(id) ON DELETE CASCADE,
 FOREIGN KEY(sender_id) REFERENCES users(id) ON DELETE CASCADE,
 FOREIGN KEY(recipient_id) REFERENCES users(id) ON DELETE CASCADE
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS social_friends(
 user_low BIGINT UNSIGNED NOT NULL, user_high BIGINT UNSIGNED NOT NULL,
 PRIMARY KEY(user_low,user_high), KEY ix_friend_high(user_high),
 FOREIGN KEY(user_low) REFERENCES users(id) ON DELETE CASCADE,
 FOREIGN KEY(user_high) REFERENCES users(id) ON DELETE CASCADE,
 CHECK(user_low<user_high)
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS social_requests(
 sender_id BIGINT UNSIGNED NOT NULL,recipient_id BIGINT UNSIGNED NOT NULL,
 created_at_utc DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
 PRIMARY KEY(sender_id,recipient_id),KEY ix_requests_recipient(recipient_id),
 FOREIGN KEY(sender_id) REFERENCES users(id) ON DELETE CASCADE,
 FOREIGN KEY(recipient_id) REFERENCES users(id) ON DELETE CASCADE,CHECK(sender_id<>recipient_id)
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS social_blocks(
 owner_id BIGINT UNSIGNED NOT NULL,target_id BIGINT UNSIGNED NOT NULL,
 PRIMARY KEY(owner_id,target_id), KEY ix_block_target(target_id),
 FOREIGN KEY(owner_id) REFERENCES users(id) ON DELETE CASCADE,
 FOREIGN KEY(target_id) REFERENCES users(id) ON DELETE CASCADE,CHECK(owner_id<>target_id)
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS social_presence(
 user_id BIGINT UNSIGNED NOT NULL PRIMARY KEY,presence VARCHAR(16) NOT NULL DEFAULT 'Online',
 context VARCHAR(16) NOT NULL DEFAULT 'Other',last_seen_utc DATETIME(6) NOT NULL,
 FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS social_whispers(
 id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
 sender_id BIGINT UNSIGNED NOT NULL,recipient_id BIGINT UNSIGNED NOT NULL,body VARCHAR(500) NOT NULL,
 created_at_utc DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
 KEY ix_whisper_recipient(recipient_id,id),KEY ix_whisper_sender(sender_id,created_at_utc),
 FOREIGN KEY(sender_id) REFERENCES users(id) ON DELETE CASCADE,
 FOREIGN KEY(recipient_id) REFERENCES users(id) ON DELETE CASCADE
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS social_notifications(
 id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,user_id BIGINT UNSIGNED NOT NULL,
 kind VARCHAR(24) NOT NULL,body VARCHAR(250) NOT NULL,unread BOOLEAN NOT NULL DEFAULT TRUE,
 created_at_utc DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),KEY ix_notification_user(user_id,id),
 FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS launcher_posts(
 id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,kind VARCHAR(16) NOT NULL,
 title VARCHAR(120) NOT NULL,body TEXT NOT NULL,published BOOLEAN NOT NULL DEFAULT TRUE,
 preview_image_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NULL,
 updated_at_utc DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6)
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS support_inquiries(
 id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
 receipt_hash CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL UNIQUE,
 subject VARCHAR(120) NOT NULL,body TEXT NOT NULL,reply TEXT NULL,status VARCHAR(16) NOT NULL DEFAULT 'Open',
 created_at_utc DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6)
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS launcher_images(
 id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL PRIMARY KEY,
 content_type VARCHAR(24) NOT NULL,data MEDIUMBLOB NOT NULL,
 created_at_utc DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6)
) ENGINE=InnoDB;
-- Keep submitted context even when a room disappears. No credentials or client-supplied reporter identity.
CREATE TABLE IF NOT EXISTS player_reports(
 id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
 reporter_id BIGINT UNSIGNED NOT NULL,target_id BIGINT UNSIGNED NOT NULL,
 reporter_account VARCHAR(32) NOT NULL,reporter_name VARCHAR(64) NOT NULL,
 target_account VARCHAR(32) NOT NULL,target_name VARCHAR(64) NOT NULL,
 room_id CHAR(36) NOT NULL,room_name VARCHAR(120) NOT NULL,
 reason VARCHAR(24) NOT NULL,details TEXT NOT NULL,status VARCHAR(16) NOT NULL DEFAULT 'Open',
 review_note TEXT NULL,created_at_utc DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),reviewed_at_utc DATETIME(6) NULL,
 INDEX ix_reporter_time(reporter_id,created_at_utc),INDEX ix_report_target(reporter_id,target_id,created_at_utc),
 INDEX ix_report_status(status,id)
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS random_match_searches(
 search_id VARCHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL PRIMARY KEY,
 user_id BIGINT UNSIGNED NOT NULL,state VARCHAR(16) NOT NULL DEFAULT 'Waiting',room_id VARCHAR(36) NULL,
 created_at_utc DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),last_seen_utc DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
 INDEX ix_match_wait(state,created_at_utc),INDEX ix_match_user(user_id,state),
 FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS random_match_rooms(
 room_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL PRIMARY KEY,
 FOREIGN KEY(room_id) REFERENCES game_rooms(id) ON DELETE CASCADE
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS random_match_parties(
 room_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL PRIMARY KEY,
 search_id VARCHAR(36) CHARACTER SET ascii COLLATE ascii_bin NULL,
 FOREIGN KEY(room_id) REFERENCES game_rooms(id) ON DELETE CASCADE
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS random_match_search_members(
 search_id VARCHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,user_id BIGINT UNSIGNED NOT NULL,
 PRIMARY KEY(search_id,user_id),INDEX ix_search_member(user_id),
 FOREIGN KEY(search_id) REFERENCES random_match_searches(search_id) ON DELETE CASCADE,
 FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS random_match_origins(
 room_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,user_id BIGINT UNSIGNED NOT NULL,
 party_key VARCHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,original_host BIGINT UNSIGNED NOT NULL,
 PRIMARY KEY(room_id,user_id),FOREIGN KEY(room_id) REFERENCES game_rooms(id) ON DELETE CASCADE,
 FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE
) ENGINE=InnoDB;
