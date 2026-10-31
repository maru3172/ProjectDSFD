ALTER TABLE comments ADD COLUMN mentions_json TEXT NOT NULL DEFAULT '[]';
ALTER TABLE notifications ADD COLUMN comment_id TEXT;
CREATE INDEX attachment_previous ON attachments(previous_id);
