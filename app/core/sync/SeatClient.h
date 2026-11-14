// v0.11 church seat client: talks to seat-service over an injected transport (juce::URL in the app, fakes in tests),
// verifies Ed25519 entitlement tokens locally (TweetNaCl), caches session + token so seats work offline up to 30 days.
#pragma once
#include "model/Json.h"
#include "model/Packs.h"
#include "model/CrossApp.h"
#include <functional>
#include <optional>
#include <string>
#include <cstdint>

namespace wr {
struct HttpResponse { int status = 0; std::string body; };   // status 0 = network failure
using SeatTransport = std::function<HttpResponse (const std::string& method, const std::string& path, const std::string& jsonBody, const std::string& bearer)>;

struct SeatClaims { std::string team, member, role, aud; int64_t exp = 0, iat = 0, off = 0; };
// Parses + verifies "wr1.<payload>.<sig>". Returns nullopt on bad signature, wrong audience or malformed payload (not on expiry).
std::optional<SeatClaims> verifySeatToken (const std::string& token, const std::string& publicKeyHex, const std::string& audience);
bool seatClaimsUsable (const SeatClaims& c, int64_t now);      // now < min(exp, off)
SeatToken toSeatToken (const SeatClaims& c);                   // teamExpiresAt = exp, refreshedAt = iat  (Entitlements applies the 30-day cap)

struct SeatResult { bool ok = false; int status = 0; std::string error; Json body; };
struct SeatMember { std::string id, displayName, role; int64_t joinedAt = 0; bool you = false; };
struct SeatTeamInfo { std::string id, churchName, status, role; int64_t paidThrough = 0; int seatCap = 15, openInvites = 0; std::vector<SeatMember> members; };

class SeatClient {
public:
    SeatClient (SeatTransport t, std::string publicKeyHex, std::string audience) : http_ (std::move (t)), key_ (std::move (publicKeyHex)), aud_ (std::move (audience)) {}
    bool signedIn() const { return ! session_.empty(); }
    SeatResult signInWithApple (const std::string& identityToken);
    SeatResult createTeam (const std::string& churchName, const std::string& displayName, const std::string& signedTransaction);
    SeatResult myTeam (SeatTeamInfo* out = nullptr);
    SeatResult createInvite (std::string* code = nullptr);
    SeatResult revokeInvite (const std::string& code);
    SeatResult redeem (const std::string& code, const std::string& displayName);
    SeatResult leave();
    SeatResult removeMember (const std::string& memberId);
    SeatResult deleteAccount();      // clears session + token locally too
    // v0.18 one subscription for every Bearing Live app: post this app's signed StoreKit 2 transaction (purchase, renewal,
    // Restore Purchases), and read what the person is covered by across all apps (GET /v1/entitlement)
    struct PurchasePost { bool ok = false, trialOk = true; std::string plan, duplicateApp, error; };
    SeatResult postPurchase (const std::string& signedTransaction, PurchasePost* out = nullptr);
    SeatResult coverage (CrossAppCoverage* out = nullptr);
    const CrossAppCoverage& lastCoverage() const { return coverage_; }

    // Fetch a fresh token if the cached one is missing, older than refreshAfterSec, or near expiry; on network failure keep the
    // cached token. Applies the result to `ent` (setSeat / clearSeat). Returns true if a usable seat is now held.
    bool refresh (Entitlements& ent, int64_t now, int64_t refreshAfterSec = 12 * 3600);
    const std::optional<SeatClaims>& cachedClaims() const { return claims_; }
    std::string lastError() const { return lastError_; }

    // persistence (session token + entitlement token); token is re-verified on load
    std::string serialise() const;
    bool parse (const std::string& s);
    static std::string normaliseCode (const std::string& in);   // mirrors the server: uppercase, strip spaces/dashes, O->0, I/L->1
private:
    SeatResult call (const char* method, const std::string& path, const Json& body = Json::object(), bool auth = true);
    void adoptToken (const std::string& tok);
    SeatTransport http_; std::string key_, aud_, session_, token_, lastError_; std::optional<SeatClaims> claims_; CrossAppCoverage coverage_;
};
} // namespace wr
