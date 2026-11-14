// v0.13 tests: one free trial per person (single subscription group intro-offer eligibility -> paywall wording).
#include "model/StoreMapping.h"
#include <cstdio>
using namespace wr;
static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (! (c)) { ++fails; std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)
int main() {
    const std::string IND = "com.tabernacletools.guitar.individual.monthly", CH = "com.tabernacletools.guitar.church.monthly";
    std::vector<StoreEntitlement> none;
    CHECK (introEligibilityFrom (true, true, none) == IntroEligibility::Eligible);
    CHECK (introEligibilityFrom (true, false, none) == IntroEligibility::Ineligible);
    CHECK (introEligibilityFrom (false, true, none) == IntroEligibility::Unknown);
    // a trial taken on EITHER plan spends it for both (same group)
    StoreEntitlement t; t.productId = CH; t.trial = true; t.expiresAt = 100; t.state = "expired";
    CHECK (introEligibilityFrom (true, true, { t }) == IntroEligibility::Ineligible);
    t.productId = IND; CHECK (introEligibilityFrom (false, false, { t }) == IntroEligibility::Ineligible);
    // wording: trial text only when eligible; Unknown/ineligible never mention a trial
    CHECK (paywallButtonLabel (IntroEligibility::Eligible, "$4.99") == "Start 2-week free trial, then $4.99/month");
    CHECK (paywallButtonLabel (IntroEligibility::Ineligible, "$19.99") == "Subscribe - $19.99/month");
    CHECK (paywallButtonLabel (IntroEligibility::Unknown, "$4.99").find ("trial") == std::string::npos);
    CHECK (! showTrialOffer (IntroEligibility::Unknown) && ! showTrialOffer (IntroEligibility::Ineligible));
    // usedTrial follows eligibility in the subscription model
    CHECK (subscriptionFromStore (none, 50, false).usedTrial);
    CHECK (! subscriptionFromStore (none, 50, true).usedTrial);
    CHECK (subscriptionFromStore ({ t }, 50, true).usedTrial);
    std::printf ("v0.13 trial: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
