// v0.13: one free trial per person (Sign in with Apple sub), kept across account deletion, hashed only.
import { test } from "node:test";
import assert from "node:assert/strict";
import { handle } from "../src/app.ts";
import { world, txFor, BUNDLE } from "./helpers.ts";

type W = Awaited<ReturnType<typeof world>>;
async function call(w: W, method: string, path: string, body?: unknown, session?: string) {
  const h: Record<string, string> = { "content-type": "application/json" }; if (session) h.authorization = "Bearer " + session;
  const r = await handle(new Request("https://seat.test" + path, { method, headers: h, body: body === undefined ? undefined : JSON.stringify(body) }), w.deps);
  return { status: r.status, body: (await r.json()) as Record<string, any> };
}
const login = async (w: W, sub: string) => (await call(w, "POST", "/v1/auth/apple", { identityToken: await w.siwa.token({ sub }) })).body.session as string;
async function team(w: W, s: string, otid: string, trial: boolean) {
  const exp = w.clock.t + 14 * 86400, tx = txFor(otid, exp, trial ? { offerType: 1 } : {});
  w.apple.subs.set(otid, { status: 1, tx, renewal: { originalTransactionId: otid, autoRenewStatus: 1 } });
  return call(w, "POST", "/v1/teams", { churchName: "Grace", signedTransaction: await w.chain.sign(tx) }, s);
}
async function notify(w: W, type: string, tx: Record<string, unknown>) {
  const payload = { notificationType: type, notificationUUID: crypto.randomUUID(), data: { bundleId: BUNDLE, environment: "Sandbox", signedTransactionInfo: await w.chain.sign(tx), signedRenewalInfo: await w.chain.sign({ originalTransactionId: tx.originalTransactionId, autoRenewStatus: 1 }) } };
  return call(w, "POST", "/v1/appstore/notifications", { signedPayload: await w.chain.sign(payload) });
}

test("fresh user is trial-eligible; trial team records it; entitlement reports trial + not eligible", async () => {
  const w = await world(); const s = await login(w, "p1");
  const e0 = await call(w, "POST", "/v1/entitlement", {}, s); assert.equal(e0.status, 404); assert.equal(e0.body.trialEligible, true);
  assert.equal((await team(w, s, "OT1", true)).status, 201);
  const e = await call(w, "POST", "/v1/entitlement", {}, s); assert.equal(e.status, 200); assert.equal(e.body.trial, true); assert.equal(e.body.trialEligible, false);
  const rows = await w.db.all<Record<string, string>>("SELECT * FROM trial_history"); assert.equal(rows.length, 1);
  assert.ok(!JSON.stringify(rows).includes("p1") && !JSON.stringify(rows).includes("OT1"), "only hashes stored");
});

test("delete account + sign in again: trial NOT usable twice, paid still works", async () => {
  const w = await world(); let s = await login(w, "p2");
  assert.equal((await team(w, s, "OT1", true)).status, 201);
  assert.equal((await call(w, "DELETE", "/v1/account", undefined, s)).status, 200);
  assert.equal((await w.db.all("SELECT * FROM trial_history")).length, 1, "kept after account deletion");
  s = await login(w, "p2");
  const e0 = await call(w, "POST", "/v1/entitlement", {}, s); assert.equal(e0.body.trialEligible, false);
  const r = await team(w, s, "OT2", true); assert.equal(r.status, 402); assert.equal(r.body.error, "trial_already_used");
  assert.equal((await team(w, s, "OT3", false)).status, 201, "paid subscription is fine");
});

test("same subscription (same otid) can rebuild its team during the trial", async () => {
  const w = await world(); let s = await login(w, "p3");
  assert.equal((await team(w, s, "OT1", true)).status, 201);
  await call(w, "DELETE", "/v1/account", undefined, s); s = await login(w, "p3");
  assert.equal((await team(w, s, "OT1", true)).status, 201);
});

test("different people each get one trial", async () => {
  const w = await world();
  assert.equal((await team(w, await login(w, "a"), "OA", true)).status, 201);
  assert.equal((await team(w, await login(w, "b"), "OB", true)).status, 201);
});

test("notification trial start: second trial for the same person gets no entitlement until paid", async () => {
  const w = await world(); let s = await login(w, "p4");
  assert.equal((await team(w, s, "OT1", true)).status, 201);
  await call(w, "DELETE", "/v1/account", undefined, s); s = await login(w, "p4");
  assert.equal((await team(w, s, "OT2", false)).status, 201);               // team made on a paid tx...
  const exp = w.clock.t + 14 * 86400;
  assert.equal((await notify(w, "SUBSCRIBED", txFor("OT2", exp, { offerType: 1 }))).status, 200);   // ...then Apple reports a trial on it
  const t = await w.db.first<{ trial_blocked: number; in_trial: number }>("SELECT trial_blocked, in_trial FROM teams"); assert.deepEqual({ ...t }, { trial_blocked: 1, in_trial: 1 });
  const e = await call(w, "POST", "/v1/entitlement", {}, s); assert.equal(e.status, 402); assert.equal(e.body.error, "trial_already_used");
  await notify(w, "DID_RENEW", txFor("OT2", exp + 30 * 86400));             // first paid month
  const e2 = await call(w, "POST", "/v1/entitlement", {}, s); assert.equal(e2.status, 200); assert.equal(e2.body.trial, false);
});

test("notification trial start on a first-time person records it", async () => {
  const w = await world(); const s = await login(w, "p5");
  assert.equal((await team(w, s, "OT1", false)).status, 201);
  await notify(w, "SUBSCRIBED", txFor("OT1", w.clock.t + 14 * 86400, { offerType: 1 }));
  assert.equal((await w.db.all("SELECT * FROM trial_history")).length, 1);
  const e = await call(w, "POST", "/v1/entitlement", {}, s); assert.equal(e.status, 200); assert.equal(e.body.trialEligible, false);
});
