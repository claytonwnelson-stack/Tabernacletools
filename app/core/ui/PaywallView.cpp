#include "model/Legal.h"
#include "PaywallView.h"
#include "CoreProcessor.h"
#include "SeatSession.h"

static const char* kInd = "com.tabernacletools.guitar.individual.monthly";
static const char* kChurch = "com.tabernacletools.guitar.church.monthly";

PaywallView::PaywallView (CoreProcessor& p) : proc_ (p) {
    for (auto* b : { &individual_, &church_, &restore_, &join_, &close_ }) addAndMakeVisible (*b);
    terms_.setURL (juce::URL (wr::legal::termsUrl())); privacy_.setURL (juce::URL (wr::legal::privacyUrl())); addAndMakeVisible (terms_); addAndMakeVisible (privacy_);
    individual_.onClick = [this] { buy (kInd); }; church_.onClick = [this] { buy (kChurch); };
    close_.onClick = [this] { setVisible (false); };
    join_.onClick = [this] { setVisible (false); if (onJoinTeam) onJoinTeam(); };
    restore_.onClick = [this] {
        setBusy (true, "Checking the App Store for your purchases...");
        auto alive = alive_;
        wr::store::restore ([this, alive] (bool ok, std::string err, std::vector<wr::StoreEntitlement> e) {
            if (! *alive) return; proc_.applyStoreEntitlements (e);
            setBusy (false, ! ok ? "Restore failed: " + juce::String (err) : proc_.fullAccess() ? "Restored - welcome back!" : "No active subscription found on this Apple ID");
            if (ok && proc_.fullAccess()) setVisible (false); });
    };
}
void PaywallView::open (const juce::String& reason) {
    reason_ = reason; status_ = {}; setVisible (true); toFront (true);
    intro_ = proc_.introEligibility();
    auto alive = alive_;
    wr::store::products ([this, alive] (std::vector<wr::store::ProductInfo> v) {
        if (! *alive) return; for (auto& p : v) prices_[p.id] = p.displayPrice;
        resized(); repaint(); });
    proc_.refreshIntroEligibility ([this, alive] { if (! *alive) return; intro_ = proc_.introEligibility(); resized(); repaint(); });
#if WR_STORE_BUILD
    // v0.18 already covered by a subscription bought in another Tabernacle Tools app (or a church seat)? Then don't sell again.
    if (auto* ss = proc_.seats()) if (ss->client().signedIn())
        ss->async ([] (wr::SeatClient& c) { return c.coverage(); }, [this, alive, ss] (wr::SeatResult) { if (! *alive) return; cov_ = ss->client().lastCoverage(); updateMode(); resized(); repaint(); });
#endif
    updateMode(); resized(); repaint();
}
void PaywallView::updateMode() { mode_ = wr::paywallModeFor (proc_.entitlements, cov_, (int64_t) juce::Time::currentTimeMillis() / 1000); }
bool PaywallView::trialShown() const { return wr::showTrialAcrossApps (wr::showTrialOffer (intro_), cov_); }
void PaywallView::setBusy (bool b, const juce::String& msg) { busy_ = b; status_ = msg; for (auto* x : { &individual_, &church_, &restore_ }) x->setEnabled (! b); repaint(); }
void PaywallView::buy (const std::string& pid) {
    if (! wr::store::available()) { status_ = "Purchases work in the App Store build only."; repaint(); return; }
    setBusy (true, "Opening the App Store...");
    auto alive = alive_;
    wr::store::purchase (pid, [this, alive] (std::string status, std::string err) {
        if (! *alive) return;
        if (status == "success") { wr::store::entitlements ([this, alive] (std::vector<wr::StoreEntitlement> e) { if (! *alive) return; proc_.applyStoreEntitlements (e); setBusy (false, "Thank you!"); setVisible (false); }); return; }
        setBusy (false, status == "pending" ? "Waiting for approval (Ask to Buy) - it unlocks automatically when approved"
                       : status == "cancelled" ? juce::String() : "Purchase failed: " + juce::String (err)); });
}
void PaywallView::resized() {
    box_ = getLocalBounds().withSizeKeepingCentre (juce::jmin (760, getWidth() - 32), juce::jmin (560, getHeight() - 32));
    auto price = [this] (const char* id, double usd) { auto it = prices_.find (id); return it != prices_.end() && ! it->second.empty() ? juce::String (it->second) : "$" + juce::String (usd, 2); };
    const auto ie = trialShown() ? wr::IntroEligibility::Eligible : wr::IntroEligibility::Ineligible;
    individual_.setButtonText (wr::paywallButtonLabel (ie, price (kInd, 4.99).toStdString()));
    church_.setButtonText (wr::paywallButtonLabel (ie, price (kChurch, 19.99).toStdString()));
    individual_.setVisible (mode_ == wr::PaywallMode::Sell); church_.setVisible (mode_ != wr::PaywallMode::Covered);   // v0.18 no double billing
    auto r = box_.reduced (28); r.removeFromTop (120);
    auto cols = r.removeFromTop (220); auto left = cols.removeFromLeft (cols.getWidth() / 2).reduced (8, 0), right = cols.reduced (8, 0);
    individual_.setBounds (left.removeFromBottom (52)); church_.setBounds (right.removeFromBottom (52));
    r.removeFromTop (16); auto row = r.removeFromTop (44);
    restore_.setBounds (row.removeFromLeft (200)); row.removeFromLeft (10); join_.setBounds (row.removeFromLeft (260)); close_.setBounds (row.removeFromRight (120));
    r.removeFromTop (8); auto legal = r.removeFromTop (28); terms_.setBounds (legal.removeFromLeft (140)); privacy_.setBounds (legal.removeFromLeft (140));
#if ! WR_STORE_BUILD
    join_.setVisible (false);
#endif
}
void PaywallView::paint (juce::Graphics& g) {
    g.fillAll (juce::Colours::black.withAlpha (0.7f));
    g.setColour (juce::Colour (0xff1c1f26)); g.fillRoundedRectangle (box_.toFloat(), 16);
    auto r = box_.reduced (28);
    g.setColour (juce::Colours::white); g.setFont (juce::FontOptions (28.0f, juce::Font::bold)); g.drawText ("Tabernacle Tools", r.removeFromTop (36), juce::Justification::left);
    g.setColour (juce::Colour (0xffffd166)); g.setFont (juce::FontOptions (17.0f));
    g.drawText (reason_.isNotEmpty() ? reason_ : juce::String ("Subscribe to play, lead and project"), r.removeFromTop (26), juce::Justification::left);
    g.setColour (juce::Colours::lightgrey); g.setFont (juce::FontOptions (14.0f));
    g.drawFittedText ("Your library, charts, the hymnal and backup export always stay available - even without a subscription.", r.removeFromTop (40), juce::Justification::topLeft, 2);
    r.removeFromTop (18);
    auto cols = r.removeFromTop (168); auto L = cols.removeFromLeft (cols.getWidth() / 2).reduced (8, 0), R = cols.reduced (8, 0);
    auto card = [&] (juce::Rectangle<int> c, const char* title, const juce::String& lines) {
        g.setColour (juce::Colour (0xff262b35)); g.fillRoundedRectangle (c.toFloat().withHeight (220), 12);
        auto t = c.reduced (14, 12); g.setColour (juce::Colours::white); g.setFont (juce::FontOptions (20.0f, juce::Font::bold)); g.drawText (title, t.removeFromTop (28), juce::Justification::left);
        g.setColour (juce::Colours::lightgrey); g.setFont (juce::FontOptions (14.0f)); g.drawFittedText (lines, t, juce::Justification::topLeft, 6); };
    card (L, "Individual", "One person, all your iPads\nGuitar, Keys, Bass + Charts apps, pedal, amp and cab\nClick, cues, live view, projector, band sync\n" + juce::String (trialShown() ? "2-week free trial (once per person, all apps)" : "Cancel anytime"));
    card (R, "Church", "One leader + up to 14 team members\n(15 people, each on their own Apple ID)\nEverything in Individual for the whole team\nLeader invites with single-use codes");
    if (mode_ != wr::PaywallMode::Sell) { // v0.18 "You're covered" (one subscription unlocks every Tabernacle Tools app)
        auto msg = wr::coveredMessage (proc_.entitlements, cov_, (int64_t) juce::Time::currentTimeMillis() / 1000);
        if (mode_ == wr::PaywallMode::UpgradeOnly) msg += " Upgrade to Church only if your whole team needs it.";
        auto b = L.withHeight (200); g.setColour (juce::Colour (0xf0262b35)); g.fillRoundedRectangle (b.toFloat(), 12);
        g.setColour (juce::Colour (0xff7bd88f)); g.setFont (juce::FontOptions (18.0f, juce::Font::bold)); g.drawFittedText (msg, b.reduced (14), juce::Justification::centredLeft, 6); }
    if (status_.isNotEmpty()) { g.setColour (juce::Colour (0xffffd166)); g.setFont (juce::FontOptions (15.0f)); g.drawFittedText (status_, box_.reduced (28).removeFromBottom (64).removeFromTop (34), juce::Justification::left, 2); }
    g.setColour (juce::Colours::grey); g.setFont (juce::FontOptions (11.0f));
    g.drawFittedText ("Payment is charged to your Apple ID. Subscriptions renew monthly unless cancelled at least 24 hours before the end of the period, in "
                      "Settings > Apple ID > Subscriptions. "
                      + juce::String (trialShown() ? "The free trial is once per person across all Tabernacle Tools apps. " : "") + "One subscription unlocks every Tabernacle Tools app when you sign in with Apple. " + "Terms of Use and Privacy Policy: see the App Store page.",
                      box_.reduced (28).removeFromBottom (28), juce::Justification::left, 2);
}
