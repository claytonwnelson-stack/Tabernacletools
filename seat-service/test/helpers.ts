// Test fixtures: fake Apple (SIWA RSA JWKS, App Store CA chain with Apple's OIDs, Server API), sqlite-backed Db.
import { DatabaseSync } from "node:sqlite";
import { readFileSync } from "node:fs";
import * as x509 from "@peculiar/x509";
import { b64urlEncode, enc } from "../src/util.ts";
import type { Db } from "../src/db.ts";
import type { Deps } from "../src/app.ts";
import { LEAF_OID, INTERMEDIATE_OID } from "../src/apple/jws.ts";
import { clearJwksCache } from "../src/apple/siwa.ts";
x509.cryptoProvider.set(crypto as Crypto);

export function sqliteDb(): Db {
  const s = new DatabaseSync(":memory:"); s.exec("PRAGMA foreign_keys = ON");
  for (const f of ["0001_init.sql", "0002_trial_history.sql", "0003_cross_app.sql"]) s.exec(readFileSync(new URL("../migrations/" + f, import.meta.url), "utf8"));
  return {
    async run(sql, ...p) { const r = s.prepare(sql).run(...(p as never[])); return { changes: Number(r.changes) }; },
    async first(sql, ...p) { return (s.prepare(sql).get(...(p as never[])) as never) ?? null; },
    async all(sql, ...p) { return s.prepare(sql).all(...(p as never[])) as never; },
  };
}
const EC = { name: "ECDSA", namedCurve: "P-256", hash: "SHA-256" } as const;
export async function makeChain(opts: { leafOid?: boolean } = {}) {
  const now = new Date(Date.now() - 86400_000), later = new Date(Date.now() + 365 * 86400_000);
  const rk = await crypto.subtle.generateKey(EC, true, ["sign", "verify"]) as CryptoKeyPair;
  const ik = await crypto.subtle.generateKey(EC, true, ["sign", "verify"]) as CryptoKeyPair;
  const lk = await crypto.subtle.generateKey(EC, true, ["sign", "verify"]) as CryptoKeyPair;
  const root = await x509.X509CertificateGenerator.createSelfSigned({ serialNumber: "01", name: "CN=Fake Apple Root", notBefore: now, notAfter: later, keys: rk, signingAlgorithm: EC, extensions: [new x509.BasicConstraintsExtension(true, undefined, true)] });
  const inter = await x509.X509CertificateGenerator.create({ serialNumber: "02", subject: "CN=Fake WWDR", issuer: root.subject, notBefore: now, notAfter: later, publicKey: ik.publicKey, signingKey: rk.privateKey, signingAlgorithm: EC,
    extensions: [new x509.BasicConstraintsExtension(true, 0, true), new x509.Extension(INTERMEDIATE_OID, false, new Uint8Array([5, 0]))] });
  const leafExt = opts.leafOid === false ? [] : [new x509.Extension(LEAF_OID, false, new Uint8Array([5, 0]))];
  const leaf = await x509.X509CertificateGenerator.create({ serialNumber: "03", subject: "CN=Fake StoreKit", issuer: inter.subject, notBefore: now, notAfter: later, publicKey: lk.publicKey, signingKey: ik.privateKey, signingAlgorithm: EC, extensions: leafExt });
  const x5c = [leaf, inter, root].map((c) => Buffer.from(c.rawData).toString("base64"));
  return { rootDer: new Uint8Array(root.rawData), x5c,
    async sign(payload: unknown): Promise<string> {
      const s = b64urlEncode(JSON.stringify({ alg: "ES256", x5c })) + "." + b64urlEncode(JSON.stringify(payload));
      return s + "." + b64urlEncode(new Uint8Array(await crypto.subtle.sign({ name: "ECDSA", hash: "SHA-256" }, lk.privateKey, enc.encode(s))));
    } };
}
export async function makeSiwa() {
  const kp = await crypto.subtle.generateKey({ name: "RSASSA-PKCS1-v1_5", modulusLength: 2048, publicExponent: new Uint8Array([1, 0, 1]), hash: "SHA-256" }, true, ["sign", "verify"]) as CryptoKeyPair;
  const jwk = await crypto.subtle.exportKey("jwk", kp.publicKey) as JsonWebKey;
  return { jwks: { keys: [{ kid: "testkid", kty: "RSA", alg: "RS256", use: "sig", n: jwk.n!, e: jwk.e! }] },
    async token(claims: Record<string, unknown>, kid = "testkid") {
      const now = Math.floor(Date.now() / 1000);
      const s = b64urlEncode(JSON.stringify({ alg: "RS256", kid })) + "." + b64urlEncode(JSON.stringify({ iss: "https://appleid.apple.com", aud: "com.tabernacletools.guitar", iat: now, exp: now + 600, sub: "000123.abc", ...claims }));
      return s + "." + b64urlEncode(new Uint8Array(await crypto.subtle.sign("RSASSA-PKCS1-v1_5", kp.privateKey, enc.encode(s))));
    } };
}
export const BUNDLE = "com.tabernacletools.guitar";
export const CHURCH = "com.tabernacletools.guitar.church.monthly";
export async function world() {
  clearJwksCache();
  const db = sqliteDb(), chain = await makeChain(), siwa = await makeSiwa();
  const clock = { t: Math.floor(Date.now() / 1000) };
  const apple = { calls: [] as string[], subs: new Map<string, { status: number; tx: Record<string, unknown>; renewal: Record<string, unknown> }>() };
  const fetcher = (async (input: string | URL | Request, init?: RequestInit) => {
    const u = String(input); apple.calls.push(u);
    if (u === "https://siwa.test/keys") return Response.json(siwa.jwks);
    const m = u.match(/\/inApps\/v1\/subscriptions\/(.+)$/);
    if (m) {
      const auth = new Headers(init?.headers).get("authorization") ?? ""; if (!auth.startsWith("Bearer ey")) return new Response("", { status: 401 });
      const s = apple.subs.get(decodeURIComponent(m[1])); if (!s) return new Response("", { status: 404 });
      return Response.json({ data: [{ subscriptionGroupIdentifier: "g", lastTransactions: [{ originalTransactionId: m[1], status: s.status, signedTransactionInfo: await chain.sign(s.tx), signedRenewalInfo: await chain.sign(s.renewal) }] }] });
    }
    return new Response("", { status: 404 });
  }) as typeof fetch;
  const ascKey = await crypto.subtle.generateKey({ name: "ECDSA", namedCurve: "P-256" }, true, ["sign"]) as CryptoKeyPair;
  const pem = "-----BEGIN PRIVATE KEY-----\n" + Buffer.from(await crypto.subtle.exportKey("pkcs8", ascKey.privateKey)).toString("base64") + "\n-----END PRIVATE KEY-----";
  const sk = await crypto.subtle.generateKey({ name: "Ed25519" }, true, ["sign", "verify"]) as CryptoKeyPair;
  const jws = { rootCertsDer: [chain.rootDer] };
  const deps: Deps = { db, now: () => clock.t, sessionSecret: "test-session-secret", subPepper: "test-pepper", signingKey: sk.privateKey, bundleId: BUNDLE,
    siwa: { audiences: [BUNDLE], jwksUrl: "https://siwa.test/keys", fetcher }, jws,
    serverApi: { issuerId: "iss", keyId: "KEY123", privateKeyPkcs8Pem: pem, bundleId: BUNDLE, environment: "Sandbox", baseUrl: "https://asc.test", fetcher, jws } };
  return { db, chain, siwa, clock, apple, deps, publicKey: sk.publicKey };
}
export function txFor(otid: string, expiresSec: number, extra: Record<string, unknown> = {}) {
  return { transactionId: otid + "-" + expiresSec, originalTransactionId: otid, bundleId: BUNDLE, productId: CHURCH, expiresDate: expiresSec * 1000, environment: "Sandbox", type: "Auto-Renewable Subscription", ...extra };
}
