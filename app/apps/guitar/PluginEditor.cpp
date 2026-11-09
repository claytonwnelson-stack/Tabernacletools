#include "PluginEditor.h"
#include "ProjectorView.h"
#include "BandPanel.h"
#include "LiveView.h"
#include "SetlistView.h"
#include "WorkshopView.h"

using namespace wr;

TabernacleGuitarEditor::TabernacleGuitarEditor (TabernacleGuitarProcessor& p) : CoreEditor (p), proc (p) {
    live_ = std::make_unique<LiveView> (*this); setlist_ = std::make_unique<SetlistView> (*this); workshop_ = std::make_unique<WorkshopView> (*this);
    for (juce::Component* c : { (juce::Component*) live_.get(), (juce::Component*) setlist_.get(), (juce::Component*) workshop_.get() }) addChildComponent (c);
    tuner_ = std::make_unique<TunerOverlay> (*this); addChildComponent (*tuner_);
    band_->toFront (false);
    setResizable (true, false); setResizeLimits (900, 640, 3000, 2000);
    setSize (1194, 834); // iPad Pro 11" landscape points, same as the mockups
    show (Page::Live);
    startTimerHz (30);
}
TabernacleGuitarEditor::~TabernacleGuitarEditor() = default;

juce::Rectangle<int> TabernacleGuitarEditor::tabRect (int i) const { const int w[] = { 58, 82, 96 }; int x = 21; for (int k = 0; k < i; ++k) x += w[k] + 2; return { x, 17, w[i], 30 }; }

void TabernacleGuitarEditor::show (Page p) {
    // v0.12 read-only: Live (playing) and Workshop (editing rigs) lock; Setlist stays open for reading charts / hymnal / backup
    if (p == Page::Live && ! requireAccess (wr::Feature::LiveView)) p = Page::Setlist;
    if (p == Page::Workshop && ! requireAccess (wr::Feature::Edit)) p = Page::Setlist;
    page_ = p; live_->setVisible (p == Page::Live); setlist_->setVisible (p == Page::Setlist); workshop_->setVisible (p == Page::Workshop);
    if (p == Page::Setlist) setlist_->refresh(); if (p == Page::Workshop) workshop_->refresh();
    repaint();
}

void TabernacleGuitarEditor::paint (juce::Graphics& g) {
    using namespace theme;
    g.fillAll (bg); g.setColour (bar); g.fillRect (0, 0, getWidth(), 64); g.setColour (line); g.drawHorizontalLine (63, 0, (float) getWidth());
    pill (g, juce::Rectangle<float> (18, 14, 246, 36), bg, cardBorder, 10);
    const char* names[] = { "LIVE", "SETLIST", "WORKSHOP" };
    for (int i = 0; i < 3; ++i) { auto r = tabRect (i).toFloat(); bool on = (int) page_ == i; if (on) { g.setColour (orange); g.fillRoundedRectangle (r, 7); }
        g.setColour (on ? onAccent : dim); g.setFont (font (12, true)); g.drawText (names[i], r, juce::Justification::centred); }
    juce::Rectangle<int> info (280, 0, getWidth() - 280 - 150, 64);
    if (page_ == Page::Live) live_->paintTopBar (g, info);
    else if (page_ == Page::Setlist) setlist_->paintTopBar (g, info);
    else workshop_->paintTopBar (g, info);
}

void TabernacleGuitarEditor::resized() {
    auto r = getLocalBounds(); r.removeFromTop (64); r.removeFromBottom (practiceBarHeight());
    for (juce::Component* c : { (juce::Component*) live_.get(), (juce::Component*) setlist_.get(), (juce::Component*) workshop_.get() }) c->setBounds (r);
    tuner_->setBounds (getLocalBounds());
    coreLayout();
}

void TabernacleGuitarEditor::mouseUp (const juce::MouseEvent& e) {
    for (int i = 0; i < 3; ++i) if (tabRect (i).contains (e.getPosition())) { show ((Page) i); return; }
    if (page_ == Page::Live) live_->topBarClick (e.getPosition());
}

void TabernacleGuitarEditor::timerCallback() {
    coreTick();
    if (page_ != Page::Setlist && ! proc.allows (page_ == Page::Live ? wr::Feature::LiveView : wr::Feature::Edit)) show (page_);   // lapsed mid-session
    const bool tOpen = proc.live().tunerOpen(); if (tuner_->isVisible() != tOpen) { tuner_->setVisible (tOpen); if (tOpen) tuner_->toFront (false); }
    if (tOpen) tuner_->tick();
    if (proc.live().version() != lastVersion_) { lastVersion_ = proc.live().version(); live_->liveChanged(); if (page_ == Page::Workshop) workshop_->refreshTempo(); repaint (0, 0, getWidth(), 64); }
    if (page_ == Page::Live) { live_->tick(); repaint (getWidth() - 420, 0, 280, 64); }
}

// guitar-only settings (the shared click / cues / MIDI / band sync / projector menu is in CoreEditor::openSettings)
void TabernacleGuitarEditor::addAudioSettings (juce::PopupMenu& m) {
    auto& s = proc.store.lib.settings;
    m.addItem ("Routing: 4-cable (MOTU M4: out1 -> amp in, outs 3/4 -> loop return)", true, s.routing == 0, [this] { proc.store.lib.settings.routing = 0; proc.applySettings(); proc.store.save(); });
    m.addItem ("Routing: 2-cable (everything -> outs 1/2)", true, s.routing == 1, [this] { proc.store.lib.settings.routing = 1; proc.applySettings(); proc.store.save(); });
}
void TabernacleGuitarEditor::addInstrumentSettings (juce::PopupMenu& m) {
    auto& s = proc.store.lib.settings;
    juce::PopupMenu tl; for (int sec : { 0, 2, 3, 5, 8 }) tl.addItem (sec == 0 ? juce::String ("Off (cut)") : juce::String (sec) + " s", true, (int) s.rigTailSec == sec, [this, sec] { proc.store.lib.settings.rigTailSec = (float) sec; proc.applySettings(); proc.store.save(); });
    m.addSubMenu ("Rig change: let old tails ring", tl);
    // v0.12 amp / drive anti-aliasing
    juce::PopupMenu os; const int now = proc.rig.oversamplingNow.load();
    os.addItem ("In use: " + (now == 1 ? juce::String ("Off") : juce::String (now) + "x") + (proc.rig.oversamplingReduced.load() ? " (auto-reduced: CPU)" : "")
                + " - latency " + juce::String (proc.rig.latencySamples() / proc.getSampleRate() * 1000.0, 2) + " ms", false, false, [] {});
    for (int f : { 1, 2, 4, 8 })
        os.addItem (f == 1 ? juce::String ("Off (lowest CPU, more aliasing fizz)") : juce::String (f) + "x" + (f == 4 ? " (recommended)" : f == 8 ? " (smoothest, most CPU)" : ""), true, s.ampOversampling == f,
                    [this, f] { proc.store.lib.settings.ampOversampling = f; proc.applySettings(); proc.store.save(); });
    os.addSeparator();
    os.addItem ("Reduce automatically when the iPad is busy", true, s.autoOversampling, [this] { auto& st = proc.store.lib.settings; st.autoOversampling = ! st.autoOversampling; proc.applySettings(); proc.store.save(); });
    m.addSubMenu ("Amp & drive anti-aliasing (oversampling)", os);
}
void TabernacleGuitarEditor::addViewSettings (juce::PopupMenu& m) {
    m.addItem (tunerShown ? "Hide tuner" : "Show tuner", [this] { tunerShown = ! tunerShown; proc.rig.post (cmdTuner (tunerShown, false)); });
}

// ================================================================== tuner overlay
TunerOverlay::TunerOverlay (TabernacleGuitarEditor& e) : ed (e) { setInterceptsMouseClicks (true, false); }
void TunerOverlay::resized() {
    auto r = getLocalBounds(); close_ = { r.getRight() - 170, 24, 140, 48 };
    refMinus_ = { r.getCentreX() - 130, r.getBottom() - 110, 56, 48 }; refPlus_ = { r.getCentreX() + 74, r.getBottom() - 110, 56, 48 };
}
void TunerOverlay::tick() { float hz = ed.proc.rig.tunerHz.load(); hzSm_ = hz > 0 ? (hzSm_ > 0 && std::abs (hz - hzSm_) < hzSm_ * 0.03f ? hzSm_ * 0.75f + hz * 0.25f : hz) : 0; repaint(); }
void TunerOverlay::paint (juce::Graphics& g) {
    using namespace wr::theme; g.fillAll (bg.withAlpha (0.94f));
    const float ref = ed.proc.store.lib.settings.tunerRefHz; auto rd = wr::tunerReading (hzSm_, ed.proc.rig.tunerConf.load(), ref);
    auto r = getLocalBounds(); const bool inTune = rd.valid && std::abs (rd.cents) < 3.0f;
    g.setColour (dim); g.setFont (font (16, true)); g.drawText ("TUNER - RIG MUTED", 30, 30, 400, 30, juce::Justification::left);
    pill (g, close_.toFloat(), btn, btnBorder, 10); g.setColour (text); g.setFont (font (16, true)); g.drawText ("Close", close_, juce::Justification::centred);
    auto c = r.getCentre().toFloat();
    g.setColour (inTune ? green : rd.valid ? juce::Colours::white : dimmer); g.setFont (font (180, true));
    g.drawText (rd.valid ? juce::String (rd.note) : juce::String ("-"), juce::Rectangle<float> (c.x - 200, c.y - 230, 400, 200), juce::Justification::centred);
    if (rd.valid) { g.setFont (font (28, true)); g.setColour (dim); g.drawText (juce::String (rd.octave), juce::Rectangle<float> (c.x + 120, c.y - 90, 60, 40), juce::Justification::left); }
    // cents meter -50..+50
    juce::Rectangle<float> meter (c.x - 330, c.y + 10, 660, 70); g.setColour (card); g.fillRoundedRectangle (meter, 12);
    for (int k = -50; k <= 50; k += 10) { float x = meter.getCentreX() + (float) k / 50.0f * (meter.getWidth() / 2 - 20); g.setColour (k == 0 ? green : dimmer); g.fillRect (x - (k == 0 ? 1.5f : 0.5f), meter.getY() + (k == 0 ? 6.0f : 18.0f), k == 0 ? 3.0f : 1.0f, meter.getHeight() - (k == 0 ? 12.0f : 36.0f)); }
    if (rd.valid) { float x = meter.getCentreX() + juce::jlimit (-1.0f, 1.0f, rd.cents / 50.0f) * (meter.getWidth() / 2 - 20); g.setColour (inTune ? green : warn); g.fillRoundedRectangle (x - 5, meter.getY() - 8, 10, meter.getHeight() + 16, 5); }
    g.setColour (inTune ? green : text); g.setFont (font (30, true));
    g.drawText (rd.valid ? (rd.cents > 0 ? "+" : "") + juce::String (rd.cents, 1) + " cents   " + juce::String (rd.hz, 1) + " Hz" : juce::String ("play a string"), juce::Rectangle<float> (c.x - 300, c.y + 100, 600, 40), juce::Justification::centred);
    // A reference
    for (auto* b : { &refMinus_, &refPlus_ }) { pill (g, b->toFloat(), btn, btnBorder, 10); g.setColour (text); g.setFont (font (24, true)); g.drawText (b == &refMinus_ ? "-" : "+", *b, juce::Justification::centred); }
    g.setColour (text); g.setFont (font (20, true)); g.drawText ("A = " + juce::String (ref, 0) + " Hz", juce::Rectangle<int> (refMinus_.getRight(), refMinus_.getY(), refPlus_.getX() - refMinus_.getRight(), 48), juce::Justification::centred);
    g.setColour (dimmer); g.setFont (font (13)); g.drawText ("Hold guitar switch A (1 s) or tap Close to return. Click track keeps playing.", r.withTop (r.getBottom() - 46), juce::Justification::centred);
}
void TunerOverlay::mouseUp (const juce::MouseEvent& e) {
    auto& st = ed.proc.store.lib.settings; auto p = e.getPosition();
    if (close_.contains (p)) { ed.proc.live().setTunerOpen (false); return; }
    if (refMinus_.contains (p)) { st.tunerRefHz = juce::jlimit (430.0f, 450.0f, st.tunerRefHz - 1); ed.proc.store.saveSoon(); repaint(); }
    if (refPlus_.contains (p)) { st.tunerRefHz = juce::jlimit (430.0f, 450.0f, st.tunerRefHz + 1); ed.proc.store.saveSoon(); repaint(); }
}
