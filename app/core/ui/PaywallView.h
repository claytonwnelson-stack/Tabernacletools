// v0.12 paywall: Individual $4.99/mo, Church $19.99/mo (15 people incl. the leader), 2-week free trial (once per Apple ID, across both plans), Restore purchases.
// Shown when a locked feature is tapped in read-only mode, and from Settings > Subscription. Prices come from the App Store
// (localised displayPrice) when available, else the US list prices. Never shown in sideload/dev builds (everything unlocked).
#pragma once
#include <JuceHeader.h>
#include "store/StoreBridge.h"
#include "model/CrossApp.h"

class CoreProcessor;
class PaywallView : public juce::Component {
public:
    explicit PaywallView (CoreProcessor& p);
    void open (const juce::String& reason);
    std::function<void()> onJoinTeam;      // "I was invited by my church" (opens the team panel)
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    void buy (const std::string& productId);
    void setBusy (bool b, const juce::String& msg = {});
    CoreProcessor& proc_; juce::String reason_, status_; bool busy_ = false; wr::IntroEligibility intro_ = wr::IntroEligibility::Unknown;
    wr::CrossAppCoverage cov_; wr::PaywallMode mode_ = wr::PaywallMode::Sell; bool trialShown() const;
    void updateMode();
    std::map<std::string, std::string> prices_;
    juce::TextButton individual_, church_, restore_ { "Restore purchases" }, join_ { "I was invited by my church" }, close_ { "Not now" };
    juce::HyperlinkButton terms_ { "Terms of Use", juce::URL ("about:blank") }, privacy_ { "Privacy Policy", juce::URL ("about:blank") };   // v0.16
    juce::Rectangle<int> box_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool> (true);
public:
    ~PaywallView() override { *alive_ = false; }
};
