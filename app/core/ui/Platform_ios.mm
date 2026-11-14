// Apple headers before JuceHeader.h (pulled in by Platform.h). That generated
// header ends with `using namespace juce`, so the class template juce::Point is
// already visible. Foundation, UIKit, PDFKit and CoreMIDI include MacTypes.h,
// which then declares QuickDraw `struct Point`. Parsing that declaration while
// the template is in scope is the iOS build error "Reference to 'Point' is
// ambiguous" / "class template 'Point' requires template arguments". Renaming
// Point and Component for this import is the same shield JUCE uses for
// Component in juce_BasicNativeHeaders.h. Do not edit the Apple SDK headers.
#import <TargetConditionals.h>
#define Point CarbonDummyPointName
#define Component CarbonDummyCompName
#import <Foundation/Foundation.h>
#import <PDFKit/PDFKit.h>
#import <CoreMIDI/CoreMIDI.h>
#import <AuthenticationServices/AuthenticationServices.h>
#if TARGET_OS_IPHONE
 #import <AVFoundation/AVFoundation.h>
 #import <UIKit/UIKit.h>
 #import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#endif
#undef Point
#undef Component

#include "Platform.h"
#include "store/StoreBridge.h"
#include <cmath>

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

#if JUCE_IOS
// Kept alive for the whole picker session. UIDocumentPicker's delegate is weak, so a local object would
// be freed before UIKit calls back, and the app would crash on the way out of the picker.
static juce::File copyPickedChart (NSURL* url, juce::String& error) {
    error.clear();
    if (url == nil) { error = "Couldn't read that file."; return {}; }
    @autoreleasepool {
        const BOOL scoped = [url startAccessingSecurityScopedResource];
        NSError* err = nil;
        NSData* data = [NSData dataWithContentsOfURL: url options: 0 error: &err];
        if (scoped) [url stopAccessingSecurityScopedResource];
        if (data == nil || data.bytes == nil) {
            error = (err != nil && err.localizedDescription.UTF8String != nullptr) ? juce::String::fromUTF8 (err.localizedDescription.UTF8String) : juce::String ("Couldn't read that file.");
            return {};
        }
        if (data.length > 4 * 1024 * 1024) { error = "That file is larger than 4 MB."; return {}; }
        if (data.length == 0) { error = "Couldn't read that file. Use ChordPro, plain text, or a text PDF."; return {}; }
        const char* utf = url.lastPathComponent.UTF8String;
        juce::String leaf = juce::File::createLegalFileName (utf != nullptr ? juce::String::fromUTF8 (utf) : juce::String());
        if (leaf.isEmpty()) leaf = "chart.txt";
        auto prefix = leaf.containsChar ('.') ? leaf.upToLastOccurrenceOf (".", false, false) : leaf;
        auto suffix = leaf.containsChar ('.') ? leaf.fromLastOccurrenceOf (".", true, false) : juce::String (".txt");
        if (prefix.isEmpty()) prefix = "chart";
        if (suffix.isEmpty()) suffix = ".txt";
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("tt-import");
        dir.createDirectory();
        auto dest = dir.getNonexistentChildFile (prefix, suffix);
        if (! dest.replaceWithData (data.bytes, (size_t) data.length)) { error = "Couldn't read that file."; return {}; }
        return dest;
    }
}

@interface WRChartImportPicker : NSObject <UIDocumentPickerDelegate, UIAdaptivePresentationControllerDelegate>
@property (nonatomic, copy) void (^done) (NSURL* url, NSString* error);
@property (nonatomic, strong) UIDocumentPickerViewController* picker;
@end

static WRChartImportPicker* gChartPicker = nil;

@implementation WRChartImportPicker
- (UIModalPresentationStyle) adaptivePresentationStyleForPresentationController: (UIPresentationController*) controller {
    juce::ignoreUnused (controller);
    return UIModalPresentationFormSheet; // stay a sheet; an adapted popover with no anchor is the iPad crash
}
- (UIModalPresentationStyle) adaptivePresentationStyleForPresentationController: (UIPresentationController*) controller traitCollection: (UITraitCollection*) trait {
    juce::ignoreUnused (controller, trait);
    return UIModalPresentationFormSheet;
}
- (void) finish: (NSURL*) url error: (NSString*) error {
    void (^cb) (NSURL*, NSString*) = self.done;
    self.done = nil; self.picker = nil; gChartPicker = nil;
    if (cb != nil) cb (url, error);
}
- (void) documentPicker: (UIDocumentPickerViewController*) controller didPickDocumentsAtURLs: (NSArray<NSURL*>*) urls {
    juce::ignoreUnused (controller);
    [self finish: urls.firstObject error: nil];
}
- (void) documentPickerWasCancelled: (UIDocumentPickerViewController*) controller {
    juce::ignoreUnused (controller);
    [self finish: nil error: nil];
}
@end

static UIViewController* topViewController() {
    UIViewController* root = nil;
    if (@available (iOS 13.0, *)) {
        for (UIScene* sc in [UIApplication sharedApplication].connectedScenes)
            if ([sc isKindOfClass: [UIWindowScene class]])
                for (UIWindow* w in ((UIWindowScene*) sc).windows)
                    if (w.isKeyWindow) { root = w.rootViewController; break; }
    }
    if (root == nil) root = [UIApplication sharedApplication].windows.firstObject.rootViewController;
    while (root.presentedViewController != nil) root = root.presentedViewController;
    return root;
}

static UIDocumentPickerViewController* makeChartPicker (bool anyFile) API_AVAILABLE (ios (14.0)) {
    NSMutableArray<UTType*>* types = [NSMutableArray array];
    if (anyFile) {
        if (UTType* data = [UTType typeWithIdentifier: @"public.data"]) [types addObject: data];
    } else {
        for (NSString* ident in @[ @"public.plain-text", @"public.text", @"com.adobe.pdf", @"public.data", @"org.chordpro.chart" ])
            if (UTType* t = [UTType typeWithIdentifier: ident]) [types addObject: t];
    }
    if (types.count == 0) return nil;
    return [[UIDocumentPickerViewController alloc] initForOpeningContentTypes: types asCopy: YES];
}
#endif

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
void pickChartFile (juce::Component* anchor, std::function<void (const juce::File&, const juce::String&)> done) {
   #if JUCE_IOS
    if (done == nullptr) return;
    if (! juce::MessageManager::getInstance()->isThisTheMessageThread()) {
        juce::Component::SafePointer<juce::Component> safe (anchor);
        juce::MessageManager::callAsync ([safe, done] { pickChartFile (safe.getComponent(), done); });
        return;
    }
    if (gChartPicker != nil) return;
    if (@available (iOS 14.0, *)) {
        UIDocumentPickerViewController* picker = nil;
        @try { picker = makeChartPicker (false); } @catch (NSException*) { picker = nil; }
        if (picker == nil) { @try { picker = makeChartPicker (true); } @catch (NSException*) { picker = nil; } }
        UIViewController* root = topViewController();
        if (picker == nil || root == nil) {
            juce::MessageManager::callAsync ([done] { done ({}, "The iPad could not open the file picker."); });
            return;
        }
        picker.allowsMultipleSelection = NO;
        picker.modalPresentationStyle = UIModalPresentationFormSheet;
        gChartPicker = [[WRChartImportPicker alloc] init];
        gChartPicker.picker = picker;
        picker.delegate = gChartPicker;
        if (picker.presentationController != nil) picker.presentationController.delegate = gChartPicker;
        juce::Rectangle<int> b = anchor != nullptr ? anchor->getScreenBounds() : juce::Rectangle<int> (80, 80, 2, 2);
        if (b.getWidth() < 2) b.setWidth (2);
        if (b.getHeight() < 2) b.setHeight (2);
        CGRect screen = CGRectMake ((CGFloat) b.getX(), (CGFloat) b.getY(), (CGFloat) b.getWidth(), (CGFloat) b.getHeight());
        CGRect rect = [root.view convertRect: screen fromView: nil];
        if (rect.size.width < 2) rect.size.width = 2;
        if (rect.size.height < 2) rect.size.height = 2;
        if (picker.popoverPresentationController != nil) {
            picker.popoverPresentationController.sourceView = root.view;
            picker.popoverPresentationController.sourceRect = rect;
            picker.popoverPresentationController.permittedArrowDirections = UIPopoverArrowDirectionAny;
        }
        gChartPicker.done = ^(NSURL* url, NSString* err) {
            juce::String error = (err != nil && err.UTF8String != nullptr) ? juce::String::fromUTF8 (err.UTF8String) : juce::String();
            juce::File file;
            if (url != nil && error.isEmpty()) file = copyPickedChart (url, error);
            juce::MessageManager::callAsync ([done, file, error] { done (file, error); });
        };
        @try {
            [root presentViewController: picker animated: YES completion: nil];
        } @catch (NSException* ex) {
            if (gChartPicker != nil) { gChartPicker.done = nil; gChartPicker.picker = nil; gChartPicker = nil; }
            juce::String why = (ex.reason != nil && ex.reason.UTF8String != nullptr) ? juce::String::fromUTF8 (ex.reason.UTF8String) : juce::String ("The iPad could not open the file picker.");
            juce::MessageManager::callAsync ([done, why] { done ({}, why); });
        }
        return;
    }
    juce::MessageManager::callAsync ([done] { done ({}, "Needs iPadOS 14 or later"); });
   #else
    auto chooser = std::make_shared<juce::FileChooser> ("Import chart", juce::File(), "*.cho;*.chopro;*.chordpro;*.txt;*.pdf;*.crd", true, false, anchor);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [chooser, done] (const juce::FileChooser& fc) {
        auto f = fc.getResult(); if (done) done (f.existsAsFile() ? f : juce::File(), {}); });
   #endif
}
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
struct IosMidiPort : MidiSourcePort {
    MIDIClientRef client = 0; MIDIPortRef port = 0; MIDIEndpointRef source = 0;
    void (*fn) (void*, uint8_t, uint8_t, uint8_t, double) = nullptr; void* ctx = nullptr;
    ~IosMidiPort() override { if (port && source) MIDIPortDisconnectSource (port, source); if (port) MIDIPortDispose (port); if (client) MIDIClientDispose (client); }
    void start() override {}
    static void read (const MIDIPacketList* list, void* ref, void*) {
        auto* self = (IosMidiPort*) ref; if (! self || ! self->fn || ! list) return;
        const MIDIPacket* p = &list->packet[0];
        for (UInt32 i = 0; i < list->numPackets; ++i) {
            const UInt8* d = p->data; int n = (int) p->length, o = 0;
            while (o < n) {
                uint8_t st = d[o]; if (st < 0x80) { ++o; continue; }
                int need = 3; if (st >= 0xF8) need = 1; else if (st == 0xF2) need = 3; else if ((st & 0xF0) == 0xC0 || (st & 0xF0) == 0xD0 || st == 0xF1 || st == 0xF3) need = 2;
                if (o + need > n) break;
                self->fn (self->ctx, st, need > 1 ? d[o + 1] : 0, need > 2 ? d[o + 2] : 0, juce::Time::getMillisecondCounterHiRes());
                o += need;
            }
            p = MIDIPacketNext (p);
        }
    }
};
std::unique_ptr<MidiSourcePort> openMidiSourceIndex (int index, void (*fn) (void*, uint8_t, uint8_t, uint8_t, double), void* ctx) {
    if (index < 0 || fn == nullptr) return {};
    ItemCount n = MIDIGetNumberOfSources(); if ((ItemCount) index >= n) return {};
    MIDIEndpointRef src = MIDIGetSource ((ItemCount) index); if (src == 0) return {};
    auto port = std::make_unique<IosMidiPort>(); port->fn = fn; port->ctx = ctx; port->source = src;
    if (MIDIClientCreate (CFSTR ("BearingLiveFoot"), nullptr, nullptr, &port->client) != noErr) return {};
    if (MIDIInputPortCreate (port->client, CFSTR ("Foot"), IosMidiPort::read, port.get(), &port->port) != noErr) return {};
    if (MIDIPortConnectSource (port->port, src, nullptr) != noErr) return {};
    return port;
}
void churchSubscriptionJws (std::function<void (juce::String)> done) {
    // v0.12: StoreKit 2 Transaction.currentEntitlements via WRStoreKit.swift; the seat service verifies this JWS
    wr::store::entitlements ([done] (std::vector<wr::StoreEntitlement> e) {
        done (juce::String (wr::churchTransactionJws (e, (int64_t) juce::Time::currentTimeMillis() / 1000))); });
}
}
