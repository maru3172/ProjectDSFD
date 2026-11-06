CREATE TABLE ue5_revisions (
  workspace_id TEXT NOT NULL REFERENCES workspaces(id) ON DELETE CASCADE,
  item_id TEXT NOT NULL,
  revision INTEGER NOT NULL,
  author TEXT NOT NULL,
  saved_at TEXT NOT NULL,
  payload TEXT NOT NULL,
  PRIMARY KEY (workspace_id, item_id, revision)
);
