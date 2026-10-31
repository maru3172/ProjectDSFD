// User content only. Authentication credentials and personal notification states are excluded.
export const exportTables={
  "workspaces": [
    "id",
    "name",
    "owner_id",
    "created_at"
  ],
  "members": [
    "id",
    "workspace_id",
    "user_id",
    "name",
    "role",
    "created_at"
  ],
  "projects": [
    "id",
    "workspace_id",
    "name",
    "color",
    "archived",
    "created_at"
  ],
  "topics": [
    "id",
    "workspace_id",
    "name",
    "description",
    "owner_id",
    "color",
    "archived",
    "created_at"
  ],
  "record_types": [
    "id",
    "workspace_id",
    "name",
    "description",
    "owner_id",
    "color",
    "archived",
    "created_at"
  ],
  "entries": [
    "id",
    "workspace_id",
    "project_id",
    "kind",
    "title",
    "body",
    "author_id",
    "assigned_id",
    "topic_id",
    "custom_type_id",
    "summary",
    "auto_summary",
    "day",
    "due_day",
    "status",
    "priority",
    "parent_id",
    "pinned",
    "created_at",
    "updated_at",
    "version",
    "deleted_at"
  ],
  "comments": [
    "id",
    "workspace_id",
    "entry_id",
    "author_id",
    "body",
    "attachment_id",
    "created_at"
  ],
  "activity": [
    "id",
    "workspace_id",
    "entry_id",
    "actor_id",
    "action",
    "before_json",
    "after_json",
    "created_at"
  ],
  "attachments": [
    "id",
    "workspace_id",
    "entry_id",
    "name",
    "size",
    "key",
    "content_type",
    "created_at"
  ]
} as const;
