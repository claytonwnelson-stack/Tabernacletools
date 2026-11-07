#include "Platform.h"
namespace wr::platform {
HealthReading deviceHealth() { return {}; }
juce::String clipboardText() { return juce::SystemClipboard::getTextFromClipboard(); }
std::vector<SharedChart> takeSharedCharts() { return takeInboxFiles (dataDirectory().getChildFile ("Inbox")); }
juce::File dataDirectory() { return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile (WR_DATA_FOLDER); }
juce::String extractPdfText (const juce::File& f) {
    // Desktop dev builds: use poppler's pdftotext -layout if present.
    juce::ChildProcess p; juce::StringArray args { "pdftotext", "-layout", f.getFullPathName(), "-" };
    if (p.start (args)) return p.readAllProcessOutput();
    return {};
}
void enableNetworkMidi() {}
void requestLowLatency (double, int) {}
void renderSpeech (const juce::String&, double, std::function<void (std::vector<float>)> done) { juce::MessageManager::callAsync ([done] { done ({}); }); }
int externalDisplayCount() { return juce::Desktop::getInstance().getDisplays().displays.size() > 1 ? 1 : 0; }
void setExternalDisplayContent (juce::Component* c) {
    static std::unique_ptr<juce::Component> holder;   // a top-level window hosting the projector view
    if (! c) { holder.reset(); return; }
    auto& ds = juce::Desktop::getInstance().getDisplays().displays;
    auto area = ds.size() > 1 ? ds.getReference (1).totalArea : juce::Rectangle<int> (100, 100, 1280, 720);
    holder = std::make_unique<juce::Component>(); holder->setOpaque (true); holder->addAndMakeVisible (c);
    holder->setBounds (area); c->setBounds (holder->getLocalBounds());
    holder->addToDesktop (ds.size() > 1 ? 0 : juce::ComponentPeer::windowHasTitleBar | juce::ComponentPeer::windowIsResizable); holder->setVisible (true);
}
void shareFiles (const juce::Array<juce::File>& files, juce::Component*) { if (! files.isEmpty()) files.getFirst().revealToUser(); }
void signInWithApple (std::function<void (juce::String, juce::String)> done) { juce::MessageManager::callAsync ([done] { done ({}, "Sign in with Apple needs the iPad build"); }); }
void churchSubscriptionJws (std::function<void (juce::String)> done) { juce::MessageManager::callAsync ([done] { done ({}); }); }
}
