// App Store signed data (JWS, ES256, x5c chain) verification - transactions, renewal info, Server Notifications v2.
// Chain: leaf (OID 1.2.840.113635.100.6.11.1) <- WWDR intermediate (OID 1.2.840.113635.100.6.2.1) <- Apple Root CA - G3 (pinned).
import * as x509 from "@peculiar/x509";
import { HttpError, b64Decode, b64urlDecode, dec, enc } from "../util.ts";

x509.cryptoProvider.set(crypto as Crypto);
export const LEAF_OID = "1.2.840.113635.100.6.11.1";
export const INTERMEDIATE_OID = "1.2.840.113635.100.6.2.1";
export interface JwsConfig { rootCertsDer: Uint8Array[]; now?: () => number; checkOids?: boolean }

function sameBytes(a: ArrayBuffer, b: Uint8Array) { const x = new Uint8Array(a); if (x.length !== b.length) return false; for (let i = 0; i < x.length; i++) if (x[i] !== b[i]) return false; return true; }
function joseToDer(sig: Uint8Array<ArrayBuffer>): Uint8Array<ArrayBuffer> { return sig; } // WebCrypto ECDSA verify takes raw r||s (JOSE format) directly

export async function verifyAppleJws<T = Record<string, unknown>>(jws: string, cfg: JwsConfig): Promise<T> {
  const parts = jws.split("."); if (parts.length !== 3) throw new HttpError(400, "bad_jws");
  let header: { alg?: string; x5c?: string[] };
  try { header = JSON.parse(dec.decode(b64urlDecode(parts[0]))); } catch { throw new HttpError(400, "bad_jws"); }
  if (header.alg !== "ES256" || !Array.isArray(header.x5c) || header.x5c.length !== 3) throw new HttpError(400, "bad_jws_header");
  const certs = header.x5c.map((c) => new x509.X509Certificate(b64Decode(c)));
  const [leaf, inter, root] = certs;
  if (!cfg.rootCertsDer.some((r) => sameBytes(root.rawData, r))) throw new HttpError(400, "untrusted_root");
  const at = new Date((cfg.now ? cfg.now() : Date.now() / 1000) * 1000);
  for (const c of certs) if (at < c.notBefore || at > c.notAfter) throw new HttpError(400, "cert_expired");
  if (!(await inter.verify({ publicKey: root, signatureOnly: true }))) throw new HttpError(400, "bad_chain");
  if (!(await leaf.verify({ publicKey: inter, signatureOnly: true }))) throw new HttpError(400, "bad_chain");
  if (cfg.checkOids !== false) {
    if (!leaf.getExtension(LEAF_OID)) throw new HttpError(400, "bad_leaf_oid");
    if (!inter.getExtension(INTERMEDIATE_OID)) throw new HttpError(400, "bad_intermediate_oid");
  }
  const pub = await leaf.publicKey.export({ name: "ECDSA", namedCurve: "P-256" }, ["verify"]);
  const ok = await crypto.subtle.verify({ name: "ECDSA", hash: "SHA-256" }, pub, joseToDer(b64urlDecode(parts[2])), enc.encode(parts[0] + "." + parts[1]));
  if (!ok) throw new HttpError(400, "bad_jws_signature");
  try { return JSON.parse(dec.decode(b64urlDecode(parts[1]))) as T; } catch { throw new HttpError(400, "bad_jws_payload"); }
}
