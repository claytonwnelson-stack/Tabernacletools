// App Store Server API (status lookups) + Notifications v2 decoding, and mapping Apple's data to our team status.
import { HttpError, b64urlEncode, b64Decode, enc } from "../util.ts";
import { verifyAppleJws, type JwsConfig } from "./jws.ts";

export interface TransactionInfo { transactionId: string; originalTransactionId: string; bundleId: string; productId: string; expiresDate?: number; revocationDate?: number; environment: string; type?: string; offerType?: number }
export interface RenewalInfo { originalTransactionId: string; autoRenewStatus?: number; gracePeriodExpiresDate?: number; isInBillingRetryPeriod?: boolean; expirationIntent?: number }
export type TeamStatus = "active" | "grace" | "billing_retry" | "expired" | "revoked";
export interface SubState { status: TeamStatus; expiresAt: number; graceUntil: number; trial?: boolean }
// offerType 1 = introductory offer (our 2-week free trial). Apple: https://developer.apple.com/documentation/appstoreserverapi/offertype
export const isTrialTx = (tx: TransactionInfo | null | undefined) => !!tx && tx.offerType === 1;

// Apple's status codes (Get All Subscription Statuses): 1 active, 2 expired, 3 billing retry, 4 grace period, 5 revoked.
export function stateFrom(tx: TransactionInfo, renewal: RenewalInfo | null, appleStatus?: number): SubState {
  const s = stateFromInner(tx, renewal, appleStatus); s.trial = isTrialTx(tx); return s;
}
function stateFromInner(tx: TransactionInfo, renewal: RenewalInfo | null, appleStatus?: number): SubState {
  const expiresAt = Math.floor((tx.expiresDate ?? 0) / 1000), graceUntil = Math.floor((renewal?.gracePeriodExpiresDate ?? 0) / 1000);
  if (tx.revocationDate || appleStatus === 5) return { status: "revoked", expiresAt, graceUntil: 0 };
  if (appleStatus === 4 || (graceUntil > expiresAt)) return { status: "grace", expiresAt, graceUntil };
  if (appleStatus === 3 || renewal?.isInBillingRetryPeriod) return { status: "billing_retry", expiresAt, graceUntil: 0 };
  if (appleStatus === 2) return { status: "expired", expiresAt, graceUntil: 0 };
  return { status: "active", expiresAt, graceUntil };
}
// paid-through = when a seat stops working (end of period, or end of grace). 0 = no access.
export function paidThrough(s: SubState, now: number): number {
  if (s.status === "revoked" || s.status === "billing_retry" || s.status === "expired") return 0;
  const t = s.status === "grace" ? Math.max(s.expiresAt, s.graceUntil) : s.expiresAt; return t > now ? t : 0;
}

export interface ServerApiConfig { issuerId: string; keyId: string; privateKeyPkcs8Pem: string; bundleId: string; environment: "Production" | "Sandbox"; baseUrl?: string; fetcher?: typeof fetch; jws: JwsConfig }
async function apiToken(cfg: ServerApiConfig): Promise<string> {
  const pem = cfg.privateKeyPkcs8Pem.replace(/-----[^-]+-----/g, "").replace(/\s+/g, "");
  const key = await crypto.subtle.importKey("pkcs8", b64Decode(pem), { name: "ECDSA", namedCurve: "P-256" }, false, ["sign"]);
  const now = Math.floor(Date.now() / 1000);
  const h = b64urlEncode(JSON.stringify({ alg: "ES256", kid: cfg.keyId, typ: "JWT" }));
  const p = b64urlEncode(JSON.stringify({ iss: cfg.issuerId, iat: now, exp: now + 1200, aud: "appstoreconnect-v1", bid: cfg.bundleId }));
  const sig = new Uint8Array(await crypto.subtle.sign({ name: "ECDSA", hash: "SHA-256" }, key, enc.encode(h + "." + p)));
  return h + "." + p + "." + b64urlEncode(sig);
}
export function serverApiBase(cfg: ServerApiConfig) { return cfg.baseUrl ?? (cfg.environment === "Production" ? "https://api.storekit.itunes.apple.com" : "https://api.storekit-sandbox.itunes.apple.com"); }
// GET /inApps/v1/subscriptions/{originalTransactionId} -> latest signed tx + renewal + status for our group
export async function fetchSubscriptionState(cfg: ServerApiConfig, originalTransactionId: string): Promise<{ tx: TransactionInfo; state: SubState } | null> {
  const r = await (cfg.fetcher ?? fetch)(`${serverApiBase(cfg)}/inApps/v1/subscriptions/${encodeURIComponent(originalTransactionId)}`, { headers: { authorization: "Bearer " + (await apiToken(cfg)) } });
  if (r.status === 404) return null; if (!r.ok) throw new HttpError(502, "appstore_api_error", String(r.status));
  const body = (await r.json()) as { data?: { lastTransactions?: { status: number; signedTransactionInfo: string; signedRenewalInfo: string }[] }[] };
  for (const g of body.data ?? []) for (const lt of g.lastTransactions ?? []) {
    const tx = await verifyAppleJws<TransactionInfo>(lt.signedTransactionInfo, cfg.jws);
    if (tx.originalTransactionId !== originalTransactionId) continue;
    const ren = lt.signedRenewalInfo ? await verifyAppleJws<RenewalInfo>(lt.signedRenewalInfo, cfg.jws) : null;
    return { tx, state: stateFrom(tx, ren, lt.status) };
  }
  return null;
}
export interface DecodedNotification { notificationType: string; subtype?: string; notificationUUID: string; tx: TransactionInfo | null; renewal: RenewalInfo | null; bundleId?: string; environment?: string }
export async function decodeNotification(signedPayload: string, jws: JwsConfig): Promise<DecodedNotification> {
  const p = await verifyAppleJws<{ notificationType: string; subtype?: string; notificationUUID: string; data?: { bundleId?: string; environment?: string; signedTransactionInfo?: string; signedRenewalInfo?: string } }>(signedPayload, jws);
  const tx = p.data?.signedTransactionInfo ? await verifyAppleJws<TransactionInfo>(p.data.signedTransactionInfo, jws) : null;
  const renewal = p.data?.signedRenewalInfo ? await verifyAppleJws<RenewalInfo>(p.data.signedRenewalInfo, jws) : null;
  return { notificationType: p.notificationType, subtype: p.subtype, notificationUUID: p.notificationUUID, tx, renewal, bundleId: p.data?.bundleId, environment: p.data?.environment };
}
// Notification -> new state (null = no change to the team's access)
export function stateFromNotification(n: DecodedNotification, prev: SubState): SubState | null {
  if (!n.tx) return null;
  switch (n.notificationType) {
    case "SUBSCRIBED": case "DID_RENEW": case "OFFER_REDEEMED": case "RENEWAL_EXTENDED": case "DID_CHANGE_RENEWAL_STATUS": case "DID_CHANGE_RENEWAL_PREF":
      return stateFrom(n.tx, n.renewal);
    case "DID_FAIL_TO_RENEW": return n.subtype === "GRACE_PERIOD" ? stateFrom(n.tx, n.renewal, 4) : { status: "billing_retry", expiresAt: prev.expiresAt, graceUntil: 0 };
    case "GRACE_PERIOD_EXPIRED": return { status: "billing_retry", expiresAt: prev.expiresAt, graceUntil: 0 };
    case "EXPIRED": return { status: "expired", expiresAt: prev.expiresAt, graceUntil: 0 };
    case "REFUND": case "REVOKE": return { status: "revoked", expiresAt: prev.expiresAt, graceUntil: 0 };
    case "REFUND_REVERSED": return stateFrom({ ...n.tx, revocationDate: undefined }, n.renewal);
    default: return null; // TEST, CONSUMPTION_REQUEST, PRICE_INCREASE, ... don't change access
  }
}
