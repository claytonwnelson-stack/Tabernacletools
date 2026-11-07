// v0.18 one Tabernacle Tools subscription unlocks every app (Guitar, Keys, Bass, Charts). Each app is a separate App Store app with
// its own products + subscription group; the seat service links purchases to the Sign in with Apple account.
// Pure C++ (tested on Linux): paywall mode, trial honesty across apps, and "never go silent on lapse".
#pragma once
#include "model/Json.h"
#include "model/Packs.h"
#include <string>
#include <vector>
#include <cstdint>

namespace wr {
struct FamilyApp { const char* bundleId; const char* name; };
inline const std::vector<FamilyApp>& familyApps() {
    static const std::vector<FamilyApp> a = { { "com.tabernacletools.guitar", "Tabernacle Tools - Electric Guitar" }, { "com.tabernacletools.keys", "Tabernacle Tools - Keys" },
                                              { "com.tabernacletools.bass", "Tabernacle Tools - Bass" }, { "com.tabernacletools.charts", "Tabernacle Tools - Charts" } };
    return a;
}
// this app's products: <bundleId>.individual.monthly / <bundleId>.church.monthly (Guitar keeps its v0.12 ids, which match)
inline std::string familyProductId (const std::string& bundleId, Plan p) { return bundleId + (p == Plan::Church ? ".church.monthly" : ".individual.monthly"); }

struct CrossAppCoverage {
    bool known = false;             // the server answered at least once (signed in)
    bool covered = false, trialEligible = false;
    std::string plan, source;       // individual | church ; purchase | team
    std::string coveredByApp, coveredByBundle;
    int64_t paidThrough = 0, offlineUntil = 0;
};
inline CrossAppCoverage coverageFromJson (const Json& j) {
    CrossAppCoverage c; c.known = true; c.covered = j["covered"].boolean(); c.trialEligible = j["trialEligible"].boolean();
    c.plan = j["plan"].str(); c.source = j["source"].str(); c.coveredByApp = j["coveredBy"]["app"].str(); c.coveredByBundle = j["coveredBy"]["bundleId"].str();
    c.paidThrough = (int64_t) j["paidThrough"].num(); c.offlineUntil = (int64_t) j["offlineUntil"].num(); return c;
}

// What the paywall shows. Covered = never sell (no double billing); UpgradeOnly = Individual elsewhere, Church still offered.
enum class PaywallMode { Sell, Covered, UpgradeOnly };
PaywallMode paywallModeFor (const Entitlements& e, const CrossAppCoverage& c, int64_t now);
std::string coveredMessage (const Entitlements& e, const CrossAppCoverage& c, int64_t now);   // "You're covered - ..."

// Trial wording is shown only when BOTH StoreKit (this app's group) and the seat service (all apps, once per person) agree.
// Not signed in: only StoreKit's answer is known, so we show Apple's actual offer (the server refuses the unlock later if it's a
// second trial for this person - honourLocalTrial). Purchases never require an account.
bool showTrialAcrossApps (bool storeKitEligible, const CrossAppCoverage& c);
// A StoreKit trial in this app is honoured locally unless the seat service said this person's trial was already used.
bool honourLocalTrial (bool storeTrial, bool serverAnswered, bool serverTrialOk);

// Lapse never cuts audio mid-service: once this run of the app has had full access, the audio engine (rig, click, backing tracks /
// backup mix) keeps running until the app is closed. Read-only takes effect on the next launch.
class LapseGuard {
public:
    bool audioAllowed (bool fullAccessNow) { if (fullAccessNow) latched_ = true; return latched_; }
    bool latched() const { return latched_; }
private:
    bool latched_ = false;
};
} // namespace wr
