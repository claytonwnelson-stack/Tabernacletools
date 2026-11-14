-- v0.18 one subscription unlocks every Tabernacle Tools app (Guitar, Keys, Bass, Charts).
-- StoreKit subscriptions are per app, so each app's purchase is posted here (signed StoreKit 2 transaction) and recorded against
-- the Sign in with Apple user. Every app reads GET /v1/entitlement. One row per App Store subscription (originalTransactionId).
-- Church subscriptions still live in `teams` (seats); individual ones live here. Deleted with the user (trial_history is not).
CREATE TABLE IF NOT EXISTS purchases (
  original_transaction_id TEXT PRIMARY KEY,
  user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  bundle_id TEXT NOT NULL,          -- which app sold it
  product_id TEXT NOT NULL,
  plan TEXT NOT NULL,               -- individual | church
  environment TEXT NOT NULL,
  status TEXT NOT NULL,             -- active | grace | billing_retry | expired | revoked
  expires_at INTEGER NOT NULL,
  grace_until INTEGER NOT NULL DEFAULT 0,
  in_trial INTEGER NOT NULL DEFAULT 0,
  trial_blocked INTEGER NOT NULL DEFAULT 0,   -- a second free trial (other app) for a person who already had one: no access until paid
  updated_at INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS purchases_user ON purchases(user_id);
