using MySqlConnector;

internal static class ServiceAnnouncementDelivery
{
    internal static Task EnsureAsync(MySqlConnection c,CancellationToken ct)=>CommunityEndpoints.Exec(c,null,"""
        CREATE TABLE IF NOT EXISTS service_announcement_delivery(id TINYINT PRIMARY KEY,tag VARCHAR(64) NOT NULL DEFAULT '',repeat_count INT NOT NULL DEFAULT 1,broadcast_id VARCHAR(64) NOT NULL DEFAULT '');
        INSERT IGNORE INTO service_announcement_delivery(id) VALUES(1);
        CREATE TABLE IF NOT EXISTS service_announcement_windows(id TINYINT PRIMARY KEY,starts_at_utc DATETIME(6) NULL,ends_at_utc DATETIME(6) NULL);
        INSERT IGNORE INTO service_announcement_windows(id) VALUES(1);
        """,ct);
}
