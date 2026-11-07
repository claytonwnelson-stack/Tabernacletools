// v0.18 one subscription unlocks every Tabernacle Tools app
import { test } from "node:test";
import assert from "node:assert/strict";
import { handle } from "../src/app.ts";
import { verifyEntitlement } from "../src/tokens.ts";
import { world, txFor } from "./helpers.ts";
import { FAMILY_AUDIENCE, productId } from "../src/catalog.ts";

type W = Awaited<ReturnType<typeof world>>;
const KEYS = "com.tabernacletools.keys", GUITAR = "com.tabernacletools.guitar", BASS = "com.tabernacletools.bass";
async function call(w: W, method: string, path: string, body?: unknown, session?: string) {
  const h: Record<string, string> = { "content-type": "application/json" }; if (session) h.authorization = "Bearer " + session;
  const r = await handle(new Request("https://seat.test" + path, { method, headers: h, body: body === undefined ? undefined : JSON.stringify(body) }), w.deps);
  return { status: r.status, body: (await r.json()) as Record<string, any> };
}
async function login(w: W, sub: string) { return (await call(w, "POST", "/v1/auth/apple", { identityToken: await w.siwa.token({ sub }) })).body.session as string; }
function appTx(w: W, otid: string, bundle: string, plan: "individual" | "church", extra: Record<string, unknown> = {}, days = 20) {
  const exp = w.clock.t + days * 86400; const tx = txFor(otid, exp, { bundleId: bundle, productId: productId(bundle, plan), ...extra });
  w.apple.subs.set(otid, { status: 1, tx, renewal: { originalTransactionId: otid, autoRenewStatus: 1 } }); return tx;
}
async function post(w: W, s: string, tx: Record<string, unknown>) { return call(w, "POST", "/v1/purchases", { signedTransaction: await w.chain.sign(tx) }, s); }

test("buy Individual in Keys -> Guitar/Bass read a family token; not covered before", async () => {
  const w = await world(); const s = await login(w, "p1");
  let e = await call(w, "GET", "/v1/entitlement", undefined, s); assert.equal(e.status, 200); assert.equal(e.body.covered, false); assert.equal(e.body.trialEligible, true); assert.equal(e.body.apps.length, 4);
  const r = await post(w, s, appTx(w, "K1", KEYS, "individual")); assert.equal(r.status, 201, JSON.stringify(r.body)); assert.equal(r.body.plan, "individual"); assert.equal(r.body.duplicate, null);
  e = await call(w, "GET", "/v1/entitlement", undefined, s);
  assert.equal(e.body.covered, true); assert.equal(e.body.plan, "individual"); assert.equal(e.body.coveredBy.app, "Tabernacle Tools - Keys");
  const raw = new Uint8Array(await crypto.subtle.exportKey("raw", w.publicKey)); const c = (await verifyEntitlement(raw, e.body.token))!;
  assert.equal(c.aud, FAMILY_AUDIENCE); assert.equal(c.role, "individual"); assert.equal(c.team, "");
  // re-posting (restore / renewal) is idempotent
  assert.equal((await post(w, s, appTx(w, "K1", KEYS, "individual"))).status, 200);
});

test("same subscription can't unlock a second person; unknown products/apps refused", async () => {
  const w = await world(); const a = await login(w, "a"), b = await login(w, "b"); const tx = appTx(w, "K2", KEYS, "individual");
  assert.equal((await post(w, a, tx)).status, 201);
  assert.equal((await post(w, b, tx)).status, 409);
  assert.equal((await post(w, a, { ...tx, originalTransactionId: "X", bundleId: "com.other.app" })).status, 400);
  assert.equal((await post(w, a, { ...tx, originalTransactionId: "Y", productId: KEYS + ".lifetime" })).status, 400);
});

test("buying again in another app is flagged as a duplicate (avoid paying twice)", async () => {
  const w = await world(); const s = await login(w, "dup");
  await post(w, s, appTx(w, "G1", GUITAR, "individual"));
  const r = await post(w, s, appTx(w, "B1", BASS, "individual")); assert.equal(r.body.duplicate.app, "Tabernacle Tools - Electric Guitar");
  // a Church upgrade over an Individual isn't a duplicate, and Church wins
  const c = await post(w, s, appTx(w, "B2", BASS, "church")); assert.equal(c.body.duplicate, null);
  assert.equal((await call(w, "GET", "/v1/entitlement", undefined, s)).body.plan, "church");
});

test("one free trial per person across all apps", async () => {
  const w = await world(); const s = await login(w, "trial");
  const t1 = await post(w, s, appTx(w, "T1", GUITAR, "individual", { offerType: 1 })); assert.equal(t1.body.trialOk, true);
  let e = await call(w, "GET", "/v1/entitlement", undefined, s); assert.equal(e.body.covered, true); assert.equal(e.body.trial, true); assert.equal(e.body.trialEligible, false);
  // Guitar trial lapses; a second trial started in Keys gives no access until it is paid
  w.apple.subs.delete("T1"); w.clock.t += 21 * 86400;
  const t2 = await post(w, s, appTx(w, "T2", KEYS, "individual", { offerType: 1 })); assert.equal(t2.body.trialOk, false);
  e = await call(w, "GET", "/v1/entitlement", undefined, s); assert.equal(e.body.covered, false);
  // first paid month of T2 -> covered
  const paid = await post(w, s, appTx(w, "T2", KEYS, "individual")); assert.equal(paid.status, 200);
  e = await call(w, "GET", "/v1/entitlement", undefined, s); assert.equal(e.body.covered, true); assert.equal(e.body.trial, false);
});

test("notifications from any app: renewal, expiry, refund", async () => {
  const w = await world(); const s = await login(w, "n"); const tx = appTx(w, "N1", KEYS, "individual"); await post(w, s, tx); w.deps.serverApi = null;
  const notify = async (type: string, t: Record<string, unknown>) => call(w, "POST", "/v1/appstore/notifications", { signedPayload: await w.chain.sign({ notificationType: type, notificationUUID: crypto.randomUUID(), data: { bundleId: KEYS, signedTransactionInfo: await w.chain.sign(t) } }) });
  const later = txFor("N1", w.clock.t + 50 * 86400, { bundleId: KEYS, productId: productId(KEYS, "individual") });
  assert.equal((await notify("DID_RENEW", later)).status, 200);
  assert.equal((await call(w, "GET", "/v1/entitlement", undefined, s)).body.paidThrough, w.clock.t + 50 * 86400);
  await notify("REFUND", later);
  assert.equal((await call(w, "GET", "/v1/entitlement", undefined, s)).body.covered, false);
});

test("account delete removes purchases, keeps trial_history", async () => {
  const w = await world(); const s = await login(w, "del");
  await post(w, s, appTx(w, "D1", BASS, "individual", { offerType: 1 }));
  assert.equal((await call(w, "DELETE", "/v1/account", undefined, s)).status, 200);
  assert.equal((await w.db.first<{ n: number }>("SELECT COUNT(*) AS n FROM purchases"))!.n, 0);
  assert.equal((await w.db.first<{ n: number }>("SELECT COUNT(*) AS n FROM trial_history"))!.n, 1);
  const s2 = await login(w, "del"); assert.equal((await call(w, "GET", "/v1/entitlement", undefined, s2)).body.trialEligible, false);
});
