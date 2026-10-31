CREATE TABLE `git_outbox` (
	`id` integer PRIMARY KEY AUTOINCREMENT NOT NULL,
	`workspace_id` text NOT NULL,
	`entity_type` text NOT NULL,
	`entity_id` text NOT NULL,
	`payload` text NOT NULL,
	`operation` text NOT NULL,
	`created_at` text NOT NULL,
	`synced_at` text,
	`error` text
);
--> statement-breakpoint
CREATE INDEX `git_pending` ON `git_outbox` (`workspace_id`,`synced_at`,`id`);--> statement-breakpoint
CREATE TABLE `git_settings` (
	`workspace_id` text PRIMARY KEY NOT NULL,
	`enabled` integer DEFAULT 0 NOT NULL,
	`lease_token` text,
	`lease_until` integer DEFAULT 0 NOT NULL,
	`last_commit` text,
	`last_sync` text,
	`last_error` text
);

--> statement-breakpoint
CREATE TRIGGER daybook_git_workspaces_insert AFTER INSERT ON "workspaces" BEGIN INSERT INTO git_outbox(workspace_id,entity_type,entity_id,payload,operation,created_at) VALUES(NEW."id", 'workspaces', NEW."id", json_object('id', NEW."id", 'name', NEW."name", 'owner_id', NEW."owner_id", 'created_at', NEW."created_at"), 'insert', strftime('%Y-%m-%dT%H:%M:%fZ','now')); END;

--> statement-breakpoint
CREATE TRIGGER daybook_git_workspaces_update AFTER UPDATE ON "workspaces" BEGIN INSERT INTO git_outbox(workspace_id,entity_type,entity_id,payload,operation,created_at) VALUES(NEW."id", 'workspaces', NEW."id", json_object('id', NEW."id", 'name', NEW."name", 'owner_id', NEW."owner_id", 'created_at', NEW."created_at"), 'update', strftime('%Y-%m-%dT%H:%M:%fZ','now')); END;

--> statement-breakpoint
CREATE TRIGGER daybook_git_workspaces_delete AFTER DELETE ON "workspaces" BEGIN INSERT INTO git_outbox(workspace_id,entity_type,entity_id,payload,operation,created_at) VALUES(OLD."id", 'workspaces', OLD."id", json_object('id', OLD."id", 'name', OLD."name", 'owner_id', OLD."owner_id", 'created_at', OLD."created_at"), 'delete', strftime('%Y-%m-%dT%H:%M:%fZ','now')); END;

--> statement-breakpoint
CREATE TRIGGER daybook_git_members_insert AFTER INSERT ON "members" BEGIN INSERT INTO git_outbox(workspace_id,entity_type,entity_id,payload,operation,created_at) VALUES(NEW."workspace_id", 'members', NEW."id", json_object('id', NEW."id", 'workspace_id', NEW."workspace_id", 'user_id', NEW."user_id", 'name', NEW."name", 'email', NEW."email", 'role', NEW."role", 'created_at', NEW."created_at"), 'insert', strftime('%Y-%m-%dT%H:%M:%fZ','now')); END;

--> statement-breakpoint
CREATE TRIGGER daybook_git_members_update AFTER UPDATE ON "members" BEGIN INSERT INTO git_outbox(workspace_id,entity_type,entity_id,payload,operation,created_at) VALUES(NEW."workspace_id", 'members', NEW."id", json_object('id', NEW."id", 'workspace_id', NEW."workspace_id", 'user_id', NEW."user_id", 'name', NEW."name", 'email', NEW."email", 'role', NEW."role", 'created_at', NEW."created_at"), 'update', strftime('%Y-%m-%dT%H:%M:%fZ','now')); END;

--> statement-breakpoint
CREATE TRIGGER daybook_git_members_delete AFTER DELETE ON "members" BEGIN INSERT INTO git_outbox(workspace_id,entity_type,entity_id,payload,operation,created_at) VALUES(OLD."workspace_id", 'members', OLD."id", json_object('id', OLD."id", 'workspace_id', OLD."workspace_id", 'user_id', OLD."user_id", 'name', OLD."name", 'email', OLD."email", 'role', OLD."role", 'created_at', OLD."created_at"), 'delete', strftime('%Y-%m-%dT%H:%M:%fZ','now')); END;

--> statement-breakpoint
CREATE TRIGGER daybook_git_projects_insert AFTER INSERT ON "projects" BEGIN INSERT INTO git_outbox(workspace_id,entity_type,entity_id,payload,operation,created_at) VALUES(NEW."workspace_id", 'projects', NEW."id", json_object('id', NEW."id", 'workspace_id', NEW."workspace_id", 'name', NEW."name", 'color', NEW."color", 'archived', NEW."archived", 'created_at', NEW."created_at"), 'insert', strftime('%Y-%m-%dT%H:%M:%fZ','now')); END;

--> statement-breakpoint
CREATE TRIGGER daybook_git_projects_update AFTER UPDATE ON "projects" BEGIN INSERT INTO git_outbox(workspace_id,entity_type,entity_id,payload,operation,created_at) VALUES(NEW."workspace_id", 'projects', NEW."id", json_object('id', NEW."id", 'workspace_id', NEW."workspace_id", 'name', NEW."name", 'color', NEW."color", 'archived', NEW."archived", 'created_at', NEW."created_at"), 'update', strftime('%Y-%m-%dT%H:%M:%fZ','now')); END;

--> statement-breakpoint
CREATE TRIGGER daybook_git_projects_delete AFTER DELETE ON "projects" BEGIN INSERT INTO git_outbox(workspace_id,entity_type,entity_id,payload,operation,created_at) VALUES(OLD."workspace_id", 'projects', OLD."id", json_object('id', OLD."id", 'workspace_id', OLD."workspace_id", 'name', OLD."name", 'color', OLD."color", 'archived', OLD."archived", 'created_at', OLD."created_at"), 'delete', strftime('%Y-%m-%dT%H:%M:%fZ','now')); END;

--> statement-breakpoint
CREATE TRIGGER daybook_git_topics_insert AFTER INSERT ON "topics" BEGIN INSERT INTO git_outbox(workspace_id,entity_type,entity_id,payload,operation,created_at) VALUES(NEW."workspace_id", 'topics', NEW."id", json_object('id', NEW."id", 'workspace_id', NEW."workspace_id", 'name', NEW."name", 'description', NEW."description", 'owner_id', NEW."owner_id", 'color', NEW."color", 'archived', NEW."archived", 'created_at', NEW."created_at"), 'insert', strftime('%Y-%m-%dT%H:%M:%fZ','now')); END;

--> statement-breakpoint
CREATE TRIGGER daybook_git_topics_update AFTER UPDATE ON "topics" BEGIN INSERT INTO git_outbox(workspace_id,entity_type,entity_id,payload,operation,created_at) VALUES(NEW."workspace_id", 'topics', NEW."id", json_object('id', NEW."id", 'workspace_id', NEW."workspace_id", 'name', NEW."name", 'description', NEW."description", 'owner_id', NEW."owner_id", 'color', NEW."color", 'archived', NEW."archived", 'created_at', NEW."created_at"), 'update', strftime('%Y-%m-%dT%H:%M:%fZ','now')); END;

--> statement-breakpoint
CREATE TRIGGER daybook_git_topics_delete AFTER DELETE ON "topics" BEGIN INSERT INTO git_outbox(workspace_id,entity_type,entity_id,payload,operation,created_at) VALUES(OLD."workspace_id", 'topics', OLD."id", json_object('id', OLD."id", 'workspace_id', OLD."workspace_id", 'name', OLD."name", 'description', OLD."description", 'owner_id', OLD."owner_id", 'color', OLD."color", 'archived', OLD."archived", 'created_at', OLD."created_at"), 'delete', strftime('%Y-%m-%dT%H:%M:%fZ','now')); END;

--> statement-breakpoint
CREATE TRIGGER daybook_git_record_types_insert AFTER INSERT ON "record_types" BEGIN INSERT INTO git_outbox(workspace_id,entity_type,entity_id,payload,operation,created_at) VALUES(NEW."workspace_id", 'record_types', NEW."id", json_object('id', NEW."id", 'workspace_id', NEW."workspace_id", 'name', NEW."name", 'description', NEW."description", 'owner_id', NEW."owner_id", 'color', NEW."color", 'archived', NEW."archived", 'created_at', NEW."created_at"), 'insert', strftime('%Y-%m-%dT%H:%M:%fZ','now')); END;

--> statement-breakpoint
CREATE TRIGGER daybook_git_record_types_update AFTER UPDATE ON "record_types" BEGIN INSERT INTO git_outbox(workspace_id,entity_type,entity_id,payload,operation,created_at) VALUES(NEW."workspace_id", 'record_types', NEW."id", json_object('id', NEW."id", 'workspace_id', NEW."workspace_id", 'name', NEW."name", 'description', NEW."description", 'owner_id', NEW."owner_id", 'color', NEW."color", 'archived', NEW."archived", 'created_at', NEW."created_at"), 'update', strftime('%Y-%m-%dT%H:%M:%fZ','now')); END;

--> statement-breakpoint
CREATE TRIGGER daybook_git_record_types_delete AFTER DELETE ON "record_types" BEGIN INSERT INTO git_outbox(workspace_id,entity_type,entity_id,payload,operation,created_at) VALUES(OLD."workspace_id", 'record_types', OLD."id", json_object('id', OLD."id", 'workspace_id', OLD."workspace_id", 'name', OLD."name", 'description', OLD."description", 'owner_id', OLD."owner_id", 'color', OLD."color", 'archived', OLD."archived", 'created_at', OLD."created_at"), 'delete', strftime('%Y-%m-%dT%H:%M:%fZ','now')); END;

--> statement-breakpoint
CREATE TRIGGER daybook_git_entries_insert AFTER INSERT ON "entries" BEGIN INSERT INTO git_outbox(workspace_id,entity_type,entity_id,payload,operation,created_at) VALUES(NEW."workspace_id", 'entries', NEW."id", json_object('id', NEW."id", 'workspace_id', NEW."workspace_id", 'project_id', NEW."project_id", 'kind', NEW."kind", 'title', NEW."title", 'body', NEW."body", 'author_id', NEW."author_id", 'assigned_id', NEW."assigned_id", 'topic_id', NEW."topic_id", 'custom_type_id', NEW."custom_type_id", 'summary', NEW."summary", 'auto_summary', NEW."auto_summary", 'day', NEW."day", 'due_day', NEW."due_day", 'status', NEW."status", 'priority', NEW."priority", 'parent_id', NEW."parent_id", 'pinned', NEW."pinned", 'created_at', NEW."created_at", 'updated_at', NEW."updated_at", 'version', NEW."version", 'deleted_at', NEW."deleted_at", 'mutation_id', NEW."mutation_id"), 'insert', strftime('%Y-%m-%dT%H:%M:%fZ','now')); END;

--> statement-breakpoint
CREATE TRIGGER daybook_git_entries_update AFTER UPDATE ON "entries" BEGIN INSERT INTO git_outbox(workspace_id,entity_type,entity_id,payload,operation,created_at) VALUES(NEW."workspace_id", 'entries', NEW."id", json_object('id', NEW."id", 'workspace_id', NEW."workspace_id", 'project_id', NEW."project_id", 'kind', NEW."kind", 'title', NEW."title", 'body', NEW."body", 'author_id', NEW."author_id", 'assigned_id', NEW."assigned_id", 'topic_id', NEW."topic_id", 'custom_type_id', NEW."custom_type_id", 'summary', NEW."summary", 'auto_summary', NEW."auto_summary", 'day', NEW."day", 'due_day', NEW."due_day", 'status', NEW."status", 'priority', NEW."priority", 'parent_id', NEW."parent_id", 'pinned', NEW."pinned", 'created_at', NEW."created_at", 'updated_at', NEW."updated_at", 'version', NEW."version", 'deleted_at', NEW."deleted_at", 'mutation_id', NEW."mutation_id"), 'update', strftime('%Y-%m-%dT%H:%M:%fZ','now')); END;

--> statement-breakpoint
CREATE TRIGGER daybook_git_entries_delete AFTER DELETE ON "entries" BEGIN INSERT INTO git_outbox(workspace_id,entity_type,entity_id,payload,operation,created_at) VALUES(OLD."workspace_id", 'entries', OLD."id", json_object('id', OLD."id", 'workspace_id', OLD."workspace_id", 'project_id', OLD."project_id", 'kind', OLD."kind", 'title', OLD."title", 'body', OLD."body", 'author_id', OLD."author_id", 'assigned_id', OLD."assigned_id", 'topic_id', OLD."topic_id", 'custom_type_id', OLD."custom_type_id", 'summary', OLD."summary", 'auto_summary', OLD."auto_summary", 'day', OLD."day", 'due_day', OLD."due_day", 'status', OLD."status", 'priority', OLD."priority", 'parent_id', OLD."parent_id", 'pinned', OLD."pinned", 'created_at', OLD."created_at", 'updated_at', OLD."updated_at", 'version', OLD."version", 'deleted_at', OLD."deleted_at", 'mutation_id', OLD."mutation_id"), 'delete', strftime('%Y-%m-%dT%H:%M:%fZ','now')); END;

--> statement-breakpoint
CREATE TRIGGER daybook_git_comments_insert AFTER INSERT ON "comments" BEGIN INSERT INTO git_outbox(workspace_id,entity_type,entity_id,payload,operation,created_at) VALUES(NEW."workspace_id", 'comments', NEW."id", json_object('id', NEW."id", 'workspace_id', NEW."workspace_id", 'entry_id', NEW."entry_id", 'author_id', NEW."author_id", 'body', NEW."body", 'attachment_id', NEW."attachment_id", 'created_at', NEW."created_at"), 'insert', strftime('%Y-%m-%dT%H:%M:%fZ','now')); END;

--> statement-breakpoint
CREATE TRIGGER daybook_git_comments_update AFTER UPDATE ON "comments" BEGIN INSERT INTO git_outbox(workspace_id,entity_type,entity_id,payload,operation,created_at) VALUES(NEW."workspace_id", 'comments', NEW."id", json_object('id', NEW."id", 'workspace_id', NEW."workspace_id", 'entry_id', NEW."entry_id", 'author_id', NEW."author_id", 'body', NEW."body", 'attachment_id', NEW."attachment_id", 'created_at', NEW."created_at"), 'update', strftime('%Y-%m-%dT%H:%M:%fZ','now')); END;

--> statement-breakpoint
CREATE TRIGGER daybook_git_comments_delete AFTER DELETE ON "comments" BEGIN INSERT INTO git_outbox(workspace_id,entity_type,entity_id,payload,operation,created_at) VALUES(OLD."workspace_id", 'comments', OLD."id", json_object('id', OLD."id", 'workspace_id', OLD."workspace_id", 'entry_id', OLD."entry_id", 'author_id', OLD."author_id", 'body', OLD."body", 'attachment_id', OLD."attachment_id", 'created_at', OLD."created_at"), 'delete', strftime('%Y-%m-%dT%H:%M:%fZ','now')); END;

--> statement-breakpoint
CREATE TRIGGER daybook_git_activity_insert AFTER INSERT ON "activity" BEGIN INSERT INTO git_outbox(workspace_id,entity_type,entity_id,payload,operation,created_at) VALUES(NEW."workspace_id", 'activity', NEW."id", json_object('id', NEW."id", 'workspace_id', NEW."workspace_id", 'entry_id', NEW."entry_id", 'actor_id', NEW."actor_id", 'action', NEW."action", 'before_json', NEW."before_json", 'after_json', NEW."after_json", 'created_at', NEW."created_at"), 'insert', strftime('%Y-%m-%dT%H:%M:%fZ','now')); END;

--> statement-breakpoint
CREATE TRIGGER daybook_git_activity_update AFTER UPDATE ON "activity" BEGIN INSERT INTO git_outbox(workspace_id,entity_type,entity_id,payload,operation,created_at) VALUES(NEW."workspace_id", 'activity', NEW."id", json_object('id', NEW."id", 'workspace_id', NEW."workspace_id", 'entry_id', NEW."entry_id", 'actor_id', NEW."actor_id", 'action', NEW."action", 'before_json', NEW."before_json", 'after_json', NEW."after_json", 'created_at', NEW."created_at"), 'update', strftime('%Y-%m-%dT%H:%M:%fZ','now')); END;

--> statement-breakpoint
CREATE TRIGGER daybook_git_activity_delete AFTER DELETE ON "activity" BEGIN INSERT INTO git_outbox(workspace_id,entity_type,entity_id,payload,operation,created_at) VALUES(OLD."workspace_id", 'activity', OLD."id", json_object('id', OLD."id", 'workspace_id', OLD."workspace_id", 'entry_id', OLD."entry_id", 'actor_id', OLD."actor_id", 'action', OLD."action", 'before_json', OLD."before_json", 'after_json', OLD."after_json", 'created_at', OLD."created_at"), 'delete', strftime('%Y-%m-%dT%H:%M:%fZ','now')); END;

--> statement-breakpoint
CREATE TRIGGER daybook_git_attachments_insert AFTER INSERT ON "attachments" BEGIN INSERT INTO git_outbox(workspace_id,entity_type,entity_id,payload,operation,created_at) VALUES(NEW."workspace_id", 'attachments', NEW."id", json_object('id', NEW."id", 'workspace_id', NEW."workspace_id", 'entry_id', NEW."entry_id", 'name', NEW."name", 'size', NEW."size", 'key', NEW."key", 'content_type', NEW."content_type", 'created_at', NEW."created_at"), 'insert', strftime('%Y-%m-%dT%H:%M:%fZ','now')); END;

--> statement-breakpoint
CREATE TRIGGER daybook_git_attachments_update AFTER UPDATE ON "attachments" BEGIN INSERT INTO git_outbox(workspace_id,entity_type,entity_id,payload,operation,created_at) VALUES(NEW."workspace_id", 'attachments', NEW."id", json_object('id', NEW."id", 'workspace_id', NEW."workspace_id", 'entry_id', NEW."entry_id", 'name', NEW."name", 'size', NEW."size", 'key', NEW."key", 'content_type', NEW."content_type", 'created_at', NEW."created_at"), 'update', strftime('%Y-%m-%dT%H:%M:%fZ','now')); END;

--> statement-breakpoint
CREATE TRIGGER daybook_git_attachments_delete AFTER DELETE ON "attachments" BEGIN INSERT INTO git_outbox(workspace_id,entity_type,entity_id,payload,operation,created_at) VALUES(OLD."workspace_id", 'attachments', OLD."id", json_object('id', OLD."id", 'workspace_id', OLD."workspace_id", 'entry_id', OLD."entry_id", 'name', OLD."name", 'size', OLD."size", 'key', OLD."key", 'content_type', OLD."content_type", 'created_at', OLD."created_at"), 'delete', strftime('%Y-%m-%dT%H:%M:%fZ','now')); END;
