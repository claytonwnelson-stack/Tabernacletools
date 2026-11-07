// Fixed-window rate limiter stored in the DB (D1 is fine at church scale; swap for the Workers Rate Limiting binding if traffic grows).
import { HttpError } from "./util.ts";
import type { Db } from "./db.ts";
export async function rateLimit(db: Db, key: string, limit: number, windowSec: number, now: number): Promise<void> {
  const w = now - (now % windowSec);
  await db.run(`INSERT INTO rate_limits(key, window_start, count) VALUES(?, ?, 1)
    ON CONFLICT(key) DO UPDATE SET count = CASE WHEN window_start = excluded.window_start THEN count + 1 ELSE 1 END, window_start = excluded.window_start`, key, w);
  const r = await db.first<{ count: number }>("SELECT count FROM rate_limits WHERE key = ?", key);
  if ((r?.count ?? 0) > limit) throw new HttpError(429, "rate_limited");
}
