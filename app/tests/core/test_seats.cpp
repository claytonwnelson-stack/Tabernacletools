// v0.11 church seat client tests: tokens produced by the Node seat-service (seat_fixture.h, generated) verified by TweetNaCl.
#include "sync/SeatClient.h"
#include "seat_fixture.h"
#include <cstdio>
#include <map>
using namespace wr;
static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (! (c)) { ++fails; std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)
static const std::string AUD = "com.tabernacletools";

struct FakeServer {
    std::map<std::string, HttpResponse> routes; std::vector<std::string> log; bool offline = false; std::string lastBearer, lastBody;
    SeatTransport transport() { return [this] (const std::string& m, const std::string& p, const std::string& b, const std::string& bearer) {
        log.push_back (m + " " + p); lastBearer = bearer; lastBody = b; if (offline) return HttpResponse { 0, "" };
        auto it = routes.find (m + " " + p); return it == routes.end() ? HttpResponse { 404, "{\"error\":\"not_found\"}" } : it->second; }; }
};

int main() {
    const std::string pk = seat_fixture::kPublicKeyHex; const int64_t iat = seat_fixture::kIat;
    // --- cross-implementation verification
    auto c = verifySeatToken (seat_fixture::kMemberToken, pk, AUD);
    CHECK (c.has_value());
    if (c) { CHECK (c->team == "t_fixture"); CHECK (c->member == "u_fixture"); CHECK (c->role == "member"); CHECK (c->exp == iat + 20 * 86400); CHECK (c->off == iat + 30 * 86400); }
    auto L = verifySeatToken (seat_fixture::kLeaderToken, pk, AUD); CHECK (L && L->role == "leader");
    std::string t = seat_fixture::kMemberToken;
    { auto bad = t; bad[6] = bad[6] == 'A' ? 'B' : 'A'; CHECK (! verifySeatToken (bad, pk, AUD)); }                  // payload tampered
    { auto bad = t; bad.back() = bad.back() == 'A' ? 'B' : 'A'; CHECK (! verifySeatToken (bad, pk, AUD)); }          // signature tampered
    { auto otherKey = pk; otherKey[0] = otherKey[0] == 'e' ? 'f' : 'e'; CHECK (! verifySeatToken (t, otherKey, AUD)); }
    CHECK (! verifySeatToken (t, pk, "com.other.app"));
    CHECK (! verifySeatToken (t, std::string (64, '0'), AUD));                                                        // placeholder key verifies nothing
    CHECK (! verifySeatToken ("", pk, AUD)); CHECK (! verifySeatToken ("wr1.x.y", pk, AUD)); CHECK (! verifySeatToken ("wr2" + t.substr (3), pk, AUD));
    // --- usability windows: min(paid-through, 30-day offline cap)
    CHECK (seatClaimsUsable (*c, iat)); CHECK (seatClaimsUsable (*c, iat + 20 * 86400 - 1)); CHECK (! seatClaimsUsable (*c, iat + 20 * 86400));
    CHECK (seatClaimsUsable (*L, iat + 30 * 86400 - 1)); CHECK (! seatClaimsUsable (*L, iat + 30 * 86400));   // leader paid 45d, offline cap wins
    { Entitlements e; e.setSeat (toSeatToken (*L)); CHECK (e.fullAccess (iat + 86400)); CHECK (! e.fullAccess (iat + 31 * 86400)); CHECK (e.allows (Feature::ReadCharts, iat + 31 * 86400)); CHECK (! e.allows (Feature::AudioEngine, iat + 31 * 86400)); }
    // --- code normalisation mirrors the server
    CHECK (SeatClient::normaliseCode (" ab3o1-xy9il ") == "AB301XY911");
    // --- client flow against a fake server
    FakeServer srv; SeatClient sc (srv.transport(), pk, AUD);
    CHECK (! sc.myTeam().ok); CHECK (sc.lastError() == "not_signed_in"); CHECK (srv.log.empty());
    srv.routes["POST /v1/auth/apple"] = { 200, "{\"session\":\"s1.abc.def\",\"userId\":\"u_fixture\"}" };
    CHECK (sc.signInWithApple ("idtoken").ok); CHECK (sc.signedIn()); CHECK (srv.lastBearer.empty()); CHECK (srv.lastBody.find ("idtoken") != std::string::npos);
    srv.routes["POST /v1/invites/redeem"] = { 409, "{\"error\":\"team_full\"}" };
    auto rr = sc.redeem ("abcde-fghjk", "Drums"); CHECK (! rr.ok); CHECK (rr.error == "team_full"); CHECK (srv.lastBearer == "s1.abc.def"); CHECK (srv.lastBody.find ("ABCDEFGHJK") != std::string::npos);
    srv.routes["POST /v1/invites/redeem"] = { 201, "{\"team\":{\"id\":\"t_fixture\"}}" }; CHECK (sc.redeem ("ABCDE-FGHJK", "Drums").ok);
    srv.routes["POST /v1/entitlement"] = { 200, std::string ("{\"token\":\"") + seat_fixture::kMemberToken + "\",\"status\":\"active\"}" };
    Entitlements ent;
    CHECK (sc.refresh (ent, iat + 60)); CHECK (ent.fullAccess (iat + 60)); CHECK (sc.cachedClaims() && sc.cachedClaims()->member == "u_fixture");
    size_t calls = srv.log.size(); CHECK (sc.refresh (ent, iat + 3600)); CHECK (srv.log.size() == calls);    // fresh cache: no network
    // offline: stale but kept up to paid-through
    srv.offline = true; CHECK (sc.refresh (ent, iat + 10 * 86400)); CHECK (srv.log.size() == calls + 1); CHECK (ent.fullAccess (iat + 10 * 86400));
    CHECK (! sc.refresh (ent, iat + 21 * 86400)); CHECK (! ent.fullAccess (iat + 21 * 86400));
    // persistence round trip re-verifies the token
    auto saved = sc.serialise(); SeatClient sc2 (srv.transport(), pk, AUD); CHECK (sc2.parse (saved)); CHECK (sc2.signedIn()); CHECK (sc2.cachedClaims().has_value());
    SeatClient sc3 (srv.transport(), std::string (64, '0'), AUD); CHECK (sc3.parse (saved)); CHECK (! sc3.cachedClaims().has_value());   // wrong key -> token dropped
    CHECK (! sc2.parse ("garbage"));
    // server says lapsed -> seat dropped immediately even though cached token is otherwise valid
    srv.offline = false; srv.routes["POST /v1/entitlement"] = { 402, "{\"error\":\"subscription_inactive\"}" };
    Entitlements e2; CHECK (! sc2.refresh (e2, iat + 13 * 3600)); CHECK (! sc2.cachedClaims()); CHECK (! e2.fullAccess (iat + 13 * 3600));
    // forged token from server rejected
    srv.routes["POST /v1/entitlement"] = { 200, "{\"token\":\"wr1.eyJ2IjoxfQ.AAAA\"}" }; Entitlements e3; CHECK (! sc2.refresh (e3, iat + 14 * 3600)); CHECK (sc2.lastError() == "bad_token_signature");
    // team info parsing + leader actions
    srv.routes["GET /v1/teams/me"] = { 200, "{\"team\":{\"id\":\"t1\",\"churchName\":\"Grace\",\"status\":\"grace\",\"paidThrough\":1800086400,\"seatCap\":15},\"role\":\"leader\",\"openInvites\":2,"
                                            "\"members\":[{\"id\":\"u1\",\"displayName\":\"Clayton\",\"role\":\"leader\",\"joinedAt\":1,\"you\":true},{\"id\":\"u2\",\"displayName\":\"Amy\",\"role\":\"member\",\"joinedAt\":2,\"you\":false}]}" };
    SeatTeamInfo ti; CHECK (sc.myTeam (&ti).ok); CHECK (ti.members.size() == 2); CHECK (ti.role == "leader"); CHECK (ti.openInvites == 2); CHECK (ti.paidThrough == 1800086400LL); CHECK (ti.members[0].you); CHECK (ti.status == "grace");
    srv.routes["POST /v1/teams/me/invites"] = { 201, "{\"code\":\"ABCDE-12345\",\"expiresAt\":5}" }; std::string code; CHECK (sc.createInvite (&code).ok); CHECK (code == "ABCDE-12345");
    srv.routes["DELETE /v1/teams/me/members/u2"] = { 200, "{\"ok\":true}" }; CHECK (sc.removeMember ("u2").ok);
    srv.routes["DELETE /v1/teams/me/invites/ABCDE12345"] = { 200, "{\"ok\":true,\"revoked\":true}" }; CHECK (sc.revokeInvite ("abcde-12345").ok);
    // 401 clears the session
    srv.routes["POST /v1/teams/me/leave"] = { 401, "{\"error\":\"session_expired\"}" }; CHECK (! sc.leave().ok); CHECK (! sc.signedIn());
    sc.signInWithApple ("x"); srv.routes["DELETE /v1/account"] = { 200, "{\"ok\":true}" }; CHECK (sc.deleteAccount().ok); CHECK (! sc.signedIn()); CHECK (! sc.cachedClaims());
    std::printf ("seats: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
