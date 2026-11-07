#include "Platform.h"
#include "store/StoreBridge.h"
#include <cmath>
#import <Foundation/Foundation.h>
#import <PDFKit/PDFKit.h>
#import <CoreMIDI/CoreMIDI.h>
#import <AuthenticationServices/AuthenticationServices.h>
#if JUCE_IOS
 #import <AVFoundation/AVFoundation.h>
 #import <UIKit/UIKit.h>
#endif

// v0.11 Sign in with Apple delegate (ASAuthorizationController). Holds the callback until Apple answers.
API_AVAILABLE(ios(13.0), macos(10.15))
@interface WRSiwaDelegate : NSObject <ASAuthorizationControllerDelegate, ASAuthorizationControllerPresentationContextProviding>
@property (nonatomic, copy) void (^done)(NSString* token, NSString* error);
@property (nonatomic, strong) ASAuthorizationController* controller;
@end
@implementation WRSiwaDelegate
- (void) authorizationController: (ASAuthorizationController*) c didCompleteWithAuthorization: (ASAuthorization*) auth API_AVAILABLE(ios(13.0), macos(10.15)) {
    NSString* tok = nil;
    if ([auth.credential isKindOfClass: [ASAuthorizationAppleIDCredential class]]) {
        NSData* d = ((ASAuthorizationAppleIDCredential*) auth.credential).identityToken; if (d) tok = [[NSString alloc] initWithData: d encoding: NSUTF8StringEncoding];
    }
    if (self.done) self.done (tok, tok ? nil : @"Apple returned no identity token"); self.done = nil;
}
- (void) authorizationController: (ASAuthorizationController*) c didCompleteWithError: (NSError*) e API_AVAILABLE(ios(13.0), macos(10.15)) {
    if (self.done) self.done (nil, e.code == ASAuthorizationErrorCanceled ? @"" : e.localizedDescription); self.done = nil;
}
- (ASPresentationAnchor) presentationAnchorForAuthorizationController: (ASAuthorizationController*) c API_AVAILABLE(ios(13.0), macos(10.15)) {
#if JUCE_IOS
    for (UIScene* sc in [UIApplication sharedApplication].connectedScenes)
        if ([sc isKindOfClass: [UIWindowScene class]]) for (UIWindow* w in ((UIWindowScene*) sc).windows) if (w.isKeyWindow) return w;
    return [UIApplication sharedApplication].windows.firstObject;
#else
    return [NSApplication sharedApplication].keyWindow;
#endif
}
@end

namespace wr::platform {
juce::String clipboardText() { return juce::SystemClipboard::getTextFromClipboard(); }
std::vector<SharedChart> takeSharedCharts() {
   #if JUCE_IOS
    std::vector<SharedChart> v;
    @autoreleasepool {
        auto docs = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
        v = takeInboxFiles (docs.getChildFile ("Inbox"));
        UIPasteboard* pb = [UIPasteboard generalPasteboard];
        NSString* type = [NSString stringWithUTF8String: kSharedChartType];
        if ([pb containsPasteboardTypes: @[ type ]]) {
            NSData* d = [pb dataForPasteboardType: type];
            if (d != nil && d.length > 0) {
                SharedChart c; c.text = juce::String::fromUTF8 ((const char*) d.bytes, (int) d.length); c.name = "Shared chart";
                v.push_back (c);
            }
            pb.items = @[];   // take it once
        }
    }
    return v;
   #else
    return takeInboxFiles (dataDirectory().getChildFile ("Inbox"));
   #endif
}
HealthReading deviceHealth() {
    HealthReading h;
   #if JUCE_IOS
    @autoreleasepool {
        UIDevice* d = [UIDevice currentDevice]; if (! d.batteryMonitoringEnabled) d.batteryMonitoringEnabled = YES;
        const float lvl = d.batteryLevel; h.batteryPct = lvl < 0 ? -1 : (int) std::lround (lvl * 100.0f);
        h.charging = d.batteryState == UIDeviceBatteryStateCharging || d.batteryState == UIDeviceBatteryStateFull;
        switch ([[NSProcessInfo processInfo] thermalState]) { case NSProcessInfoThermalStateFair: h.thermal = 1; break; case NSProcessInfoThermalStateSerious: h.thermal = 2; break;
            case NSProcessInfoThermalStateCritical: h.thermal = 3; break; default: h.thermal = 0; break; }
    }
   #else
    if (@available (macOS 10.10.3, *)) { auto t = [[NSProcessInfo processInfo] thermalState]; h.thermal = t == NSProcessInfoThermalStateCritical ? 3 : t == NSProcessInfoThermalStateSerious ? 2 : t == NSProcessInfoThermalStateFair ? 1 : 0; }
   #endif
    return h;
}
juce::File dataDirectory() {
   #ifdef WR_APP_GROUP
    NSURL* url = [[NSFileManager defaultManager] containerURLForSecurityApplicationGroupIdentifier: @WR_APP_GROUP];
    if (url != nil) return juce::File (juce::String::fromUTF8 (url.path.UTF8String)).getChildFile (WR_DATA_FOLDER);
   #endif
    // free Apple IDs sometimes can't use app groups -> fall back to the app's own Documents (visible in Files app)
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (WR_DATA_FOLDER);
}
juce::String extractPdfText (const juce::File& f) {
    @autoreleasepool {
        NSURL* url = [NSURL fileURLWithPath: [NSString stringWithUTF8String: f.getFullPathName().toRawUTF8()]];
        PDFDocument* doc = [[PDFDocument alloc] initWithURL: url];
        if (doc == nil) return {};
        juce::String out;
        for (NSUInteger i = 0; i < doc.pageCount; ++i) {
            PDFPage* page = [doc pageAtIndex: i];
            NSString* s = page.string; // PDFKit keeps line breaks; column alignment of chord lines is approximate
            if (s != nil) out << juce::String::fromUTF8 (s.UTF8String) << "\n";
        }
       #if ! __has_feature(objc_arc)
        [doc release];
       #endif
        return out;
    }
}
void enableNetworkMidi() {
   #if JUCE_IOS
    MIDINetworkSession* session = [MIDINetworkSession defaultSession];
    session.enabled = YES;
    session.connectionPolicy = MIDINetworkConnectionPolicy_Anyone;
   #endif
}
void requestLowLatency (double sampleRate, int frames) {
   #if JUCE_IOS
    AVAudioSession* s = [AVAudioSession sharedInstance];
    NSError* err = nil;
    [s setPreferredSampleRate: sampleRate error: &err];
    [s setPreferredIOBufferDuration: (double) frames / sampleRate error: &err];
   #else
    juce::ignoreUnused (sampleRate, frames);
   #endif
}
void shareFiles (const juce::Array<juce::File>& files, juce::Component* anchor) {
   #if JUCE_IOS
    NSMutableArray* items = [NSMutableArray array];
    for (auto& f : files) [items addObject: [NSURL fileURLWithPath: [NSString stringWithUTF8String: f.getFullPathName().toRawUTF8()]]];
    if (items.count == 0) return;
    UIActivityViewController* vc = [[UIActivityViewController alloc] initWithActivityItems: items applicationActivities: nil];
    UIViewController* root = nil;
    if (anchor != nullptr) if (auto* peer = anchor->getPeer()) { UIView* v = (UIView*) peer->getNativeHandle(); root = v.window.rootViewController; }
    if (root == nil) for (UIScene* sc in [UIApplication sharedApplication].connectedScenes) if ([sc isKindOfClass: [UIWindowScene class]]) { root = ((UIWindowScene*) sc).windows.firstObject.rootViewController; break; }
    while (root.presentedViewController != nil) root = root.presentedViewController;
    if (root == nil) return;
    if (vc.popoverPresentationController != nil) { // iPad: share sheet is a popover anchored on the button
        vc.popoverPresentationController.sourceView = root.view;
        juce::Rectangle<int> r = anchor != nullptr ? anchor->getScreenBounds() : juce::Rectangle<int> (100, 100, 1, 1);
        vc.popoverPresentationController.sourceRect = CGRectMake (r.getX(), r.getY(), r.getWidth(), r.getHeight());
    }
    [root presentViewController: vc animated: YES completion: nil];
   #else
    if (! files.isEmpty()) files.getFirst().revealToUser(); juce::ignoreUnused (anchor);
   #endif
}
void renderSpeech (const juce::String& text, double sampleRate, std::function<void (std::vector<float>)> done) {
   #if JUCE_IOS
    if (@available (iOS 13.0, *)) {
        static AVSpeechSynthesizer* synth = [[AVSpeechSynthesizer alloc] init]; // must outlive the render
        AVSpeechUtterance* u = [AVSpeechUtterance speechUtteranceWithString: [NSString stringWithUTF8String: text.toRawUTF8()]];
        u.voice = [AVSpeechSynthesisVoice voiceWithLanguage: @"en-US"]; u.rate = AVSpeechUtteranceDefaultSpeechRate * 1.15f;
        auto acc = std::make_shared<std::vector<float>>(); auto srcRate = std::make_shared<double> (0.0); auto fired = std::make_shared<bool> (false);
        [synth writeUtterance: u toBufferCallback: ^(AVAudioBuffer* buffer) {
            AVAudioPCMBuffer* pcm = (AVAudioPCMBuffer*) buffer;
            if (pcm == nil || pcm.frameLength == 0) { // end of utterance: resample + trim, hand back on the message thread
                if (*fired) return; *fired = true;
                std::vector<float> out; double sr = *srcRate > 0 ? *srcRate : 22050;
                if (! acc->empty()) { const double ratio = sr / sampleRate; size_t n = (size_t) ((double) (acc->size() - 1) / ratio) + 1; out.resize (n);
                    for (size_t i = 0; i < n; ++i) { double x = (double) i * ratio; size_t a = (size_t) x; double f = x - (double) a; float y0 = (*acc)[a], y1 = a + 1 < acc->size() ? (*acc)[a + 1] : y0; out[i] = (float) (y0 + (y1 - y0) * f); }
                    size_t a = 0, b = out.size(); while (a < b && std::abs (out[a]) < 0.01f) ++a; while (b > a && std::abs (out[b - 1]) < 0.01f) --b; out = std::vector<float> (out.begin() + (long) a, out.begin() + (long) b);
                    float pk = 0; for (float v : out) pk = std::max (pk, std::abs (v)); if (pk > 0) for (auto& v : out) v *= 0.89f / pk; }
                juce::MessageManager::callAsync ([done, out] { done (out); });
                return;
            }
            *srcRate = pcm.format.sampleRate; const AVAudioFrameCount n = pcm.frameLength;
            if (pcm.floatChannelData != nullptr) { const float* d = pcm.floatChannelData[0]; acc->insert (acc->end(), d, d + n); }
            else if (pcm.int16ChannelData != nullptr) { const int16_t* d = pcm.int16ChannelData[0]; for (AVAudioFrameCount i = 0; i < n; ++i) acc->push_back ((float) d[i] / 32768.0f); }
        }];
        return;
    }
   #endif
    juce::ignoreUnused (text, sampleRate); juce::MessageManager::callAsync ([done] { done ({}); });
}

// ---- external display (projector). iOS 16+: the external screen arrives as a UIWindowScene with role
// UIWindowSceneSessionRoleExternalDisplayNonInteractive; older: UIScreen.screens. Unverified on device.
#if JUCE_IOS
static juce::Component* gExtContent = nullptr;
static UIWindow* gExtWindow = nil;
static id gExtObservers[2] = { nil, nil };
static UIScreen* externalScreen() { for (UIScreen* s in [UIScreen screens]) if (s != [UIScreen mainScreen]) return s; return nil; }
static void detachExternal() {
    if (gExtContent != nullptr && gExtContent->isOnDesktop()) gExtContent->removeFromDesktop();
    if (gExtWindow != nil) { gExtWindow.hidden = YES;
       #if ! __has_feature(objc_arc)
        [gExtWindow release];
       #endif
        gExtWindow = nil; }
}
static void attachExternal() {
    detachExternal(); UIScreen* scr = externalScreen(); if (scr == nil || gExtContent == nullptr) return;
    UIWindow* w = nil;
    if (@available (iOS 13.0, *)) {
        for (UIScene* sc in [UIApplication sharedApplication].connectedScenes)
            if ([sc isKindOfClass: [UIWindowScene class]] && ((UIWindowScene*) sc).screen == scr) { w = [[UIWindow alloc] initWithWindowScene: (UIWindowScene*) sc]; break; }
    }
    if (w == nil) { w = [[UIWindow alloc] initWithFrame: scr.bounds]; w.screen = scr; }
    w.rootViewController = [[UIViewController alloc] init]; w.backgroundColor = [UIColor blackColor]; w.hidden = NO; gExtWindow = w;
    const CGRect b = scr.bounds; w.frame = b; w.rootViewController.view.frame = b;
    gExtContent->setBounds (0, 0, (int) b.size.width, (int) b.size.height);
    gExtContent->addToDesktop (0, (void*) w.rootViewController.view);
    gExtContent->setVisible (true);
}
int externalDisplayCount() { return externalScreen() != nil ? 1 : 0; }
void setExternalDisplayContent (juce::Component* c) {
    detachExternal(); gExtContent = c;
    NSNotificationCenter* nc = [NSNotificationCenter defaultCenter];
    for (auto& o : gExtObservers) if (o != nil) { [nc removeObserver: o]; o = nil; }
    if (c == nullptr) return;
    gExtObservers[0] = [nc addObserverForName: UIScreenDidConnectNotification object: nil queue: [NSOperationQueue mainQueue] usingBlock: ^(NSNotification*) { attachExternal(); }];
    gExtObservers[1] = [nc addObserverForName: UIScreenDidDisconnectNotification object: nil queue: [NSOperationQueue mainQueue] usingBlock: ^(NSNotification*) { detachExternal(); }];
    attachExternal();
}
#else
int externalDisplayCount() { return 0; }
void setExternalDisplayContent (juce::Component*) {}
#endif
void signInWithApple (std::function<void (juce::String, juce::String)> done) {
    if (@available (iOS 13.0, macOS 10.15, *)) {
        static WRSiwaDelegate* del = nil; del = [[WRSiwaDelegate alloc] init];
        del.done = ^(NSString* tok, NSString* err) {
            juce::String t = tok ? juce::String ([tok UTF8String]) : juce::String(), e = err ? juce::String ([err UTF8String]) : juce::String();
            juce::MessageManager::callAsync ([done, t, e] { done (t, e); });
        };
        ASAuthorizationAppleIDRequest* req = [[[ASAuthorizationAppleIDProvider alloc] init] createRequest];
        req.requestedScopes = @[];   // privacy-minimal: no name, no email
        del.controller = [[ASAuthorizationController alloc] initWithAuthorizationRequests: @[req]];
        del.controller.delegate = del; del.controller.presentationContextProvider = del;
        [del.controller performRequests];
    } else juce::MessageManager::callAsync ([done] { done ({}, "Needs iPadOS 13 or later"); });
}
void churchSubscriptionJws (std::function<void (juce::String)> done) {
    // v0.12: StoreKit 2 Transaction.currentEntitlements via WRStoreKit.swift; the seat service verifies this JWS
    wr::store::entitlements ([done] (std::vector<wr::StoreEntitlement> e) {
        done (juce::String (wr::churchTransactionJws (e, (int64_t) juce::Time::currentTimeMillis() / 1000))); });
}
}
