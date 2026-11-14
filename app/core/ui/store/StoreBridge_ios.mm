// v0.12 Objective-C++ bridge: C++ <-> WRStoreKitBridge (Swift). Found at runtime by class name, so this file needs neither a
// bridging header nor the generated "<Module>-Swift.h" header (see BUILD_ON_MAC.md "StoreKit 2 / Swift").
// Same Point shield as Platform_ios.mm: Foundation pulls in MacTypes.h, and
// JuceHeader.h's `using namespace juce` makes juce::Point (a class template)
// ambiguous with QuickDraw Point if the Apple header is included second.
#define Point CarbonDummyPointName
#define Component CarbonDummyCompName
#import <Foundation/Foundation.h>
#undef Point
#undef Component

#include "StoreBridge.h"
#include <JuceHeader.h>

namespace wr::store {
#if WR_STORE_BUILD
static id bridge() {
    static id b = nil;
    if (b == nil) { if (@available (iOS 15.0, macOS 12.0, *)) { Class c = NSClassFromString (@"WRStoreKitBridge"); if (c) b = [[c alloc] init]; else NSLog (@"Bearing Live: StoreKit bridge not found (WRStoreKitBridge Swift class missing - check -ObjC / target membership)"); } }
    return b;
}
static std::string S (id v) { return [v isKindOfClass: [NSString class]] ? std::string ([(NSString*) v UTF8String]) : std::string(); }
static double N (id v) { return [v isKindOfClass: [NSNumber class]] ? [(NSNumber*) v doubleValue] : 0.0; }
static std::vector<StoreEntitlement> convert (NSArray* a) {
    std::vector<StoreEntitlement> out; if (! [a isKindOfClass: [NSArray class]]) return out;
    for (NSDictionary* d in a) { if (! [d isKindOfClass: [NSDictionary class]]) continue;
        StoreEntitlement e; e.productId = S (d[@"productId"]); e.originalTransactionId = S (d[@"originalTransactionId"]); e.jws = S (d[@"jws"]); e.state = S (d[@"state"]);
        e.purchasedAt = (int64_t) N (d[@"purchasedAt"]); e.expiresAt = (int64_t) N (d[@"expiresAt"]); e.graceUntil = (int64_t) N (d[@"graceUntil"]);
        e.revoked = N (d[@"revoked"]) != 0; e.trial = N (d[@"trial"]) != 0; e.upgraded = N (d[@"upgraded"]) != 0; out.push_back (e); }
    return out;
}
// performSelector with object/block arguments only (the Swift methods are designed for this)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Warc-performSelector-leaks"
static bool call1 (const char* sel, id a) { id b = bridge(); SEL s = NSSelectorFromString ([NSString stringWithUTF8String: sel]); if (! b || ! [b respondsToSelector: s]) return false; [b performSelector: s withObject: [a copy]]; return true; }   // copy: moves blocks to the heap (ARC or MRC)
static bool call2 (const char* sel, id a, id c) { id b = bridge(); SEL s = NSSelectorFromString ([NSString stringWithUTF8String: sel]); if (! b || ! [b respondsToSelector: s]) return false; [b performSelector: s withObject: [a copy] withObject: [c copy]]; return true; }
#pragma clang diagnostic pop

bool available() { return bridge() != nil; }
void start (std::function<void (std::vector<StoreEntitlement>)> cb) {
    if (! call1 ("start:", ^(NSArray* a) { cb (convert (a)); })) juce::MessageManager::callAsync ([cb] { cb ({}); });
}
void products (std::function<void (std::vector<ProductInfo>)> cb) {
    if (! call1 ("products:", ^(NSArray* a) { std::vector<ProductInfo> v;
            for (NSDictionary* d in a) v.push_back ({ S (d[@"id"]), S (d[@"displayName"]), S (d[@"displayPrice"]), N (d[@"trialEligible"]) != 0 }); cb (v); }))
        juce::MessageManager::callAsync ([cb] { cb ({}); });
}
void purchase (const std::string& pid, std::function<void (std::string, std::string)> cb) {
    if (! call2 ("purchase:completion:", [NSString stringWithUTF8String: pid.c_str()], ^(NSDictionary* d) { cb (S (d[@"status"]), S (d[@"error"])); }))
        juce::MessageManager::callAsync ([cb] { cb ("failed", "The App Store isn't available in this build"); });
}
void restore (std::function<void (bool, std::string, std::vector<StoreEntitlement>)> cb) {
    if (! call1 ("restore:", ^(NSDictionary* d) { cb (N (d[@"ok"]) != 0, S (d[@"error"]), convert (d[@"entitlements"])); }))
        juce::MessageManager::callAsync ([cb] { cb (false, "The App Store isn't available in this build", {}); });
}
void introEligibility (std::function<void (bool, bool)> cb) {
    if (! call1 ("introEligibility:", ^(NSDictionary* d) { cb (N (d[@"known"]) != 0, N (d[@"eligible"]) != 0); }))
        juce::MessageManager::callAsync ([cb] { cb (false, false); });
}
void entitlements (std::function<void (std::vector<StoreEntitlement>)> cb) {
    if (! call1 ("entitlements:", ^(NSArray* a) { cb (convert (a)); })) juce::MessageManager::callAsync ([cb] { cb ({}); });
}
#else
bool available() { return false; }
void start (std::function<void (std::vector<StoreEntitlement>)> cb) { juce::MessageManager::callAsync ([cb] { cb ({}); }); }
void products (std::function<void (std::vector<ProductInfo>)> cb) { juce::MessageManager::callAsync ([cb] { cb ({}); }); }
void purchase (const std::string&, std::function<void (std::string, std::string)> cb) { juce::MessageManager::callAsync ([cb] { cb ("failed", "Not an App Store build"); }); }
void restore (std::function<void (bool, std::string, std::vector<StoreEntitlement>)> cb) { juce::MessageManager::callAsync ([cb] { cb (false, "Not an App Store build", {}); }); }
void introEligibility (std::function<void (bool, bool)> cb) { juce::MessageManager::callAsync ([cb] { cb (false, false); }); }
void entitlements (std::function<void (std::vector<StoreEntitlement>)> cb) { juce::MessageManager::callAsync ([cb] { cb ({}); }); }
#endif
}
