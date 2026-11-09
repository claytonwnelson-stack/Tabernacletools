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
    arr_->defaults = &core.store.lib.settings.sectionColors;   // v0.8 section colours edited in the arrangement editor
    arr_->onColours = [this] (const std::string& id, std::vector<std::string> cc) {
        if (auto* so = core.store.lib.song (id)) { for (size_t i = 0; i < so->chart.sections.size() && i < cc.size(); ++i) so->chart.sections[i].customColor = cc[i];
            wr::applySectionColors (so->chart, {}); core.live().refreshChart(); core.store.save(); repaint(); } };
    imp_ = std::make_unique<ImportPreviewView>(); addChildComponent (*imp_);
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
void CoreEditor::openLiveArrangement() {
    if (! requireAccess (wr::Feature::Edit)) return;
    auto& L = core.live(); auto* s = L.song(); if (! s) return;
    arr_->open (s->chart, L.arrangement(), true, [this] (std::vector<std::string> t, int scope) {
        core.live().setArrangement (t, scope == 1); core.store.save(); if (core.practiceMode()) core.loadPracticeTrack(); }, s->id);
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
CoreEditor::~CoreEditor() { setLookAndFeel (nullptr); }

void CoreEditor::coreLayout() {
    auto top = getLocalBounds().removeFromTop (64);
    settingsBtn_.setBounds (top.removeFromRight (140).reduced (8, 16));
    band_->setBounds (getLocalBounds()); arr_->setBounds (getLocalBounds()); imp_->setBounds (getLocalBounds()); if (team_) team_->setBounds (getLocalBounds()); if (paywall_) paywall_->setBounds (getLocalBounds());
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

void CoreEditor::openSettings (juce::Component* anchor) {
    auto& proc = core; juce::PopupMenu m; auto& s = proc.store.lib.settings;
   #if JucePlugin_Build_Standalone
    if (proc.wrapperType == juce::AudioProcessor::wrapperType_Standalone) m.addItem ("Audio interface, sample rate & buffer (64-128)...", [] { if (auto* h = juce::StandalonePluginHolder::getInstance()) h->showAudioSettingsDialog(); });
   #endif
    addAudioSettings (m);
    { // v0.17 MIDI Clock: Off / Send / Receive + port pickers (USB, Bluetooth MIDI, network session, virtual)
      juce::PopupMenu ck; const int mode = s.midiClockMode;
      ck.addItem ("Off", true, mode == 0, [this] { core.setMidiClockMode (wr::MidiClockMode::Off); });
      ck.addItem ("Send (24 ppqn + Start/Stop + Song Position on section jumps)", true, mode == 1, [this] { core.setMidiClockMode (wr::MidiClockMode::Send); });
      ck.addItem ("Receive (follow external tempo + transport)", true, mode == 2, [this] { core.setMidiClockMode (wr::MidiClockMode::Receive); });
      ck.addSeparator();
      ck.addItem ("Leader-only when sending; followers keep Wi-Fi band sync", false, false, [] {});
      ck.addItem ("Enable the CoreMIDI network session (Cubase / Mac)", false, false, [] {});
      if (mode == 1) {
        juce::PopupMenu outs; outs.addItem ("Virtual port: Tabernacle Tools Clock (other iPad apps)", true, s.midiClockOutId.empty(), [this] {
          core.store.lib.settings.midiClockOutId.clear(); core.applySettings(); core.store.save(); });
        for (auto& d : juce::MidiOutput::getAvailableDevices()) {
          auto id = d.identifier.toStdString(); auto name = d.name;
          outs.addItem (name, true, s.midiClockOutId == id || s.midiClockOutId == name.toStdString(), [this, id] {
            core.store.lib.settings.midiClockOutId = id; core.applySettings(); core.store.save(); });
        }
        ck.addSubMenu ("MIDI output (USB / Bluetooth / network / virtual)", outs);
      }
      if (mode == 2) {
        juce::PopupMenu ins; ins.addItem ("All inputs (Audio MIDI settings)", true, s.midiClockInId.empty(), [this] {
          core.store.lib.settings.midiClockInId.clear(); core.applySettings(); core.store.save(); });
        for (auto& d : juce::MidiInput::getAvailableDevices()) {
          auto id = d.identifier.toStdString(); auto name = d.name;
          ins.addItem (name, true, s.midiClockInId == id || s.midiClockInId == name.toStdString(), [this, id] {
            core.store.lib.settings.midiClockInId = id; core.applySettings(); core.store.save(); });
        }
        ck.addSubMenu ("MIDI input (USB / Bluetooth / network)", ins);
      }
      m.addSubMenu ("MIDI Clock: " + juce::String (wr::midiClockModeName (wr::midiClockModeFromInt (mode))), ck); }
    juce::PopupMenu ar; for (int sec : { 0, 5, 10, 20, 30 }) ar.addItem (sec == 0 ? "Never" : juce::String (sec) + " s", true, s.autoReturnSec == sec, [this, sec] { core.store.lib.settings.autoReturnSec = sec; core.store.save(); });
    m.addSubMenu ("Chart: auto-return to full view after", ar);
    juce::PopupMenu ex; for (int ch = 1; ch <= 16; ++ch) ex.addItem ("ch " + juce::String (ch), true, s.externalChannel == ch, [this, ch] { core.store.lib.settings.externalChannel = ch; core.applySettings(); core.store.save(); });
    m.addSubMenu ("External MIDI cue channel (PC n / note " + juce::String (s.externalChannel) + ")", ex);
    { juce::PopupMenu ck; bool anyOut = false;
      for (int i = 0; i < 3; ++i) { auto lbl = proc.clickOutLabel (i); if (lbl.isEmpty()) continue; anyOut = true; ck.addItem (lbl, true, s.clickOut == i, [this, i] { core.store.lib.settings.clickOut = i; core.applySettings(); core.store.save(); }); }
      if (anyOut) ck.addSeparator();
      for (int db : { -30, -24, -18, -12, -6, -3, 0 }) ck.addItem ("Volume " + juce::String (db) + " dB", true, (int) s.clickDb == db, [this, db] { core.store.lib.settings.clickDb = (float) db; core.applySettings(); core.store.save(); });
      ck.addSeparator(); { juce::PopupMenu ps; for (int i = 0; i < ClickGen::kNumPresets; ++i) ps.addItem (ClickGen::presetName (i), true, s.clickPreset == i, [this, i] { core.store.lib.settings.clickPreset = i; core.applySettings(); core.store.save(); });
        ck.addSubMenu ("Sound: " + juce::String (ClickGen::presetName (s.clickPreset)), ps);
        juce::PopupMenu dl; for (int db : { -6, -3, 0, 2, 4, 6, 9, 12 }) dl.addItem ((db > 0 ? "+" : "") + juce::String (db) + " dB vs other beats", true, (int) std::round (s.downbeatDb) == db, [this, db] { core.store.lib.settings.downbeatDb = (float) db; core.applySettings(); core.store.save(); });
        ck.addSubMenu ("Downbeat level", dl); }
      ck.addItem ("Distinct downbeat (beat 1 sound)", true, s.clickAccent, [this] { auto& st = core.store.lib.settings; st.clickAccent = ! st.clickAccent; core.applySettings(); core.store.save(); });
      ck.addItem ("Muted (also: hold guitar D)", true, s.clickMuted, [this] { core.live().toggleClickMute(); core.store.save(); });
      ck.addSeparator();
      ck.addItem ("Spoken section cues (\"Verse two... 2, 3, 4\")", true, s.voiceCues, [this] { auto& st = core.store.lib.settings; st.voiceCues = ! st.voiceCues; core.applySettings(); core.store.save(); });
      juce::PopupMenu cv; for (int db : { -24, -18, -12, -9, -6, -3, 0 }) cv.addItem (juce::String (db) + " dB", true, (int) s.cueDb == db, [this, db] { core.store.lib.settings.cueDb = (float) db; core.applySettings(); core.store.save(); });
      ck.addSubMenu ("Cue voice volume", cv);
      ck.addItem ("Cues still speak when the click is muted", true, s.cuesWhenClickMuted, [this] { auto& st = core.store.lib.settings; st.cuesWhenClickMuted = ! st.cuesWhenClickMuted; core.applySettings(); core.store.save(); });
      ck.addItem ("Section changes wait for the bar line (click / PLAY / MIDI clock)", true, s.quantizeSections, [this] { auto& st = core.store.lib.settings; st.quantizeSections = ! st.quantizeSections; core.store.save(); });
      ck.addSeparator();
      juce::PopupMenu cin; const std::pair<int, const char*> opts[] = { { 0, "Off" }, { 4, "4 clicks" }, { 8, "8 clicks (default)" }, { 101, "1 bar" }, { 102, "2 bars" } };
      for (auto& o : opts) { int code = o.first; cin.addItem (o.second, true, s.countIn == code, [this, code] { core.store.lib.settings.countIn = code; core.store.save(); }); }
      cin.addSeparator(); cin.addItem ("Speak the count (1, 2, 3, 4)", true, s.countInSpoken, [this] { auto& st = core.store.lib.settings; st.countInSpoken = ! st.countInSpoken; core.store.save(); });
      ck.addSubMenu ("Count-in on PLAY (plays even when muted)", cin);
      m.addSubMenu ("Click track, voice cues, count-in", ck); }
    m.addItem ("Auto-advance sections by bars (when no MIDI clock)", true, s.autoAdvance, [this] { auto& st = core.store.lib.settings; st.autoAdvance = ! st.autoAdvance; core.store.save(); });
    wr::ui::addSectionColourSettings (*this, m, anchor ? anchor : this);   // v0.8
    wr::ui::addV08Settings (*this, m, anchor ? anchor : this);
    { juce::PopupMenu sub; const auto nowS = (int64_t) juce::Time::currentTimeMillis() / 1000;   // v0.10 plan / trial status (StoreKit hookup TODO)
      sub.addItem (juce::String (core.entitlements.statusText (nowS)), false, false, [] {});
#ifdef WR_STORE_BUILD
      sub.addItem ("Subscribe / free trial / restore purchases...", true, false, [this] { openPaywall(); });
#endif
      for (auto& pr : wr::subscriptionProducts()) sub.addItem (juce::String (pr.name) + " - $" + juce::String (pr.usdPrice, 2) + "/month" + (pr.seats > 1 ? " (up to " + juce::String (pr.seats) + " people)" : juce::String()) + " - 14-day free trial", false, false, [] {});
#ifdef WR_STORE_BUILD
      sub.addSeparator();
      sub.addItem ("Church team: join with a code / manage your team...", true, false, [this] { openTeam(); });
#else
      sub.addItem ("Church teams: App Store build only (this build is fully unlocked)", false, false, [] {});
#endif
      sub.addSeparator();
      sub.addItem ("Terms of Use", [] { juce::URL (wr::legal::termsUrl()).launchInDefaultBrowser(); });
      sub.addItem ("Privacy Policy", [] { juce::URL (wr::legal::privacyUrl()).launchInDefaultBrowser(); });
      m.addSubMenu ("Subscription", sub); }
    { juce::PopupMenu lg; lg.addItem ("Terms of Use", [] { juce::URL (wr::legal::termsUrl()).launchInDefaultBrowser(); });
      lg.addItem ("Privacy Policy", [] { juce::URL (wr::legal::privacyUrl()).launchInDefaultBrowser(); });
      m.addSubMenu ("About & legal", lg); }
    addInstrumentSettings (m);
    // ---- band sync: leader / follower over local Wi-Fi (Bonjour), Ableton Link tempo + beat phase
    { auto& S = proc.sync(); juce::PopupMenu bs; S.browse (true);
      bs.addItem (juce::String (S.statusLine()), false, false, [] {});
      if (S.mode() == BandSync::Mode::Leading) for (auto& n : S.followerNames()) bs.addItem ("   " + juce::String (n.empty() ? "follower" : n), false, false, [] {});
      bs.addSeparator();
      bs.addItem ("Lead this session (followers mirror song, sections, cues, tempo, PLAY)", core.fullAccess(), S.mode() == BandSync::Mode::Leading, [this] { core.sync().lead (core.store.lib.settings.deviceName); });
      auto leaders = S.leaders();
      if (leaders.empty()) bs.addItem ("Join: looking for leaders on this Wi-Fi...", false, false, [] {});
      for (auto& p : leaders) { auto peer = p; bs.addItem ("Join " + juce::String (peer.name) + "  (" + juce::String (peer.host) + ")", core.fullAccess(), false, [this, peer] { core.sync().join (peer.host, peer.port, core.store.lib.settings.deviceName); core.store.save(); }); }
      if (! s.lastLeaderHost.empty()) bs.addItem ("Rejoin " + juce::String (s.lastLeaderHost), [this] { auto& st = core.store.lib.settings; core.sync().join (st.lastLeaderHost, st.lastLeaderPort > 0 ? st.lastLeaderPort : wr::sync::kDefaultPort, st.deviceName); });
      bs.addItem ("Join by IP address...", [this] { askText ("Join leader", "Leader IP address (shown on the leader under Band sync)", core.store.lib.settings.lastLeaderHost,
          [this] (juce::String ip) { if (ip.isNotEmpty()) { core.sync().join (ip.trim().toStdString(), wr::sync::kDefaultPort, core.store.lib.settings.deviceName); core.store.save(); } }); });
      bs.addItem ("Leave / stop sync", S.mode() != BandSync::Mode::Off, false, [this] { core.sync().stop(); });
      bs.addSeparator();
     #if __has_include(<ableton/Link.hpp>)
      bs.addItem ("Use Ableton Link for tempo + beat phase", true, s.useLink, [this] { auto& st = core.store.lib.settings; st.useLink = ! st.useLink; core.sync().useLink = st.useLink; core.store.save(); });
     #else
      bs.addItem ("Ableton Link: off (MIDI Clock + Wi-Fi sync)", false, false, [] {});
     #endif
      bs.addItem ("This device's name: " + juce::String (s.deviceName) + "...", [this] { askText ("Device name", "Shown to the band", core.store.lib.settings.deviceName,
          [this] (juce::String n) { if (n.isNotEmpty()) { core.store.lib.settings.deviceName = n.toStdString(); core.store.save(); } }); });
      m.addSubMenu ("Band sync (leader / follower)", bs); }
    // ---- projector lyrics
    { auto& pj = s.projector; juce::PopupMenu pm; auto& out = proc.projector();
      pm.addItem (out.externalAvailable() ? "Show on external display (HDMI)" : "Show on external display (none connected - shows when plugged in)", true, out.externalOn(), [this] { auto& o = core.projector(); if (o.externalOn()) o.showExternal (false); else if (requireAccess (wr::Feature::Projector)) o.showExternal (true); });
      pm.addItem ("Show full screen on this iPad (projector-only follower; double-tap to close)", [this] { if (requireAccess (wr::Feature::Projector)) core.projector().showOnThisScreen (*this); });
      pm.addSeparator();
      juce::PopupMenu lp; for (int n : { 1, 2, 3, 4, 6 }) lp.addItem (juce::String (n) + (n == 1 ? " line" : " lines"), true, pj.linesPerSlide == n, [this, n] { core.store.lib.settings.projector.linesPerSlide = n; core.store.save(); });
      pm.addSubMenu ("Lines per slide (" + juce::String (pj.linesPerSlide) + ")", lp);
      juce::PopupMenu fz; const char* sz[] = { "Small", "Medium", "Large (default)", "Extra large" }; for (int i = 0; i < 4; ++i) fz.addItem (sz[i], true, pj.fontSize == i, [this, i] { core.store.lib.settings.projector.fontSize = i; core.store.save(); });
      pm.addSubMenu ("Font size", fz);
      pm.addItem ("Title slide before each song", true, pj.titleSlide, [this] { auto& p = core.store.lib.settings.projector; p.titleSlide = ! p.titleSlide; core.store.save(); });
      pm.addItem ("Blank / logo slide between songs", true, pj.blankBetweenSongs, [this] { auto& p = core.store.lib.settings.projector; p.blankBetweenSongs = ! p.blankBetweenSongs; core.store.save(); });
      pm.addItem ("Show service notes at the bottom of the slides", true, pj.showNotes, [this] { auto& p = core.store.lib.settings.projector; p.showNotes = ! p.showNotes; core.store.save(); });
      pm.addItem ("ALL CAPS", true, pj.uppercase, [this] { auto& p = core.store.lib.settings.projector; p.uppercase = ! p.uppercase; core.store.save(); });
      pm.addItem (pj.background.empty() ? "Background image... (now: black)" : "Background image... (" + juce::File (pj.background).getFileName() + ")", [this] { chooseImage (false); });
      if (! pj.background.empty()) pm.addItem ("Background: plain black", [this] { core.store.lib.settings.projector.background.clear(); core.store.save(); });
      pm.addItem (pj.logo.empty() ? "Logo for the blank slide..." : "Logo: " + juce::File (pj.logo).getFileName() + "...", [this] { chooseImage (true); });
      m.addSubMenu ("Projector lyrics", pm); }
    addViewSettings (m);
    m.addSeparator();
    m.addItem ("Data folder: " + proc.store.file().getParentDirectory().getFullPathName(), false, false, [] {});
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (anchor));
}
