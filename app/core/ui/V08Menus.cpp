#include "model/Ccli.h"
// v0.8 menus shared by every app: footswitch banks / remapping, stage mode, auto-scroll, rehearsal recorder,
// song stats, one-file backup (export / import).
#include "LiveExtras.h"
#include "model/StageTools.h"
using namespace wr;

static const char* kAssignable[] = { "", "prevSection", "nextSection", "jumpChorus", "nextSong", "prevSong", "tap", "boost", "switchC", "switchD",
                                     "tunerToggle", "toggleView", "clickMute", "clickToggle", "play", "playStart", "stop", "bankNext", "bankPrev",
                                     "tracksPlay", "tracksNext", "tracksLoop", "tracksStop" };
static juce::String actionLabel (const std::string& a) {
    static const std::pair<const char*, const char*> n[] = { { "", "(nothing)" }, { "prevSection", "Previous section" }, { "nextSection", "Next section" }, { "jumpChorus", "Jump to chorus" },
        { "nextSong", "Next song" }, { "prevSong", "Previous song" }, { "tap", "Tap tempo" }, { "boost", "Boost" }, { "switchC", "Song switch C" }, { "switchD", "Song switch D" },
        { "tunerToggle", "Tuner" }, { "toggleView", "Full / section view" }, { "clickMute", "Mute click" }, { "clickToggle", "Click on / off" }, { "play", "Play / stop" },
        { "playStart", "Start / play" }, { "stop", "Stop" },
        { "bankNext", "Next bank" }, { "bankPrev", "Previous bank" },
        { "tracksPlay", "Tracks: play / stop" }, { "tracksNext", "Tracks: next section" }, { "tracksLoop", "Tracks: loop section (vamp)" }, { "tracksStop", "Tracks: stop" } };
    for (auto& p : n) if (a == p.first) return p.second; return juce::String (a);
}
void wr::ui::addFootswitchMenu (CoreEditor& ed, juce::PopupMenu& m, int u, int sw) {
    auto& lib = ed.core.store.lib; if (u < 0 || u >= (int) lib.controllers.size()) return; auto& c = lib.controllers[(size_t) u]; CoreEditor* e = &ed;
    auto commit = [e] { e->core.pushControllerProfiles(); e->core.store.save(); e->repaint(); };
    const bool banks = ! c.banks.empty(); const int b = banks ? std::clamp (c.bank, 0, (int) c.banks.size() - 1) : 0;
    for (int lp = 0; lp < 2; ++lp) {
        const std::string cur = banks ? (lp ? c.banks[(size_t) b].longAction[sw] : c.banks[(size_t) b].action[sw]) : (lp ? c.sw[sw].longAction : c.sw[sw].action);
        juce::PopupMenu am;
        for (auto* a : kAssignable) { std::string an = a; if (! banks && (an == "bankNext" || an == "bankPrev")) continue;
            am.addItem (actionLabel (an), true, cur == an, [e, u, sw, lp, an, commit] { auto& cc = e->core.store.lib.controllers[(size_t) u];
                if (! cc.banks.empty()) { auto& bk = cc.banks[(size_t) std::clamp (cc.bank, 0, (int) cc.banks.size() - 1)]; (lp ? bk.longAction[sw] : bk.action[sw]) = an; }
                else (lp ? cc.sw[sw].longAction : cc.sw[sw].action) = an;
                commit(); }); }
        m.addSubMenu (juce::String (lp ? "Long press" : "Press") + " does: " + actionLabel (cur) + (banks ? "  [bank " + juce::String (b + 1) + "]" : juce::String()), am);
    }
    juce::PopupMenu bm;
    if (! banks) bm.addItem ("Enable banks (bank 1 = this mapping, bank 2 = template)", [e, u, commit] { enableDefaultBanks (e->core.store.lib.controllers[(size_t) u]); commit(); });
    else {
        for (int i = 0; i < (int) c.banks.size(); ++i) bm.addItem ("Bank " + juce::String (i + 1) + " - " + juce::String (c.banks[(size_t) i].name), true, i == b, [e, u, i, commit] { e->core.store.lib.controllers[(size_t) u].bank = i; commit(); });
        bm.addSeparator();
        bm.addItem ("Add a bank (copy of this one)", c.banks.size() < 8, false, [e, u, commit] { auto& cc = e->core.store.lib.controllers[(size_t) u]; auto nb = cc.banks[(size_t) cc.bank]; nb.name = "Bank " + std::to_string (cc.banks.size() + 1); cc.banks.push_back (nb); cc.bank = (int) cc.banks.size() - 1; commit(); });
        bm.addItem ("Rename this bank...", [e, u, b, commit] { e->askText ("Bank name", "Shown on the Live screen when you switch banks", e->core.store.lib.controllers[(size_t) u].banks[(size_t) b].name, [e, u, b, commit] (juce::String t) {
            auto& cc = e->core.store.lib.controllers[(size_t) u]; if (b < (int) cc.banks.size() && t.isNotEmpty()) { cc.banks[(size_t) b].name = t.toStdString(); commit(); } }); });
        bm.addItem ("Remove this bank", c.banks.size() > 1, false, [e, u, b, commit] { auto& cc = e->core.store.lib.controllers[(size_t) u]; cc.banks.erase (cc.banks.begin() + b); cc.bank = std::min (cc.bank, (int) cc.banks.size() - 1); commit(); });
        bm.addItem ("Turn banks off (keep bank 1 as the mapping)", [e, u, commit] { auto& cc = e->core.store.lib.controllers[(size_t) u];
            for (int i = 0; i < 4; ++i) { cc.sw[i].action = cc.banks[0].action[i]; cc.sw[i].longAction = cc.banks[0].longAction[i]; } cc.banks.clear(); cc.bank = 0; commit(); });
    }
    m.addSubMenu ("Banks / modes" + (banks ? juce::String (" (") + juce::String ((int) c.banks.size()) + ")" : juce::String()), bm);
    auto combo = [&] (const char* title, bool isE) {
        const std::string cur = isE ? c.comboE : c.comboF;
        juce::PopupMenu cm;
        for (auto* a : kAssignable) { std::string an = a; if (an == "bankNext" || an == "bankPrev") continue;
            cm.addItem (actionLabel (an), true, cur == an, [e, u, isE, an, commit] {
                auto& cc = e->core.store.lib.controllers[(size_t) u]; (isE ? cc.comboE : cc.comboF) = an; commit(); }); }
        m.addSubMenu (juce::String (title) + ": " + actionLabel (cur), cm);
    };
    combo ("E  (A+B together)", true); combo ("F  (C+D together)", false);
}

void wr::ui::addV08Settings (CoreEditor& ed, juce::PopupMenu& m, juce::Component* anchor) {
    CoreEditor* e = &ed; auto& s = ed.core.store.lib.settings; juce::Component::SafePointer<juce::Component> sp (anchor);
    juce::PopupMenu v;
    v.addItem ("Auto-scroll the chart with the bar position while playing", true, s.autoScroll, [e] { auto& st = e->core.store.lib.settings; st.autoScroll = ! st.autoScroll; e->core.store.save(); });
    m.addSubMenu ("Stage view", v);
    { juce::PopupMenu c;   // v0.15 CCLI song-usage log (manual entry on CCLI's site; nothing is sent automatically)
      c.addItem (juce::String ((int) e->core.store.lib.usageLog.size()) + " uses logged (live sets only)", false, false, [] {});
      auto exportRange = [e, sp] (bool pd) { auto* w = new juce::AlertWindow ("Export CCLI usage", "Dates as YYYY-MM-DD (leave blank for open-ended). You enter these on CCLI's site yourself.", juce::MessageBoxIconType::NoIcon);
          w->addTextEditor ("from", ""); w->addTextEditor ("to", juce::String (CoreProcessor::today())); w->addButton ("Export", 1); w->addButton ("Cancel", 0);
          w->enterModalState (true, juce::ModalCallbackFunction::create ([e, sp, w, pd] (int r) { if (r != 1) return;
              auto f = w->getTextEditorContents ("from").trim().toStdString(), t = w->getTextEditorContents ("to").trim().toStdString();
              if ((! f.empty() && ! wr::ccli::validDate (f)) || (! t.empty() && ! wr::ccli::validDate (t))) { juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "CCLI export", "Use dates like 2026-11-08"); return; }
              wr::platform::shareFiles (e->core.exportCcliUsage (f, t, pd), sp.getComponent()); }), true); };
      c.addItem ("Export usage (CSV + summary)...", [exportRange] { exportRange (false); });
      c.addItem ("Export usage incl. public-domain hymns...", [exportRange] { exportRange (true); });
      m.addSubMenu ("CCLI song usage", c); }
    // rehearsal recorder
    juce::PopupMenu r; auto& P = ed.core;
    if (P.recorder.recording()) r.addItem ("STOP recording (" + juce::String (P.recorder.seconds(), 0) + " s)", [e] { auto f = e->core.stopRecording(); e->core.status = "Saved " + f.getFileName(); });
    else r.addItem ("Record (guitar input" + juce::String (s.recordClick ? " + click" : "") + ")", [e] { auto err = e->core.startRecording(); if (err.isNotEmpty()) juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Recorder", err); });
    r.addItem ("Record the click too (2nd channel)", ! P.recorder.recording(), s.recordClick, [e] { auto& st = e->core.store.lib.settings; st.recordClick = ! st.recordClick; e->core.store.save(); });
    r.addSeparator();
    auto takes = P.takes(); int shown = 0;
    for (auto& f : takes) { if (++shown > 8) break; juce::PopupMenu t; auto file = f;
        t.addItem ("Play", ! P.recorder.recording(), false, [e, file] { e->core.playTake (file); });
        t.addItem ("Stop playback", [e] { e->core.practice.play (false); });
        t.addItem ("Share / save to Files...", [file, sp] { wr::platform::shareFiles ({ file }, sp.getComponent()); });
        t.addItem ("Delete", [file] { file.deleteFile(); });
        r.addSubMenu (f.getFileNameWithoutExtension(), t); }
    if (takes.isEmpty()) r.addItem ("(no takes yet)", false, false, [] {});
    m.addSubMenu ("Rehearsal recorder", r);
    // song stats
    juce::PopupMenu st; const auto today = CoreProcessor::today();
    for (auto& x : songStats (ed.core.store.lib)) { auto rt = recentText (ed.core.store.lib, x.songId, today);
        st.addItem (juce::String (x.title) + "  -  " + juce::String (x.plays) + " plays" + (x.last.empty() ? juce::String() : ", last " + juce::String (x.last)) + (rt.empty() ? juce::String() : "  (RECENT)"), false, false, [] {}); }
    st.addSeparator(); juce::PopupMenu wd; for (int d : { 7, 14, 21, 28, 42 }) wd.addItem (juce::String (d) + " days", true, s.repeatWarnDays == d, [e, d] { e->core.store.lib.settings.repeatWarnDays = d; e->core.store.save(); });
    st.addSubMenu ("Flag songs repeated within", wd);
    m.addSubMenu ("Song stats / play history", st);
    // backup bundle
    juce::PopupMenu bk;
    bk.addItem ("Export everything (one file -> Files / AirDrop)...", [e, sp] { auto f = e->core.exportEverything(); wr::platform::shareFiles ({ f }, sp.getComponent()); });
    for (int mode = 0; mode < 2; ++mode)
        bk.addItem (mode == 0 ? "Import a backup - add missing songs / setlists / rigs..." : "Import a backup - REPLACE everything...", [e, mode] {
            auto ch = std::make_shared<juce::FileChooser> ("Bearing Live backup", juce::File(), "*.json;*.wrbackup", true, false, e);
            ch->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [e, mode, ch] (const juce::FileChooser& fc) {
                auto f = fc.getResult(); if (! f.existsAsFile()) return; auto r = e->core.importEverything (f, mode == 0 ? ImportMode::Merge : ImportMode::Replace);
                juce::AlertWindow::showMessageBoxAsync (r.ok ? juce::MessageBoxIconType::InfoIcon : juce::MessageBoxIconType::WarningIcon, "Import backup",
                    r.ok ? "Added " + juce::String (r.songs) + " songs, " + juce::String (r.setlists) + " setlists, " + juce::String (r.rigs) + " rigs" + (r.skipped ? " (" + juce::String (r.skipped) + " already here, kept)" : juce::String()) + ".\nThe previous library is in Backups."
                         : juce::String (r.error)); e->repaint(); }); });
    m.addSubMenu ("Backup / restore everything", bk);
}
void wr::ui::addFootPedalSettings (CoreEditor& ed, juce::PopupMenu& m) {
    CoreEditor* e = &ed; auto& st = e->core.store.lib.settings; juce::PopupMenu pd;
      juce::Array<juce::MidiDeviceInfo> devs = juce::MidiInput::getAvailableDevices();
      std::vector<FootPortDesc> desc; for (int i = 0; i < devs.size(); ++i) desc.push_back ({ devs[i].identifier.toStdString(), devs[i].name.toStdString(), i });
      auto asg = assignFootPedals (desc, st.guitarPedalId, st.sectionPedalId, st.midiClockInId, st.onePedalSection);
      auto pick = [&] (bool guitar) {
          juce::PopupMenu sub; const int n = asg.detected;
          for (auto& p : asg.pedals) {
              auto id = p.id; auto nm = p.name;
              bool on = (guitar ? asg.guitar.id : asg.section.id) == id;
              sub.addItem (juce::String (nm), true, on, [e, id, nm, guitar, n] {
                  auto& s2 = e->core.store.lib.settings;
                  auto& mine = guitar ? s2.guitarPedalId : s2.sectionPedalId;
                  auto& other = guitar ? s2.sectionPedalId : s2.guitarPedalId;
                  auto& myName = guitar ? s2.guitarPedalName : s2.sectionPedalName;
                  auto& otherName = guitar ? s2.sectionPedalName : s2.guitarPedalName;
                  if (other == id) { std::swap (mine, other); std::swap (myName, otherName); }
                  else { mine = id; myName = nm; }
                  if (n <= 1) s2.onePedalSection = ! guitar;
                  e->core.refreshFootswitchPorts(); e->core.store.save(); });
          }
          if (asg.pedals.empty()) sub.addItem ("(no foot pedal yet)", false, false, [] {});
          return sub;
      };
      pd.addSubMenu ("Guitar pedal is...", pick (true));
      pd.addSubMenu ("Sections pedal is...", pick (false));
      pd.addItem ("Swap the two pedals", asg.detected > 0, false, [e] {
          auto& s2 = e->core.store.lib.settings; std::vector<FootPortDesc> d2; auto devs2 = juce::MidiInput::getAvailableDevices();
          for (int i = 0; i < devs2.size(); ++i) d2.push_back ({ devs2[i].identifier.toStdString(), devs2[i].name.toStdString(), i });
          auto now = assignFootPedals (d2, s2.guitarPedalId, s2.sectionPedalId, s2.midiClockInId, s2.onePedalSection);
          if (now.detected <= 1) s2.onePedalSection = ! s2.onePedalSection;
          else { std::swap (s2.guitarPedalId, s2.sectionPedalId); std::swap (s2.guitarPedalName, s2.sectionPedalName); }
          e->core.refreshFootswitchPorts(); e->core.store.save(); });
      pd.addItem ("One pedal defaults to guitar. Two identical Chocolates stay two pedals. Swap if Next/Play should be the only one.", false, false, [] {});
    m.addSubMenu ("Foot pedals (two Chocolates)", pd);
}
