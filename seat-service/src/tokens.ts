// Session tokens (HMAC, 30 days) and device entitlement tokens (Ed25519, verified offline by the app).
import { HttpError, b64urlDecode, b64urlEncode, b64Decode, dec, enc, hmacHex } from "./util.ts";

export const SESSION_TTL = 30 * 86400;
export const OFFLINE_CAP = 30 * 86400;
export async function makeSession(secret: string, userId: string, now: number): Promise<string> {
  const body = b64urlEncode(JSON.stringify({ u: userId, exp: now + SESSION_TTL }));
  return "s1." + body + "." + (await hmacHex(secret, "session:" + body));
}
export async function readSession(secret: string, token: string, now: number): Promise<string> {
  const [v, body, mac] = token.split(".");
  if (v !== "s1" || !body || !mac) throw new HttpError(401, "bad_session");
  const want = await hmacHex(secret, "session:" + body);
  let diff = want.length ^ mac.length; for (let i = 0; i < want.length; i++) diff |= want.charCodeAt(i) ^ (mac.charCodeAt(i) | 0);
  if (diff) throw new HttpError(401, "bad_session");
  const p = JSON.parse(dec.decode(b64urlDecode(body))) as { u: string; exp: number };
  if (p.exp < now) throw new HttpError(401, "session_expired");
  return p.u;
}
// Ed25519 key: secret is base64 PKCS8; public raw 32 bytes is derived via JWK export.
export async function importSigningKey(pkcs8B64: string) { return await crypto.subtle.importKey("pkcs8", b64Decode(pkcs8B64), { name: "Ed25519" }, true, ["sign"]); }
export async function publicKeyB64url(key: CryptoKey): Promise<string> { const j = (await crypto.subtle.exportKey("jwk", key)) as JsonWebKey; return j.x as string; }
export interface Entitlement { v: 1; aud: string; team: string; mem: string; role: string; exp: number; iat: number; off: number }
// Token = "wr1." + b64url(payload) + "." + b64url(sig over the ASCII "wr1.<payload>")
export async function signEntitlement(key: CryptoKey, e: Entitlement): Promise<string> {
  const signed = "wr1." + b64urlEncode(JSON.stringify(e));
  const sig = new Uint8Array(await crypto.subtle.sign("Ed25519", key, enc.encode(signed)));
  return signed + "." + b64urlEncode(sig);
}
export async function verifyEntitlement(pubRaw: Uint8Array<ArrayBuffer>, token: string): Promise<Entitlement | null> {
  const i = token.lastIndexOf("."); if (i < 0) return null;
  const k = await crypto.subtle.importKey("raw", pubRaw, { name: "Ed25519" }, false, ["verify"]);
  if (!(await crypto.subtle.verify("Ed25519", k, b64urlDecode(token.slice(i + 1)), enc.encode(token.slice(0, i))))) return null;
  return JSON.parse(dec.decode(b64urlDecode(token.slice(4, i))));
}
