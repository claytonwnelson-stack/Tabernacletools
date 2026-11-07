#include "model/StoreMapping.h"
#include <algorithm>

namespace wr {
static bool usable (const StoreEntitlement& s, int64_t now) {
    if (s.revoked || s.upgraded || s.state == "revoked" || s.state == "expired" || s.state == "billingRetry") return false;
    if (s.state == "grace") return std::max (s.expiresAt, s.graceUntil) > now;
    return s.expiresAt > now;
}
static int rank (const StoreEntitlement& s) { auto* p = findProduct (s.productId); return ! p ? 0 : p->plan == Plan::Church ? 2 : 1; }
SubscriptionState subscriptionFromStore (const std::vector<StoreEntitlement>& e, int64_t now, bool introEligible) {
    SubscriptionState st; st.usedTrial = ! introEligible;
    const StoreEntitlement* best = nullptr;
    for (auto& s : e) { if (s.trial) st.usedTrial = true; if (rank (s) == 0 || ! usable (s, now)) continue;
        if (! best || rank (s) > rank (*best) || (rank (s) == rank (*best) && s.expiresAt > best->expiresAt)) best = &s; }
    if (! best) {   // nothing usable: report the most relevant lapsed state for the status text
        const StoreEntitlement* last = nullptr; for (auto& s : e) if (rank (s) > 0 && (! last || s.expiresAt > last->expiresAt)) last = &s;
        if (last) { st.productId = last->productId; st.expiresAt = last->expiresAt;
            st.status = (last->revoked || last->state == "revoked") ? SubStatus::Revoked : last->state == "billingRetry" ? SubStatus::BillingRetry : SubStatus::Expired; }
        return st;
    }
    st.productId = best->productId; st.expiresAt = best->expiresAt;
    if (best->state == "grace") { st.status = SubStatus::GracePeriod; st.graceUntil = best->graceUntil; }
    else st.status = best->trial ? SubStatus::Trial : SubStatus::Active;
    return st;
}
IntroEligibility introEligibilityFrom (bool known, bool eligible, const std::vector<StoreEntitlement>& e) {
    for (auto& s : e) if (s.trial) return IntroEligibility::Ineligible;
    if (! known) return IntroEligibility::Unknown;
    return eligible ? IntroEligibility::Eligible : IntroEligibility::Ineligible;
}
std::string paywallButtonLabel (IntroEligibility i, const std::string& price) {
    return (showTrialOffer (i) ? "Start 2-week free trial, then " : "Subscribe - ") + price + "/month";
}
std::string churchTransactionJws (const std::vector<StoreEntitlement>& e, int64_t now) {
    const StoreEntitlement* best = nullptr;
    for (auto& s : e) if (rank (s) == 2 && usable (s, now) && ! s.jws.empty() && (! best || s.expiresAt > best->expiresAt)) best = &s;
    return best ? best->jws : std::string();
}
} // namespace wr
