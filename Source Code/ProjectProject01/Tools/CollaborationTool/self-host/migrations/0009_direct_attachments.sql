CREATE TABLE direct_attachments(id TEXT PRIMARY KEY,workspace_id TEXT NOT NULL,from_user TEXT NOT NULL,to_user TEXT NOT NULL,name TEXT NOT NULL,size INTEGER NOT NULL,key TEXT NOT NULL,content_type TEXT NOT NULL,created_at TEXT NOT NULL);
ALTER TABLE direct_messages ADD COLUMN attachment_id TEXT;
