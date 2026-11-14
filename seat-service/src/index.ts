// Cloudflare Worker entry.
import { handle, type Deps } from "./app.ts";
import { d1Db } from "./db.ts";
import { importSigningKey } from "./tokens.ts";
import { b64Decode } from "./util.ts";
// Apple Root CA - G3, embedded (base64 DER) at build time by scripts/embed-root.mjs -> src/apple/rootG3.ts
import { APPLE_ROOT_G3_B64 } from "./apple/rootG3.ts";

export interface Env {
  DB: never; SESSION_SECRET: string; SUB_PEPPER: string; ENTITLEMENT_SIGNING_KEY: string;
  BUNDLE_ID: string; SIWA_AUDIENCES: string; APPSTORE_ENV: "Production" | "Sandbox";
  ASC_ISSUER_ID?: string; ASC_KEY_ID?: string; ASC_PRIVATE_KEY?: string;
}
let key: CryptoKey | null = null;
export default {
  async fetch(req: Request, env: Env): Promise<Response> {
    key ??= await importSigningKey(env.ENTITLEMENT_SIGNING_KEY);
    const jws = { rootCertsDer: [b64Decode(APPLE_ROOT_G3_B64)] };
    const deps: Deps = {
      db: d1Db(env.DB), now: () => Math.floor(Date.now() / 1000), sessionSecret: env.SESSION_SECRET, subPepper: env.SUB_PEPPER, signingKey: key,
      bundleId: env.BUNDLE_ID, siwa: { audiences: env.SIWA_AUDIENCES.split(",").map((s) => s.trim()) }, jws,
      serverApi: env.ASC_PRIVATE_KEY && env.ASC_KEY_ID && env.ASC_ISSUER_ID ? { issuerId: env.ASC_ISSUER_ID, keyId: env.ASC_KEY_ID, privateKeyPkcs8Pem: env.ASC_PRIVATE_KEY, bundleId: env.BUNDLE_ID, environment: env.APPSTORE_ENV, jws } : null,
    };
    return handle(req, deps);
  },
};
