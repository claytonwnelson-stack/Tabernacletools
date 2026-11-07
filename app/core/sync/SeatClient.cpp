#include "sync/SeatClient.h"
extern "C" {
#include "third_party/tweetnacl/tweetnacl.h"
}
#include <vector>
#include <cctype>

namespace wr {
static bool b64urlDecode (const std::string& in, std::vector<unsigned char>& out) {
    out.clear(); unsigned buf = 0; int bits = 0;
    for (char c : in) {
        int v; if (c >= 'A' && c <= 'Z') v = c - 'A'; else if (c >= 'a' && c <= 'z') v = c - 'a' + 26; else if (c >= '0' && c <= '9') v = c - '0' + 52;
        else if (c == '-') v = 62; else if (c == '_') v = 63; else return false;
        buf = (buf << 6) | (unsigned) v; bits += 6; if (bits >= 8) { bits -= 8; out.push_back ((unsigned char) ((buf >> bits) & 0xff)); }
    }
    return true;
}
static bool hexDecode (const std::string& h, std::vector<unsigned char>& out) {
    if (h.size() % 2) return false; out.clear();
    auto nib = [] (char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
    for (size_t i = 0; i < h.size(); i += 2) { int a = nib (h[i]), b = nib (h[i + 1]); if (a < 0 || b < 0) return false; out.push_back ((unsigned char) (a * 16 + b)); }
    return true;
}
std::optional<SeatClaims> verifySeatToken (const std::string& token, const std::string& publicKeyHex, const std::string& audience) {
    if (token.rfind ("wr1.", 0) != 0) return std::nullopt;
    auto dot = token.rfind ('.'); if (dot <= 4) return std::nullopt;
    std::vector<unsigned char> pk, sig, payload;
    if (! hexDecode (publicKeyHex, pk) || pk.size() != 32) return std::nullopt;
    if (! b64urlDecode (token.substr (dot + 1), sig) || sig.size() != 64) return std::nullopt;
    const std::string signedPart = token.substr (0, dot);
    std::vector<unsigned char> sm (sig.begin(), sig.end()); sm.insert (sm.end(), signedPart.begin(), signedPart.end());
    std::vector<unsigned char> m (sm.size()); unsigned long long mlen = 0;
    if (crypto_sign_open (m.data(), &mlen, sm.data(), (unsigned long long) sm.size(), pk.data()) != 0) return std::nullopt;
    if (! b64urlDecode (signedPart.substr (4), payload)) return std::nullopt;
    bool ok = false; Json j = Json::parse (std::string (payload.begin(), payload.end()), &ok); if (! ok || ! j.isObject()) return std::nullopt;
    if (j["v"].integer() != 1 || j["aud"].str() != audience) return std::nullopt;
    SeatClaims c; c.team = j["team"].str(); c.member = j["mem"].str(); c.role = j["role"].str(); c.aud = j["aud"].str();
    c.exp = (int64_t) j["exp"].num(); c.iat = (int64_t) j["iat"].num(); c.off = (int64_t) j["off"].num();
    if (c.team.empty() || c.member.empty() || c.exp <= 0 || c.off <= c.iat) return std::nullopt;
    return c;
}
bool seatClaimsUsable (const SeatClaims& c, int64_t now) { return now < c.exp && now < c.off && now >= c.iat - 600; }
SeatToken toSeatToken (const SeatClaims& c) { SeatToken t; t.teamId = c.team; t.memberId = c.member; t.teamExpiresAt = c.exp; t.refreshedAt = c.iat; t.role = c.role; return t; }

std::string SeatClient::normaliseCode (const std::string& in) {
    std::string s; for (char ch : in) { char c = (char) std::toupper ((unsigned char) ch); if (c == ' ' || c == '-') continue; if (c == 'O') c = '0'; if (c == 'I' || c == 'L') c = '1'; s += c; }
    return s;
}
SeatResult SeatClient::call (const char* method, const std::string& path, const Json& body, bool auth) {
    SeatResult r; if (auth && session_.empty()) { r.error = "not_signed_in"; lastError_ = r.error; return r; }
    auto resp = http_ (method, path, std::string (method) == "GET" || std::string (method) == "DELETE" ? std::string() : body.dump (0), auth ? session_ : std::string());
    r.status = resp.status; bool ok = false; r.body = Json::parse (resp.body, &ok);
    r.ok = resp.status >= 200 && resp.status < 300;
    if (! r.ok) { r.error = resp.status == 0 ? "network" : (ok ? r.body["error"].str ("http_" + std::to_string (resp.status)) : "http_" + std::to_string (resp.status)); lastError_ = r.error; }
    if (resp.status == 401 && auth) session_.clear();   // expired session: sign in again
    return r;
}
SeatResult SeatClient::signInWithApple (const std::string& idt) {
    auto r = call ("POST", "/v1/auth/apple", Json::object().set ("identityToken", idt), false);
    if (r.ok) session_ = r.body["session"].str(); return r;
}
SeatResult SeatClient::createTeam (const std::string& church, const std::string& name, const std::string& stx) {
    return call ("POST", "/v1/teams", Json::object().set ("churchName", church).set ("displayName", name).set ("signedTransaction", stx));
}
SeatResult SeatClient::myTeam (SeatTeamInfo* out) {
    auto r = call ("GET", "/v1/teams/me");
    if (r.ok && out) {
        auto& t = r.body["team"]; *out = {}; out->id = t["id"].str(); out->churchName = t["churchName"].str(); out->status = t["status"].str();
        out->paidThrough = (int64_t) t["paidThrough"].num(); out->seatCap = t["seatCap"].integer (15); out->role = r.body["role"].str(); out->openInvites = r.body["openInvites"].integer();
        for (auto& m : r.body["members"].items()) out->members.push_back ({ m["id"].str(), m["displayName"].str(), m["role"].str(), (int64_t) m["joinedAt"].num(), m["you"].boolean() });
    }
    return r;
}
SeatResult SeatClient::createInvite (std::string* code) { auto r = call ("POST", "/v1/teams/me/invites"); if (r.ok && code) *code = r.body["code"].str(); return r; }
SeatResult SeatClient::revokeInvite (const std::string& code) { return call ("DELETE", "/v1/teams/me/invites/" + normaliseCode (code)); }
SeatResult SeatClient::redeem (const std::string& code, const std::string& name) { return call ("POST", "/v1/invites/redeem", Json::object().set ("code", normaliseCode (code)).set ("displayName", name)); }
SeatResult SeatClient::leave() { auto r = call ("POST", "/v1/teams/me/leave"); if (r.ok) { token_.clear(); claims_.reset(); } return r; }
SeatResult SeatClient::removeMember (const std::string& id) { return call ("DELETE", "/v1/teams/me/members/" + id); }
SeatResult SeatClient::deleteAccount() { auto r = call ("DELETE", "/v1/account"); if (r.ok) { session_.clear(); token_.clear(); claims_.reset(); } return r; }
SeatResult SeatClient::postPurchase (const std::string& jws, PurchasePost* out) {
    auto r = call ("POST", "/v1/purchases", Json::object().set ("signedTransaction", jws));
    if (out) { *out = {}; out->ok = r.ok; out->plan = r.body["plan"].str(); out->trialOk = r.body["trialOk"].boolean (true);
        out->duplicateApp = r.body["duplicate"]["app"].str(); out->error = r.error; }
    return r;
}
SeatResult SeatClient::coverage (CrossAppCoverage* out) {
    auto r = call ("GET", "/v1/entitlement");
    if (r.ok) { coverage_ = coverageFromJson (r.body); if (coverage_.covered) adoptToken (r.body["token"].str()); }
    if (out) *out = coverage_;
    return r;
}
void SeatClient::adoptToken (const std::string& tok) {
    auto c = verifySeatToken (tok, key_, aud_); if (! c) { lastError_ = "bad_token_signature"; return; }
    token_ = tok; claims_ = c;
}
bool SeatClient::refresh (Entitlements& ent, int64_t now, int64_t refreshAfter) {
    bool stale = ! claims_ || now - claims_->iat >= refreshAfter || claims_->exp - now < 86400;
    if (stale && signedIn()) {
        // v0.18 cross-app entitlement (own purchase in ANY Tabernacle Tools app, or a church seat); older servers: POST team-only
        auto r = call ("GET", "/v1/entitlement");
        if (r.ok) { coverage_ = coverageFromJson (r.body); if (coverage_.covered) adoptToken (r.body["token"].str()); else { token_.clear(); claims_.reset(); } }
        else {
            if (r.status == 404 && r.error == "not_found") r = call ("POST", "/v1/entitlement");
            if (r.ok) adoptToken (r.body["token"].str());
            else if (r.status == 402 || r.status == 404) { token_.clear(); claims_.reset(); }   // lapsed / removed: drop seat now
        }
        // network or 5xx: keep cached token (offline grace up to 30 days)
    }
    if (claims_ && seatClaimsUsable (*claims_, now)) { ent.setSeat (toSeatToken (*claims_)); return true; }
    ent.clearSeat(); return false;
}
std::string SeatClient::serialise() const { return Json::object().set ("v", 1).set ("session", session_).set ("token", token_).dump (0); }
bool SeatClient::parse (const std::string& s) {
    bool ok = false; Json j = Json::parse (s, &ok); if (! ok || j["v"].integer() != 1) return false;
    session_ = j["session"].str(); token_.clear(); claims_.reset(); if (! j["token"].str().empty()) adoptToken (j["token"].str());
    return true;
}
} // namespace wr
