CREATE TABLE `record_types` (
	`id` text PRIMARY KEY NOT NULL,
	`workspace_id` text NOT NULL,
	`name` text NOT NULL,
	`description` text DEFAULT '' NOT NULL,
	`owner_id` text NOT NULL,
	`color` text NOT NULL,
	`archived` integer DEFAULT 0 NOT NULL,
	`created_at` text NOT NULL
);
--> statement-breakpoint
CREATE INDEX `type_workspace` ON `record_types` (`workspace_id`);--> statement-breakpoint
ALTER TABLE `entries` ADD `custom_type_id` text;