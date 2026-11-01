CREATE TABLE purged_records(workspace_id TEXT NOT NULL,table_name TEXT NOT NULL,row_id TEXT NOT NULL,purged_at TEXT NOT NULL,PRIMARY KEY(workspace_id,table_name,row_id));
