CREATE TABLE direct_messages(id TEXT PRIMARY KEY,workspace_id TEXT NOT NULL,from_user TEXT NOT NULL,to_user TEXT NOT NULL,body TEXT NOT NULL,created_at TEXT NOT NULL);
CREATE INDEX direct_messages_to ON direct_messages(workspace_id,to_user,from_user);
CREATE INDEX direct_messages_from ON direct_messages(workspace_id,from_user,to_user);
CREATE TABLE direct_reads(workspace_id TEXT NOT NULL,user_id TEXT NOT NULL,peer_id TEXT NOT NULL,last_row INTEGER NOT NULL DEFAULT 0,PRIMARY KEY(workspace_id,user_id,peer_id));
