CREATE TABLE `access_requests` (
	`id` text PRIMARY KEY NOT NULL,
	`workspace_id` text NOT NULL,
	`user_id` text NOT NULL,
	`name` text NOT NULL,
	`email` text NOT NULL,
	`invite_id` text NOT NULL,
	`requested_role` text NOT NULL,
	`status` text DEFAULT 'pending' NOT NULL,
	`created_at` text NOT NULL,
	`reviewed_at` text,
	`reviewed_by` text,
	`decision_token` text
);
--> statement-breakpoint
CREATE UNIQUE INDEX `access_applicant` ON `access_requests` (`workspace_id`,`user_id`);--> statement-breakpoint
CREATE INDEX `access_pending` ON `access_requests` (`workspace_id`,`status`);--> statement-breakpoint
CREATE TABLE `presence_sessions` (
	`workspace_id` text NOT NULL,
	`user_id` text NOT NULL,
	`session_id` text PRIMARY KEY NOT NULL,
	`entry_id` text,
	`mode` text NOT NULL,
	`seen_at` integer NOT NULL
);
--> statement-breakpoint
CREATE INDEX `presence_workspace` ON `presence_sessions` (`workspace_id`,`seen_at`);