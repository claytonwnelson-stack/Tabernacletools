// Sign in with Apple: server-side validation of the identity token (RS256 JWT) against Apple's published keys.
// Checks: signature (kid from https://appleid.apple.com/auth/keys), iss, aud (one of our bundle ids), exp, iat skew.
import { HttpError, b64urlDecode, dec, enc } from "../util.ts";

export const APPLE_ISSUER = "https://appleid.apple.com";
export const APPLE_JWKS_URL = "https://appleid.apple.com/auth/keys";
type Jwk = { kid: string; kty: string; alg?: string; n: string; e: string };
export interface SiwaConfig { audiences: string[]; jwksUrl?: string; fetcher?: typeof fetch; now?: () => number }

let cache: { at: number; keys: Jwk[]; url: string } | null = null;
export function clearJwksCache() { cache = null; }
async function keys(cfg: SiwaConfig, forceRefresh = false): Promise<Jwk[]> {
  const url = cfg.jwksUrl ?? APPLE_JWKS_URL; const now = Date.now();
  if (!forceRefresh && cache && cache.url === url && now - cache.at < 6 * 3600_000) return cache.keys;
  const r = await (cfg.fetcher ?? fetch)(url); if (!r.ok) throw new HttpError(503, "apple_keys_unavailable");
  const body = (await r.json()) as { keys: Jwk[] }; cache = { at: now, keys: body.keys ?? [], url }; return cache.keys;
}
export interface AppleIdentity { sub: string }
export async function verifyAppleIdentityToken(token: string, cfg: SiwaConfig): Promise<AppleIdentity> {
  const parts = token.split("."); if (parts.length !== 3) throw new HttpError(401, "bad_identity_token");
  let header: { alg?: string; kid?: string }, payload: Record<string, unknown>;
  try { header = JSON.parse(dec.decode(b64urlDecode(parts[0]))); payload = JSON.parse(dec.decode(b64urlDecode(parts[1]))); } catch { throw new HttpError(401, "bad_identity_token"); }
  if (header.alg !== "RS256" || !header.kid) throw new HttpError(401, "bad_identity_token");
  let jwk = (await keys(cfg)).find((k) => k.kid === header.kid);
  if (!jwk) jwk = (await keys(cfg, true)).find((k) => k.kid === header.kid); // Apple rotates keys
  if (!jwk) throw new HttpError(401, "unknown_apple_key");
  const key = await crypto.subtle.importKey("jwk", { kty: "RSA", n: jwk.n, e: jwk.e, alg: "RS256", ext: true }, { name: "RSASSA-PKCS1-v1_5", hash: "SHA-256" }, false, ["verify"]);
  const ok = await crypto.subtle.verify("RSASSA-PKCS1-v1_5", key, b64urlDecode(parts[2]), enc.encode(parts[0] + "." + parts[1]));
  if (!ok) throw new HttpError(401, "bad_identity_signature");
  const now = Math.floor((cfg.now ?? (() => Date.now() / 1000))());
  if (payload.iss !== APPLE_ISSUER) throw new HttpError(401, "bad_issuer");
  const aud = Array.isArray(payload.aud) ? payload.aud : [payload.aud];
  if (!aud.some((a) => typeof a === "string" && cfg.audiences.includes(a))) throw new HttpError(401, "bad_audience");
  if (typeof payload.exp !== "number" || payload.exp < now - 60) throw new HttpError(401, "identity_token_expired");
  if (typeof payload.iat === "number" && payload.iat > now + 300) throw new HttpError(401, "identity_token_future");
  if (typeof payload.sub !== "string" || !payload.sub) throw new HttpError(401, "bad_subject");
  return { sub: payload.sub };
}
