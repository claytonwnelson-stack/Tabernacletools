#include "WorkshopView.h"
#include "dsp/AmpSection.h"
#include "dsp/PedalFaces.h"
#include <algorithm>
#include <cmath>
using namespace wr; using namespace wr::theme;
void showBackupsMenu (CoreEditor& ed, juce::Component* anchor, std::function<void()> after);

namespace {
const wr::PedalInfo* infoFor (const std::string& type) {
    for (auto& pi : wr::pedalCatalog()) if (pi.type == type) return &pi;
    return nullptr;
}
void motif (juce::Graphics& g, juce::Rectangle<float> r, wr::PedalCategory cat, juce::Colour hue) {
    g.setColour (hue.brighter (0.65f).withAlpha (0.45f));
    using C = wr::PedalCategory;
    auto jmaxf = [] (float a, float b) { return a > b ? a : b; };
    if (cat == C::Drive) {
        for (int i = 0; i < 3; ++i) {
            float x = r.getX() + r.getWidth() * (0.22f + i * 0.22f);
            juce::Path f; f.startNewSubPath (x, r.getBottom());
            f.cubicTo (x - r.getWidth() * 0.08f, r.getCentreY(), x + r.getWidth() * 0.02f, r.getY() + 4, x + 6, r.getY());
            f.cubicTo (x + r.getWidth() * 0.12f, r.getCentreY(), x + 10, r.getBottom() - 4, x + 4, r.getBottom());
            f.closeSubPath(); g.fillPath (f);
        }
    } else if (cat == C::Modulation) {
        juce::Path w; w.startNewSubPath (r.getX(), r.getCentreY());
        for (int i = 0; i <= 24; ++i) { float t = (float) i / 24.f; w.lineTo (r.getX() + t * r.getWidth(), r.getCentreY() + std::sin (t * 6.283f * 2) * r.getHeight() * 0.28f); }
        g.strokePath (w, juce::PathStrokeType (jmaxf (1.5f, r.getHeight() * 0.08f)));
    } else if (cat == C::Time) {
        for (int i = 0; i < 4; ++i) { float y = r.getY() + r.getHeight() * (0.2f + i * 0.2f); g.fillRoundedRectangle (r.getX(), y, r.getWidth() * (0.35f + (i % 2) * 0.4f), jmaxf (2.f, r.getHeight() * 0.06f), 1.f); }
    } else if (cat == C::Ambient) {
        for (int i = 0; i < 5; ++i) {
            float x = r.getX() + r.getWidth() * (0.15f + (i * 37 % 70) / 100.f);
            float y = r.getY() + r.getHeight() * (0.15f + (i * 53 % 70) / 100.f);
            float s = r.getHeight() * 0.12f;
            g.drawLine (x - s, y, x + s, y, 1.4f); g.drawLine (x, y - s, x, y + s, 1.4f);
        }
    } else if (cat == C::Dynamics) {
        for (int i = 0; i < 5; ++i) { float h = r.getHeight() * (0.35f + (i % 3) * 0.2f); g.fillRoundedRectangle (r.getX() + i * r.getWidth() / 5.f, r.getBottom() - h, r.getWidth() / 9.f, h, 2.f); }
    } else if (cat == C::Pitch) {
        juce::Path c; float cx = r.getCentreX(), cy = r.getCentreY();
        c.addTriangle (cx, r.getY(), cx - r.getWidth() * 0.16f, cy, cx + r.getWidth() * 0.16f, cy);
        c.addTriangle (cx, r.getBottom(), cx - r.getWidth() * 0.16f, cy, cx + r.getWidth() * 0.16f, cy);
        g.fillPath (c);
    } else if (cat == C::Synth) {
        for (int y = 0; y < 3; ++y) for (int x = 0; x < 4; ++x)
            g.fillEllipse (r.getX() + (x + 0.5f) * r.getWidth() / 4.f - 3, r.getY() + (y + 0.5f) * r.getHeight() / 3.f - 3, 6, 6);
    } else if (cat == C::AmpCab) {
        juce::Path arc; arc.addCentredArc (r.getCentreX(), r.getBottom(), r.getWidth() * 0.42f, r.getHeight() * 0.75f, 0, -2.4f, -0.74f, true);
        g.strokePath (arc, juce::PathStrokeType (jmaxf (2.f, r.getHeight() * 0.06f)));
    } else g.fillRoundedRectangle (r.getX(), r.getCentreY() - 2, r.getWidth(), jmaxf (3.f, r.getHeight() * 0.08f), 2.f);
}
void miniKnob (juce::Graphics& g, juce::Rectangle<float> r, float pos) {
    float d = std::min (r.getWidth(), r.getHeight());
    auto c = r.withSizeKeepingCentre (d, d);
    g.setColour (bg); g.fillEllipse (c); g.setColour (line); g.drawEllipse (c, 1.f);
    float a0 = -2.2f, ang = a0 + pos * 4.4f;
    juce::Path arc; arc.addCentredArc (c.getCentreX(), c.getCentreY(), d * 0.5f - 0.5f, d * 0.5f - 0.5f, 0, a0, ang, true);
    g.setColour (accentText); g.strokePath (arc, juce::PathStrokeType (1.2f));
    auto ctr = c.getCentre();
    g.setColour (juce::Colours::white);
    g.drawLine (ctr.x + std::sin (ang) * d * 0.12f, ctr.y - std::cos (ang) * d * 0.12f, ctr.x + std::sin (ang) * d * 0.32f, ctr.y - std::cos (ang) * d * 0.32f, 1.3f);
}
void paintChainStomp (juce::Graphics& g, juce::Rectangle<int> full, const std::string& type, const juce::String& name, wr::PedalCategory cat, juce::Colour hue, bool on, bool sel) {
    { juce::Graphics::ScopedSaveState ss (g); if (! on) g.setOpacity (0.42f);
      juce::ColourGradient grad (hue.brighter (0.38f), (float) full.getX(), (float) full.getY(), hue.darker (0.42f), (float) full.getRight(), (float) full.getBottom(), false);
      g.setGradientFill (grad); g.fillRoundedRectangle (full.toFloat(), 10);
      g.setColour (juce::Colours::white.withAlpha (0.16f));
      g.fillRoundedRectangle (full.toFloat().reduced (3).withHeight (full.getHeight() * 0.28f), 8);
      g.setColour (hue.darker (0.25f)); g.drawRoundedRectangle (full.toFloat().reduced (1.2f), 10, 1.2f);
      auto face = full.reduced (6, 4).toFloat();
      motif (g, face.withTrimmedTop (face.getHeight() * 0.22f).withTrimmedBottom (face.getHeight() * 0.38f), cat, hue);
      const int nk = full.getWidth() < 100 ? 2 : 3;
      float kd = std::min (12.f, full.getHeight() * 0.16f);
      float ky = (float) full.getY() + 5.f, kx = (float) full.getX() + 7.f;
      for (int i = 0; i < nk; ++i) miniKnob (g, { kx + i * (kd + 3.f), ky, kd, kd }, 0.25f + 0.18f * i);
      led (g, { (float) full.getRight() - 11.f, ky + kd * 0.45f }, on);
      float fs = std::min (15.f, full.getWidth() * 0.14f);
      juce::Rectangle<float> sw (full.getCentreX() - fs * 0.5f, (float) full.getBottom() - 4.f - fs, fs, fs);
      g.setColour (juce::Colours::white.withAlpha (0.92f)); g.fillEllipse (sw);
      g.setColour (bg); g.fillEllipse (sw.reduced (fs * 0.28f));
      g.setColour (hue.brighter (0.45f)); g.drawEllipse (sw.reduced (0.6f), 1.f);
    }
    auto nameR = full.reduced (6, 2); nameR.removeFromTop ((int) (std::min (12.f, full.getHeight() * 0.16f) + 6)); nameR.removeFromBottom (16);
    auto faceCol = bg.interpolatedWith (hue.brighter (0.18f), on ? 1.0f : 0.55f);
    const bool light = faceCol.getPerceivedBrightness() > 0.45f;
    if (light) { g.setColour (juce::Colours::black.withAlpha (on ? 0.42f : 0.55f)); g.fillRoundedRectangle (nameR.reduced (1, 0).toFloat(), 4.0f); }
    g.setFont (font (std::min (16.f, std::max (12.f, full.getHeight() * 0.20f)), true));
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.drawFittedText (name, nameR.translated (0, 1), juce::Justification::centred, 2);
    g.setColour (juce::Colours::white);
    g.drawFittedText (name, nameR, juce::Justification::centred, 2);
    if (sel) { g.setColour (accentText); g.drawRoundedRectangle (full.toFloat().reduced (1.2f), 10, 2.2f); }
}
}

WorkshopView::WorkshopView (TabernacleGuitarEditor& e) : ed (e) {
    setFocusContainerType (juce::Component::FocusContainerType::keyboardFocusContainer);
    addAndMakeVisible (search_); search_.setTextToShowWhenEmpty ("Search pedals...", dimmer); search_.setFont (font (14)); search_.onTextChange = [this] { repaint(); };
    for (auto* b : { &onOff_, &remove_, &reset_, &saveScene_, &assign_, &saveRig_, &backups_ }) addAndMakeVisible (*b);
    addChildComponent (realAmp_); addChildComponent (loadIr_); addChildComponent (removeIr_);
    removeIr_.onClick = [this] { clearUserIr (work_); msg_ = "Back to the built-in 2x12 Open Back cab"; rebuild(); }; realAmp_.setClickingTogglesState (false);
    realAmp_.onClick = [this] { const bool real = ! usingRealAmp (work_); setRealAmp (work_, real); ed.proc.setPedal (kAmpUid, ! real);
        msg_ = real ? "Amp sim bypassed - your own amp does the work" : "Amp sim on"; buildEditor(); repaint(); };
    loadIr_.onClick = [this] { loadIrFile(); };
    backups_.onClick = [this] { showBackupsMenu (ed, &backups_, [this] { refresh(); }); };
    onOff_.setClickingTogglesState (false); onOff_.setLookAndFeel (&footLnF_); onOff_.setMouseClickGrabsKeyboardFocus (false);
    onOff_.onClick = [this] { if (selected_.empty()) return; bool on = ! ed.proc.pedalOn (selected_); ed.proc.setPedal (selected_, on); for (auto& l : work_.lanes) for (auto& d : l) if (d.uid == selected_) d.enabled = on; buildEditor(); repaint(); };
    remove_.onClick = [this] {
        juce::Component::unfocusAllComponents(); // deleting the pedal's knobs must not move focus onto the search field
        for (auto& l : work_.lanes) l.erase (std::remove_if (l.begin(), l.end(), [this] (const PedalDef& d) { return d.uid == selected_; }), l.end()); selected_.clear(); rebuild(); };
    reset_.onClick = [this] { for (auto& l : work_.lanes) for (auto& d : l) if (d.uid == selected_) d.params.clear(); rebuild(); };
    saveScene_.setColour (juce::TextButton::buttonColourId, accentFill); saveScene_.setColour (juce::TextButton::textColourOffId, onAccent);
    saveScene_.onClick = [this] { saveScene(); };
    assign_.onClick = [this] { assignMenu(); };
    saveRig_.onClick = [this] { auto& rigs = ed.proc.store.lib.rigs; bool found = false; for (auto& r : rigs) if (r.name == work_.name) { r = work_; found = true; } if (! found) rigs.push_back (work_); ed.proc.store.save(); msg_ = "Rig \"" + juce::String (work_.name) + "\" saved"; repaint(); };
    for (auto* s : { &trimPre_, &trimPost_ }) { addAndMakeVisible (*s); s->setSliderStyle (juce::Slider::LinearHorizontal); s->setRange (-30, 6, 0.5); s->setTextValueSuffix (" dB"); s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20); }
    for (auto* l : { &trimPreL_, &trimPostL_ }) { addAndMakeVisible (*l); l->setFont (font (11, true)); l->setColour (juce::Label::textColourId, dim); }
    tempoHost_.setFocusContainerType (juce::Component::FocusContainerType::keyboardFocusContainer);
    tempoHost_.addAndMakeVisible (tempoL_); tempoL_.setFont (font (12, true)); tempoL_.setColour (juce::Label::textColourId, text); tempoL_.setText ("BPM", juce::dontSendNotification); tempoL_.setJustificationType (juce::Justification::centredLeft);
    tempoHost_.addAndMakeVisible (tempo_); tempo_.setFont (font (16, true)); tempo_.setJustification (juce::Justification::centred); tempo_.setInputRestrictions (5, "0123456789.");
    tempo_.setColour (juce::TextEditor::textColourId, juce::Colours::white);
    tempo_.setColour (juce::TextEditor::highlightedTextColourId, juce::Colours::white);
    tempo_.setColour (juce::CaretComponent::caretColourId, juce::Colours::white);
    tempo_.onReturnKey = [this] { setSongTempo (tempo_.getText().getDoubleValue()); }; tempo_.onFocusLost = [this] { setSongTempo (tempo_.getText().getDoubleValue()); };
    for (auto* b : { &tempoMinus_, &tempoPlus_ }) tempoHost_.addAndMakeVisible (*b);
    ed.addChildComponent (tempoHost_);
    tempoMinus_.setRepeatSpeed (400, 80); tempoPlus_.setRepeatSpeed (400, 80); // hold to step
    tempoMinus_.onClick = [this] { setSongTempo (tempo_.getText().getDoubleValue() - 1); }; tempoPlus_.onClick = [this] { setSongTempo (tempo_.getText().getDoubleValue() + 1); };
    trimPreL_.setText ("OUT 1 TRIM (amp input)", juce::dontSendNotification); trimPostL_.setText ("OUTS 3/4 TRIM (loop return)", juce::dontSendNotification);
    trimPre_.onValueChange = [this] { ed.proc.store.lib.settings.trimPreDb = (float) trimPre_.getValue(); ed.proc.rig.post (cmdTrim (Lane::Pre, (float) trimPre_.getValue())); ed.proc.store.saveSoon(); };
    trimPost_.onValueChange = [this] { ed.proc.store.lib.settings.trimPostDb = (float) trimPost_.getValue(); ed.proc.rig.post (cmdTrim (Lane::Post, (float) trimPost_.getValue())); ed.proc.store.saveSoon(); };
}

WorkshopView::~WorkshopView() { onOff_.setLookAndFeel (nullptr); }
void WorkshopView::FootLnF::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool, bool) {
    auto r = b.getLocalBounds().toFloat().reduced (1); float d = std::min (r.getWidth(), r.getHeight());
    auto c = r.withSizeKeepingCentre (d, d);
    g.setColour (juce::Colours::white.withAlpha (0.94f)); g.fillEllipse (c);
    g.setColour (bg); g.fillEllipse (c.reduced (d * 0.28f));
    g.setColour (b.getToggleState() ? accentText : line); g.drawEllipse (c.reduced (1.2f), std::max (1.6f, d * 0.06f));
}
std::unique_ptr<juce::ComponentTraverser> WorkshopView::TempoHost::createKeyboardFocusTraverser() {
    struct Quiet : juce::KeyboardFocusTraverser { juce::Component* getDefaultComponent (juce::Component*) override { return nullptr; } };
    return std::make_unique<Quiet>();
}
void WorkshopView::visibilityChanged() { tempoHost_.setVisible (isVisible()); if (isVisible()) layoutHeaderTempo(); }
juce::Rectangle<int> WorkshopView::headerTempoBounds() const {
    const int w = 32 + 4 + 58 + 2 + 34 + 4 + 32;          // -  72  BPM  +
    const int settingsLeft = ed.getWidth() - 164 + 10;   // same slot as the Settings button
    return { settingsLeft - 8 - w, 16, w, 32 };
}
void WorkshopView::layoutHeaderTempo() {
    tempoHost_.setBounds (headerTempoBounds());
    auto r = tempoHost_.getLocalBounds();
    tempoMinus_.setBounds (r.removeFromLeft (32)); r.removeFromLeft (4);
    tempoPlus_.setBounds (r.removeFromRight (32)); r.removeFromRight (4);
    tempoL_.setBounds (r.removeFromRight (34)); r.removeFromRight (2);
    tempo_.setBounds (r);
}
bool WorkshopView::categoryOpen (int cat) const { return std::find (openCats_.begin(), openCats_.end(), cat) != openCats_.end(); }
void WorkshopView::toggleCategory (int cat) {
    auto it = std::find (openCats_.begin(), openCats_.end(), cat);
    if (it != openCats_.end()) openCats_.erase (it);
    else { openCats_.push_back (cat); while ((int) openCats_.size() > 2) openCats_.erase (openCats_.begin()); }
    repaint();
}
void WorkshopView::toggleFavorite (const std::string& type) {
    auto& fav = ed.proc.store.lib.settings.favoritePedals;
    auto it = std::find (fav.begin(), fav.end(), type);
    juce::String name = infoFor (type) ? juce::String (infoFor (type)->name) : juce::String (type);
    if (it != fav.end()) { fav.erase (it); msg_ = name + " removed from favorites"; }
    else { fav.insert (fav.begin(), type); msg_ = name + " added to favorites"; }
    ed.proc.store.saveSoon(); repaint();
}
void WorkshopView::rememberRecent (const std::string& type) {
    auto& r = ed.proc.store.lib.settings.recentPedals;
    r.erase (std::remove (r.begin(), r.end(), type), r.end());
    r.insert (r.begin(), type); if (r.size() > 8) r.resize (8);
    ed.proc.store.saveSoon();
}
void WorkshopView::addFromLibrary (const std::string& type) {
    int lane = 0;
    if (auto* info = infoFor (type)) if (info->category == PedalCategory::Time || info->category == PedalCategory::Ambient) lane = 1;
    std::string uid = type; int n = 2;
    auto exists = [&] (const std::string& u) { for (auto& l : work_.lanes) for (auto& d : l) if (d.uid == u) return true; return false; };
    while (exists (uid)) uid = type + std::to_string (n++);
    int idx = (int) work_.lanes[lane].size();
    if (lane == 0) for (int i = 0; i < idx; ++i) if (work_.lanes[0][(size_t) i].uid == kAmpUid) { idx = i; break; }
    work_.lanes[lane].insert (work_.lanes[lane].begin() + idx, PedalDef { uid, type, true, {} });
    selected_ = uid; rememberRecent (type);
    msg_ = (infoFor (type) ? juce::String (infoFor (type)->name) : juce::String (type)) + (lane == 0 ? " added before the amp" : " added after the amp");
    rebuild();
}

void WorkshopView::refreshTempo() {
    auto& L = ed.proc.live(); const bool has = L.song() != nullptr;
    for (juce::Component* c : { (juce::Component*) &tempo_, (juce::Component*) &tempoMinus_, (juce::Component*) &tempoPlus_ }) c->setEnabled (has);
    if (! tempo_.hasKeyboardFocus (true)) tempo_.setText (has ? juce::String (wr::formatBpm (L.songBpm())) : juce::String ("-"), false);
}
void WorkshopView::setSongTempo (double bpm) {
    auto& L = ed.proc.live(); const Song* cur = L.song(); if (! cur) return;
    bpm = clampBpm (bpm); if (bpm <= 0) { refreshTempo(); return; }
    L.setBpm (bpm); if (L.saveTempoToSong()) ed.proc.store.saveSoon();
    tempo_.setText (juce::String (wr::formatBpm (bpm)), false);
    msg_ = "Tempo for \"" + juce::String (cur->chart.title) + "\" set to " + juce::String (wr::formatBpm (bpm)) + " BPM"; repaint();
}

void WorkshopView::refresh() {
    refreshTempo();
    work_ = ed.proc.currentRig();
    trimPre_.setValue (ed.proc.store.lib.settings.trimPreDb, juce::dontSendNotification); trimPost_.setValue (ed.proc.store.lib.settings.trimPostDb, juce::dontSendNotification);
    // reflect live state (scene) into the working copy
    for (auto& l : work_.lanes) for (auto& d : l) { d.enabled = ed.proc.pedalOn (d.uid); }
    if (ampSection (work_) == nullptr) { ensureAmpSection (work_, ed.proc.store.lib.settings.routing == 0); ed.proc.rebuildRig (work_); }
    if (selected_.empty() && ! work_.lanes[0].empty()) selected_ = work_.lanes[0].front().uid;
    buildEditor(); repaint();
}

void WorkshopView::rebuild() {
    for (auto& l : work_.lanes) for (auto& d : l) { if (! ed.proc.pedalOn (d.uid) && d.enabled) {} }
    ensureAmpSection (work_, usingRealAmp (work_)); // keep the amp section last in the before-amp lane
    ed.proc.rebuildRig (work_); buildEditor(); repaint();
}
void WorkshopView::loadIrFile() {
    irChooser_ = std::make_unique<juce::FileChooser> ("Load a cab impulse response (WAV)", juce::File(), "*.wav", true, false, this);
    // iOS: the Files picker hands back a security-scoped URL outside the sandbox. JUCE's URL::createInputStream opens it with
    // startAccessingSecurityScopedResource / stopAccessing..., so read the bytes through the URL (not File) and copy them into
    // the app container (<data>/IRs) - the original URL stops working after this call.
    irChooser_->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc) {
        auto url = fc.getURLResult(); if (url.isEmpty()) return;
        juce::MemoryBlock mb;
        if (auto in = url.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress))) {
            if (in->getTotalLength() > 8 * 1024 * 1024) { juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Load IR", "That file is larger than 8 MB - cab IRs are usually under 1 MB."); return; }
            in->readIntoMemoryBlock (mb, 8 * 1024 * 1024 + 1);
        }
        auto name = url.isLocalFile() ? url.getLocalFile().getFileName() : url.getFileName();
        auto dir = ed.proc.store.file().getParentDirectory().getChildFile ("IRs"); dir.createDirectory();
        std::string err; auto path = importIr ((const uint8_t*) mb.getData(), mb.getSize(), juce::URL::removeEscapeChars (name).toStdString(), dir.getFullPathName().toStdString(), err);
        if (path.empty()) { juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Load IR", juce::String (err)); return; }
        auto info = checkIr ((const uint8_t*) mb.getData(), mb.getSize());
        if (setUserIr (work_, path)) {
            msg_ = "IR loaded: " + juce::String (userIrName (work_)) + "  (" + juce::String (info.ms, 0) + " ms, " + juce::String ((int) info.sampleRate) + " Hz" + (info.truncated ? ", first 21 ms used" : "") + ")";
            rebuild(); }
        else juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Load IR", "Couldn't read that file. Use a mono or stereo WAV (16/24-bit or 32-bit float)."); });
}

void WorkshopView::select (const std::string& uid) { selected_ = uid; buildEditor(); repaint(); }

std::unique_ptr<juce::ComponentTraverser> WorkshopView::createKeyboardFocusTraverser() {
    struct Quiet : juce::KeyboardFocusTraverser {
        juce::Component* getDefaultComponent (juce::Component*) override { return nullptr; } // never auto-focus the search field
    };
    return std::make_unique<Quiet>();
}
void WorkshopView::buildEditor() {
    if (auto* f = juce::Component::getCurrentlyFocusedComponent())
        if (f != &search_ && f != &tempo_ && isParentOf (f)) juce::Component::unfocusAllComponents();
    for (auto* c : knobs_) removeChildComponent (c); for (auto* c : knobLabels_) removeChildComponent (c); for (auto* c : choices_) removeChildComponent (c);
    knobs_.clear(); knobLabels_.clear(); choices_.clear(); choiceBtns_.clear(); cellKind_.clear(); pillAt_.clear(); pillN_.clear();
    const Chain* chain = ed.proc.layout(); const PedalSlot* slot = chain ? const_cast<Chain*> (chain)->find (selected_) : nullptr;
    onOff_.setVisible (slot != nullptr); remove_.setVisible (slot != nullptr); reset_.setVisible (slot != nullptr);
    if (! slot) { resized(); return; }
    bool on = ed.proc.pedalOn (selected_); onOff_.setButtonText ({}); onOff_.setToggleState (on, juce::dontSendNotification);
    std::string uid = selected_; const bool isAmp = uid == kAmpUid; groups_.clear();
    remove_.setVisible (! isAmp); realAmp_.setVisible (isAmp); loadIr_.setVisible (isAmp);
    { const auto irName = userIrName (work_); removeIr_.setVisible (isAmp && ! irName.empty()); loadIr_.setButtonText (irName.empty() ? "Load IR file..." : "Replace IR (" + juce::String (irName) + ")..."); }
    if (isAmp) { const bool real = usingRealAmp (work_); realAmp_.setButtonText (real ? "Using my own amp (sim bypassed)" : "No amp - use the sim");
        realAmp_.setToggleState (real, juce::dontSendNotification); onOff_.setVisible (false); }
    // parameter order: amp = HEAD / CAB + MIC / OUTPUT groups; pedals with a face = main knobs first, then MORE
    std::vector<int> order;
    auto idx = [&] (const std::string& id) { return slot->pedal->paramIndex (id); };
    if (isAmp) {
        groups_.push_back ({ 0, "HEAD" }); for (auto* id : { "model", "gain", "bass", "mid", "treble", "presence", "master", "level" }) order.push_back (idx (id));
        groups_.push_back ({ (int) order.size(), "CAB + MIC" }); for (auto* id : { "cab", "mic", "micpos" }) order.push_back (idx (id));
        groups_.push_back ({ (int) order.size(), "OUTPUT" }); order.push_back (idx ("output"));
    } else if (auto* face = pedalFace (slot->pedal->type)) {
        for (auto& id : face->mainParams) if (idx (id) >= 0) order.push_back (idx (id));
        const int nMain = (int) order.size(); for (int i = 0; i < slot->pedal->numParams(); ++i) if (std::find (order.begin(), order.end(), i) == order.end()) order.push_back (i);
        if ((int) order.size() > nMain) groups_.push_back ({ nMain, "MORE" });
    } else for (int i = 0; i < slot->pedal->numParams(); ++i) order.push_back (i);
    order.erase (std::remove (order.begin(), order.end(), -1), order.end());
    for (int i : order) {
        auto sp = slot->pedal->specs()[(size_t) i]; float v = ed.proc.paramValue (uid, sp.id);
        auto* lab = knobLabels_.add (new juce::Label ({}, sp.name)); lab->setJustificationType (juce::Justification::centred); lab->setMinimumHorizontalScale (0.6f);
        lab->setFont (font (isAmp ? 11.f : 12.f, true)); lab->setColour (juce::Label::textColourId, isAmp ? dim : text); addAndMakeVisible (lab);
        bool pills = ! isAmp && sp.isChoice() && sp.choices.size() >= 2 && sp.choices.size() <= 4;
        if (pills) for (auto& c : sp.choices) if ((int) c.size() > 12) pills = false;
        if (sp.isChoice() && pills) {
            knobs_.add (nullptr); cellKind_.push_back (2); pillAt_.push_back ((int) choiceBtns_.size()); pillN_.push_back ((int) sp.choices.size());
            int which = (int) std::lround (v), base = (int) choiceBtns_.size(), nch = (int) sp.choices.size();
            for (int k = 0; k < nch; ++k) {
                auto* b = choiceBtns_.add (new juce::TextButton (sp.choices[(size_t) k]));
                b->setClickingTogglesState (false); b->setToggleState (k == which, juce::dontSendNotification); b->setMouseClickGrabsKeyboardFocus (false); addAndMakeVisible (b);
                auto id = sp.id;
                b->onClick = [this, uid, id, k, base, nch] { ed.proc.setParam (uid, id, (float) k); for (auto& l : work_.lanes) for (auto& d : l) if (d.uid == uid) d.params[id] = (float) k;
                    for (int j = 0; j < nch; ++j) if (auto* sibl = choiceBtns_[base + j]) sibl->setToggleState (j == k, juce::dontSendNotification); };
            }
        } else if (sp.isChoice()) {
            auto* cb = choices_.add (new juce::ComboBox()); for (int k = 0; k < (int) sp.choices.size(); ++k) cb->addItem (sp.choices[(size_t) k], k + 1);
            cb->setSelectedId ((int) std::lround (v) + 1, juce::dontSendNotification); addAndMakeVisible (cb);
            cb->onChange = [this, uid, sp, cb] { ed.proc.setParam (uid, sp.id, (float) (cb->getSelectedId() - 1)); for (auto& l : work_.lanes) for (auto& d : l) if (d.uid == uid) d.params[sp.id] = (float) (cb->getSelectedId() - 1); };
            knobs_.add (nullptr); cellKind_.push_back (1); pillAt_.push_back (-1); pillN_.push_back (0);
        } else {
            auto* k = knobs_.add (new juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow));
            k->setRange (sp.min, sp.max, (sp.max - sp.min) > 50 ? 1.0 : 0.0); k->setValue (v, juce::dontSendNotification); k->setTextValueSuffix (sp.unit.empty() ? "" : " " + sp.unit);
            k->setNumDecimalPlacesToDisplay ((sp.max - sp.min) > 50 ? 0 : 2); k->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16); k->setDoubleClickReturnValue (true, sp.def);
            if (! isAmp) { k->setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack); k->setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack); k->setColour (juce::Slider::textBoxTextColourId, juce::Colours::white); }
            k->onValueChange = [this, uid, sp, k] { ed.proc.setParam (uid, sp.id, (float) k->getValue()); for (auto& l : work_.lanes) for (auto& d : l) if (d.uid == uid) d.params[sp.id] = (float) k->getValue(); };
            addAndMakeVisible (k); cellKind_.push_back (0); pillAt_.push_back (-1); pillN_.push_back (0);
        }
    }
    if (! isAmp) { float al = on ? 1.f : 0.55f; for (auto* k : knobs_) if (k) k->setAlpha (al); for (auto* l : knobLabels_) l->setAlpha (al); for (auto* c : choices_) c->setAlpha (al); for (auto* b : choiceBtns_) b->setAlpha (al); reset_.setAlpha (al); remove_.setAlpha (al); }
    resized();
}

void WorkshopView::resized() {
    auto r = getLocalBounds();
    if (auto* disp = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()) {
        auto in = disp->safeAreaInsets;
        r.removeFromLeft (in.getLeft()); r.removeFromRight (in.getRight()); r.removeFromBottom (in.getBottom());
    }
    r = r.reduced (18, 14);
    libArea_ = r.removeFromLeft (250); r.removeFromLeft (14); edArea_ = r.removeFromRight (330); r.removeFromRight (14);
    search_.setBounds (libArea_.getX(), libArea_.getY() + 22, libArea_.getWidth(), 36);
    libList_ = libArea_.withTrimmedTop (64);
    layoutHeaderTempo();
    auto lanes = r.withTrimmedTop (24); int lh = (lanes.getHeight() - 60) / 2;
    laneArea_[0] = lanes.removeFromTop (lh); ampBar_ = lanes.removeFromTop (60).reduced (0, 14); laneArea_[1] = lanes;
    auto bottom = edArea_.reduced (10, 8); auto b2 = bottom.removeFromBottom (36); saveScene_.setBounds (b2.removeFromLeft (b2.getWidth() / 2 - 4)); b2.removeFromLeft (8); assign_.setBounds (b2);
    bottom.removeFromBottom (6); { auto rr = bottom.removeFromBottom (30); backups_.setBounds (rr.removeFromRight (100)); rr.removeFromRight (6); saveRig_.setBounds (rr); }
    bottom.removeFromBottom (6); trimPost_.setBounds (bottom.removeFromBottom (24)); trimPostL_.setBounds (bottom.removeFromBottom (14)); bottom.removeFromBottom (2);
    trimPre_.setBounds (bottom.removeFromBottom (24)); trimPreL_.setBounds (bottom.removeFromBottom (14));
    faceArea_ = {};
    if (selected_ == kAmpUid) {
        pedalRect_ = {};
        auto e = edArea_.reduced (14, 12); e.removeFromTop (54);
        auto bt = e.removeFromTop (34);
        realAmp_.setBounds (bt.removeFromLeft (bt.getWidth() - 88)); bt.removeFromLeft (8); reset_.setBounds (bt);
        e.removeFromTop (10);
        auto row = e.removeFromBottom (230).removeFromTop (30); if (removeIr_.isVisible()) { removeIr_.setBounds (row.removeFromRight (120)); row.removeFromRight (8); } loadIr_.setBounds (row);
        const int cols = 3, kw = e.getWidth() / cols, kh = 84, n = std::max (knobs_.size(), knobLabels_.size()); int ci = 0, col = 0, y = e.getY();
        for (int i = 0; i < n; ++i) {
            for (auto& gr : groups_) if (gr.first == i) { if (col != 0) { y += kh; col = 0; } y += 18; }
            juce::Rectangle<int> cell (e.getX() + col * kw, y, kw, kh); if (++col == cols) { col = 0; y += kh; }
            if (i < knobLabels_.size()) knobLabels_[i]->setBounds (cell.removeFromTop (16));
            if (i < knobs_.size() && knobs_[i] != nullptr) knobs_[i]->setBounds (cell.reduced (4, 0)); else if (ci < choices_.size()) choices_[ci++]->setBounds (cell.withSizeKeepingCentre (kw - 10, 30));
        }
        return;
    }
    if (knobs_.size() == 0) { pedalRect_ = {}; return; }
    int dockTop = trimPreL_.getY() - 6;
    pedalRect_ = edArea_.withBottom (dockTop).reduced (6, 4);
    if (pedalRect_.getRight() > edArea_.getRight() - 2) pedalRect_.setRight (edArea_.getRight() - 2);
    if (pedalRect_.getHeight() < 120) pedalRect_ = edArea_.reduced (6, 4);
    auto face = pedalRect_.reduced (18, 12);
    face.removeFromTop (44);
    auto footBand = face.removeFromBottom (54);
    auto act = face.removeFromBottom (26);
    auto actions = act.withSizeKeepingCentre (std::min (act.getWidth(), 168), 24);
    reset_.setBounds (actions.removeFromLeft (78)); actions.removeFromLeft (8); remove_.setBounds (actions);
    onOff_.setBounds (footBand.withSizeKeepingCentre (46, 46));
    int n = (int) knobs_.size();
    int cols = n <= 1 ? 1 : n <= 3 ? n : n <= 4 ? 2 : n <= 6 ? 3 : 4;
    int rowsN = (n + cols - 1) / cols;
    auto grid = face.reduced (4, 2);
    float cw = grid.getWidth() / (float) std::max (1, cols), ch = grid.getHeight() / (float) std::max (1, rowsN); int ci = 0;
    for (int i = 0; i < n; ++i) {
        auto cell = juce::Rectangle<float> (grid.getX() + (i % cols) * cw, grid.getY() + (i / cols) * ch, cw, ch).reduced (5, 3);
        if (i < knobLabels_.size()) knobLabels_[i]->setBounds (cell.removeFromTop (16).toNearestInt());
        int kind = i < (int) cellKind_.size() ? cellKind_[(size_t) i] : (knobs_[i] != nullptr ? 0 : 1);
        if (kind == 0 && knobs_[i] != nullptr) {
            const int textH = 15;
            int d = (int) std::min (cell.getWidth() - 6.f, cell.getHeight() - textH - 2.f);
            d = std::min (108, std::max (36, d));
            auto box = juce::Rectangle<int> (d, d + textH).withCentre ({ cell.toNearestInt().getCentreX(), (int) cell.getY() + (d + textH) / 2 });
            if (box.getBottom() > (int) cell.getBottom()) box.setBottom ((int) cell.getBottom());
            if (box.getRight() > pedalRect_.getRight() - 10) box.setRight (pedalRect_.getRight() - 10);
            if (box.getX() < pedalRect_.getX() + 10) box.setX (pedalRect_.getX() + 10);
            knobs_[i]->setTextBoxStyle (juce::Slider::TextBoxBelow, false, d, textH);
            knobs_[i]->setBounds (box);
        } else if (kind == 2) {
            int start = pillAt_[(size_t) i], cnt = pillN_[(size_t) i];
            bool side = cnt <= 3 && cell.getWidth() >= cnt * 46.f;
            auto area = cell;
            if (side) { float bw = area.getWidth() / (float) std::max (1, cnt); for (int k = 0; k < cnt; ++k) if (auto* b = choiceBtns_[start + k]) b->setBounds (area.removeFromLeft (bw).reduced (2, std::max (2.f, (area.getHeight() - 28) * 0.5f)).toNearestInt()); }
            else { int bh = std::max (20, std::min (28, (int) area.getHeight() / std::max (1, cnt))); for (int k = 0; k < cnt; ++k) if (auto* b = choiceBtns_[start + k]) b->setBounds (area.removeFromTop ((float) bh).reduced (2, 1).toNearestInt()); }
        } else if (ci < choices_.size()) choices_[ci++]->setBounds (cell.withSizeKeepingCentre (cell.getWidth() - 4, std::min (30.f, cell.getHeight())).toNearestInt());
    }
}

void WorkshopView::paintTopBar (juce::Graphics& g, juce::Rectangle<int> r) {
    auto cluster = headerTempoBounds();
    int trim = std::max (0, r.getRight() - cluster.getX() + 8);
    auto title = r.withTrimmedRight (trim);
    auto& L = ed.proc.live(); g.setColour (dim); g.setFont (font (13)); g.drawText ("Rig workshop - editing rig \"" + juce::String (work_.name) + "\"", title.getX(), 12, title.getWidth(), 16, juce::Justification::left, true);
    g.setColour (juce::Colours::white); g.setFont (font (19, true)); g.drawText (L.song() ? juce::String (L.chart().title) + "  -  " + juce::String (L.currentSceneName().empty() ? "no scene" : L.currentSceneName()) : juce::String ("No song loaded"), title.getX(), 28, title.getWidth(), 24, juce::Justification::left, true);
}

int WorkshopView::dropIndex (int lane, juce::Point<int> p) const {
    int idx = 0, best = 1 << 30;
    for (auto& t : tiles_) if (std::get<1> (t) == lane) {
        auto r = std::get<0> (t); int d = std::abs (p.x - r.getCentreX()) + std::abs (p.y - r.getCentreY()) * 3;
        if (d < best) { best = d; bool after = p.y > r.getBottom() - 8 || (p.y >= r.getY() && p.x > r.getCentreX()); idx = std::get<2> (t) + (after ? 1 : 0); }
    }
    return idx;
}

void WorkshopView::paint (juce::Graphics& g) {
    g.fillAll (bg); libItems_.clear(); libHeaders_.clear(); tiles_.clear();
    label (g, "PEDAL LIBRARY", libArea_.withHeight (18)); label (g, "tap or drag", libArea_.withHeight (18), dimmer, 11, juce::Justification::centredRight);
    auto q = search_.getText().toLowerCase();
    auto drawChips = [&] (juce::Graphics& gg, std::vector<const PedalInfo*> items, int& cy) {
        if (items.empty()) return;
        const int gap = 6, cw = (libList_.getWidth() - gap) / 2, ch = 34;
        int rowsN = ((int) items.size() + 1) / 2;
        for (int i = 0; i < (int) items.size(); ++i) {
            auto* pi = items[(size_t) i];
            juce::Rectangle<int> b (libList_.getX() + (i % 2) * (cw + gap), cy + (i / 2) * (ch + gap), cw, ch);
            if (b.intersects (libList_)) {
                auto look = pedalLook (pi->type); juce::Colour hue ((juce::uint32) look.argb);
                pill (gg, b.toFloat(), card, pi->implemented ? cardBorder : line, 8);
                gg.setColour (hue); gg.fillRoundedRectangle ((float) b.getX() + 5, (float) b.getY() + 7, 5, (float) b.getHeight() - 14, 2);
                gg.setColour (pi->implemented ? text : dimmer); gg.setFont (font (12, true));
                gg.drawFittedText (pi->name, b.withTrimmedLeft (16).withTrimmedRight (6), juce::Justification::centredLeft, 1);
                auto hit = b.getIntersection (libList_); if (! hit.isEmpty()) libItems_.push_back ({ hit, pi->type });
            }
        }
        cy += rowsN * (ch + gap) + 4;
    };
    g.saveState(); g.reduceClipRegion (libList_);
    int cy = libList_.getY() - libScroll_;
    g.setColour (dimmer); g.setFont (font (10, true));
    g.drawText ("RECENT / FAVORITES", libList_.getX(), cy, libList_.getWidth(), 16, juce::Justification::centredLeft); cy += 18;
    { std::vector<const PedalInfo*> pins; auto& fav = ed.proc.store.lib.settings.favoritePedals; auto& rec = ed.proc.store.lib.settings.recentPedals;
      auto pushType = [&] (const std::string& t) { if (auto* pi = infoFor (t)) if (q.isEmpty() || juce::String (pi->name).toLowerCase().contains (q)) if (std::find (pins.begin(), pins.end(), pi) == pins.end()) pins.push_back (pi); };
      for (auto& t : fav) pushType (t); for (auto& t : rec) pushType (t);
      if ((int) pins.size() > 6) pins.resize (6);
      if (pins.empty()) { g.setColour (dimmer); g.setFont (font (11)); g.drawText ("Long-press a pedal to favorite it", libList_.getX(), cy, libList_.getWidth(), 18, juce::Justification::centredLeft); cy += 22; }
      else drawChips (g, pins, cy);
    }
    for (int cat = 0; cat <= (int) PedalCategory::Utility; ++cat) {
        std::vector<const PedalInfo*> all, shown;
        for (auto& pi : pedalCatalog()) if ((int) pi.category == cat) { all.push_back (&pi); if (q.isEmpty() || juce::String (pi.name).toLowerCase().contains (q)) shown.push_back (&pi); }
        if (all.empty() || (q.isNotEmpty() && shown.empty())) continue;
        bool open = q.isNotEmpty() || categoryOpen (cat);
        juce::Rectangle<int> hdr (libList_.getX(), cy, libList_.getWidth(), 24);
        if (hdr.intersects (libList_)) {
            g.setColour (dim); g.setFont (font (12, true));
            g.drawText ((open ? juce::String ("v  ") : juce::String (">  ")) + juce::String (categoryName ((PedalCategory) cat)).toUpperCase(), hdr.withTrimmedRight (28), juce::Justification::centredLeft, true);
            g.setColour (dimmer); g.drawText (juce::String ((int) all.size()), hdr, juce::Justification::centredRight);
            auto hit = hdr.getIntersection (libList_); if (! hit.isEmpty()) libHeaders_.push_back ({ hit, cat });
        }
        cy += 26;
        if (open) drawChips (g, shown, cy);
    }
    int contentH = cy + libScroll_ - libList_.getY();
    int maxS = std::max (0, contentH - libList_.getHeight());
    libMaxScroll_ = maxS;
    if (libScroll_ > libMaxScroll_) { libScroll_ = libMaxScroll_; repaint(); }
    g.restoreState();
    // ---- lanes
    const char* titles[2] = { "BEFORE AMP  (mono)  -  guitar -> M4 in 1 -> ... -> M4 out 1 -> Simplifier input", "AFTER AMP  (stereo)  -  Simplifier loop send -> M4 in 2 -> ... -> M4 outs 3/4 -> loop return L/R" };
    const char* ends[2][2] = { { "IN 1", "OUT 1" }, { "IN 2", "OUT 3/4" } };
    label (g, "SIGNAL CHAIN", juce::Rectangle<int> (laneArea_[0].getX(), libArea_.getY(), 300, 18)); label (g, ed.proc.store.lib.settings.routing == 0 ? "4-cable" : "2-cable (lanes run in series)", juce::Rectangle<int> (laneArea_[0].getX(), libArea_.getY(), laneArea_[0].getWidth(), 18), orange, 11, juce::Justification::centredRight);
    for (int l = 0; l < 2; ++l) {
        auto a = laneArea_[l]; pill (g, a.toFloat(), panel, l == dropLane_ ? pink : cardBorder, 12);
        g.setColour (dim); g.setFont (font (11, true)); g.drawText (titles[l], a.reduced (12, 8).removeFromTop (16), juce::Justification::left, true);
        auto row = a.reduced (12, 0).withTrimmedTop (28).withTrimmedBottom (8);
        auto endBox = [&] (juce::Rectangle<int> b, const char* t) { pill (g, b.toFloat(), well, cardBorder, 8); g.setColour (dimmer); g.setFont (font (11, true)); g.drawText (t, b, juce::Justification::centred); };
        endBox (row.removeFromLeft (44).withSizeKeepingCentre (44, 36), ends[l][0]); endBox (row.removeFromRight (52).withSizeKeepingCentre (52, 36), ends[l][1]);
        row.reduce (6, 0);
        std::vector<int> idx; for (int i = 0; i < (int) work_.lanes[l].size(); ++i) if (work_.lanes[l][(size_t) i].uid != kAmpUid) idx.push_back (i);
        const int n = (int) idx.size(), gap = 6, minW = 108, th = 72;
        const int per = std::max (1, (row.getWidth() + gap) / (minW + gap));
        const int needRows = n == 0 ? 1 : (n + per - 1) / per;
        const int maxRows = std::max (1, (row.getHeight() + gap) / (th + gap));
        const bool scroll = needRows > maxRows;
        int tw = scroll ? minW : std::min (148, std::max (minW, (row.getWidth() - gap * (per - 1)) / std::max (1, std::min (per, std::max (1, n)))));
        const int content = scroll ? n * (tw + gap) : row.getWidth();
        laneMaxScroll_[l] = std::max (0, content - row.getWidth());
        laneScroll_[l] = std::clamp (laneScroll_[l], 0, laneMaxScroll_[l]);
        laneLeft_[l] = laneRight_[l] = {};
        if (scroll) {
            auto hdr = a.reduced (12, 6).removeFromTop (18).removeFromRight (52);
            laneRight_[l] = hdr.removeFromRight (22); laneLeft_[l] = hdr.removeFromLeft (22);
            g.setColour (laneScroll_[l] > 0 ? text : dimmer); g.setFont (font (14, true)); g.drawText ("<", laneLeft_[l], juce::Justification::centred);
            g.setColour (laneScroll_[l] < laneMaxScroll_[l] ? text : dimmer); g.drawText (">", laneRight_[l], juce::Justification::centred);
        }
        g.saveState(); g.reduceClipRegion (row);
        const int x0 = row.getX() - (scroll ? laneScroll_[l] : 0);
        for (int k = 0; k < n; ++k) {
            int i = idx[(size_t) k]; auto& d = work_.lanes[l][(size_t) i];
            int col = scroll ? k : k % per, rr = scroll ? 0 : k / per;
            juce::Rectangle<int> full (x0 + col * (tw + gap), row.getY() + rr * (th + gap), tw, std::min (th, row.getHeight()));
            if (l == dropLane_ && i == dropIdx_) { g.setColour (pink); g.fillRect (full.getX() - 4, full.getY(), 3, full.getHeight()); }
            auto look = pedalLook (d.type); juce::Colour hue ((juce::uint32) look.argb);
            bool sel = d.uid == selected_, on = ed.proc.pedalOn (d.uid);
            auto* info = infoFor (d.type);
            paintChainStomp (g, full, d.type, info ? juce::String (info->name) : juce::String (d.type), info ? info->category : PedalCategory::Utility, hue, on, sel);
            tiles_.push_back ({ full, l, i });
        }
        g.restoreState();
    }
    pill (g, ampBar_.toFloat(), panel, accentText.withAlpha (0.7f), 10); g.setColour (accentText); g.setFont (font (12, true));
    g.drawText ("v  AMP: " + juce::String (ampSummary (work_)) + "   (tap to edit)  v", ampBar_, juce::Justification::centred, true);
    g.setColour (dimmer); g.setFont (font (10)); g.drawText ("tap = select - double-tap = bypass - drag tiles between lanes - " + msg_, laneArea_[1].translated (0, laneArea_[1].getHeight() + 2).withHeight (14), juce::Justification::left);
    const Chain* chain = ed.proc.layout(); const PedalSlot* slot = chain ? const_cast<Chain*> (chain)->find (selected_) : nullptr;
    if (slot && selected_ != kAmpUid && ! pedalRect_.isEmpty()) {
        g.saveState(); g.reduceClipRegion (edArea_);
        auto full = pedalRect_; bool on = ed.proc.pedalOn (selected_);
        auto look = pedalLook (slot->pedal->type); juce::Colour hue ((juce::uint32) look.argb);
        { juce::Graphics::ScopedSaveState ss (g); if (! on) g.setOpacity (0.45f);
          juce::ColourGradient grad (hue.brighter (0.38f), (float) full.getX(), (float) full.getY(), hue.darker (0.45f), (float) full.getRight(), (float) full.getBottom(), false);
          g.setGradientFill (grad); g.fillRoundedRectangle (full.toFloat(), 14);
          g.setColour (juce::Colours::white.withAlpha (0.14f));
          g.fillRoundedRectangle (full.toFloat().reduced (4).withHeight (full.getHeight() * 0.16f), 12);
          g.setColour (hue.darker (0.25f)); g.drawRoundedRectangle (full.toFloat().reduced (1.5f), 14, 1.4f);
          auto art = full.reduced (20, 16).toFloat(); art = art.withTrimmedTop (art.getHeight() * 0.16f).withTrimmedBottom (art.getHeight() * 0.55f);
          motif (g, art, slot->pedal->category, hue);
          g.setColour (text); g.setFont (font (std::min (32.f, full.getHeight() * 0.07f), true));
          g.drawFittedText (slot->pedal->displayName, full.reduced (16, 10).removeFromTop (40), juce::Justification::centred, 1);
          g.setColour (hue.brighter (0.7f)); g.setFont (font (11, true));
          g.drawText (look.tag, full.reduced (16, 0).withY (full.getY() + 44).withHeight (16), juce::Justification::centred);
          led (g, { (float) full.getRight() - 18, (float) full.getY() + 18 }, on);
          if (! slot->pedal->implemented) { g.setColour (juce::Colours::white); g.setFont (font (10, true)); g.drawText ("passes audio", full.removeFromBottom (18), juce::Justification::centred); }
        }
        g.setColour (accentText); g.drawRoundedRectangle (full.toFloat().reduced (1.2f), 14, 2.4f);
        g.restoreState();
    } else {
        pill (g, edArea_.toFloat(), panel, cardBorder, 12);
        auto e = edArea_.reduced (14, 12);
        if (slot) { Lane ln; int idx; const_cast<Chain*> (chain)->find (selected_, &ln, &idx);
            label (g, "SELECTED - " + juce::String (categoryName (slot->pedal->category)).toUpperCase(), e.removeFromTop (16));
            g.setColour (text); g.setFont (font (18, true)); g.drawText (juce::String (slot->pedal->displayName) + "  -  " + (ln == Lane::Pre ? "before amp" : "after amp"), e.removeFromTop (28), juce::Justification::centredLeft, true);
            for (auto& gr : groups_) if (gr.first < knobLabels_.size()) { auto lb = knobLabels_[gr.first]->getBounds(); label (g, gr.second, juce::Rectangle<int> (edArea_.getX() + 14, lb.getY() - 18, 200, 16), orange, 11); }
            if (selected_ == kAmpUid) label (g, usingRealAmp (work_) ? "Sim bypassed: the lanes run into your real amp as before." : "Direct to PA = no amp needed. FX Return = preamp into a real power amp.",
                loadIr_.getBounds().translated (0, 34).withHeight (32).withX (edArea_.getX() + 14).withWidth (edArea_.getWidth() - 28), dim, 10);
        } else label (g, "Select a pedal to edit it", e.removeFromTop (16));
    }
}

void WorkshopView::mouseDown (const juce::MouseEvent& e) {
    scrollLane_ = -1; libScrolling_ = false; libPress_ = -1; libHeaderPress_ = -1;
    for (int l = 0; l < 2 && scrollLane_ < 0; ++l) if (laneArea_[l].contains (e.getPosition()) && laneMaxScroll_[l] > 0) {
        bool onTile = false; for (auto& t : tiles_) if (std::get<1> (t) == l && std::get<0> (t).contains (e.getPosition())) onTile = true;
        if (! onTile && ! laneLeft_[l].contains (e.getPosition()) && ! laneRight_[l].contains (e.getPosition())) { scrollLane_ = l; scrollGrab_ = laneScroll_[l]; }
    }
    if (libList_.contains (e.getPosition())) {
        for (int i = 0; i < (int) libItems_.size(); ++i) if (libItems_[(size_t) i].first.contains (e.getPosition())) libPress_ = i;
        for (auto& h : libHeaders_) if (h.first.contains (e.getPosition())) libHeaderPress_ = h.second;
        if (libPress_ < 0 && libHeaderPress_ < 0 && libMaxScroll_ > 0) { libScrolling_ = true; libScrollGrab_ = libScroll_; }
    }
}
void WorkshopView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) {
    if (libList_.contains (e.getPosition()) && libMaxScroll_ > 0) { libScroll_ = std::clamp (libScroll_ - (int) (w.deltaY * 80), 0, libMaxScroll_); repaint(); return; }
    for (int l = 0; l < 2; ++l) if (laneArea_[l].contains (e.getPosition()) && laneMaxScroll_[l] > 0) {
        laneScroll_[l] = std::clamp (laneScroll_[l] - (int) ((w.deltaX + w.deltaY) * 140), 0, laneMaxScroll_[l]); repaint(); }
}
void WorkshopView::mouseDrag (const juce::MouseEvent& e) {
    if (libScrolling_ && ! isDragAndDropActive()) { libScroll_ = std::clamp (libScrollGrab_ - e.getDistanceFromDragStartY(), 0, libMaxScroll_); repaint(); return; }
    if (scrollLane_ >= 0 && ! isDragAndDropActive()) {
        laneScroll_[scrollLane_] = std::clamp (scrollGrab_ - e.getDistanceFromDragStartX(), 0, laneMaxScroll_[scrollLane_]); repaint(); return; }
    if (e.getDistanceFromDragStart() < 8 || isDragAndDropActive()) return;
    auto p = e.getMouseDownPosition();
    for (auto& it : libItems_) if (it.first.contains (p)) { auto look = pedalLook (it.second); juce::Colour hue ((juce::uint32) look.argb); juce::Image img (juce::Image::ARGB, it.first.getWidth(), it.first.getHeight(), true); { juce::Graphics g (img); pill (g, img.getBounds().toFloat(), hue, hue, 8); }
        startDragging ("new:" + juce::String (it.second), this, juce::ScaledImage (img)); return; }
    for (auto& t : tiles_) if (laneArea_[std::get<1> (t)].contains (p) && std::get<0> (t).contains (p)) { juce::Image img (juce::Image::ARGB, std::get<0> (t).getWidth(), std::get<0> (t).getHeight(), true); { juce::Graphics g (img); pill (g, img.getBounds().toFloat(), pink.withAlpha (0.6f), pink, 10); }
        startDragging ("move:" + juce::String (std::get<1> (t)) + ":" + juce::String (std::get<2> (t)), this, juce::ScaledImage (img)); return; }
}
void WorkshopView::mouseUp (const juce::MouseEvent& e) {
    if (libScrolling_ && e.getDistanceFromDragStart() > 8) { libScrolling_ = false; return; }
    libScrolling_ = false;
    if (scrollLane_ >= 0 && e.getDistanceFromDragStart() > 8) { scrollLane_ = -1; return; }
    scrollLane_ = -1;
    for (int l = 0; l < 2; ++l) {
        if (laneLeft_[l].contains (e.getPosition())) { laneScroll_[l] = std::max (0, laneScroll_[l] - 120); repaint(); return; }
        if (laneRight_[l].contains (e.getPosition())) { laneScroll_[l] = std::min (laneMaxScroll_[l], laneScroll_[l] + 120); repaint(); return; }
    }
    if (e.getDistanceFromDragStart() > 8) return;
    if (libHeaderPress_ >= 0) { for (auto& h : libHeaders_) if (h.second == libHeaderPress_ && h.first.contains (e.getPosition())) { toggleCategory (libHeaderPress_); return; } }
    if (libPress_ >= 0 && libPress_ < (int) libItems_.size() && libItems_[(size_t) libPress_].first.contains (e.getPosition())) {
        auto type = libItems_[(size_t) libPress_].second;
        if (e.getLengthOfMousePress() >= 450) toggleFavorite (type); else addFromLibrary (type);
        return;
    }
    if (ampBar_.contains (e.getPosition())) { select (kAmpUid); return; }
    for (auto& t : tiles_) if (laneArea_[std::get<1> (t)].contains (e.getPosition()) && std::get<0> (t).contains (e.getPosition())) {
        auto& d = work_.lanes[std::get<1> (t)][(size_t) std::get<2> (t)];
        if (e.getNumberOfClicks() >= 2) { bool on = ! ed.proc.pedalOn (d.uid); ed.proc.setPedal (d.uid, on); d.enabled = on; }
        select (d.uid); return;
    }
}
void WorkshopView::itemDragMove (const SourceDetails& s) {
    dropLane_ = -1; for (int l = 0; l < 2; ++l) if (laneArea_[l].contains (s.localPosition)) { dropLane_ = l; dropIdx_ = dropIndex (l, s.localPosition); } repaint();
}
void WorkshopView::itemDropped (const SourceDetails& s) {
    int lane = -1; for (int l = 0; l < 2; ++l) if (laneArea_[l].contains (s.localPosition)) lane = l;
    dropLane_ = -1; if (lane < 0) { repaint(); return; }
    int idx = dropIndex (lane, s.localPosition); auto desc = s.description.toString();
    if (desc.startsWith ("new:")) {
        auto type = desc.fromFirstOccurrenceOf ("new:", false, false).toStdString(); std::string uid = type; int n = 2;
        auto exists = [&] (const std::string& u) { for (auto& l : work_.lanes) for (auto& d : l) if (d.uid == u) return true; return false; };
        while (exists (uid)) uid = type + std::to_string (n++);
        work_.lanes[lane].insert (work_.lanes[lane].begin() + std::min (idx, (int) work_.lanes[lane].size()), PedalDef { uid, type, true, {} }); selected_ = uid; rememberRecent (type);
    } else if (desc.startsWith ("move:")) {
        auto parts = juce::StringArray::fromTokens (desc, ":", ""); int fl = parts[1].getIntValue(), fi = parts[2].getIntValue();
        if (fl < 0 || fl > 1 || fi >= (int) work_.lanes[fl].size()) return;
        auto d = work_.lanes[fl][(size_t) fi]; work_.lanes[fl].erase (work_.lanes[fl].begin() + fi); if (fl == lane && idx > fi) --idx;
        work_.lanes[lane].insert (work_.lanes[lane].begin() + std::clamp (idx, 0, (int) work_.lanes[lane].size()), d); selected_ = d.uid;
    }
    rebuild();
}

void WorkshopView::saveScene() {
    auto* song = ed.proc.live().song(); if (! song) { msg_ = "Load a song (Setlist -> Go Live) to save scenes"; repaint(); return; }
    auto* w = new juce::AlertWindow ("Save as Scene", "Scene name for \"" + juce::String (song->chart.title) + "\"", juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("name", ed.proc.live().currentSceneName().empty() ? "New Scene" : juce::String (ed.proc.live().currentSceneName()));
    w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey)); w->addButton ("Cancel", 0);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int r) {
        if (r != 1) return; auto name = w->getTextEditorContents ("name").toStdString(); auto* s = ed.proc.store.lib.song (ed.proc.live().song()->id); if (! s || name.empty()) return;
        Scene sc; sc.name = name;
        const Chain* chain = ed.proc.layout();
        for (auto& l : work_.lanes) for (auto& d : l) { PedalState ps; ps.enabled = ed.proc.pedalOn (d.uid);
            if (chain) if (auto* slot = const_cast<Chain*> (chain)->find (d.uid)) for (auto& sp : slot->pedal->specs()) ps.params[sp.id] = ed.proc.paramValue (d.uid, sp.id);
            if (d.uid != "boost") sc.pedals[d.uid] = ps; } // boost stays on its fixed footswitch
        bool replaced = false; for (auto& x : s->scenes) if (x.name == name) { x = sc; replaced = true; } if (! replaced) s->scenes.push_back (sc);
        ed.proc.store.save(); msg_ = "Scene \"" + juce::String (name) + "\" saved"; repaint(); }), true);
}

void WorkshopView::assignMenu() {
    auto* cur = ed.proc.live().song(); if (! cur) return; auto* s = ed.proc.store.lib.song (cur->id); if (! s) return;
    juce::PopupMenu m; std::string id = s->id;
    for (auto& sc : s->scenes) {
        juce::PopupMenu sub; auto scn = sc.name;
        for (auto& sec : ed.proc.live().chart().sections) { auto nm = sec.name; sub.addItem ("Section: " + juce::String (nm), [this, id, nm, scn] { if (auto* so = ed.proc.store.lib.song (id)) { so->sectionScene[nm] = scn; ed.proc.store.save(); } }); }
        sub.addSeparator();
        for (const char* kind : { "verse", "prechorus", "chorus", "bridge", "tag" }) { std::string k = kind; sub.addItem ("Every " + juce::String (kind), [this, id, k, scn] { if (auto* so = ed.proc.store.lib.song (id)) { so->sectionScene[k] = scn; ed.proc.store.save(); } }); }
        sub.addItem ("Song default", [this, id, scn] { if (auto* so = ed.proc.store.lib.song (id)) { so->defaultScene = scn; ed.proc.store.save(); } });
        m.addSubMenu ("Scene \"" + juce::String (scn) + "\" ->", sub);
    }
    if (! selected_.empty()) {
        auto uid = selected_; const Chain* chain = ed.proc.layout(); auto* slot = chain ? const_cast<Chain*> (chain)->find (uid) : nullptr; std::string nm = slot ? slot->pedal->displayName : uid;
        m.addSeparator();
        m.addItem ("Guitar switch C -> " + juce::String (nm) + " (this song)", [this, id, uid, nm] { if (auto* so = ed.proc.store.lib.song (id)) { so->switchC = { nm, uid }; ed.proc.store.save(); } });
        m.addItem ("Guitar switch D -> " + juce::String (nm) + " (this song)", [this, id, uid, nm] { if (auto* so = ed.proc.store.lib.song (id)) { so->switchD = { nm, uid }; ed.proc.store.save(); } });
    }
    // song options: click default, capo, section lengths (bars) for auto-advance
    { juce::PopupMenu so;
      so.addItem ("Click on when this song loads", true, s->clickDefault, [this, id] { if (auto* so2 = ed.proc.store.lib.song (id)) { so2->clickDefault = ! so2->clickDefault; ed.proc.store.save(); } });
      juce::PopupMenu cp; for (auto& o : capoOptions (s->chart.key)) { int c = o.capo; cp.addItem (c == 0 ? juce::String ("No capo") : "Capo " + juce::String (c) + " - " + juce::String (o.shapes) + " shapes", true, s->chart.capo == c, [this, id, c] { if (auto* so2 = ed.proc.store.lib.song (id)) { so2->chart.capo = c; ed.proc.store.save(); ed.proc.live().refreshChart(); } }); }
      so.addSubMenu ("Capo (key " + juce::String (s->chart.key) + ")", cp);
      juce::PopupMenu bars; int si = 0;
      for (auto& sec : s->chart.sections) { juce::PopupMenu b1; for (int nb : { 0, 1, 2, 4, 6, 8, 12, 16 }) b1.addItem (nb == 0 ? juce::String ("Manual") : juce::String (nb) + " bars", true, sec.bars == nb, [this, id, si, nb] { if (auto* so2 = ed.proc.store.lib.song (id)) if (si < (int) so2->chart.sections.size()) { so2->chart.sections[(size_t) si].bars = nb; ed.proc.store.save(); ed.proc.live().refreshChart(); } });
          bars.addSubMenu (juce::String (sec.name) + (sec.bars > 0 ? "  (" + juce::String (sec.bars) + ")" : juce::String()), b1); ++si; }
      so.addSubMenu ("Section lengths (auto-advance)", bars);
      juce::PopupMenu ci; const std::pair<int, const char*> opts[] = { { -1, "Use global setting" }, { 0, "Off" }, { 4, "4 clicks" }, { 8, "8 clicks" }, { 101, "1 bar" }, { 102, "2 bars" } };
      for (auto& o : opts) { int code = o.first; ci.addItem (o.second, true, s->countIn == code, [this, id, code] { if (auto* so2 = ed.proc.store.lib.song (id)) { so2->countIn = code; ed.proc.store.save(); } }); }
      so.addSubMenu ("Count-in for this song", ci);
      m.addSeparator(); m.addSubMenu ("Song options", so); }
    m.addSeparator(); m.addItem ("Song uses rig \"" + juce::String (work_.name) + "\"", [this, id] { if (auto* so = ed.proc.store.lib.song (id)) { so->rigPreset = work_.name; ed.proc.store.save(); } });
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&assign_));
}
