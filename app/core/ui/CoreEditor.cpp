#include "model/Legal.h"
#include "Tracks.h"
#include "CoreEditor.h"
#include "BandPanel.h"
#include "ProjectorView.h"
#include "LiveExtras.h"
#include "ImportPreviewView.h"
#include "HymnBrowserView.h"
#include "TeamPanel.h"
#include "PaywallView.h"
#if JucePlugin_Build_Standalone
 #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif
using namespace wr;

CoreEditor::CoreEditor (CoreProcessor& p) : juce::AudioProcessorEditor (p), core (p) {
    setLookAndFeel (&lnf_);
    addAndMakeVisible (settingsBtn_); settingsBtn_.onClick = [this] { openSettings (&settingsBtn_); };
    band_ = std::make_unique<BandPanel> (core); addChildComponent (*band_);
    pbar_ = std::make_unique<PracticeBar> (*this); addChildComponent (*pbar_);
    tbar_ = std::make_unique<TracksBar> (*this); addChildComponent (*tbar_);
    arr_ = std::make_unique<ArrangementEditor>(); addChildComponent (*arr_);
    songEd_ = std::make_unique<SongEditor>(); addChildComponent (*songEd_);
    arr_->defaults = &core.store.lib.settings.sectionColors;   // v0.8 section colours edited in the arrangement editor
    arr_->onColours = [this] (const std::string& id, std::vector<std::string> cc) {
        if (auto* so = core.store.lib.song (id)) { for (size_t i = 0; i < so->chart.sections.size() && i < cc.size(); ++i) so->chart.sections[i].customColor = cc[i];
            wr::applySectionColors (so->chart, {}); core.live().refreshChart(); core.store.save(); repaint(); } };
    imp_ = std::make_unique<ImportPreviewView>(); addChildComponent (*imp_);
    imp_->onEdit = [this] (wr::Chart c) { songEd_->open (this, {}, std::move (c), [this] (wr::Chart edited) { imp_->setChart (std::move (edited)); imp_->setVisible (true); imp_->toFront (true); }); };
    hymns_ = std::make_unique<HymnBrowserView>(); addChildComponent (*hymns_);
}
bool CoreEditor::requireAccess (wr::Feature f) {
    if (core.allows (f)) return true;
    openPaywall (wr::featureLockedMessage (f)); return false;
}
void CoreEditor::openPaywall (const juce::String& reason) {
    if (! paywall_) { paywall_ = std::make_unique<PaywallView> (core); addChildComponent (*paywall_); paywall_->onJoinTeam = [this] { openTeam(); }; }
    paywall_->setBounds (getLocalBounds()); paywall_->open (reason);
}
void CoreEditor::openTeam() {
    auto* s = core.seats(); if (! s) return;
    if (! team_) { team_ = std::make_unique<TeamPanel> (*s); addChildComponent (*team_); }
    team_->setBounds (getLocalBounds()); team_->open(); s->refreshEntitlement();
}
void CoreEditor::openHymnal (std::function<void (juce::String)> onAdded) {
    hymns_->setBounds (getLocalBounds());
    hymns_->open ([this, onAdded] (const std::string& hymnId) {
        auto id = wr::copyHymnToLibrary (core.store.lib, hymnId); if (id.empty()) return;
        if (auto* so = core.store.lib.song (id)) wr::applySectionColors (so->chart, core.store.lib.settings.sectionColors);
        core.store.save(); toast_ = "Hymn copied to your library - edit it freely"; toastSev_ = 0; toastUntil_ = core.nowSec() + 3.0; repaint();
        if (onAdded) onAdded (juce::String (id)); });
}
void CoreEditor::openImportPreview (const juce::String& text, const juce::String& fallbackTitle, std::function<void (juce::String)> onAdded) {
    if (! requireAccess (wr::Feature::Import)) return;   // v0.12
    imp_->setBounds (getLocalBounds()); if (hymns_) hymns_->setBounds (getLocalBounds());
    imp_->open (text, fallbackTitle, [this, onAdded] (wr::Chart c) {
        auto id = core.store.addSong (std::move (c)); toast_ = "Added to the song library"; toastSev_ = 0; toastUntil_ = core.nowSec() + 3.0; repaint();
        if (onAdded) onAdded (id); });
}
int CoreEditor::practiceBarHeight() const { return (core.practiceMode() ? 56 : 0) + (lastTracks_ ? 56 : 0); }
void CoreEditor::openSongEditor (const std::string& songId, std::function<void()> afterSave) {
    if (! requireAccess (wr::Feature::Edit)) return;
    auto* s = core.store.lib.song (songId); if (! s || ! songEd_) return;
    songEd_->open (this, songId, s->chart, [this, afterSave] (wr::Chart) { if (afterSave) afterSave(); repaint(); });
}
void CoreEditor::openLiveArrangement() {
    if (! requireAccess (wr::Feature::Edit)) return;
    auto& L = core.live(); auto* s = L.song(); if (! s) return;
    const auto& ent = L.songIndex() >= 0 && L.songIndex() < (int) L.setlist().entries.size() ? L.setlist().entries[(size_t) L.songIndex()] : SetlistEntry();
    auto bars = ent.arrangement.empty() ? s->chart.arrangementBars : ent.arrangementBars;
    arr_->open (s->chart, L.arrangement(), true, [this] (std::vector<std::string> t, int scope, std::vector<int> passBars) {
        core.live().setArrangement (t, scope == 1, passBars); core.store.save(); if (core.practiceMode()) core.loadPracticeTrack(); }, s->id, bars);
}
void CoreEditor::chooseStems() {
    if (! requireAccess (wr::Feature::Import)) return;
    chooser_ = std::make_unique<juce::FileChooser> ("Backing-track stems for this song (files or a folder)", juce::File(), "*.wav;*.aif;*.aiff;*.m4a;*.mp3;*.caf;*.aac", true, false, this);
    chooser_->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectDirectories
                           | juce::FileBrowserComponent::canSelectMultipleItems, [this] (const juce::FileChooser& fc) {
        auto files = fc.getResults(); if (files.isEmpty()) return; auto err = core.importStems (files);
        if (err.isNotEmpty()) juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Backing tracks", err); });
}
void CoreEditor::choosePracticeTrack() {
    chooser_ = std::make_unique<juce::FileChooser> ("Practice track for this song", juce::File(), "*.mp3;*.m4a;*.aac;*.wav;*.aif;*.aiff;*.caf", true, false, this);
    chooser_->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc) {
        auto f = fc.getResult(); if (! f.existsAsFile()) return; auto err = core.attachPracticeTrack (f);
        if (err.isNotEmpty()) juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Practice track", err);
        else if (! core.practiceMode()) core.setPracticeMode (true); });
}
// Destructor is defined after SettingsPanel so the unique_ptr can see the complete type.

void CoreEditor::coreLayout() {
    auto top = getLocalBounds().removeFromTop (64);
    settingsBtn_.setBounds (top.removeFromRight (164).reduced (10, 16));
    band_->setBounds (getLocalBounds()); arr_->setBounds (getLocalBounds()); if (songEd_) songEd_->setBounds (getLocalBounds()); imp_->setBounds (getLocalBounds()); if (team_) team_->setBounds (getLocalBounds()); if (paywall_) paywall_->setBounds (getLocalBounds());
    layoutSettingsOverlay();
    pbar_->setBounds (getLocalBounds().removeFromBottom (56));
    tbar_->setBounds (getLocalBounds().withTrimmedBottom (core.practiceMode() ? 56 : 0).removeFromBottom (56));
}
void CoreEditor::askText (const juce::String& title, const juce::String& msg, const std::string& initial, std::function<void (juce::String)> done) {
    auto* w = new juce::AlertWindow (title, msg, juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("t", juce::String (initial)); w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey)); w->addButton ("Cancel", 0);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([w, done] (int r) { if (r == 1) done (w->getTextEditorContents ("t")); }), true);
}
void CoreEditor::chooseImage (bool logo) {
    chooser_ = std::make_unique<juce::FileChooser> (logo ? "Logo image" : "Background image", juce::File(), "*.jpg;*.jpeg;*.png;*.heic", true, false, this);
    chooser_->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, logo] (const juce::FileChooser& fc) {
        auto f = fc.getResult(); if (! f.existsAsFile()) return;
        auto dst = core.store.file().getParentDirectory().getChildFile (logo ? "projector-logo" + f.getFileExtension() : "projector-bg" + f.getFileExtension()); // copy in: iOS picks are temporary
        f.copyFileTo (dst); auto& p = core.store.lib.settings.projector; (logo ? p.logo : p.background) = dst.getFullPathName().toStdString(); core.store.save(); });
}
void CoreEditor::showBandPanel() { band_->setVisible (true); band_->toFront (false); band_->refresh(); }
void CoreEditor::paintOverChildren (juce::Graphics& g) {
    using namespace theme;
    { const auto w = wr::sync::healthWarning (core.health());   // v0.7: this device low / hot
      if (! w.empty()) { auto r = juce::Rectangle<int> (0, 0, 360, 26).withCentre ({ getWidth() / 2, 78 }); pill (g, r.toFloat(), panel, clip, 8);
          g.setColour (juce::Colours::white); g.setFont (font (13, true)); g.drawText ("THIS IPAD: " + juce::String (w), r, juce::Justification::centred); } }
    if (toast_.isEmpty() || core.nowSec() > toastUntil_) return;
    auto r = juce::Rectangle<int> (0, 0, 420, 40).withCentre ({ getWidth() / 2, 96 });
    pill (g, r.toFloat(), panel, toastSev_ ? clip : accentText, 12);
    g.setColour (juce::Colours::white); g.setFont (font (15, true)); g.drawText (toast_, r, juce::Justification::centred);
}
void CoreEditor::coreTick() {
    if (core.practiceMode() != lastPractice_) { lastPractice_ = core.practiceMode(); pbar_->setVisible (lastPractice_); if (lastPractice_) pbar_->toFront (false); resized(); }
    { const bool t = core.tracks.loaded() || (core.live().following() && core.sync().leaderTracks().loaded) || wantTracksBar;   // v0.14
      if (t != lastTracks_) { lastTracks_ = t; tbar_->setVisible (t); if (t) tbar_->toFront (false); resized(); } }
    { static int k = 0; if (++k % 30 == 0) repaint (0, 64, getWidth(), 40); }   // local battery / thermal warning
    if (shareTick_ % 60 == 30 && pendingStems_.isEmpty()) { pendingStems_ = wr::platform::takeSharedStems();   // v0.16 shared stems
        if (! pendingStems_.isEmpty()) { auto* s = core.live().song(); const juce::String title = s ? juce::String (s->chart.title) : juce::String ("(no song)");
            juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Backing tracks", "Add " + juce::String (pendingStems_.size()) + " shared audio file(s) as stems for \"" + title + "\"?", "Add", "Not now", this,
                juce::ModalCallbackFunction::create ([this] (int r) { auto files = pendingStems_; pendingStems_.clear();
                    if (r == 1 && requireAccess (wr::Feature::Import)) { wantTracksBar = true; auto e = core.importStems (files); if (e.isNotEmpty()) juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Backing tracks", e); }
                    for (auto& f : files) f.deleteFile(); })); } }
    if (++shareTick_ % 60 == 0) { auto in = wr::platform::takeSharedCharts(); sharedQueue_.insert (sharedQueue_.end(), in.begin(), in.end()); }   // ~2 s at 30 Hz
    if (! sharedQueue_.empty() && ! imp_->isVisible() && core.allows (wr::Feature::Import)) {   // shared charts wait while read-only
        auto c = sharedQueue_.front(); sharedQueue_.erase (sharedQueue_.begin()); openImportPreview (c.text, c.name, onSharedSongAdded); }
    for (auto& t : core.sync().takeToasts()) { toast_ = juce::String (t.text); toastSev_ = t.severity; toastUntil_ = core.nowSec() + 3.0; repaint(); }
    if (toast_.isNotEmpty() && core.nowSec() > toastUntil_) { toast_.clear(); repaint(); }
    if (band_->isVisible() && ++bandTick_ % 15 == 0) band_->refresh();
    if (! noteShown_ && core.store.recoveryNote.isNotEmpty()) { noteShown_ = true; juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Recovered", core.store.recoveryNote); core.store.recoveryNote.clear(); }
   #if JucePlugin_Build_Standalone
    if (core.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
        if (auto* h = juce::StandalonePluginHolder::getInstance())
            if (auto* dev = h->deviceManager.getCurrentAudioDevice()) core.setPhysicalOutputs (dev->getActiveOutputChannels().countNumberOfSetBits());
   #endif
}

class CoreEditor::SettingsPanel : public juce::Component {
public:
    explicit SettingsPanel (CoreEditor& e) : ed (e) { setOpaque (true); }
    void showHome() { nav.clear(); nav.push_back (make (KHome)); scroll = 0; setVisible (true); toFront (false); }
    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override { drag = false; grab = scroll; juce::ignoreUnused (e); }
    void mouseDrag (const juce::MouseEvent& e) override {
        if (e.getDistanceFromDragStart() > 8) { drag = true; scroll = std::clamp (grab - e.getDistanceFromDragStartY(), 0, maxScroll); repaint(); }
    }
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override {
        scroll = std::clamp (scroll - (int) ((w.deltaY + w.deltaX) * 90), 0, maxScroll); repaint();
    }
private:
    enum Kind { KHome, KAudio, KMidi, KClick, KBand, KDisplay, KAbout, KMap, KMenu };
    struct Row { juce::String title, sub; bool tick = false; bool on = true; std::function<void()> go; };
    struct Level { Kind kind = KHome; juce::String title, blurb; std::vector<Row> rows; bool closeOnPick = true; };
    CoreEditor& ed;
    std::vector<Level> nav;
    int scroll = 0, grab = 0, maxScroll = 0; bool drag = false;
    std::vector<juce::Rectangle<int>> hits;
    juce::Rectangle<int> closeR, backR, listR;
    bool mapGuitar = true;

    void dismiss() { setVisible (false); }
    void pop() { if (nav.size() > 1) nav.pop_back(); else dismiss(); scroll = 0; repaint(); }
    Level& cur() { return nav.back(); }
    Level make (Kind k);
    void addMenu (Level& L, const juce::PopupMenu& menu);
    static juce::String actName (const std::string& a);
    int unit (bool guitar) const;
    std::string liveAct (const wr::ControllerProfile& c, int sw, bool lng, bool combo) const;
    void writeAct (bool guitar, int sw, bool lng, bool combo, const std::string& name);
    void chooseAct (const juce::String& title, int sw, bool lng, bool combo);
};

juce::String CoreEditor::SettingsPanel::actName (const std::string& a) {
    static const std::pair<const char*, const char*> n[] = {
        { "", "(nothing)" }, { "tap", "Tap tempo" }, { "tunerToggle", "Tuner" }, { "boost", "Boost" },
        { "switchC", "This song's switch C" }, { "switchD", "This song's switch D" }, { "clickMute", "Mute the click" },
        { "clickToggle", "Click on / off" }, { "playStart", "Start" }, { "stop", "Stop" }, { "play", "Play / stop" },
        { "nextSection", "Next section" }, { "prevSection", "Previous section" }, { "jumpChorus", "Jump to chorus" },
        { "nextSong", "Next song" }, { "prevSong", "Previous song" }, { "toggleView", "Full song / one section" },
        { "tracksPlay", "Tracks: play / stop" }, { "tracksNext", "Tracks: next section" }, { "tracksLoop", "Loop / vamp this section" },
        { "tracksStop", "Tracks: stop" }, { "bankNext", "Next bank" }, { "bankPrev", "Previous bank" } };
    for (auto& p : n) if (a == p.first) return p.second;
    return a.empty() ? juce::String ("(nothing)") : juce::String (a);
}
int CoreEditor::SettingsPanel::unit (bool guitar) const {
    auto& cs = ed.core.store.lib.controllers; const char* role = guitar ? "guitar" : "section";
    for (int i = 0; i < (int) cs.size(); ++i) if (cs[(size_t) i].role == role) return i;
    return -1;
}
std::string CoreEditor::SettingsPanel::liveAct (const wr::ControllerProfile& c, int sw, bool lng, bool combo) const {
    if (combo) return sw == 0 ? c.comboE : c.comboF;
    if (! c.banks.empty()) { auto& b = c.banks[(size_t) std::clamp (c.bank, 0, (int) c.banks.size() - 1)]; return lng ? b.longAction[sw] : b.action[sw]; }
    return lng ? c.sw[sw].longAction : c.sw[sw].action;
}
void CoreEditor::SettingsPanel::writeAct (bool guitar, int sw, bool lng, bool combo, const std::string& name) {
    int u = unit (guitar); if (u < 0) return;
    auto& c = ed.core.store.lib.controllers[(size_t) u];
    if (combo) { if (sw == 0) c.comboE = name; else c.comboF = name; }
    else {
        if (! c.banks.empty()) { auto& b = c.banks[(size_t) std::clamp (c.bank, 0, (int) c.banks.size() - 1)]; (lng ? b.longAction[sw] : b.action[sw]) = name; }
        (lng ? c.sw[sw].longAction : c.sw[sw].action) = name;
    }
    ed.core.pushControllerProfiles(); ed.core.store.save();
}
void CoreEditor::SettingsPanel::chooseAct (const juce::String& title, int sw, bool lng, bool combo) {
    static const char* kAct[] = { "", "tap", "tunerToggle", "boost", "switchC", "switchD", "clickMute", "clickToggle", "playStart", "stop", "play",
        "nextSection", "prevSection", "jumpChorus", "nextSong", "prevSong", "toggleView", "tracksPlay", "tracksNext", "tracksLoop", "tracksStop", "bankNext", "bankPrev" };
    int u = unit (mapGuitar); if (u < 0) return;
    auto cur = liveAct (ed.core.store.lib.controllers[(size_t) u], sw, lng, combo);
    Level L; L.kind = KMenu; L.title = title; L.closeOnPick = false;
    L.blurb = "Choose what this switch does. C and D still use the target saved on the current song.";
    for (auto* a : kAct) { std::string name = a;
        L.rows.push_back ({ actName (name), {}, name == cur, true, [this, sw, lng, combo, name] {
            writeAct (mapGuitar, sw, lng, combo, name);
            if (nav.size() > 1) nav.pop_back();
            if (! nav.empty() && nav.back().kind == KMap) nav.back() = make (KMap);
            scroll = 0; repaint();
        } }); }
    nav.push_back (std::move (L)); scroll = 0; repaint();
}
void CoreEditor::SettingsPanel::addMenu (Level& L, const juce::PopupMenu& menu) {
    for (juce::PopupMenu::MenuItemIterator it (menu); it.next();) {
        auto item = it.getItem();
        if (item.isSeparator || item.text.isEmpty()) continue;
        if (item.isSectionHeader) { L.rows.push_back ({ item.text, {}, false, false, {} }); continue; }
        if (item.subMenu != nullptr && item.subMenu->getNumItems() > 0) {
            juce::PopupMenu sub = *item.subMenu; auto title = item.text; bool close = L.closeOnPick;
            L.rows.push_back ({ title, "Opens a list", false, item.isEnabled, [this, title, sub, close] {
                Level child; child.kind = KMenu; child.title = title; child.closeOnPick = close;
                child.blurb = close ? "Pick one. This screen closes after you choose." : "Pick one.";
                addMenu (child, sub); nav.push_back (std::move (child)); scroll = 0; repaint();
            } });
        } else if (item.action) {
            auto fn = item.action; bool tick = item.isTicked, close = L.closeOnPick, enabled = item.isEnabled;
            L.rows.push_back ({ item.text, tick ? "On" : juce::String(), tick, enabled, [this, fn, close] {
                if (fn) fn(); if (close) dismiss(); else pop();
            } });
        } else L.rows.push_back ({ item.text, {}, false, false, {} });
    }
}
CoreEditor::SettingsPanel::Level CoreEditor::SettingsPanel::make (Kind k) {
    using namespace wr;
    Level L; L.kind = k; L.closeOnPick = true; auto& proc = ed.core; auto& s = proc.store.lib.settings;
    auto homeRow = [&] (Kind dest, const char* title, const char* blurb) {
        L.rows.push_back ({ title, blurb, false, true, [this, dest] { nav.push_back (make (dest)); scroll = 0; repaint(); } });
    };
    if (k == KHome) {
        L.title = "Settings"; L.blurb = "Each group is one screen. Choosing a setting closes it, so it does not stay open over the show.";
        homeRow (KAudio, "Audio & Interface", "The sound card, the buffer, and how the guitar is routed.");
        homeRow (KMidi, "MIDI & Footswitches", "Clock, the cue channel, which Chocolate is which, and what each switch does.");
        homeRow (KClick, "Click & Count-in", "The click sound, spoken cues, and the count-in before Play.");
        homeRow (KBand, "Band Sync", "Lead or follow the rest of the band on this Wi-Fi.");
        homeRow (KDisplay, "Display", "The chart, section colours, the projector, and the stage view.");
        homeRow (KAbout, "About & Legal", "Subscription, terms of use, privacy, backup, and the recorder.");
        return L;
    }
    if (k == KAudio) {
        L.title = "Audio & Interface"; L.blurb = "The sound card, the buffer size, and how the guitar is routed.";
       #if JucePlugin_Build_Standalone
        if (proc.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
            L.rows.push_back ({ "Audio interface, sample rate and buffer", "Opens the system audio screen, then closes Settings.", false, true, [this] {
                if (auto* h = juce::StandalonePluginHolder::getInstance()) h->showAudioSettingsDialog(); dismiss(); } });
       #endif
        juce::PopupMenu extra; ed.addAudioSettings (extra); addMenu (L, extra); return L;
    }
    if (k == KMidi) {
        L.title = "MIDI & Footswitches"; L.blurb = "Clock, which Chocolate is the guitar unit, and what each switch does. A hold needs momentary CC; factory program-change mode has no release, so double-tap does the hold.";
        { std::vector<wr::FootPortDesc> desc; auto devs = juce::MidiInput::getAvailableDevices();
          for (int i = 0; i < devs.size(); ++i) desc.push_back ({ devs[i].identifier.toStdString(), devs[i].name.toStdString(), i });
          auto asg = wr::assignFootPedals (desc, s.guitarPedalId, s.sectionPedalId, s.midiClockInId, s.onePedalSection);
          juce::String detail = asg.detected == 0 ? "Plug a Chocolate into the iPad. Two can share a powered USB hub."
              : asg.detected == 1 ? juce::String ("One pedal, used as the ") + (asg.section.connected ? "sections unit (Next / Play)." : "guitar unit (tap, boost, tuner).")
              : juce::String (asg.detected) + " pedals, each opened on its own. Same name or same USB id does not merge them.";
          L.rows.push_back ({ "Pedals detected: " + juce::String (asg.detected), detail, false, true, {} });
          L.rows.push_back ({ "Guitar unit", asg.guitar.connected ? juce::String (asg.guitar.name) : juce::String ("Not connected"), false, true, {} });
          L.rows.push_back ({ "Sections unit", asg.section.connected ? juce::String (asg.section.name) : juce::String ("Not connected"), false, true, {} });
          L.rows.push_back ({ "Swap pedal roles", asg.detected <= 1 ? "Use this one pedal for the other job." : "Guitar and sections trade places.", false, asg.detected > 0, [this] {
              auto& s2 = ed.core.store.lib.settings; std::vector<wr::FootPortDesc> d2; auto devs2 = juce::MidiInput::getAvailableDevices();
              for (int i = 0; i < devs2.size(); ++i) d2.push_back ({ devs2[i].identifier.toStdString(), devs2[i].name.toStdString(), i });
              auto now = wr::assignFootPedals (d2, s2.guitarPedalId, s2.sectionPedalId, s2.midiClockInId, s2.onePedalSection);
              if (now.detected <= 1) s2.onePedalSection = ! s2.onePedalSection;
              else { std::swap (s2.guitarPedalId, s2.sectionPedalId); std::swap (s2.guitarPedalName, s2.sectionPedalName); }
              ed.core.refreshFootswitchPorts(); ed.core.store.save(); nav.back() = make (KMidi); scroll = 0; repaint(); } });
          L.rows.push_back ({ "Holds (tuner, stop, mute)", "Factory mode has no release, so hold does nothing. Double-tap for that action. A+B = tuner, C+D = stop. Real hold: momentary CC 80-83 in the M-Vave app.", false, true, {} });
          if (ed.core.footProgramChangeMode()) L.rows.push_back ({ "Program changes are coming in", "Holds will not work until the Chocolate is set to momentary CC. Double-tap still does the hold.", false, true, {} });
        }
        { juce::PopupMenu ck; const int mode = s.midiClockMode;
          ck.addItem ("Off", true, mode == 0, [this] { ed.core.setMidiClockMode (wr::MidiClockMode::Off); });
          ck.addItem ("Send (24 ppqn + Start/Stop + position on section jumps)", true, mode == 1, [this] { ed.core.setMidiClockMode (wr::MidiClockMode::Send); });
          ck.addItem ("Receive (follow an external tempo and transport)", true, mode == 2, [this] { ed.core.setMidiClockMode (wr::MidiClockMode::Receive); });
          ck.addItem ("The leader sends clock. Followers stay on Wi-Fi band sync.", false, false, [] {});
          if (mode == 1) { juce::PopupMenu outs; outs.addItem ("Virtual port: Bearing Live Clock", true, s.midiClockOutId.empty(), [this] {
              ed.core.store.lib.settings.midiClockOutId.clear(); ed.core.applySettings(); ed.core.store.save(); });
            for (auto& d : juce::MidiOutput::getAvailableDevices()) { auto id = d.identifier.toStdString(); auto name = d.name;
              outs.addItem (name, true, s.midiClockOutId == id || s.midiClockOutId == name.toStdString(), [this, id] { ed.core.store.lib.settings.midiClockOutId = id; ed.core.applySettings(); ed.core.store.save(); }); }
            ck.addSubMenu ("MIDI output", outs); }
          if (mode == 2) { juce::PopupMenu ins; ins.addItem ("All inputs", true, s.midiClockInId.empty(), [this] {
              ed.core.store.lib.settings.midiClockInId.clear(); ed.core.applySettings(); ed.core.store.save(); });
            for (auto& d : juce::MidiInput::getAvailableDevices()) { auto id = d.identifier.toStdString(); auto name = d.name;
              ins.addItem (name, true, s.midiClockInId == id || s.midiClockInId == name.toStdString(), [this, id] { ed.core.store.lib.settings.midiClockInId = id; ed.core.applySettings(); ed.core.store.save(); }); }
            ck.addSubMenu ("MIDI input", ins); }
          L.rows.push_back ({ "MIDI clock: " + juce::String (wr::midiClockModeName (wr::midiClockModeFromInt (mode))), "Off, send, or follow another device.", false, true, [this, ck] {
              Level child; child.kind = KMenu; child.title = "MIDI clock"; child.blurb = "Pick one. This screen closes after you choose."; child.closeOnPick = true; addMenu (child, ck); nav.push_back (std::move (child)); scroll = 0; repaint(); } }); }
        { juce::PopupMenu ex; for (int ch = 1; ch <= 16; ++ch) ex.addItem ("Channel " + juce::String (ch), true, s.externalChannel == ch, [this, ch] { ed.core.store.lib.settings.externalChannel = ch; ed.core.applySettings(); ed.core.store.save(); });
          L.rows.push_back ({ "External cue channel (now " + juce::String (s.externalChannel) + ")", "Program changes and notes on this channel jump sections.", false, true, [this, ex] {
              Level child; child.kind = KMenu; child.title = "Cue channel"; child.blurb = "Pick one. This screen closes after you choose."; child.closeOnPick = true; addMenu (child, ex); nav.push_back (std::move (child)); scroll = 0; repaint(); } }); }
        { juce::PopupMenu pd; wr::ui::addFootPedalSettings (ed, pd); addMenu (L, pd); }
        L.rows.push_back ({ "Footswitch mapping", "What A-D, hold, and the E/F two-button combos do.", false, true, [this] { nav.push_back (make (KMap)); scroll = 0; repaint(); } });
        return L;
    }
    if (k == KMap) {
        L.title = "Footswitch Mapping"; L.closeOnPick = false;
        L.blurb = mapGuitar ? "Guitar unit. Factory program changes still arrive as they do now; this only changes the action."
                            : "Sections unit. Factory program changes still arrive as they do now; this only changes the action.";
        auto& st = s;
        auto shown = mapGuitar ? (st.guitarPedalName.empty() ? juce::String ("first Chocolate plugged in") : juce::String (st.guitarPedalName))
                               : (st.sectionPedalName.empty() ? juce::String ("second Chocolate plugged in") : juce::String (st.sectionPedalName));
        L.rows.push_back ({ mapGuitar ? "Guitar unit" : "Sections unit", shown + "  -  tap to switch unit", false, true, [this] { mapGuitar = ! mapGuitar; nav.back() = make (KMap); scroll = 0; repaint(); } });
        int u = unit (mapGuitar);
        if (u < 0) { L.rows.push_back ({ "No saved pedal for this unit", "The actions are still here. Plug in a Chocolate, or swap roles on the MIDI screen.", false, false, {} }); return L; }
        auto& c = proc.store.lib.controllers[(size_t) u];
        const char* letters = "ABCD";
        for (int i = 0; i < 4; ++i) {
            L.rows.push_back ({ juce::String::charToString ((juce::juce_wchar) letters[i]) + "  press", actName (liveAct (c, i, false, false)), false, true, [this, i] { chooseAct (juce::String::charToString ((juce::juce_wchar) ("ABCD"[i])) + " press", i, false, false); } });
            L.rows.push_back ({ juce::String::charToString ((juce::juce_wchar) letters[i]) + "  hold", actName (liveAct (c, i, true, false)), false, true, [this, i] { chooseAct (juce::String::charToString ((juce::juce_wchar) ("ABCD"[i])) + " hold", i, true, false); } });
        }
        L.rows.push_back ({ "E   A+B together", actName (c.comboE), false, true, [this] { chooseAct ("E  (A+B)", 0, false, true); } });
        L.rows.push_back ({ "F   C+D together", actName (c.comboF), false, true, [this] { chooseAct ("F  (C+D)", 1, false, true); } });
        L.rows.push_back ({ "Reset to factory defaults", "Guitar: tap, boost, this song's C and D. Sections: start / hold stop, next, chorus, next song.", false, true, [this] {
            int u2 = unit (mapGuitar); if (u2 < 0) return;
            wr::resetFootswitchMap (ed.core.store.lib.controllers[(size_t) u2]); ed.core.pushControllerProfiles(); ed.core.store.save();
            nav.back() = make (KMap); scroll = 0; repaint(); } });
        return L;
    }
    if (k == KClick) {
        L.title = "Click & Count-in"; L.blurb = "The click sound, spoken section names, and the count-in before Play.";
        juce::PopupMenu ck;
        for (int i = 0; i < 3; ++i) { auto lbl = proc.clickOutLabel (i); if (lbl.isEmpty()) continue; ck.addItem (lbl, true, s.clickOut == i, [this, i] { ed.core.store.lib.settings.clickOut = i; ed.core.applySettings(); ed.core.store.save(); }); }
        for (int db : { -30, -24, -18, -12, -6, -3, 0 }) ck.addItem ("Volume " + juce::String (db) + " dB", true, (int) s.clickDb == db, [this, db] { ed.core.store.lib.settings.clickDb = (float) db; ed.core.applySettings(); ed.core.store.save(); });
        { juce::PopupMenu ps; for (int i = 0; i < ClickGen::kNumPresets; ++i) ps.addItem (ClickGen::presetName (i), true, s.clickPreset == i, [this, i] { ed.core.store.lib.settings.clickPreset = i; ed.core.applySettings(); ed.core.store.save(); });
          ck.addSubMenu ("Sound: " + juce::String (ClickGen::presetName (s.clickPreset)), ps);
          juce::PopupMenu dl; for (int db : { -6, -3, 0, 2, 4, 6, 9, 12 }) dl.addItem ((db > 0 ? "+" : "") + juce::String (db) + " dB versus the other beats", true, (int) std::round (s.downbeatDb) == db, [this, db] { ed.core.store.lib.settings.downbeatDb = (float) db; ed.core.applySettings(); ed.core.store.save(); });
          ck.addSubMenu ("Downbeat level", dl); }
        ck.addItem ("Beat 1 has its own sound", true, s.clickAccent, [this] { auto& st = ed.core.store.lib.settings; st.clickAccent = ! st.clickAccent; ed.core.applySettings(); ed.core.store.save(); });
        ck.addItem ("Muted (hold guitar D does this too)", true, s.clickMuted, [this] { ed.core.live().toggleClickMute(); ed.core.store.save(); });
        ck.addItem ("Speak the section name", true, s.voiceCues, [this] { auto& st = ed.core.store.lib.settings; st.voiceCues = ! st.voiceCues; ed.core.applySettings(); ed.core.store.save(); });
        { juce::PopupMenu cv; for (int db : { -24, -18, -12, -9, -6, -3, 0 }) cv.addItem (juce::String (db) + " dB", true, (int) s.cueDb == db, [this, db] { ed.core.store.lib.settings.cueDb = (float) db; ed.core.applySettings(); ed.core.store.save(); }); ck.addSubMenu ("Spoken cue volume", cv); }
        ck.addItem ("Still speak when the click is muted", true, s.cuesWhenClickMuted, [this] { auto& st = ed.core.store.lib.settings; st.cuesWhenClickMuted = ! st.cuesWhenClickMuted; ed.core.applySettings(); ed.core.store.save(); });
        ck.addItem ("Section changes wait for the next bar", true, s.quantizeSections, [this] { auto& st = ed.core.store.lib.settings; st.quantizeSections = ! st.quantizeSections; ed.core.store.save(); });
        { juce::PopupMenu cin; const std::pair<int, const char*> opts[] = { { 0, "Off" }, { 4, "4 clicks" }, { 8, "8 clicks (usual)" }, { 101, "1 bar" }, { 102, "2 bars" } };
          for (auto& o : opts) { int code = o.first; cin.addItem (o.second, true, s.countIn == code, [this, code] { ed.core.store.lib.settings.countIn = code; ed.core.store.save(); }); }
          cin.addItem ("Speak the count (1, 2, 3, 4)", true, s.countInSpoken, [this] { auto& st = ed.core.store.lib.settings; st.countInSpoken = ! st.countInSpoken; ed.core.store.save(); });
          ck.addSubMenu ("Count-in when you press Play", cin); }
        ck.addItem ("Advance sections from their bar counts when no MIDI clock is coming in", true, s.autoAdvance, [this] { auto& st = ed.core.store.lib.settings; st.autoAdvance = ! st.autoAdvance; ed.core.store.save(); });
        ed.addInstrumentSettings (ck);
        addMenu (L, ck); return L;
    }
    if (k == KBand) {
        L.title = "Band Sync"; L.blurb = "One iPad leads. The others follow the song, the section, and Play over this Wi-Fi.";
        auto& S = proc.sync(); juce::PopupMenu bs; S.browse (true);
        bs.addItem (juce::String (S.statusLine()), false, false, [] {});
        if (S.mode() == BandSync::Mode::Leading) for (auto& n : S.followerNames()) bs.addItem ("   " + juce::String (n.empty() ? "follower" : n), false, false, [] {});
        bs.addItem ("Lead this session", proc.fullAccess(), S.mode() == BandSync::Mode::Leading, [this] { ed.core.sync().lead (ed.core.store.lib.settings.deviceName); });
        if (S.canTakeLead()) bs.addItem ("Take lead", true, false, [this] { ed.core.sync().takeLead (ed.core.nowSec()); });
        for (auto& p : S.leaders()) { auto peer = p; bs.addItem ("Join " + juce::String (peer.name), proc.fullAccess(), false, [this, peer] { ed.core.sync().join (peer.host, peer.port, ed.core.store.lib.settings.deviceName); ed.core.store.save(); }); }
        if (S.leaders().empty()) bs.addItem ("No other leader on this Wi-Fi yet", false, false, [] {});
        if (! s.lastLeaderHost.empty()) bs.addItem ("Rejoin " + juce::String (s.lastLeaderHost), [this] { auto& st = ed.core.store.lib.settings; ed.core.sync().join (st.lastLeaderHost, st.lastLeaderPort > 0 ? st.lastLeaderPort : wr::sync::kDefaultPort, st.deviceName); });
        bs.addItem ("Join by IP address...", [this] { ed.askText ("Join leader", "The leader's IP address, shown on their Band Sync screen", ed.core.store.lib.settings.lastLeaderHost, [this] (juce::String ip) {
            if (ip.isNotEmpty()) { ed.core.sync().join (ip.trim().toStdString(), wr::sync::kDefaultPort, ed.core.store.lib.settings.deviceName); ed.core.store.save(); } }); });
        bs.addItem ("Leave / stop sync", S.mode() != BandSync::Mode::Off, false, [this] { ed.core.sync().stop(); });
       #if __has_include(<ableton/Link.hpp>)
        bs.addItem ("Use Ableton Link for the tempo", true, s.useLink, [this] { auto& st = ed.core.store.lib.settings; st.useLink = ! st.useLink; ed.core.sync().useLink = st.useLink; ed.core.store.save(); });
       #else
        bs.addItem ("Ableton Link is off. Tempo follows MIDI clock or this iPad.", false, false, [] {});
       #endif
        bs.addItem ("This iPad's name: " + juce::String (s.deviceName), [this] { ed.askText ("Device name", "The name the rest of the band sees", ed.core.store.lib.settings.deviceName, [this] (juce::String n) { if (n.isNotEmpty()) { ed.core.store.lib.settings.deviceName = n.toStdString(); ed.core.store.save(); } }); });
        addMenu (L, bs); return L;
    }
    if (k == KDisplay) {
        L.title = "Display"; L.blurb = "The chart, the section colours, the projector words, and how the stage view scrolls.";
        { juce::PopupMenu ar; for (int sec : { 0, 5, 10, 20, 30 }) ar.addItem (sec == 0 ? "Never" : juce::String (sec) + " seconds", true, s.autoReturnSec == sec, [this, sec] { ed.core.store.lib.settings.autoReturnSec = sec; ed.core.store.save(); });
          L.rows.push_back ({ "Return the chart to the whole song", s.autoReturnSec == 0 ? "Never" : juce::String (s.autoReturnSec) + " seconds after a jump", false, true, [this, ar] {
              Level child; child.kind = KMenu; child.title = "Chart auto-return"; child.blurb = "Pick one. This screen closes after you choose."; child.closeOnPick = true; addMenu (child, ar); nav.push_back (std::move (child)); scroll = 0; repaint(); } }); }
        { juce::PopupMenu sc; wr::ui::addSectionColourSettings (ed, sc, this); addMenu (L, sc); }
        { auto& pj = s.projector; juce::PopupMenu pm;
          pm.addItem (proc.projector().externalAvailable() ? "Show on the external display" : "Show on an external display when one is plugged in", true, proc.projector().externalOn(), [this] { auto& o = ed.core.projector(); if (o.externalOn()) o.showExternal (false); else if (ed.requireAccess (wr::Feature::Projector)) o.showExternal (true); });
          pm.addItem ("Fill this iPad (double-tap the picture to close it)", [this] { if (ed.requireAccess (wr::Feature::Projector)) ed.core.projector().showOnThisScreen (ed); });
          juce::PopupMenu lp; for (int n : { 1, 2, 3, 4, 6 }) lp.addItem (juce::String (n) + (n == 1 ? " line" : " lines"), true, pj.linesPerSlide == n, [this, n] { ed.core.store.lib.settings.projector.linesPerSlide = n; ed.core.store.save(); });
          pm.addSubMenu ("Lines on each slide (" + juce::String (pj.linesPerSlide) + ")", lp);
          juce::PopupMenu fz; const char* sz[] = { "Small", "Medium", "Large (usual)", "Extra large" }; for (int i = 0; i < 4; ++i) fz.addItem (sz[i], true, pj.fontSize == i, [this, i] { ed.core.store.lib.settings.projector.fontSize = i; ed.core.store.save(); });
          pm.addSubMenu ("Word size", fz);
          pm.addItem ("Title slide before each song", true, pj.titleSlide, [this] { auto& p = ed.core.store.lib.settings.projector; p.titleSlide = ! p.titleSlide; ed.core.store.save(); });
          pm.addItem ("Blank slide between songs", true, pj.blankBetweenSongs, [this] { auto& p = ed.core.store.lib.settings.projector; p.blankBetweenSongs = ! p.blankBetweenSongs; ed.core.store.save(); });
          pm.addItem ("Service notes along the bottom", true, pj.showNotes, [this] { auto& p = ed.core.store.lib.settings.projector; p.showNotes = ! p.showNotes; ed.core.store.save(); });
          pm.addItem ("ALL CAPS", true, pj.uppercase, [this] { auto& p = ed.core.store.lib.settings.projector; p.uppercase = ! p.uppercase; ed.core.store.save(); });
          pm.addItem (pj.background.empty() ? "Background picture (now plain black)" : "Background picture (" + juce::File (pj.background).getFileName() + ")", [this] { ed.chooseImage (false); });
          if (! pj.background.empty()) pm.addItem ("Background: plain black", [this] { ed.core.store.lib.settings.projector.background.clear(); ed.core.store.save(); });
          pm.addItem (pj.logo.empty() ? "Logo on the blank slide" : "Logo (" + juce::File (pj.logo).getFileName() + ")", [this] { ed.chooseImage (true); });
          L.rows.push_back ({ "Projector lyrics", "Slides, size, background, and the external screen.", false, true, [this, pm] {
              Level child; child.kind = KMenu; child.title = "Projector"; child.blurb = "Pick one. This screen closes after you choose."; child.closeOnPick = true; addMenu (child, pm); nav.push_back (std::move (child)); scroll = 0; repaint(); } }); }
        { juce::PopupMenu view; ed.addViewSettings (view); addMenu (L, view); }
        return L;
    }
    L.title = "About & Legal"; L.blurb = "The subscription, the terms, the privacy policy, backups, and usage you type into CCLI yourself.";
    { juce::PopupMenu sub; const auto nowS = (int64_t) juce::Time::currentTimeMillis() / 1000;
      sub.addItem (juce::String (proc.entitlements.statusText (nowS)), false, false, [] {});
#ifdef WR_STORE_BUILD
      sub.addItem ("Subscribe, start the trial, or restore", [this] { ed.openPaywall(); });
      sub.addItem ("Church team", [this] { ed.openTeam(); });
#else
      sub.addItem ("This build is unlocked. Church teams are in the App Store build.", false, false, [] {});
#endif
      sub.addItem ("Terms of Use", [] { juce::URL (wr::legal::termsUrl()).launchInDefaultBrowser(); });
      sub.addItem ("Privacy Policy", [] { juce::URL (wr::legal::privacyUrl()).launchInDefaultBrowser(); });
      addMenu (L, sub); }
    { juce::PopupMenu v; wr::ui::addV08Settings (ed, v, this); addMenu (L, v); }
    L.rows.push_back ({ "Library folder", proc.store.file().getParentDirectory().getFullPathName(), false, false, {} });
    return L;
}

void CoreEditor::SettingsPanel::paint (juce::Graphics& g) {
    using namespace wr::theme;
    g.fillAll (bg);
    if (nav.empty()) return;
    auto& L = cur();
    closeR = { getWidth() - 112, 14, 94, 36 };
    backR = nav.size() > 1 ? juce::Rectangle<int> (16, 14, 44, 36) : juce::Rectangle<int>();
    pill (g, closeR.toFloat(), card, cardBorder, 10);
    g.setColour (text); g.setFont (font (14, true));
    { auto x = closeR.toFloat().reduced (10, 10).removeFromLeft (16);
      g.drawLine (x.getX(), x.getY(), x.getRight(), x.getBottom(), 2.0f); g.drawLine (x.getRight(), x.getY(), x.getX(), x.getBottom(), 2.0f); }
    g.drawText ("Done", closeR.withTrimmedLeft (28), juce::Justification::centredLeft);
    auto titleR = juce::Rectangle<int> (nav.size() > 1 ? 68 : 20, 10, getWidth() - (nav.size() > 1 ? 68 : 20) - 120, 28);
    g.setFont (font (20, true)); g.drawText (L.title, titleR, juce::Justification::centredLeft, true);
    g.setColour (dim); g.setFont (font (13));
    g.drawFittedText (L.blurb, juce::Rectangle<int> (20, 42, getWidth() - 40, 36), juce::Justification::centredLeft, 2);
    if (! backR.isEmpty()) { pill (g, backR.toFloat(), card, cardBorder, 10); g.setColour (text); g.setFont (font (18, true)); g.drawText ("<", backR, juce::Justification::centred); }
    listR = getLocalBounds().withTrimmedTop (86).reduced (16, 8);
    g.saveState(); g.reduceClipRegion (listR);
    int y = listR.getY() - scroll; hits.assign (L.rows.size(), {});
    for (int i = 0; i < (int) L.rows.size(); ++i) {
        auto b = juce::Rectangle<int> (listR.getX(), y, listR.getWidth(), 56); hits[(size_t) i] = b;
        if (b.getBottom() > listR.getY() && b.getY() < listR.getBottom()) {
            auto& row = L.rows[(size_t) i];
            pill (g, b.toFloat(), row.tick ? accentFill.withAlpha (0.45f) : card, row.tick ? accentText : cardBorder, 10);
            g.setColour (row.on ? text : dimmer); g.setFont (font (15, true));
            g.drawText (row.title, b.reduced (14, 6).removeFromTop (24), juce::Justification::centredLeft, true);
            g.setColour (row.tick ? onAccent : dim); g.setFont (font (12));
            g.drawText (row.sub, b.reduced (14, 4).removeFromBottom (20), juce::Justification::centredLeft, true);
        }
        y += 62;
    }
    g.restoreState();
    int content = y + scroll - listR.getY();
    maxScroll = std::max (0, content - listR.getHeight());
}
void CoreEditor::SettingsPanel::mouseUp (const juce::MouseEvent& e) {
    if (drag) { drag = false; return; }
    if (closeR.contains (e.getPosition())) { dismiss(); return; }
    if (backR.contains (e.getPosition())) { pop(); return; }
    if (nav.empty()) return;
    auto& rows = cur().rows;
    for (int i = 0; i < (int) hits.size() && i < (int) rows.size(); ++i)
        if (rows[(size_t) i].on && rows[(size_t) i].go && hits[(size_t) i].contains (e.getPosition()) && listR.contains (e.getPosition())) { rows[(size_t) i].go(); return; }
}

void CoreEditor::layoutSettingsOverlay() { if (settings_) settings_->setBounds (getLocalBounds()); }
void CoreEditor::openSettings (juce::Component*) {
    if (! settings_) { settings_ = std::make_unique<SettingsPanel> (*this); addChildComponent (*settings_); }
    settings_->setBounds (getLocalBounds()); settings_->showHome();
}
CoreEditor::~CoreEditor() { setLookAndFeel (nullptr); }
