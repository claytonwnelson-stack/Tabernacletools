-- v0.13 one free trial per person. Apple already limits the intro offer (2 weeks free) to one per Apple ID per subscription
-- group; this is the seat service's own record so a church team can't be built on a second trial.
--
-- PRIVACY: trial_history is deliberately NOT deleted by DELETE /v1/account. If it were, "delete account, sign in again, start
-- a new trial" would defeat the rule. It holds no personal data: key_hash = HMAC(SUB_PEPPER, "trial-sub:" + Sign in with Apple
-- sub) - a different HMAC domain than users.apple_sub_hmac, so it can't be joined to a live account row - plus a hash of the
-- original transaction id and a timestamp. Without the server-side pepper it can't be linked back to an Apple account.
CREATE TABLE IF NOT EXISTS trial_history (
  key_hash TEXT PRIMARY KEY,
  otid_hash TEXT NOT NULL,         -- SHA-256 of the originalTransactionId that carried the trial
  first_trial_at INTEGER NOT NULL,
  source TEXT NOT NULL             -- team_create | notification
);
-- the trial key for the signed-in user (filled at every /v1/auth/apple; deleted with the user row)
ALTER TABLE users ADD COLUMN trial_key TEXT;
-- current period is an introductory (free-trial) period / team was built on a trial this person had already used
ALTER TABLE teams ADD COLUMN in_trial INTEGER NOT NULL DEFAULT 0;
ALTER TABLE teams ADD COLUMN trial_blocked INTEGER NOT NULL DEFAULT 0;
