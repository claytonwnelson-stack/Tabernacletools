#include "WorkshopView.h"
#include "dsp/AmpSection.h"
#include "dsp/PedalFaces.h"
using namespace wr; using namespace wr::theme;
void showBackupsMenu (CoreEditor& ed, juce::Component* anchor, std::function<void()> after);

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
    onOff_.setClickingTogglesState (false);
    onOff_.onClick = [this] { if (selected_.empty()) return; bool on = ! ed.proc.pedalOn (selected_); ed.proc.setPedal (selected_, on); for (auto& l : work_.lanes) for (auto& d : l) if (d.uid == selected_) d.enabled = on; buildEditor(); repaint(); };
    remove_.onClick = [this] {
        juce::Component::unfocusAllComponents(); // deleting the pedal's knobs must not move focus onto the search field
        for (auto& l : work_.lanes) l.erase (std::remove_if (l.begin(), l.end(), [this] (const PedalDef& d) { return d.uid == selected_; }), l.end()); selected_.clear(); rebuild(); };
    reset_.onClick = [this] { for (auto& l : work_.lanes) for (auto& d : l) if (d.uid == selected_) d.params.clear(); rebuild(); };
    saveScene_.setColour (juce::TextButton::buttonColourId, orange); saveScene_.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff111111));
    saveScene_.onClick = [this] { saveScene(); };
    assign_.onClick = [this] { assignMenu(); };
    saveRig_.onClick = [this] { auto& rigs = ed.proc.store.lib.rigs; bool found = false; for (auto& r : rigs) if (r.name == work_.name) { r = work_; found = true; } if (! found) rigs.push_back (work_); ed.proc.store.save(); msg_ = "Rig \"" + juce::String (work_.name) + "\" saved"; repaint(); };
    for (auto* s : { &trimPre_, &trimPost_ }) { addAndMakeVisible (*s); s->setSliderStyle (juce::Slider::LinearHorizontal); s->setRange (-30, 6, 0.5); s->setTextValueSuffix (" dB"); s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20); }
    for (auto* l : { &trimPreL_, &trimPostL_ }) { addAndMakeVisible (*l); l->setFont (font (11, true)); l->setColour (juce::Label::textColourId, dim); }
    addAndMakeVisible (tempoL_); tempoL_.setFont (font (11, true)); tempoL_.setColour (juce::Label::textColourId, dim); tempoL_.setText ("SONG TEMPO (BPM)", juce::dontSendNotification);
    addAndMakeVisible (tempo_); tempo_.setFont (font (16, true)); tempo_.setJustification (juce::Justification::centred); tempo_.setInputRestrictions (5, "0123456789.");
    tempo_.onReturnKey = [this] { setSongTempo (tempo_.getText().getDoubleValue()); }; tempo_.onFocusLost = [this] { setSongTempo (tempo_.getText().getDoubleValue()); };
    for (auto* b : { &tempoMinus_, &tempoPlus_ }) addAndMakeVisible (*b);
    tempoMinus_.setRepeatSpeed (400, 80); tempoPlus_.setRepeatSpeed (400, 80); // hold to step
    tempoMinus_.onClick = [this] { setSongTempo (tempo_.getText().getDoubleValue() - 1); }; tempoPlus_.onClick = [this] { setSongTempo (tempo_.getText().getDoubleValue() + 1); };
    trimPreL_.setText ("OUT 1 TRIM (amp input)", juce::dontSendNotification); trimPostL_.setText ("OUTS 3/4 TRIM (loop return)", juce::dontSendNotification);
    trimPre_.onValueChange = [this] { ed.proc.store.lib.settings.trimPreDb = (float) trimPre_.getValue(); ed.proc.rig.post (cmdTrim (Lane::Pre, (float) trimPre_.getValue())); ed.proc.store.saveSoon(); };
    trimPost_.onValueChange = [this] { ed.proc.store.lib.settings.trimPostDb = (float) trimPost_.getValue(); ed.proc.rig.post (cmdTrim (Lane::Post, (float) trimPost_.getValue())); ed.proc.store.saveSoon(); };
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
    knobs_.clear(); knobLabels_.clear(); choices_.clear();
    const Chain* chain = ed.proc.layout(); const PedalSlot* slot = chain ? const_cast<Chain*> (chain)->find (selected_) : nullptr;
    onOff_.setVisible (slot != nullptr); remove_.setVisible (slot != nullptr); reset_.setVisible (slot != nullptr);
    if (! slot) { resized(); return; }
    bool on = ed.proc.pedalOn (selected_); onOff_.setButtonText (on ? "ON" : "BYPASSED"); onOff_.setToggleState (on, juce::dontSendNotification);
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
        auto* lab = knobLabels_.add (new juce::Label ({}, sp.name)); lab->setJustificationType (juce::Justification::centred); lab->setFont (font (11, true)); lab->setColour (juce::Label::textColourId, dim); addAndMakeVisible (lab);
        if (sp.isChoice()) {
            auto* cb = choices_.add (new juce::ComboBox()); for (int k = 0; k < (int) sp.choices.size(); ++k) cb->addItem (sp.choices[(size_t) k], k + 1);
            cb->setSelectedId ((int) std::lround (v) + 1, juce::dontSendNotification); addAndMakeVisible (cb);
            cb->onChange = [this, uid, sp, cb] { ed.proc.setParam (uid, sp.id, (float) (cb->getSelectedId() - 1)); for (auto& l : work_.lanes) for (auto& d : l) if (d.uid == uid) d.params[sp.id] = (float) (cb->getSelectedId() - 1); };
            knobs_.add (nullptr);
        } else {
            auto* k = knobs_.add (new juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow));
            k->setRange (sp.min, sp.max, (sp.max - sp.min) > 50 ? 1.0 : 0.0); k->setValue (v, juce::dontSendNotification); k->setTextValueSuffix (sp.unit.empty() ? "" : " " + sp.unit);
            k->setNumDecimalPlacesToDisplay ((sp.max - sp.min) > 50 ? 0 : 2); k->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 18); k->setDoubleClickReturnValue (true, sp.def);
            k->onValueChange = [this, uid, sp, k] { ed.proc.setParam (uid, sp.id, (float) k->getValue()); for (auto& l : work_.lanes) for (auto& d : l) if (d.uid == uid) d.params[sp.id] = (float) k->getValue(); };
            addAndMakeVisible (k);
        }
    }
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
    { auto tempoZone = libArea_.removeFromBottom (56);
      tempoL_.setBounds (tempoZone.removeFromTop (16));
      tempoMinus_.setBounds (tempoZone.removeFromLeft (44)); tempoPlus_.setBounds (tempoZone.removeFromRight (44)); tempo_.setBounds (tempoZone.reduced (6, 0)); }
    search_.setBounds (libArea_.getX(), libArea_.getY() + 24, libArea_.getWidth(), 38);
    auto lanes = r.withTrimmedTop (24); int lh = (lanes.getHeight() - 60) / 2;
    laneArea_[0] = lanes.removeFromTop (lh); ampBar_ = lanes.removeFromTop (60).reduced (0, 14); laneArea_[1] = lanes;
    auto e = edArea_.reduced (14, 12); e.removeFromTop (54);
    auto bt = e.removeFromTop (34);
    if (selected_ == kAmpUid) { realAmp_.setBounds (bt.removeFromLeft (bt.getWidth() - 88)); bt.removeFromLeft (8); reset_.setBounds (bt); }
    else { onOff_.setBounds (bt.removeFromLeft (110)); bt.removeFromLeft (8); reset_.setBounds (bt.removeFromLeft (80)); bt.removeFromLeft (8); remove_.setBounds (bt); }
    e.removeFromTop (10); faceArea_ = {};
    if (selected_ == kAmpUid) { auto row = e.removeFromBottom (230).removeFromTop (30); if (removeIr_.isVisible()) { removeIr_.setBounds (row.removeFromRight (120)); row.removeFromRight (8); } loadIr_.setBounds (row); }   // under the grid, above trims
    else if (pedalFace (ed.proc.pedalType (selected_))) { faceArea_ = e.removeFromTop (40); e.removeFromTop (6); }
    const int cols = 3, kw = e.getWidth() / cols, kh = selected_ == kAmpUid ? 84 : 96; int n = std::max (knobs_.size(), knobLabels_.size()), ci = 0;
    int col = 0, y = e.getY();
    for (int i = 0; i < n; ++i) {
        for (auto& gr : groups_) if (gr.first == i) { if (col != 0) { y += kh; col = 0; } y += 18; }
        juce::Rectangle<int> cell (e.getX() + col * kw, y, kw, kh); if (++col == cols) { col = 0; y += kh; }
        if (i < knobLabels_.size()) knobLabels_[i]->setBounds (cell.removeFromTop (16));
        if (knobs_[i] != nullptr) knobs_[i]->setBounds (cell.reduced (4, 0)); else if (ci < choices_.size()) choices_[ci++]->setBounds (cell.withSizeKeepingCentre (kw - 10, 30));
    }
    auto bottom = edArea_.reduced (14, 12); auto b2 = bottom.removeFromBottom (40); saveScene_.setBounds (b2.removeFromLeft (b2.getWidth() / 2 - 4)); b2.removeFromLeft (8); assign_.setBounds (b2);
    bottom.removeFromBottom (8); { auto rr = bottom.removeFromBottom (34); backups_.setBounds (rr.removeFromRight (110)); rr.removeFromRight (8); saveRig_.setBounds (rr); }
    bottom.removeFromBottom (10); trimPost_.setBounds (bottom.removeFromBottom (26)); trimPostL_.setBounds (bottom.removeFromBottom (16)); bottom.removeFromBottom (4);
    trimPre_.setBounds (bottom.removeFromBottom (26)); trimPreL_.setBounds (bottom.removeFromBottom (16));
}

void WorkshopView::paintTopBar (juce::Graphics& g, juce::Rectangle<int> r) {
    auto& L = ed.proc.live(); g.setColour (dim); g.setFont (font (13)); g.drawText ("Rig workshop - editing rig \"" + juce::String (work_.name) + "\"", r.getX(), 12, r.getWidth(), 16, juce::Justification::left);
    g.setColour (juce::Colours::white); g.setFont (font (19, true)); g.drawText (L.song() ? juce::String (L.chart().title) + "  -  " + juce::String (L.currentSceneName().empty() ? "no scene" : L.currentSceneName()) : juce::String ("No song loaded"), r.getX(), 28, r.getWidth(), 24, juce::Justification::left);
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
    g.fillAll (bg); libItems_.clear(); tiles_.clear();
    // ---- library grouped by category
    label (g, "PEDAL LIBRARY", libArea_.withHeight (18)); label (g, "drag ->", libArea_.withHeight (18), dimmer, 11, juce::Justification::centredRight);
    auto q = search_.getText().toLowerCase(); int y = libArea_.getY() + 72;
    for (int cat = 0; cat <= (int) PedalCategory::Utility; ++cat) {
        std::vector<const PedalInfo*> items; for (auto& pi : pedalCatalog()) if ((int) pi.category == cat && (q.isEmpty() || juce::String (pi.name).toLowerCase().contains (q))) items.push_back (&pi);
        if (items.empty()) continue;
        g.setColour (dimmer); g.setFont (font (10, true)); g.drawText (juce::String (categoryName ((PedalCategory) cat)).toUpperCase(), libArea_.getX(), y, 200, 14, juce::Justification::left); y += 16;
        int x = libArea_.getX();
        for (auto* pi : items) {
            int w = (int) juce::GlyphArrangement::getStringWidth (font (12, true), pi->name) + 20; if (x + w > libArea_.getRight()) { x = libArea_.getX(); y += 30; }
            juce::Rectangle<int> b (x, y, w, 26); pill (g, b.toFloat(), card, pi->implemented ? cardBorder : juce::Colour (0xff3a3a3a), 8);
            g.setColour (pi->implemented ? text : dimmer); g.setFont (font (12, true)); g.drawText (pi->name, b, juce::Justification::centred); libItems_.push_back ({ b, pi->type }); x += w + 6;
        }
        y += 34;
        if (y > libArea_.getBottom() - 20) break;
    }
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
            const PedalInfo* info = nullptr; for (auto& pi : pedalCatalog()) if (pi.type == d.type) { info = &pi; break; }
            juce::String name = info ? juce::String (info->name) : juce::String (d.type);
            pill (g, full.toFloat(), juce::Colour (0xff141820), sel ? orange : hue.withAlpha (0.85f), 10);
            g.setColour (hue); g.fillRoundedRectangle ((float) full.getX() + 1, (float) full.getY() + 8, 5.0f, (float) full.getHeight() - 16, 2.0f);
            auto body = full.reduced (10, 4);
            auto tag = body.removeFromTop (16).removeFromLeft (34);
            g.setColour (hue.withAlpha (0.28f)); g.fillRoundedRectangle (tag.toFloat(), 4);
            g.setColour (hue.brighter (0.45f)); g.setFont (font (10, true)); g.drawText (look.tag, tag, juce::Justification::centred);
            led (g, { (float) full.getRight() - 12, (float) full.getY() + 12 }, on);
            g.setColour (on ? text : dim); g.setFont (font (13, true)); g.drawFittedText (name, body.reduced (2, 1), juce::Justification::centredLeft, 2);
            tiles_.push_back ({ full, l, i });
        }
        g.restoreState();
    }
    pill (g, ampBar_.toFloat(), juce::Colour (0xff1a1810), orange.withAlpha (0.5f), 10); g.setColour (orange); g.setFont (font (12, true));
    g.drawText ("v  AMP: " + juce::String (ampSummary (work_)) + "   (tap to edit)  v", ampBar_, juce::Justification::centred, true);
    g.setColour (dimmer); g.setFont (font (10)); g.drawText ("tap = select - double-tap = bypass - drag tiles between lanes - " + msg_, laneArea_[1].translated (0, laneArea_[1].getHeight() + 2).withHeight (14), juce::Justification::left);
    // ---- editor panel
    pill (g, edArea_.toFloat(), panel, cardBorder, 12);
    auto e = edArea_.reduced (14, 12);
    const Chain* chain = ed.proc.layout(); const PedalSlot* slot = chain ? const_cast<Chain*> (chain)->find (selected_) : nullptr;
    if (slot) { Lane ln; int idx; const_cast<Chain*> (chain)->find (selected_, &ln, &idx);
        label (g, "SELECTED - " + juce::String (categoryName (slot->pedal->category)).toUpperCase(), e.removeFromTop (16));
        g.setColour (text); g.setFont (font (18, true)); g.drawText (juce::String (slot->pedal->displayName) + "  -  " + (ln == Lane::Pre ? "before amp" : "after amp") + " - slot " + juce::String (idx + 1), e.removeFromTop (30), juce::Justification::centredLeft, true);
        if (auto* face = pedalFace (slot->pedal->type)) if (! faceArea_.isEmpty()) {   // v0.10 pedal face strip
            auto fa = faceArea_; pill (g, fa.toFloat(), juce::Colour (face->argb).withAlpha (0.35f), juce::Colour (face->argb), 8);
            g.setColour (text); g.setFont (font (11)); g.drawFittedText (juce::String (face->blurb) + "  [" + juce::String (face->footswitch) + "]", fa.reduced (8, 2), juce::Justification::centredLeft, 2); }
        for (auto& gr : groups_) if (gr.first < knobLabels_.size()) { auto lb = knobLabels_[gr.first]->getBounds(); label (g, gr.second, juce::Rectangle<int> (edArea_.getX() + 14, lb.getY() - 18, 200, 16), orange, 11); }
        if (selected_ == kAmpUid) { label (g, usingRealAmp (work_) ? "Sim bypassed: the lanes run into your real amp as before." : "Direct to PA = no amp needed. FX Return = preamp into a real power amp. Mic/position shape the built-in cabs (IRs have their own mic).",
            loadIr_.getBounds().translated (0, 34).withHeight (32).withX (edArea_.getX() + 14).withWidth (edArea_.getWidth() - 28), dim, 10); }
        if (! slot->pedal->implemented) { g.setColour (orange); g.setFont (font (11, true)); g.drawText ("v0.1 stub: passes audio through. TODO v0.2.", e.removeFromTop (16), juce::Justification::left); }
    } else label (g, "Select a pedal to edit it", e.removeFromTop (16));
}

void WorkshopView::mouseDown (const juce::MouseEvent& e) {
    scrollLane_ = -1;
    for (int l = 0; l < 2 && scrollLane_ < 0; ++l) if (laneArea_[l].contains (e.getPosition()) && laneMaxScroll_[l] > 0) {
        bool onTile = false; for (auto& t : tiles_) if (std::get<1> (t) == l && std::get<0> (t).contains (e.getPosition())) onTile = true;
        if (! onTile && ! laneLeft_[l].contains (e.getPosition()) && ! laneRight_[l].contains (e.getPosition())) { scrollLane_ = l; scrollGrab_ = laneScroll_[l]; }
    }
}
void WorkshopView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) {
    for (int l = 0; l < 2; ++l) if (laneArea_[l].contains (e.getPosition()) && laneMaxScroll_[l] > 0) {
        laneScroll_[l] = std::clamp (laneScroll_[l] - (int) ((w.deltaX + w.deltaY) * 140), 0, laneMaxScroll_[l]); repaint(); }
}
void WorkshopView::mouseDrag (const juce::MouseEvent& e) {
    if (scrollLane_ >= 0 && ! isDragAndDropActive()) {
        laneScroll_[scrollLane_] = std::clamp (scrollGrab_ - e.getDistanceFromDragStartX(), 0, laneMaxScroll_[scrollLane_]); repaint(); return; }
    if (e.getDistanceFromDragStart() < 8 || isDragAndDropActive()) return;
    auto p = e.getMouseDownPosition();
    for (auto& it : libItems_) if (it.first.contains (p)) { juce::Image img (juce::Image::ARGB, it.first.getWidth(), it.first.getHeight(), true); { juce::Graphics g (img); pill (g, img.getBounds().toFloat(), orange, orange, 8); }
        startDragging ("new:" + juce::String (it.second), this, juce::ScaledImage (img)); return; }
    for (auto& t : tiles_) if (laneArea_[std::get<1> (t)].contains (p) && std::get<0> (t).contains (p)) { juce::Image img (juce::Image::ARGB, std::get<0> (t).getWidth(), std::get<0> (t).getHeight(), true); { juce::Graphics g (img); pill (g, img.getBounds().toFloat(), pink.withAlpha (0.6f), pink, 10); }
        startDragging ("move:" + juce::String (std::get<1> (t)) + ":" + juce::String (std::get<2> (t)), this, juce::ScaledImage (img)); return; }
}
void WorkshopView::mouseUp (const juce::MouseEvent& e) {
    if (scrollLane_ >= 0 && e.getDistanceFromDragStart() > 8) { scrollLane_ = -1; return; }
    scrollLane_ = -1;
    for (int l = 0; l < 2; ++l) {
        if (laneLeft_[l].contains (e.getPosition())) { laneScroll_[l] = std::max (0, laneScroll_[l] - 120); repaint(); return; }
        if (laneRight_[l].contains (e.getPosition())) { laneScroll_[l] = std::min (laneMaxScroll_[l], laneScroll_[l] + 120); repaint(); return; }
    }
    if (e.getDistanceFromDragStart() > 8) return;
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
        work_.lanes[lane].insert (work_.lanes[lane].begin() + std::min (idx, (int) work_.lanes[lane].size()), PedalDef { uid, type, true, {} }); selected_ = uid;
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
