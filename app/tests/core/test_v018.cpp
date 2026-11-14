// v0.18 one subscription for every Bearing Live app: client side (coverage, paywall mode, trial honesty, lapse guard)
#include "sync/SeatClient.h"
#include "model/CrossApp.h"
#include "seat_fixture.h"
#include <cstdio>
#include <map>
using namespace wr;
static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (! (c)) { ++fails; std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)
static const std::string AUD = "com.tabernacletools";
struct FakeServer {
    std::map<std::string, HttpResponse> routes; std::vector<std::string> log; bool offline = false; std::string lastBody;
    SeatTransport transport() { return [this] (const std::string& m, const std::string& p, const std::string& b, const std::string&) {
        log.push_back (m + " " + p); lastBody = b; if (offline) return HttpResponse { 0, "" };
        auto it = routes.find (m + " " + p); return it == routes.end() ? HttpResponse { 404, "{\"error\":\"not_found\"}" } : it->second; }; }
};
int main() {
    const int64_t iat = seat_fixture::kIat; const std::string pk = seat_fixture::kPublicKeyHex;
    // products per app
    CHECK (familyApps().size() == 4);
    CHECK (familyProductId ("com.tabernacletools.guitar", Plan::Individual) == "com.tabernacletools.guitar.individual.monthly");   // Guitar keeps v0.12 ids
    CHECK (familyProductId ("com.tabernacletools.keys", Plan::Church) == "com.tabernacletools.keys.church.monthly");
    // coverage from the server: bought Individual in Keys
    FakeServer srv; SeatClient sc (srv.transport(), pk, AUD);
    srv.routes["POST /v1/auth/apple"] = { 200, "{\"session\":\"s1.x.y\"}" }; CHECK (sc.signInWithApple ("idt").ok);
    srv.routes["GET /v1/entitlement"] = { 200, std::string ("{\"covered\":true,\"plan\":\"individual\",\"source\":\"purchase\",\"coveredBy\":{\"app\":\"Bearing Live Keys\",\"bundleId\":\"com.tabernacletools.keys\"},\"paidThrough\":")
        + std::to_string (iat + 20 * 86400) + ",\"trialEligible\":false,\"token\":\"" + seat_fixture::kMemberToken + "\"}" };
    Entitlements ent; CHECK (sc.refresh (ent, iat + 60)); CHECK (ent.fullAccess (iat + 60));
    CHECK (srv.log.back() == "GET /v1/entitlement");
    auto& c = sc.lastCoverage(); CHECK (c.known && c.covered && c.coveredByApp == "Bearing Live Keys");
    CHECK (paywallModeFor (ent, c, iat + 60) == PaywallMode::UpgradeOnly);
    CHECK (coveredMessage (ent, c, iat + 60).find ("You're covered") == 0 && coveredMessage (ent, c, iat + 60).find ("Bearing Live Keys") != std::string::npos);
    // church team seat -> fully covered, nothing to sell
    CrossAppCoverage team = c; team.source = "team"; team.plan = "church"; CHECK (paywallModeFor (ent, team, iat + 60) == PaywallMode::Covered);
    // not covered -> sell; covered=false drops a cached token
    srv.routes["GET /v1/entitlement"] = { 200, "{\"covered\":false,\"trialEligible\":true}" };
    Entitlements e2; CHECK (! sc.refresh (e2, iat + 2 * 86400, 0)); CHECK (! e2.fullAccess (iat + 2 * 86400)); CHECK (paywallModeFor (e2, sc.lastCoverage(), iat) == PaywallMode::Sell);
    // offline: keep the cached (now empty) state, no crash; offline grace covered by the v0.11 tests
    // older server without GET: falls back to POST
    { FakeServer old; SeatClient o (old.transport(), pk, AUD); old.routes["POST /v1/auth/apple"] = { 200, "{\"session\":\"s\"}" }; o.signInWithApple ("i");
      old.routes["POST /v1/entitlement"] = { 200, std::string ("{\"token\":\"") + seat_fixture::kMemberToken + "\"}" };
      Entitlements e; CHECK (o.refresh (e, iat + 60)); CHECK (old.log.size() == 3 && old.log[1] == "GET /v1/entitlement" && old.log[2] == "POST /v1/entitlement"); }
    // posting a purchase
    srv.routes["POST /v1/purchases"] = { 201, "{\"ok\":true,\"plan\":\"individual\",\"trialOk\":false,\"duplicate\":{\"app\":\"Bearing Live Guitar\"}}" };
    SeatClient::PurchasePost pp; CHECK (sc.postPurchase ("eyJ.jws.sig", &pp).ok); CHECK (! pp.trialOk && pp.duplicateApp == "Bearing Live Guitar" && srv.lastBody.find ("eyJ.jws.sig") != std::string::npos);
    // trial honesty
    CrossAppCoverage unknown; CHECK (showTrialAcrossApps (true, unknown)); CHECK (! showTrialAcrossApps (false, unknown));
    CrossAppCoverage used; used.known = true; used.trialEligible = false; CHECK (! showTrialAcrossApps (true, used));
    CHECK (honourLocalTrial (true, false, false)); CHECK (! honourLocalTrial (true, true, false)); CHECK (honourLocalTrial (false, true, false));
    // a lapse never silences the running app
    LapseGuard g; CHECK (! g.audioAllowed (false)); CHECK (g.audioAllowed (true)); CHECK (g.audioAllowed (false)); CHECK (g.latched());
    // seat role reaches the status line
    { Entitlements e; auto cl = verifySeatToken (seat_fixture::kMemberToken, pk, AUD); auto t = toSeatToken (*cl); t.role = "individual"; e.setSeat (t);
      CHECK (e.statusText (iat + 60).find ("all Bearing Live apps") != std::string::npos); }
    std::printf ("v0.18: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
