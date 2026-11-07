#include "CrossApp.h"

namespace wr {
PaywallMode paywallModeFor (const Entitlements& e, const CrossAppCoverage& c, int64_t now) {
    if (c.known && c.covered) return c.plan == "individual" && c.source == "purchase" ? PaywallMode::UpgradeOnly : PaywallMode::Covered;
    if (e.fullAccess (now)) return e.plan (now) == Plan::Individual ? PaywallMode::UpgradeOnly : PaywallMode::Covered;
    return PaywallMode::Sell;
}
std::string coveredMessage (const Entitlements& e, const CrossAppCoverage& c, int64_t now) {
    if (c.known && c.covered) {
        if (c.source == "team") return "You're covered - your church team's plan unlocks every Tabernacle Tools app.";
        return "You're covered - your " + std::string (c.plan == "church" ? "Church" : "Individual") + " subscription"
             + (c.coveredByApp.empty() ? std::string() : " (bought in " + c.coveredByApp + ")") + " unlocks every Tabernacle Tools app. Nothing to buy here.";
    }
    if (e.fullAccess (now)) return "You're covered - " + e.statusText (now) + ".";
    return {};
}
bool showTrialAcrossApps (bool storeKitEligible, const CrossAppCoverage& c) { return storeKitEligible && (! c.known || (c.trialEligible && ! c.covered)); }
bool honourLocalTrial (bool storeTrial, bool serverAnswered, bool serverTrialOk) { return ! storeTrial || ! serverAnswered || serverTrialOk; }
} // namespace wr
