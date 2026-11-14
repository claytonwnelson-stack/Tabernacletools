// Seat service endpoint + Ed25519 public key that signs church-seat entitlement tokens.
// Clayton: replace both after `node seat-service/scripts/keygen.ts` + `wrangler deploy` (see seat-service/DEPLOY.md).
#pragma once
namespace wr {
static constexpr const char* kSeatServiceUrl = "https://tabernacle-tools-seats.REPLACE.workers.dev";
static constexpr const char* kSeatPublicKeyHex = "0000000000000000000000000000000000000000000000000000000000000000"; // placeholder: no token verifies
static constexpr const char* kSeatAudience = "com.tabernacletools";
}
