// Small helpers shared by the service. Runs on Cloudflare Workers and Node 24 (both have WebCrypto, fetch, TextEncoder).
export class HttpError extends Error {
  status: number; code: string;
  constructor(status: number, code: string, message?: string) { super(message ?? code); this.status = status; this.code = code; }
}
export const enc = new TextEncoder();
export const dec = new TextDecoder();

export function b64urlEncode(data: Uint8Array | string): string {
  const bytes = typeof data === "string" ? enc.encode(data) : data;
  let s = ""; for (let i = 0; i < bytes.length; i++) s += String.fromCharCode(bytes[i]);
  return btoa(s).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
}
export function b64urlDecode(s: string): Uint8Array<ArrayBuffer> {
  if (!/^[A-Za-z0-9_-]*$/.test(s)) throw new HttpError(400, "bad_base64");
  const b = atob(s.replace(/-/g, "+").replace(/_/g, "/") + "===".slice((s.length + 3) % 4));
  const out = new Uint8Array(b.length); for (let i = 0; i < b.length; i++) out[i] = b.charCodeAt(i); return out;
}
export function b64Decode(s: string): Uint8Array<ArrayBuffer> { const b = atob(s); const o = new Uint8Array(b.length); for (let i = 0; i < b.length; i++) o[i] = b.charCodeAt(i); return o; }
export function hex(buf: ArrayBuffer | Uint8Array): string { return [...new Uint8Array(buf)].map((x) => x.toString(16).padStart(2, "0")).join(""); }
export async function sha256Hex(s: string): Promise<string> { return hex(await crypto.subtle.digest("SHA-256", enc.encode(s))); }
export async function hmacHex(secret: string, msg: string): Promise<string> {
  const k = await crypto.subtle.importKey("raw", enc.encode(secret), { name: "HMAC", hash: "SHA-256" }, false, ["sign"]);
  return hex(await crypto.subtle.sign("HMAC", k, enc.encode(msg)));
}
export function json(body: unknown, status = 200, headers: Record<string, string> = {}): Response {
  return new Response(JSON.stringify(body), { status, headers: { "content-type": "application/json", "cache-control": "no-store", ...headers } });
}
export async function readJson(req: Request, maxBytes = 64 * 1024): Promise<Record<string, unknown>> {
  const t = await req.text(); if (t.length > maxBytes) throw new HttpError(413, "too_large");
  try { const v = JSON.parse(t || "{}"); if (typeof v !== "object" || v === null || Array.isArray(v)) throw 0; return v as Record<string, unknown>; } catch { throw new HttpError(400, "bad_json"); }
}
export function str(o: Record<string, unknown>, k: string, max = 4096, optional = false): string {
  const v = o[k]; if (v === undefined && optional) return "";
  if (typeof v !== "string" || v.length === 0 || v.length > max) throw new HttpError(400, "bad_field", k); return v;
}
// Crockford base32 without I L O U: easy to read aloud / type at rehearsal
const ALPHA = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
export function randomCode(len = 10): string {
  const r = crypto.getRandomValues(new Uint8Array(len)); let s = ""; for (const x of r) s += ALPHA[x & 31]; return s;
}
export function normaliseCode(c: string): string { return c.toUpperCase().replace(/[\s-]/g, "").replace(/O/g, "0").replace(/[IL]/g, "1"); }
export function randomId(prefix: string): string { return prefix + "_" + hex(crypto.getRandomValues(new Uint8Array(12))); }
