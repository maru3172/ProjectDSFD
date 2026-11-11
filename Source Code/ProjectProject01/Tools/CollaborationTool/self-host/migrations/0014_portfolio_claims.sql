CREATE TABLE portfolio_claims (
 workspace_id TEXT NOT NULL REFERENCES workspaces(id) ON DELETE CASCADE,
 id TEXT NOT NULL,
 project_id TEXT NOT NULL,
 week TEXT NOT NULL,
 revision INTEGER NOT NULL,
 payload TEXT NOT NULL,
 PRIMARY KEY(workspace_id,id)
);
CREATE INDEX portfolio_claims_week ON portfolio_claims(workspace_id,project_id,week);
