// v0.12 StoreKit 2 -> entitlement model (pure C++, tested on Linux). The Swift bridge produces StoreEntitlement records
// (Transaction.currentEntitlements + Product.SubscriptionInfo.Status); this decides the app's SubscriptionState.
#pragma once
#include "model/Packs.h"
#include <string>
#include <vector>
#include <cstdint>

namespace wr {
struct StoreEntitlement {
    std::string productId, originalTransactionId, jws, state;   // state: subscribed | grace | billingRetry | expired | revoked | unknown
    int64_t purchasedAt = 0, expiresAt = 0, graceUntil = 0; bool revoked = false, trial = false, upgraded = false;
};
// Best current subscription: Church beats Individual when both are usable; upgraded / revoked / expired ones are ignored.
// usedTrial: StoreKit says the intro offer was used (or any record is a trial) - keeps the "Start free trial" button honest.
SubscriptionState subscriptionFromStore (const std::vector<StoreEntitlement>& e, int64_t now, bool introEligible);
// The signed transaction to send to the seat service for team creation ("" if no usable Church subscription).
// v0.13 one trial per person. Both plans live in ONE App Store subscription group, so Apple's introductory offer (2 weeks free)
// is one per Apple ID across Individual AND Church. The Swift bridge asks Product.SubscriptionInfo.isEligibleForIntroOffer(for:
// groupID); until it answers (or if it can't) we treat the user as NOT eligible, so trial wording is never shown wrongly.
enum class IntroEligibility { Unknown, Eligible, Ineligible };
// known/eligible from StoreKit; any trial record in the entitlements also means the trial is spent.
IntroEligibility introEligibilityFrom (bool known, bool eligible, const std::vector<StoreEntitlement>& e);
inline bool showTrialOffer (IntroEligibility i) { return i == IntroEligibility::Eligible; }
// Paywall button text: "Start 2-week free trial, then $4.99/month" only when eligible, else "Subscribe - $4.99/month".
std::string paywallButtonLabel (IntroEligibility i, const std::string& pricePerMonth);
std::string churchTransactionJws (const std::vector<StoreEntitlement>& e, int64_t now);
} // namespace wr
