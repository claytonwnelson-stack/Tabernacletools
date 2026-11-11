// Small platform shims (iOS implementation in Platform_ios.mm; desktop fallbacks in Platform_other.cpp).
#pragma once
#include <JuceHeader.h>

#ifndef WR_DATA_FOLDER
 #define WR_DATA_FOLDER "TabernacleTools"   // per app: Guitar keeps "TabernacleTools" (existing libraries), Core Test uses "TabernacleCoreTest"
#endif
namespace wr::platform {
juce::File dataDirectory();                       // app-group container on iOS (shared by app + AUv3), app data dir elsewhere
juce::String extractPdfText (const juce::File&);  // text layer of a PDF (PDFKit on iOS); empty if none (scanned image)
void enableNetworkMidi();                         // turn on the CoreMIDI network session so Cubase on the Mac can connect
void requestLowLatency (double sampleRate, int bufferFrames); // AVAudioSession preferred sample rate / IO buffer
// iOS: AVSpeechSynthesizer rendered to a buffer (custom section names not in the bundled word set). Async; the callback
// runs on the message thread with mono samples at `sampleRate` (empty if unavailable). Desktop: calls back with empty.
void renderSpeech (const juce::String& text, double sampleRate, std::function<void (std::vector<float>)> done);
// external display (HDMI / USB-C adapter, AirPlay): iOS UIScreen / UIWindowScene for the external screen. The component
// fills that screen while one is connected and is re-attached automatically on plug-in; nullptr detaches.
// Desktop: a borderless window on the second monitor (or a normal window if there is only one).
int externalDisplayCount();
void setExternalDisplayContent (juce::Component* content);
// v0.7 battery + thermal: iOS UIDevice battery (monitoring on) + ProcessInfo.thermalState; desktop: unknown (-1, 0)
struct HealthReading { int batteryPct = -1; bool charging = false; int thermal = 0; };
HealthReading deviceHealth();
// v0.7 share import. Charts handed to the app from outside: files iOS put in Documents/Inbox ("Open in" / "Copy to
// Tabernacle Tools" from Files, Mail, Safari downloads), and text the optional Share Extension left on the pasteboard under
// kSharedChartType (free Apple IDs have no App Group, so the pasteboard is the hand-off). Each call takes them (files are
// moved to <data>/imported/). Desktop: files dropped in <data>/Inbox. Nothing is downloaded from the web.
static constexpr const char* kSharedChartType = "com.tabernacletools.shared-chart";
struct SharedChart { juce::String text, name; };
std::vector<SharedChart> takeSharedCharts();
juce::String clipboardText();
// Chart import. iOS does not use juce::FileChooser: on iPad that picker is a popover, and JUCE presents it
// before the popover has a source view, so UIKit throws and the app dies before the picker appears.
// This presents UIDocumentPickerViewController on the message thread, keeps it alive until it finishes,
// anchors the popover on `anchor`, and copies the file in (asCopy) using system UTIs. Empty file and empty
// error means the user cancelled.
void pickChartFile (juce::Component* anchor, std::function<void (const juce::File& file, const juce::String& error)> done);
inline bool isStemAudio (const juce::File& f) { return f.hasFileExtension ("wav;aif;aiff;m4a;mp3;caf;aac"); }
inline juce::File shareInbox() {
   #if JUCE_IOS
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Inbox");   // "Open in / Copy to Tabernacle Tools"
   #else
    return dataDirectory().getChildFile ("Inbox");
   #endif
}
// v0.16 Share Sheet / "Copy to Tabernacle Tools" audio files: moved out of the inbox into <data>/SharedStems and returned
inline juce::Array<juce::File> takeSharedStems() {
    juce::Array<juce::File> out; auto in = shareInbox(); if (! in.isDirectory()) return out;
    auto dir = dataDirectory().getChildFile ("SharedStems"); dir.createDirectory();
    for (auto& f : in.findChildFiles (juce::File::findFiles, false)) if (isStemAudio (f)) {
        auto d = dir.getNonexistentChildFile (f.getFileNameWithoutExtension(), f.getFileExtension()); if (f.moveFileTo (d)) out.add (d); }
    return out;
}
inline std::vector<SharedChart> takeInboxFiles (const juce::File& inbox) {   // read + move every chart-like file out of an inbox
    std::vector<SharedChart> v; if (! inbox.isDirectory()) return v;
    auto done = dataDirectory().getChildFile ("imported"); done.createDirectory();
    for (auto& f : inbox.findChildFiles (juce::File::findFiles, false)) {
        if (f.getFileName().startsWithChar ('.') || isStemAudio (f)) continue;   // v0.16: audio is for backing tracks (takeSharedStems)
        SharedChart c; c.name = f.getFileNameWithoutExtension();
        if (f.hasFileExtension ("pdf")) c.text = extractPdfText (f);
        else if (f.hasFileExtension ("txt;cho;chopro;chordpro;crd;pro;text;html;htm") || f.getFileExtension().isEmpty()) c.text = f.loadFileAsString();
        else c.text = "[unsupported]" + f.getFileName();
        f.moveFileTo (done.getNonexistentChildFile (f.getFileNameWithoutExtension(), f.getFileExtension()));
        v.push_back (c);
    }
    return v;
}                     // "Paste chart" (juce::SystemClipboard)
void shareFiles (const juce::Array<juce::File>& files, juce::Component* anchor); // iOS share sheet (AirDrop, Messages, Mail, Files); desktop: reveal
// v0.11 church seats (App Store builds). Sign in with Apple: calls back on the message thread with the identity token (JWT,
// verified server-side) or an error. Desktop: error "Sign in with Apple needs the iPad build".
void signInWithApple (std::function<void (juce::String identityToken, juce::String error)> done);
// Latest Church-plan transaction as StoreKit 2 Transaction.jwsRepresentation (empty if none), via the v0.12 Swift bridge
// (core/ui/store/WRStoreKit.swift). Desktop / non-store builds: empty.
void churchSubscriptionJws (std::function<void (juce::String jws)> done);
}
