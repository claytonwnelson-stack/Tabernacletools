// Seat service router. Pure function of (Request, Deps) so it runs identically on Workers and in node tests.
import { HttpError, json, readJson, str, randomCode, normaliseCode, randomId, sha256Hex, hmacHex } from "./util.ts";
import type { Db } from "./db.ts";
import { verifyAppleIdentityToken, type SiwaConfig } from "./apple/siwa.ts";
import { verifyAppleJws, type JwsConfig } from "./apple/jws.ts";
import { isTrialTx, fetchSubscriptionState, decodeNotification, stateFromNotification, paidThrough, type ServerApiConfig, type TransactionInfo, type SubState } from "./apple/appstore.ts";
import { makeSession, readSession, signEntitlement, publicKeyB64url, OFFLINE_CAP } from "./tokens.ts";
import { rateLimit } from "./ratelimit.ts";
import { APPS, planOf, appName, FAMILY_AUDIENCE, type Plan } from "./catalog.ts";

export const SEAT_CAP = 15; // including the leader
export const INVITE_TTL = 7 * 86400;
export const MAX_OPEN_INVITES = 20;
export const CHURCH_PRODUCT = "com.tabernacletools.guitar.church.monthly";
export interface Deps {
  db: Db; now: () => number; sessionSecret: string; subPepper: string; signingKey: CryptoKey;
  bundleId: string; siwa: SiwaConfig; jws: JwsConfig; serverApi: ServerApiConfig | null; churchProductId?: string;
  bundleIds?: string[];   // v0.18 every app in the family (default: catalog APPS)
}
interface Team { id: string; leader_user_id: string; church_name: string; original_transaction_id: string; product_id: string; environment: string; status: string; expires_at: number; grace_until: number; in_trial: number; trial_blocked: number }

async function authUser(req: Request, d: Deps): Promise<string> {
  const h = req.headers.get("authorization") ?? ""; if (!h.startsWith("Bearer ")) throw new HttpError(401, "no_session");
  const uid = await readSession(d.sessionSecret, h.slice(7), d.now());
  if (!(await d.db.first("SELECT id FROM users WHERE id = ?", uid))) throw new HttpError(401, "no_user");
  return uid;
}
const teamOfUser = (d: Deps, uid: string) => d.db.first<Team & { role: string }>("SELECT t.*, m.role FROM members m JOIN teams t ON t.id = m.team_id WHERE m.user_id = ?", uid);
const seatCount = async (d: Deps, teamId: string) => (await d.db.first<{ n: number }>("SELECT COUNT(*) AS n FROM members WHERE team_id = ?", teamId))!.n;
const sub = (t: Team): SubState => ({ status: t.status as SubState["status"], expiresAt: t.expires_at, graceUntil: t.grace_until, trial: !!t.in_trial });
async function saveState(d: Deps, teamId: string, s: SubState) {
  await d.db.run("UPDATE teams SET status = ?, expires_at = ?, grace_until = ?, in_trial = COALESCE(?, in_trial), updated_at = ? WHERE id = ?", s.status, s.expiresAt, s.graceUntil, s.trial === undefined ? null : s.trial ? 1 : 0, d.now(), teamId);
}
// ---- v0.13 one free trial per person (trial_history survives account deletion; see migrations/0002_trial_history.sql)
const trialKeyOf = async (d: Deps, uid: string) => (await d.db.first<{ trial_key: string | null }>("SELECT trial_key FROM users WHERE id = ?", uid))?.trial_key ?? null;
async function trialEligible(d: Deps, uid: string): Promise<boolean> {
  const k = await trialKeyOf(d, uid); if (!k) return false; // unknown person: never promise a trial
  return !(await d.db.first("SELECT key_hash FROM trial_history WHERE key_hash = ?", k));
}
// Record that this person used their trial on `otid`. Returns false if they had ALREADY used one on a different subscription.
async function recordTrial(d: Deps, key: string, otid: string, source: string): Promise<boolean> {
  const oh = await sha256Hex("otid:" + otid);
  await d.db.run("INSERT OR IGNORE INTO trial_history(key_hash, otid_hash, first_trial_at, source) VALUES(?,?,?,?)", key, oh, d.now(), source);
  const row = await d.db.first<{ otid_hash: string }>("SELECT otid_hash FROM trial_history WHERE key_hash = ?", key);
  return !!row && row.otid_hash === oh;
}
// a team gets no access from a trial period its leader wasn't entitled to; paid periods always count
const trialRefused = (t: Team, s: SubState) => !!t.trial_blocked && !!s.trial;
function teamView(t: Team, now: number) { return { id: t.id, churchName: t.church_name, status: t.status, paidThrough: paidThrough(sub(t), now), seatCap: SEAT_CAP }; }

// ---- v0.18 cross-app purchases
interface Purchase { original_transaction_id: string; user_id: string; bundle_id: string; product_id: string; plan: Plan; environment: string; status: string; expires_at: number; grace_until: number; in_trial: number; trial_blocked: number }
const familyBundles = (d: Deps) => d.bundleIds ?? APPS.map((a) => a.bundleId);
const knownBundle = (d: Deps, b: string) => familyBundles(d).includes(b);
const psub = (r: Purchase): SubState => ({ status: r.status as SubState["status"], expiresAt: r.expires_at, graceUntil: r.grace_until, trial: !!r.in_trial });
const apiFor = (d: Deps, bundleId: string): ServerApiConfig | null => d.serverApi ? { ...d.serverApi, bundleId } : null; // Server API JWT "bid" is per app
async function savePurchase(d: Deps, otid: string, s: SubState) {
  await d.db.run("UPDATE purchases SET status = ?, expires_at = ?, grace_until = ?, in_trial = COALESCE(?, in_trial), updated_at = ? WHERE original_transaction_id = ?", s.status, s.expiresAt, s.graceUntil, s.trial === undefined ? null : s.trial ? 1 : 0, d.now(), otid);
}
// What this person is covered by right now, across every app: their own subscriptions (any app) and a church seat.
async function coverage(d: Deps, uid: string, now: number) {
  const rows = await d.db.all<Purchase>("SELECT * FROM purchases WHERE user_id = ?", uid);
  const usable: { plan: Plan; bundleId: string; paidThrough: number; status: string; trial: boolean; otid: string }[] = [];
  for (let r of rows) {
    let s = psub(r);
    if (d.serverApi && s.expiresAt - now < 86400 && s.status !== "revoked") { // missed webhook near expiry: ask Apple
      const live = await fetchSubscriptionState(apiFor(d, r.bundle_id)!, r.original_transaction_id).catch(() => null);
      if (live) { s = live.state; await savePurchase(d, r.original_transaction_id, s); }
    }
    const pt = paidThrough(s, now);
    if (pt && !(r.trial_blocked && s.trial)) usable.push({ plan: r.plan, bundleId: r.bundle_id, paidThrough: pt, status: s.status, trial: !!s.trial, otid: r.original_transaction_id });
  }
  return usable.sort((a, b) => (a.plan === b.plan ? b.paidThrough - a.paidThrough : a.plan === "church" ? -1 : 1));
}
export async function handle(req: Request, d: Deps): Promise<Response> {
  try { return await route(req, d); }
  catch (e) {
    if (e instanceof HttpError) return json({ error: e.code, detail: e.message !== e.code ? e.message : undefined }, e.status, e.status === 429 ? { "retry-after": "60" } : {});
    console.error(e); return json({ error: "internal" }, 500);
  }
}
async function route(req: Request, d: Deps): Promise<Response> {
  const url = new URL(req.url), p = url.pathname, m = req.method, now = d.now();
  const ip = req.headers.get("cf-connecting-ip") ?? "local";
  if (m === "GET" && p === "/healthz") return json({ ok: true });
  if (m === "GET" && p === "/v1/keys") return json({ alg: "Ed25519", keys: [{ kid: "k1", x: await publicKeyB64url(d.signingKey) }] });

  if (m === "POST" && p === "/v1/appstore/notifications") {
    await rateLimit(d.db, "notif:" + ip, 300, 60, now);
    const body = await readJson(req, 256 * 1024); const n = await decodeNotification(str(body, "signedPayload", 200_000), d.jws);
    if (n.bundleId && n.bundleId !== d.bundleId && !knownBundle(d, n.bundleId)) throw new HttpError(400, "wrong_bundle");
    const fresh = await d.db.run("INSERT OR IGNORE INTO notifications(uuid, received_at) VALUES(?, ?)", n.notificationUUID, now);
    if (fresh.changes === 0) return json({ ok: true, duplicate: true });
    if (n.tx) { // v0.18 individual (any app) purchase rows
      const pr = await d.db.first<Purchase>("SELECT * FROM purchases WHERE original_transaction_id = ?", n.tx.originalTransactionId);
      if (pr) {
        let next = stateFromNotification(n, psub(pr));
        if (next && next.status !== "revoked" && next.expiresAt < pr.expires_at && n.notificationType !== "EXPIRED" && !n.notificationType.startsWith("DID_FAIL") && n.notificationType !== "GRACE_PERIOD_EXPIRED") next = null;
        if (next) await savePurchase(d, pr.original_transaction_id, next);
        if (isTrialTx(n.tx) && (n.notificationType === "SUBSCRIBED" || n.notificationType === "OFFER_REDEEMED")) {
          const key = await trialKeyOf(d, pr.user_id);
          if (key && !(await recordTrial(d, key, n.tx.originalTransactionId, "notification"))) await d.db.run("UPDATE purchases SET trial_blocked = 1 WHERE original_transaction_id = ?", pr.original_transaction_id);
        }
      }
    }
    if (n.tx) {
      const t = await d.db.first<Team>("SELECT * FROM teams WHERE original_transaction_id = ?", n.tx.originalTransactionId);
      if (t) {
        let next = stateFromNotification(n, sub(t));
        // never let an older, out-of-order notification shorten a newer paid period
        if (next && next.status !== "revoked" && next.expiresAt < t.expires_at && n.notificationType !== "EXPIRED" && !n.notificationType.startsWith("DID_FAIL") && n.notificationType !== "GRACE_PERIOD_EXPIRED") next = null;
        if (next) await saveState(d, t.id, next);
        // trial start for this team: record it against the leader; a second trial for the same person is blocked
        if (isTrialTx(n.tx) && (n.notificationType === "SUBSCRIBED" || n.notificationType === "OFFER_REDEEMED")) {
          const key = await trialKeyOf(d, t.leader_user_id);
          if (key && !(await recordTrial(d, key, n.tx.originalTransactionId, "notification"))) await d.db.run("UPDATE teams SET trial_blocked = 1 WHERE id = ?", t.id);
        }
      }
    }
    await d.db.run("DELETE FROM notifications WHERE received_at < ?", now - 90 * 86400);
    return json({ ok: true });
  }

  if (m === "POST" && p === "/v1/auth/apple") {
    await rateLimit(d.db, "auth:" + ip, 20, 60, now);
    const body = await readJson(req); const id = await verifyAppleIdentityToken(str(body, "identityToken", 8192), d.siwa);
    const h = await hmacHex(d.subPepper, "apple-sub:" + id.sub);
    let u = await d.db.first<{ id: string }>("SELECT id FROM users WHERE apple_sub_hmac = ?", h);
    const tk = await hmacHex(d.subPepper, "trial-sub:" + id.sub);
    if (!u) { u = { id: randomId("u") }; await d.db.run("INSERT INTO users(id, apple_sub_hmac, created_at, trial_key) VALUES(?, ?, ?, ?)", u.id, h, now, tk); }
    else await d.db.run("UPDATE users SET trial_key = ? WHERE id = ? AND trial_key IS NULL", tk, u.id);
    return json({ session: await makeSession(d.sessionSecret, u.id, now), userId: u.id, expiresIn: 30 * 86400 });
  }

  const uid = await authUser(req, d);
  await rateLimit(d.db, "user:" + uid, 60, 60, now);

  if (m === "POST" && p === "/v1/teams") {
    const body = await readJson(req); const churchName = str(body, "churchName", 80).trim(); const displayName = str(body, "displayName", 40, true).trim() || "Leader";
    if (await teamOfUser(d, uid)) throw new HttpError(409, "already_in_team");
    const tx = await verifyAppleJws<TransactionInfo>(str(body, "signedTransaction", 20000), d.jws);
    if (tx.bundleId !== d.bundleId && !knownBundle(d, tx.bundleId)) throw new HttpError(400, "wrong_bundle");
    if (tx.productId !== (d.churchProductId ?? CHURCH_PRODUCT) && planOf(tx.bundleId, tx.productId) !== "church") throw new HttpError(400, "not_church_plan");
    if (await d.db.first("SELECT id FROM teams WHERE original_transaction_id = ?", tx.originalTransactionId)) throw new HttpError(409, "subscription_already_has_team");
    // Ask Apple for the authoritative current state (renewals since purchase, refunds...). Fall back to the signed tx if the API isn't configured.
    let state: SubState = { status: tx.revocationDate ? "revoked" : "active", expiresAt: Math.floor((tx.expiresDate ?? 0) / 1000), graceUntil: 0 };
    if (d.serverApi) { const live = await fetchSubscriptionState(apiFor(d, tx.bundleId)!, tx.originalTransactionId); if (!live) throw new HttpError(402, "subscription_not_found"); state = live.state; }
    if (state.trial === undefined) state.trial = isTrialTx(tx);
    if (paidThrough(state, now) === 0) throw new HttpError(402, "subscription_inactive");
    // one free trial per person: a leader who already used a trial can only build a team on a PAID period
    if (state.trial) {
      const key = await trialKeyOf(d, uid); if (!key) throw new HttpError(401, "sign_in_again");
      const oh = await sha256Hex("otid:" + tx.originalTransactionId);
      const prev = await d.db.first<{ otid_hash: string }>("SELECT otid_hash FROM trial_history WHERE key_hash = ?", key);
      if (prev && prev.otid_hash !== oh) throw new HttpError(402, "trial_already_used", "Your free trial has been used; the team starts when the first paid month begins");
      await recordTrial(d, key, tx.originalTransactionId, "team_create");
    }
    const teamId = randomId("t");
    await d.db.run("INSERT INTO teams(id, leader_user_id, church_name, original_transaction_id, product_id, environment, status, expires_at, grace_until, in_trial, updated_at) VALUES(?,?,?,?,?,?,?,?,?,?,?)",
      teamId, uid, churchName, tx.originalTransactionId, tx.productId, tx.environment, state.status, state.expiresAt, state.graceUntil, state.trial ? 1 : 0, now);
    await d.db.run("INSERT INTO members(team_id, user_id, display_name, role, joined_at) VALUES(?,?,?,?,?)", teamId, uid, displayName, "leader", now);
    return json({ team: teamView((await d.db.first<Team>("SELECT * FROM teams WHERE id = ?", teamId))!, now) }, 201);
  }

  if (m === "GET" && p === "/v1/teams/me") {
    const t = await teamOfUser(d, uid); if (!t) throw new HttpError(404, "no_team");
    const members = await d.db.all<{ user_id: string; display_name: string; role: string; joined_at: number }>("SELECT user_id, display_name, role, joined_at FROM members WHERE team_id = ? ORDER BY joined_at", t.id);
    const out: Record<string, unknown> = { team: teamView(t, now), role: t.role, members: members.map((x) => ({ id: x.user_id, displayName: x.display_name, role: x.role, joinedAt: x.joined_at, you: x.user_id === uid })) };
    if (t.role === "leader") out.openInvites = (await d.db.first<{ n: number }>("SELECT COUNT(*) AS n FROM invites WHERE team_id = ? AND expires_at > ?", t.id, now))!.n;
    return json(out);
  }

  if (m === "POST" && p === "/v1/teams/me/invites") {
    const t = await teamOfUser(d, uid); if (!t || t.role !== "leader") throw new HttpError(403, "leader_only");
    if (paidThrough(sub(t), now) === 0) throw new HttpError(402, "subscription_inactive");
    await d.db.run("DELETE FROM invites WHERE expires_at <= ?", now);
    if ((await seatCount(d, t.id)) >= SEAT_CAP) throw new HttpError(409, "team_full");
    if ((await d.db.first<{ n: number }>("SELECT COUNT(*) AS n FROM invites WHERE team_id = ?", t.id))!.n >= MAX_OPEN_INVITES) throw new HttpError(409, "too_many_invites");
    const code = randomCode(10), exp = now + INVITE_TTL;
    await d.db.run("INSERT INTO invites(code_hash, team_id, expires_at, created_at) VALUES(?,?,?,?)", await sha256Hex(code), t.id, exp, now);
    return json({ code: code.slice(0, 5) + "-" + code.slice(5), expiresAt: exp }, 201);
  }
  const invDel = p.match(/^\/v1\/teams\/me\/invites\/([^/]+)$/);
  if (m === "DELETE" && invDel) {
    const t = await teamOfUser(d, uid); if (!t || t.role !== "leader") throw new HttpError(403, "leader_only");
    const r = await d.db.run("DELETE FROM invites WHERE code_hash = ? AND team_id = ?", await sha256Hex(normaliseCode(decodeURIComponent(invDel[1]))), t.id);
    return json({ ok: true, revoked: r.changes > 0 });
  }

  if (m === "POST" && p === "/v1/invites/redeem") {
    await rateLimit(d.db, "redeem:" + uid, 10, 3600, now); // brute-force guard on top of 50-bit codes
    const body = await readJson(req); const code = normaliseCode(str(body, "code", 32)); const displayName = str(body, "displayName", 40, true).trim() || "Member";
    if (await teamOfUser(d, uid)) throw new HttpError(409, "already_in_team");
    const h = await sha256Hex(code);
    const inv = await d.db.first<{ team_id: string; expires_at: number }>("SELECT team_id, expires_at FROM invites WHERE code_hash = ?", h);
    if (!inv || inv.expires_at <= now) throw new HttpError(404, "invalid_or_expired_code");
    const t = (await d.db.first<Team>("SELECT * FROM teams WHERE id = ?", inv.team_id))!;
    if (paidThrough(sub(t), now) === 0) throw new HttpError(402, "subscription_inactive");
    if ((await seatCount(d, t.id)) >= SEAT_CAP) throw new HttpError(409, "team_full");
    // single use: whoever deletes the row wins
    if ((await d.db.run("DELETE FROM invites WHERE code_hash = ?", h)).changes !== 1) throw new HttpError(404, "invalid_or_expired_code");
    // guarded insert keeps the cap even under concurrent redemptions
    const ins = await d.db.run("INSERT INTO members(team_id, user_id, display_name, role, joined_at) SELECT ?,?,?,?,? WHERE (SELECT COUNT(*) FROM members WHERE team_id = ?) < ?", t.id, uid, displayName, "member", now, t.id, SEAT_CAP);
    if (ins.changes !== 1) throw new HttpError(409, "team_full");
    await d.db.run("INSERT INTO seat_changes(team_id, at) VALUES(?, ?)", t.id, now);
    return json({ team: teamView(t, now) }, 201);
  }

  if (m === "POST" && p === "/v1/teams/me/leave") {
    const t = await teamOfUser(d, uid); if (!t) throw new HttpError(404, "no_team");
    if (t.role === "leader") throw new HttpError(409, "leader_cannot_leave", "Delete the account (or the team) instead; the subscription belongs to the leader");
    await d.db.run("DELETE FROM members WHERE user_id = ?", uid); return json({ ok: true });
  }
  const memDel = p.match(/^\/v1\/teams\/me\/members\/([^/]+)$/);
  if (m === "DELETE" && memDel) {
    const t = await teamOfUser(d, uid); if (!t || t.role !== "leader") throw new HttpError(403, "leader_only");
    if (memDel[1] === uid) throw new HttpError(409, "cannot_remove_self");
    const r = await d.db.run("DELETE FROM members WHERE team_id = ? AND user_id = ? AND role = 'member'", t.id, memDel[1]);
    if (!r.changes) throw new HttpError(404, "no_such_member"); return json({ ok: true });
  }

  if (m === "POST" && p === "/v1/entitlement") {
    const te = await trialEligible(d, uid);
    const t = await teamOfUser(d, uid); if (!t) return json({ error: "no_team", trialEligible: te }, 404);
    let s = sub(t);
    // near/after expiry with no notification yet: ask Apple directly (covers missed webhooks)
    if (d.serverApi && s.expiresAt - now < 86400) {
      const live = await fetchSubscriptionState(d.serverApi, t.original_transaction_id).catch(() => null);
      if (live) { s = live.state; await saveState(d, t.id, s); }
    }
    const pt = paidThrough(s, now);
    if (pt === 0) return json({ error: "subscription_inactive", status: s.status, trialEligible: te }, 402);
    if (trialRefused(t, s)) return json({ error: "trial_already_used", status: s.status, trialEligible: false }, 402);
    const token = await signEntitlement(d.signingKey, { v: 1, aud: FAMILY_AUDIENCE, team: t.id, mem: uid, role: t.role, exp: pt, iat: now, off: now + OFFLINE_CAP });
    return json({ token, paidThrough: pt, offlineUntil: Math.min(pt, now + OFFLINE_CAP), status: s.status, trial: !!s.trial, trialEligible: te });
  }

  // v0.18 an app posts its signed StoreKit 2 transaction after a purchase, renewal (Transaction.updates) or Restore Purchases.
  if (m === "POST" && p === "/v1/purchases") {
    const body = await readJson(req);
    const tx = await verifyAppleJws<TransactionInfo>(str(body, "signedTransaction", 20000), d.jws);
    if (!knownBundle(d, tx.bundleId)) throw new HttpError(400, "wrong_bundle");
    const plan = planOf(tx.bundleId, tx.productId) ?? (tx.productId === (d.churchProductId ?? CHURCH_PRODUCT) ? "church" : null);
    if (!plan) throw new HttpError(400, "unknown_product");
    const owner = await d.db.first<{ user_id: string }>("SELECT user_id FROM purchases WHERE original_transaction_id = ?", tx.originalTransactionId);
    if (owner && owner.user_id !== uid) throw new HttpError(409, "purchase_belongs_to_another_account");   // one Apple ID's purchase can't unlock two people
    let state: SubState = { status: tx.revocationDate ? "revoked" : "active", expiresAt: Math.floor((tx.expiresDate ?? 0) / 1000), graceUntil: 0 };
    if (d.serverApi) { const live = await fetchSubscriptionState(apiFor(d, tx.bundleId)!, tx.originalTransactionId); if (live) state = live.state; }
    if (state.trial === undefined) state.trial = isTrialTx(tx);
    const before = await coverage(d, uid, now);   // to warn about paying twice
    if (owner) await savePurchase(d, tx.originalTransactionId, state);
    else await d.db.run("INSERT INTO purchases(original_transaction_id, user_id, bundle_id, product_id, plan, environment, status, expires_at, grace_until, in_trial, updated_at) VALUES(?,?,?,?,?,?,?,?,?,?,?)",
      tx.originalTransactionId, uid, tx.bundleId, tx.productId, plan, tx.environment, state.status, state.expiresAt, state.graceUntil, state.trial ? 1 : 0, now);
    let trialOk = true;
    if (state.trial) { // one free trial per person across ALL apps
      const key = await trialKeyOf(d, uid); if (!key) throw new HttpError(401, "sign_in_again");
      trialOk = await recordTrial(d, key, tx.originalTransactionId, "purchase");
      await d.db.run("UPDATE purchases SET trial_blocked = ? WHERE original_transaction_id = ?", trialOk ? 0 : 1, tx.originalTransactionId);
    }
    const other = before.find((c) => c.otid !== tx.originalTransactionId && (c.plan === plan || c.plan === "church"));
    return json({ ok: true, plan, trialOk, duplicate: other ? { app: appName(other.bundleId), bundleId: other.bundleId, plan: other.plan } : null }, owner ? 200 : 201);
  }
  // v0.18 cross-app entitlement: any app, any plan. covered=false still answers 200 so the paywall can sell.
  if (m === "GET" && p === "/v1/entitlement") {
    const te = await trialEligible(d, uid); const cov = await coverage(d, uid, now);
    let seat: { team: string; role: string; paidThrough: number; status: string; trial: boolean } | null = null;
    const t = await teamOfUser(d, uid);
    if (t) {
      let s = sub(t);
      if (d.serverApi && s.expiresAt - now < 86400) { const live = await fetchSubscriptionState(apiFor(d, d.bundleId)!, t.original_transaction_id).catch(() => null); if (live) { s = live.state; await saveState(d, t.id, s); } }
      const pt = paidThrough(s, now); if (pt && !trialRefused(t, s)) seat = { team: t.id, role: t.role, paidThrough: pt, status: s.status, trial: !!s.trial };
    }
    const best = cov[0];
    if (!best && !seat) return json({ covered: false, trialEligible: te, apps: APPS });
    const viaSeat = !!seat && (!best || best.plan !== "church");
    const pt = viaSeat ? seat!.paidThrough : best!.paidThrough;
    const token = await signEntitlement(d.signingKey, { v: 1, aud: FAMILY_AUDIENCE, team: viaSeat ? seat!.team : "", mem: uid, role: viaSeat ? seat!.role : best!.plan, exp: pt, iat: now, off: now + OFFLINE_CAP });
    return json({ covered: true, token, paidThrough: pt, offlineUntil: Math.min(pt, now + OFFLINE_CAP), plan: viaSeat ? "church" : best!.plan, source: viaSeat ? "team" : "purchase",
      coveredBy: viaSeat ? { team: seat!.team, role: seat!.role } : { app: appName(best!.bundleId), bundleId: best!.bundleId }, trial: viaSeat ? seat!.trial : best!.trial,
      subscriptions: cov.map((c) => ({ app: appName(c.bundleId), bundleId: c.bundleId, plan: c.plan, paidThrough: c.paidThrough })), trialEligible: te, apps: APPS });
  }

  if (m === "DELETE" && p === "/v1/account") {
    const t = await d.db.first<Team>("SELECT * FROM teams WHERE leader_user_id = ?", uid);
    if (t) { // leader: team dissolves; members lose seats. (The Apple subscription itself is cancelled in iOS Settings.)
      await d.db.run("DELETE FROM invites WHERE team_id = ?", t.id); await d.db.run("DELETE FROM members WHERE team_id = ?", t.id);
      await d.db.run("DELETE FROM seat_changes WHERE team_id = ?", t.id); await d.db.run("DELETE FROM teams WHERE id = ?", t.id);
    }
    await d.db.run("DELETE FROM purchases WHERE user_id = ?", uid);
    await d.db.run("DELETE FROM members WHERE user_id = ?", uid); await d.db.run("DELETE FROM rate_limits WHERE key = ?", "user:" + uid);
    await d.db.run("DELETE FROM users WHERE id = ?", uid);   // trial_history is kept on purpose (hashed key only; see 0002 migration)
    return json({ ok: true, deleted: true, teamDissolved: !!t });
  }
  throw new HttpError(404, "not_found");
}
