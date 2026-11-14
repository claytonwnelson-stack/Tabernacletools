import { test } from "node:test";
import assert from "node:assert/strict";
import { handle, SEAT_CAP } from "../src/app.ts";
import { verifyEntitlement, OFFLINE_CAP } from "../src/tokens.ts";
import { verifyAppleJws } from "../src/apple/jws.ts";
import { makeChain } from "./helpers.ts";
import { world, txFor, CHURCH, BUNDLE } from "./helpers.ts";
import { b64urlDecode } from "../src/util.ts";

type W = Awaited<ReturnType<typeof world>>;
async function call(w: W, method: string, path: string, body?: unknown, session?: string) {
  const h: Record<string, string> = { "content-type": "application/json" }; if (session) h.authorization = "Bearer " + session;
  const r = await handle(new Request("https://seat.test" + path, { method, headers: h, body: body === undefined ? undefined : JSON.stringify(body) }), w.deps);
  return { status: r.status, body: (await r.json()) as Record<string, any> };
}
async function login(w: W, sub: string) { const r = await call(w, "POST", "/v1/auth/apple", { identityToken: await w.siwa.token({ sub }) }); assert.equal(r.status, 200, JSON.stringify(r.body)); return r.body.session as string; }
async function leaderWithTeam(w: W, otid = "OT1") {
  const exp = w.clock.t + 20 * 86400; w.apple.subs.set(otid, { status: 1, tx: txFor(otid, exp), renewal: { originalTransactionId: otid, autoRenewStatus: 1 } });
  const s = await login(w, "leader-" + otid);
  const r = await call(w, "POST", "/v1/teams", { churchName: "Grace Chapel", displayName: "Clayton", signedTransaction: await w.chain.sign(txFor(otid, exp)) }, s);
  assert.equal(r.status, 201, JSON.stringify(r.body)); return { s, exp };
}
async function invite(w: W, s: string) { const r = await call(w, "POST", "/v1/teams/me/invites", {}, s); assert.equal(r.status, 201, JSON.stringify(r.body)); return r.body.code as string; }
async function notify(w: W, type: string, subtype: string | undefined, tx: Record<string, unknown>, renewal: Record<string, unknown> = {}, uuid = crypto.randomUUID()) {
  const payload = { notificationType: type, subtype, notificationUUID: uuid, data: { bundleId: BUNDLE, environment: "Sandbox", signedTransactionInfo: await w.chain.sign(tx), signedRenewalInfo: await w.chain.sign({ originalTransactionId: tx.originalTransactionId, ...renewal }) } };
  return call(w, "POST", "/v1/appstore/notifications", { signedPayload: await w.chain.sign(payload) });
}

test("health and keys", async () => { const w = await world(); assert.equal((await call(w, "GET", "/healthz")).status, 200); const k = await call(w, "GET", "/v1/keys"); assert.equal(k.body.keys[0].x.length, 43); });

test("SIWA: valid token makes a user, stores only HMAC of sub", async () => {
  const w = await world(); await login(w, "000999.secret"); await login(w, "000999.secret");
  const rows = await w.db.all<{ apple_sub_hmac: string }>("SELECT apple_sub_hmac FROM users"); assert.equal(rows.length, 1); assert.ok(!rows[0].apple_sub_hmac.includes("000999"));
});
test("SIWA: rejects wrong aud, iss, expired, bad signature, unknown kid", async () => {
  const w = await world();
  for (const claims of [{ aud: "com.other" }, { iss: "https://evil" }, { exp: w.clock.t - 3600 }]) assert.equal((await call(w, "POST", "/v1/auth/apple", { identityToken: await w.siwa.token(claims) })).status, 401);
  const t = await w.siwa.token({}); const bad = t.slice(0, -4) + (t.endsWith("AAAA") ? "BBBB" : "AAAA");
  assert.equal((await call(w, "POST", "/v1/auth/apple", { identityToken: bad })).status, 401);
  assert.equal((await call(w, "POST", "/v1/auth/apple", { identityToken: await w.siwa.token({}, "nokid") })).status, 401);
  const other = await (await import("./helpers.ts")).makeSiwa(); assert.equal((await call(w, "POST", "/v1/auth/apple", { identityToken: await other.token({}) })).status, 401);
});
test("auth required, garbage sessions rejected", async () => {
  const w = await world(); assert.equal((await call(w, "GET", "/v1/teams/me")).status, 401); assert.equal((await call(w, "GET", "/v1/teams/me", undefined, "s1.abc.def")).status, 401);
});

test("JWS: chain, root pinning, OIDs, signature", async () => {
  const w = await world(); const tx = txFor("X", w.clock.t + 99);
  assert.equal((await verifyAppleJws<any>(await w.chain.sign(tx), w.deps.jws)).originalTransactionId, "X");
  const other = await makeChain(); await assert.rejects(verifyAppleJws(await other.sign(tx), w.deps.jws), /untrusted_root/);
  const noOid = await makeChain({ leafOid: false }); await assert.rejects(verifyAppleJws(await noOid.sign(tx), { rootCertsDer: [noOid.rootDer] }), /bad_leaf_oid/);
  const good = await w.chain.sign(tx); const [h, , s] = good.split("."); const forged = h + "." + Buffer.from(JSON.stringify({ ...tx, expiresDate: 9e15 })).toString("base64url") + "." + s;
  await assert.rejects(verifyAppleJws(forged, w.deps.jws), /bad_jws_signature/);
  await assert.rejects(verifyAppleJws(good, { ...w.deps.jws, now: () => w.clock.t + 400 * 86400 }), /cert_expired/);
});
test("real Apple Root CA G3 is embedded and parses", async () => {
  const { APPLE_ROOT_G3_B64 } = await import("../src/apple/rootG3.ts"); const der = Buffer.from(APPLE_ROOT_G3_B64, "base64");
  const fp = Buffer.from(await crypto.subtle.digest("SHA-256", der)).toString("hex"); assert.equal(fp, "63343abfb89a6a03ebb57e9b3f5fa7be7c4f5c756f3017b3a8c488c3653e9179");
});

test("team creation: leader only once, church product only, calls Server API", async () => {
  const w = await world(); const { s } = await leaderWithTeam(w);
  assert.ok(w.apple.calls.some((c) => c.startsWith("https://asc.test/inApps/v1/subscriptions/OT1")));
  const me = await call(w, "GET", "/v1/teams/me", undefined, s); assert.equal(me.body.role, "leader"); assert.equal(me.body.members.length, 1); assert.equal(me.body.team.seatCap, 15);
  assert.equal((await call(w, "POST", "/v1/teams", { churchName: "x", signedTransaction: await w.chain.sign(txFor("OT1", w.clock.t + 99)) }, s)).status, 409);
  const s2 = await login(w, "other"); const ind = txFor("OT9", w.clock.t + 999, { productId: "com.tabernacletools.guitar.individual.monthly" });
  assert.equal((await call(w, "POST", "/v1/teams", { churchName: "x", signedTransaction: await w.chain.sign(ind) }, s2)).body.error, "not_church_plan");
  assert.equal((await call(w, "POST", "/v1/teams", { churchName: "x", signedTransaction: await w.chain.sign(txFor("OT1", w.clock.t + 99)) }, s2)).body.error, "subscription_already_has_team");
  assert.equal((await call(w, "POST", "/v1/teams", { churchName: "x", signedTransaction: await w.chain.sign(txFor("OT404", w.clock.t + 99)) }, s2)).status, 402);
  w.apple.subs.set("OTR", { status: 5, tx: txFor("OTR", w.clock.t + 999, { revocationDate: 1 }), renewal: {} });
  assert.equal((await call(w, "POST", "/v1/teams", { churchName: "x", signedTransaction: await w.chain.sign(txFor("OTR", w.clock.t + 999)) }, s2)).body.error, "subscription_inactive");
  assert.equal((await call(w, "POST", "/v1/teams", { churchName: "x", signedTransaction: await w.chain.sign({ ...txFor("OT5", w.clock.t + 999), bundleId: "evil" }) }, s2)).body.error, "wrong_bundle");
});

test("invites: single use, expiry, normalisation, revoke, member can't invite", async () => {
  const w = await world(); const { s } = await leaderWithTeam(w); const code = await invite(w, s); assert.match(code, /^[0-9A-Z]{5}-[0-9A-Z]{5}$/);
  const a = await login(w, "a"), b = await login(w, "b");
  assert.equal((await call(w, "POST", "/v1/invites/redeem", { code: " " + code.toLowerCase() + " ", displayName: "Amy" }, a)).status, 201);
  assert.equal((await call(w, "POST", "/v1/invites/redeem", { code }, b)).status, 404); // used
  assert.equal((await call(w, "POST", "/v1/teams/me/invites", {}, a)).status, 403);
  const c2 = await invite(w, s); w.clock.t += 8 * 86400; // past 7-day expiry (sub still paid: 20 days)
  assert.equal((await call(w, "POST", "/v1/invites/redeem", { code: c2 }, b)).body.error, "invalid_or_expired_code");
  const c3 = await invite(w, s); assert.equal((await call(w, "DELETE", "/v1/teams/me/invites/" + c3, undefined, s)).body.revoked, true);
  assert.equal((await call(w, "POST", "/v1/invites/redeem", { code: c3 }, b)).status, 404);
  assert.equal((await call(w, "POST", "/v1/invites/redeem", { code: await invite(w, s) }, a)).body.error, "already_in_team");
});

test("15-seat cap includes the leader", async () => {
  const w = await world(); const { s } = await leaderWithTeam(w); const codes: string[] = [];
  for (let i = 0; i < SEAT_CAP; i++) codes.push(await invite(w, s)); // invites may outnumber seats; joining enforces the cap
  for (let i = 0; i < SEAT_CAP - 1; i++) { const m = await login(w, "m" + i); assert.equal((await call(w, "POST", "/v1/invites/redeem", { code: codes[i] }, m)).status, 201); }
  const me = await call(w, "GET", "/v1/teams/me", undefined, s); assert.equal(me.body.members.length, 15);
  const extra = await login(w, "extra"); assert.equal((await call(w, "POST", "/v1/invites/redeem", { code: codes[14] }, extra)).body.error, "team_full");
  assert.equal((await call(w, "POST", "/v1/teams/me/invites", {}, s)).body.error, "team_full");
  // removing someone frees a seat
  const victim = me.body.members.find((x: any) => x.role === "member").id;
  assert.equal((await call(w, "DELETE", "/v1/teams/me/members/" + victim, undefined, s)).status, 200);
  assert.equal((await call(w, "POST", "/v1/invites/redeem", { code: codes[14] }, extra)).status, 201);
});

test("leave / remove rules", async () => {
  const w = await world(); const { s } = await leaderWithTeam(w); const a = await login(w, "a");
  await call(w, "POST", "/v1/invites/redeem", { code: await invite(w, s) }, a);
  assert.equal((await call(w, "POST", "/v1/teams/me/leave", {}, s)).body.error, "leader_cannot_leave");
  const me = await call(w, "GET", "/v1/teams/me", undefined, a); const leaderId = me.body.members.find((x: any) => x.role === "leader").id;
  assert.equal((await call(w, "DELETE", "/v1/teams/me/members/" + leaderId, undefined, a)).status, 403);
  assert.equal((await call(w, "DELETE", "/v1/teams/me/members/" + leaderId, undefined, s)).status, 409);
  assert.equal((await call(w, "POST", "/v1/teams/me/leave", {}, a)).status, 200);
  assert.equal((await call(w, "GET", "/v1/teams/me", undefined, a)).status, 404);
  assert.equal((await call(w, "POST", "/v1/entitlement", {}, a)).status, 404);
});

test("entitlement token: Ed25519, paid-through, 30-day offline cap", async () => {
  const w = await world(); const { s, exp } = await leaderWithTeam(w); const a = await login(w, "a");
  await call(w, "POST", "/v1/invites/redeem", { code: await invite(w, s) }, a);
  const r = await call(w, "POST", "/v1/entitlement", {}, a); assert.equal(r.status, 200);
  const raw = new Uint8Array(await crypto.subtle.exportKey("raw", w.publicKey));
  const e = (await verifyEntitlement(raw, r.body.token))!; assert.equal(e.exp, exp); assert.equal(e.off, w.clock.t + OFFLINE_CAP); assert.equal(e.role, "member"); assert.equal(e.aud, "com.tabernacletools");
  assert.equal(await verifyEntitlement(raw, r.body.token.replace("wr1.e", "wr1.f")), null);
  assert.equal(r.body.offlineUntil, Math.min(exp, w.clock.t + OFFLINE_CAP));
});

test("notifications: renewal extends, grace, refund revokes, idempotent, out-of-order ignored", async () => {
  const w = await world(); let { s, exp } = await leaderWithTeam(w);
  const n1 = exp + 30 * 86400; const uuid = crypto.randomUUID();
  assert.equal((await notify(w, "DID_RENEW", undefined, txFor("OT1", n1), {}, uuid)).status, 200);
  assert.equal((await notify(w, "DID_RENEW", undefined, txFor("OT1", n1), {}, uuid)).body.duplicate, true);
  assert.equal((await call(w, "GET", "/v1/teams/me", undefined, s)).body.team.paidThrough, n1);
  await notify(w, "DID_RENEW", undefined, txFor("OT1", exp)); // stale, older period
  assert.equal((await call(w, "GET", "/v1/teams/me", undefined, s)).body.team.paidThrough, n1);
  // billing fails at n1 with a 16-day grace period
  w.clock.t = n1 - 3600; const g = n1 + 16 * 86400;
  await notify(w, "DID_FAIL_TO_RENEW", "GRACE_PERIOD", txFor("OT1", n1), { gracePeriodExpiresDate: g * 1000, isInBillingRetryPeriod: true });
  w.apple.subs.set("OT1", { status: 4, tx: txFor("OT1", n1), renewal: { originalTransactionId: "OT1", gracePeriodExpiresDate: g * 1000 } });
  w.clock.t = n1 + 86400; s = await login(w, "leader-OT1"); // sessions last 30 days
  const ent = await call(w, "POST", "/v1/entitlement", {}, s); assert.equal(ent.status, 200); assert.equal(ent.body.status, "grace"); assert.equal(ent.body.paidThrough, g);
  await notify(w, "GRACE_PERIOD_EXPIRED", undefined, txFor("OT1", n1));
  w.apple.subs.set("OT1", { status: 3, tx: txFor("OT1", n1), renewal: { originalTransactionId: "OT1", isInBillingRetryPeriod: true } });
  assert.equal((await call(w, "POST", "/v1/entitlement", {}, s)).status, 402);
  // recovered, then refunded
  const n2 = w.clock.t + 30 * 86400; await notify(w, "DID_RENEW", "BILLING_RECOVERY", txFor("OT1", n2));
  w.apple.subs.set("OT1", { status: 1, tx: txFor("OT1", n2), renewal: {} });
  assert.equal((await call(w, "POST", "/v1/entitlement", {}, s)).status, 200);
  await notify(w, "REFUND", undefined, txFor("OT1", n2, { revocationDate: Date.now() }));
  assert.equal((await call(w, "POST", "/v1/entitlement", {}, s)).status, 402);
  assert.equal((await call(w, "POST", "/v1/teams/me/invites", {}, s)).status, 402);
  await notify(w, "REFUND_REVERSED", undefined, txFor("OT1", n2));
  assert.equal((await call(w, "GET", "/v1/teams/me", undefined, s)).body.team.status, "active");
  await notify(w, "REVOKE", undefined, txFor("OT1", n2)); assert.equal((await call(w, "GET", "/v1/teams/me", undefined, s)).body.team.status, "revoked");
});
test("notifications: forged / foreign rejected", async () => {
  const w = await world(); const other = await makeChain();
  const forged = await other.sign({ notificationType: "DID_RENEW", notificationUUID: "x", data: {} });
  assert.equal((await call(w, "POST", "/v1/appstore/notifications", { signedPayload: forged })).status, 400);
  const foreign = await w.chain.sign({ notificationType: "TEST", notificationUUID: "y", data: { bundleId: "com.other" } });
  assert.equal((await call(w, "POST", "/v1/appstore/notifications", { signedPayload: foreign })).body.error, "wrong_bundle");
});
test("entitlement re-checks Apple near expiry (missed webhook)", async () => {
  const w = await world(); const { exp } = await leaderWithTeam(w);
  w.clock.t = exp + 60; const s = await login(w, "leader-OT1"); const n = exp + 30 * 86400; w.apple.subs.set("OT1", { status: 1, tx: txFor("OT1", n), renewal: {} });
  const r = await call(w, "POST", "/v1/entitlement", {}, s); assert.equal(r.status, 200); assert.equal(r.body.paidThrough, n);
});

test("rate limiting: redeem brute force and auth", async () => {
  const w = await world(); const a = await login(w, "a"); let last = 0;
  for (let i = 0; i < 12; i++) last = (await call(w, "POST", "/v1/invites/redeem", { code: "AAAAAAAAAA" }, a)).status;
  assert.equal(last, 429);
  for (let i = 0; i < 25; i++) last = (await call(w, "POST", "/v1/auth/apple", { identityToken: "x.y.z" })).status; assert.equal(last, 429);
});

test("account deletion: member and leader", async () => {
  const w = await world(); const { s } = await leaderWithTeam(w); const a = await login(w, "a");
  await call(w, "POST", "/v1/invites/redeem", { code: await invite(w, s) }, a); await invite(w, s);
  assert.equal((await call(w, "DELETE", "/v1/account", undefined, a)).body.teamDissolved, false);
  assert.equal((await call(w, "GET", "/v1/teams/me", undefined, a)).status, 401);
  const b = await login(w, "b"); await call(w, "POST", "/v1/invites/redeem", { code: await invite(w, s) }, b);
  assert.equal((await call(w, "DELETE", "/v1/account", undefined, s)).body.teamDissolved, true);
  for (const t of ["teams", "invites", "members"]) assert.equal((await w.db.first<{ n: number }>(`SELECT COUNT(*) n FROM ${t}`))!.n, 0, t);
  assert.equal((await w.db.first<{ n: number }>("SELECT COUNT(*) n FROM users"))!.n, 1); // only b remains, seatless
  assert.equal((await call(w, "POST", "/v1/entitlement", {}, b)).status, 404);
});
test("sessions expire after 30 days", async () => {
  const w = await world(); const s = await login(w, "z"); w.clock.t += 31 * 86400;
  assert.equal((await call(w, "GET", "/v1/teams/me", undefined, s)).body.error, "session_expired");
});
