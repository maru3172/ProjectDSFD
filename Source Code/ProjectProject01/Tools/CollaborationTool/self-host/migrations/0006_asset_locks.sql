CREATE TABLE asset_locks(id TEXT PRIMARY KEY,workspace_id TEXT NOT NULL,path TEXT NOT NULL,user_id TEXT NOT NULL,reason TEXT NOT NULL DEFAULT '',entry_id TEXT,created_at TEXT NOT NULL,expires_at TEXT NOT NULL,released_at TEXT,released_by TEXT);
CREATE INDEX asset_locks_open ON asset_locks(workspace_id,released_at,expires_at);
