#include "HarnessEditor.h"
#include "SetlistView.h"
#include "BandPanel.h"
using namespace wr; using namespace wr::theme;

HarnessEditor::HarnessEditor (HarnessProcessor& p) : CoreEditor (p), proc (p), chart_ (*this), top_ (*this) {
    setlist_ = std::make_unique<SetlistView> (*this);
    addChildComponent (chart_); addChildComponent (*setlist_);
    band_->toFront (false);
    setResizable (true, false); setResizeLimits (700, 500, 3000, 2000);
    setSize (1194, 834);
    show (0); startTimerHz (30);
}
HarnessEditor::~HarnessEditor() = default;
void HarnessEditor::show (int p) { if (p == 0 && ! requireAccess (wr::Feature::LiveView)) p = 1; page_ = p; chart_.setVisible (p == 0); setlist_->setVisible (p == 1); if (p == 1) setlist_->refresh(); repaint(); }

void HarnessEditor::resized() {
    auto r = getLocalBounds(); r.removeFromTop (64); r.removeFromBottom (practiceBarHeight());
    setlist_->setBounds (r);
    strip_ = r.removeFromBottom (72); chart_.setBounds (r);
    coreLayout();
}

void HarnessEditor::paint (juce::Graphics& g) {
    g.fillAll (bg); g.setColour (bar); g.fillRect (0, 0, getWidth(), 64); g.setColour (line); g.drawHorizontalLine (63, 0, (float) getWidth());
    pill (g, juce::Rectangle<float> (18, 14, 172, 36), bg, cardBorder, 10);
    const char* names[] = { "LIVE", "SETLIST" };
    for (int i = 0; i < 2; ++i) { auto r = tabRect (i).toFloat(); bool on = page_ == i; if (on) { g.setColour (orange); g.fillRoundedRectangle (r, 7); }
        g.setColour (on ? juce::Colour (0xff111111) : dim); g.setFont (font (12, true)); g.drawText (names[i], r, juce::Justification::centred); }
    juce::Rectangle<int> info (210, 0, getWidth() - 210 - 150, 64);
    if (page_ == 1) { setlist_->paintTopBar (g, info); return; }
    top_.paint (g, info, 8);
    // transport strip: PREV | NEXT | CLICK (beat dots) | MUTE | PLAY / COUNT-IN, sync banner above
    auto& L = proc.live(); btns_.clear(); auto r = strip_.reduced (12, 10); g.setColour (line); g.drawHorizontalLine (strip_.getY(), 0, (float) getWidth());
    const int w = (r.getWidth() - 4 * 8) / 5;
    auto cell = [&] (int i) { return juce::Rectangle<int> (r.getX() + i * (w + 8), r.getY(), w, r.getHeight()); };
    auto button = [&] (int i, const juce::String& t, bool lit, juce::Colour c) { auto b = cell (i); pill (g, b.toFloat(), lit ? c.withAlpha (0.18f) : btn, lit ? c : btnBorder, 10);
        g.setColour (lit ? c : text); g.setFont (font (14, true)); g.drawText (t, b, juce::Justification::centred); btns_.push_back ({ b, i }); };
    button (0, "<  PREV SECTION", false, orange); button (1, "NEXT SECTION  >", L.sectionPending(), pink);
    button (2, L.clickOn() ? "CLICK ON" : "CLICK OFF", L.clickOn(), accent ("blue"));
    { auto c = cell (2); const int bpb = std::min (12, L.beatsPerBar()), beat = proc.click.clickBeat.load();
      for (int b = 0; b < bpb; ++b) { float x = (float) c.getCentreX() - (float) bpb * 5.5f + (float) b * 11; g.setColour (L.clickOn() && b == beat ? (b == 0 ? orange : juce::Colours::white) : juce::Colour (0xff3a4258)); g.fillEllipse (x, (float) c.getBottom() - 12, 7, 7); } }
    button (3, L.clickMuted() ? "CLICK MUTED" : "MUTE CLICK", L.clickMuted(), juce::Colour (0xffe05050));
    button (4, proc.click.countingIn.load() ? juce::String ("COUNT-IN") : L.playing() ? juce::String ("STOP") : juce::String ("PLAY"), L.playing(), green);
    auto ban = proc.sync().banner (proc.nowSec());
    if (! ban.empty()) { auto br = juce::Rectangle<int> (0, strip_.getY() - 16, getWidth(), 16); g.setColour (juce::Colours::red.withAlpha (0.85f)); g.fillRect (br); g.setColour (juce::Colours::white); g.setFont (font (11, true)); g.drawText (juce::String (ban), br, juce::Justification::centred); }
}

void HarnessEditor::mouseUp (const juce::MouseEvent& e) {
    auto p = e.getPosition();
    for (int i = 0; i < 2; ++i) if (tabRect (i).contains (p)) { show (i); return; }
    if (page_ != 0) return;
    if (p.y < 64) { top_.click (p); return; }
    auto& L = proc.live(); const double now = proc.nowSec();
    for (auto& b : btns_) if (b.first.contains (p)) {
        Action a;
        switch (b.second) {
            case 0: a.type = Action::PrevSection; if (! proc.sync().interceptAction (a, now)) L.handle (a, now); break;
            case 1: a.type = Action::NextSection; if (! proc.sync().interceptAction (a, now)) L.handle (a, now); break;
            case 2: L.setClickOn (! L.clickOn()); break;
            case 3: L.toggleClickMute(); break;
            default: proc.sync().requestPlay (! L.playing(), now); break;
        }
        repaint(); return;
    }
}

void HarnessEditor::timerCallback() {
    coreTick();
    if (page_ == 0 && ! proc.allows (wr::Feature::LiveView)) show (0);
    if (proc.live().version() != lastVersion_) { lastVersion_ = proc.live().version(); chart_.liveChanged(); repaint(); }
    if (page_ == 0) repaint (strip_.withTop (strip_.getY() - 16));
}
