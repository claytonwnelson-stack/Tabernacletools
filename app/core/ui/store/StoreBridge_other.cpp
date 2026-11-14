// v0.12 non-Apple stub of the StoreKit bridge (Windows/Linux dev builds): no App Store.
#include "StoreBridge.h"
#include <JuceHeader.h>

namespace wr::store {
bool available() { return false; }
void start (std::function<void (std::vector<StoreEntitlement>)> cb) { juce::MessageManager::callAsync ([cb] { cb ({}); }); }
void products (std::function<void (std::vector<ProductInfo>)> cb) { juce::MessageManager::callAsync ([cb] { cb ({}); }); }
void purchase (const std::string&, std::function<void (std::string, std::string)> cb) { juce::MessageManager::callAsync ([cb] { cb ("failed", "Not an App Store build"); }); }
void restore (std::function<void (bool, std::string, std::vector<StoreEntitlement>)> cb) { juce::MessageManager::callAsync ([cb] { cb (false, "Not an App Store build", {}); }); }
void introEligibility (std::function<void (bool, bool)> cb) { juce::MessageManager::callAsync ([cb] { cb (false, false); }); }
void entitlements (std::function<void (std::vector<StoreEntitlement>)> cb) { juce::MessageManager::callAsync ([cb] { cb ({}); }); }
}
