CREATE TABLE vault_baselines(workspace_id TEXT NOT NULL,file TEXT NOT NULL,hash TEXT NOT NULL,PRIMARY KEY(workspace_id,file));
CREATE INDEX entries_page ON entries(workspace_id,updated_at DESC,id DESC);
CREATE INDEX comments_page ON comments(workspace_id,entry_id,created_at,id);
CREATE INDEX activity_page ON activity(workspace_id,entry_id,created_at,id);
CREATE TABLE file_operations(id TEXT PRIMARY KEY,attachment_id TEXT NOT NULL,actor_id TEXT NOT NULL,action TEXT NOT NULL,created_at TEXT NOT NULL);
