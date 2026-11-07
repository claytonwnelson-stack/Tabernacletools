// v0.12 C++ face of the StoreKit 2 bridge (WRStoreKit.swift via StoreBridge_ios.mm). All callbacks run on the message thread.
// Non-store builds / non-Apple platforms: available() == false and every call answers immediately with nothing.
#pragma once
#include "model/StoreMapping.h"
#include <functional>
#include <string>
#include <vector>

namespace wr::store {
struct ProductInfo { std::string id, name, displayPrice; bool trialEligible = false; };
bool available();
void start (std::function<void (std::vector<StoreEntitlement>)> onChange);   // Transaction.updates listener + first snapshot
void products (std::function<void (std::vector<ProductInfo>)> done);
void purchase (const std::string& productId, std::function<void (std::string status, std::string error)> done); // success|pending|cancelled|failed
void restore (std::function<void (bool ok, std::string error, std::vector<StoreEntitlement>)> done);
// v0.13: Product.SubscriptionInfo.isEligibleForIntroOffer(for: groupID) for the single Tabernacle Tools subscription group.
// known == false when StoreKit couldn't answer (offline, products missing, non-store build).
void introEligibility (std::function<void (bool known, bool eligible)> done);
void entitlements (std::function<void (std::vector<StoreEntitlement>)> done);
}
