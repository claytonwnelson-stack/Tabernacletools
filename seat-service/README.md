# Bearing Live seat service (v0.11)
Church plan seats: Sign in with Apple → leader creates a team from a verified Church subscription → single-use invite codes → members get Ed25519-signed entitlement tokens (paid-through date, 30-day offline cap).
- `npm test`: 17 tests, fake Apple (RSA JWKS, ECDSA x5c chain with Apple OIDs, mocked Server API), node:sqlite
- `npm run typecheck`
- `npm run dev` + `bash scripts/smoke.sh`: local server with a fake Apple, end-to-end smoke
- `node scripts/gen-fixture.ts out.h`: cross-implementation fixture for the app's C++ tests
- API: `openapi.yaml` · Deploy: `DEPLOY.md`
Privacy: we store HMAC(Apple sub), an optional display name, the team/church name, and subscription status. No email, no IP storage (IPs are only used as transient rate-limit keys), and no device ids.
