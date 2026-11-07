// Generate the Ed25519 entitlement signing key + random secrets. Prints values to paste into `wrangler secret put`.
// The PUBLIC key (32 bytes hex) goes into the app: core/sync/SeatKeys.h (kSeatPublicKeyHex).
const kp = await crypto.subtle.generateKey({ name: "Ed25519" }, true, ["sign", "verify"]) as CryptoKeyPair;
const pk8 = Buffer.from(await crypto.subtle.exportKey("pkcs8", kp.privateKey)).toString("base64");
const pub = Buffer.from(await crypto.subtle.exportKey("raw", kp.publicKey)).toString("hex");
const rnd = () => Buffer.from(crypto.getRandomValues(new Uint8Array(32))).toString("base64url");
console.log("ENTITLEMENT_SIGNING_KEY=" + pk8);
console.log("SESSION_SECRET=" + rnd());
console.log("SUB_PEPPER=" + rnd());
console.log("# app public key (kSeatPublicKeyHex): " + pub);
export {};
