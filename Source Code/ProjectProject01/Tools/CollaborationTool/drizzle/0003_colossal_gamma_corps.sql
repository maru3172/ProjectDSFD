ALTER TABLE `entries` ADD `summary` text;--> statement-breakpoint
ALTER TABLE `entries` ADD `auto_summary` text DEFAULT '' NOT NULL;