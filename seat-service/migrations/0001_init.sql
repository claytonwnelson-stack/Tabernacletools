-- Tabernacle Tools seat service schema. Privacy-minimal: no emails, no names beyond an optional display name chosen by the user,
-- Apple user ids stored only as HMAC(sub) so a database leak can't be joined to Apple accounts.
CREATE TABLE IF NOT EXISTS users (
  id TEXT PRIMARY KEY,
  apple_sub_hmac TEXT NOT NULL UNIQUE,
  created_at INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS teams (
  id TEXT PRIMARY KEY,
  leader_user_id TEXT NOT NULL UNIQUE REFERENCES users(id) ON DELETE CASCADE,
  church_name TEXT NOT NULL,
  original_transaction_id TEXT NOT NULL UNIQUE,
  product_id TEXT NOT NULL,
  environment TEXT NOT NULL,
  status TEXT NOT NULL,            -- active | grace | billing_retry | expired | revoked
  expires_at INTEGER NOT NULL,     -- unix seconds, end of paid period
  grace_until INTEGER NOT NULL DEFAULT 0,
  updated_at INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS members (
  team_id TEXT NOT NULL REFERENCES teams(id) ON DELETE CASCADE,
  user_id TEXT NOT NULL UNIQUE REFERENCES users(id) ON DELETE CASCADE,   -- one team per person
  display_name TEXT NOT NULL,
  role TEXT NOT NULL,              -- leader | member
  joined_at INTEGER NOT NULL,
  PRIMARY KEY (team_id, user_id)
);
CREATE TABLE IF NOT EXISTS invites (
  code_hash TEXT PRIMARY KEY,
  team_id TEXT NOT NULL REFERENCES teams(id) ON DELETE CASCADE,
  expires_at INTEGER NOT NULL,
  created_at INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS invites_team ON invites(team_id);
CREATE TABLE IF NOT EXISTS rate_limits (
  key TEXT PRIMARY KEY,
  window_start INTEGER NOT NULL,
  count INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS notifications (
  uuid TEXT PRIMARY KEY,           -- App Store notificationUUID, for idempotency
  received_at INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS seat_changes (
  team_id TEXT NOT NULL,
  at INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS seat_changes_team ON seat_changes(team_id, at);
