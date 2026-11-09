#include "SetlistView.h"
#include "Platform.h"
#include "LiveExtras.h"
#include "model/Chart.h"
#include "model/StageTools.h"
using namespace wr; using namespace wr::theme;
void showBackupsMenu (CoreEditor& ed, juce::Component* anchor, std::function<void()> after);

SetlistView::SetlistView (CoreEditor& e) : ed (e) {
    for (auto* t : { &search_, &date_, &name_ }) { addAndMakeVisible (*t); t->setFont (font (14)); t->setColour (juce::TextEditor::backgroundColourId, well); }
    search_.setTextToShowWhenEmpty ("Search songs, keys...", dimmer); search_.onTextChange = [this] { repaint(); };
    date_.setTextToShowWhenEmpty ("YYYY-MM-DD", dimmer); name_.setTextToShowWhenEmpty ("Set name", dimmer);
    for (auto* b : { &save_, &goLive_, &import_, &paste_, &hymnal_, &sets_, &export_, &players_, &backups_ }) addAndMakeVisible (*b);
    export_.onClick = [this] { if (ed.requireAccess (wr::Feature::ShareSetlist)) exportMenu(); }; players_.onClick = [this] { if (ed.requireAccess (wr::Feature::Edit)) playersMenu(); }; backups_.onClick = [this] { backupsMenu(); };
    goLive_.setColour (juce::TextButton::buttonColourId, orange); goLive_.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff111111));
    save_.onClick = [this] { if (ed.requireAccess (wr::Feature::Edit)) saveSet(); };
    goLive_.onClick = [this] { if (! ed.requireAccess (wr::Feature::LiveView)) return; saveSet(); ed.core.goLive (working_.date, 0); ed.showLive(); };
    import_.onClick = [this] { if (ed.requireAccess (wr::Feature::Import)) importChart(); };
    hymnal_.onClick = [this] { ed.openHymnal ([this] (juce::String id) { working_.entries.push_back ({ id.toStdString(), "", 0, "lead" }); repaint(); }); };
    paste_.onClick = [this] { if (ed.requireAccess (wr::Feature::Import)) pasteChart(); };
    sets_.onClick = [this] { juce::PopupMenu m; for (auto& s : ed.core.store.lib.setlists) { auto d = s.date; m.addItem (juce::String (d) + "  " + juce::String (s.name) + "  (" + juce::String ((int) s.entries.size()) + " songs)", [this, d] { loadSet (d); }); }
        m.addSeparator(); m.addItem ("New set for next Sunday", [this] { auto t = juce::Time::getCurrentTime(); int dw = t.getDayOfWeek(); t += juce::RelativeTime::days ((7 - dw) % 7 == 0 ? 7 : (7 - dw) % 7);
            working_ = Setlist(); working_.name = "Sunday"; working_.date = t.formatted ("%Y-%m-%d").toStdString(); date_.setText (working_.date); name_.setText (working_.name); repaint(); });
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&sets_)); };
}

void SetlistView::refresh() { const auto& cur = ed.core.live().setlist(); if (! cur.date.empty()) loadSet (cur.date); else if (! ed.core.store.lib.setlists.empty()) loadSet (ed.core.store.lib.setlists.front().date); }
void SetlistView::loadSet (const std::string& d) { if (auto* s = ed.core.store.lib.setlist (d)) working_ = *s; date_.setText (working_.date, false); name_.setText (working_.name, false); repaint(); }

void SetlistView::saveSet() {
    working_.date = date_.getText().trim().toStdString(); working_.name = name_.getText().trim().toStdString(); if (working_.date.empty()) working_.date = juce::Time::getCurrentTime().formatted ("%Y-%m-%d").toStdString();
    auto& L = ed.core.store.lib; if (auto* s = L.setlist (working_.date)) *s = working_; else L.setlists.push_back (working_);
    std::sort (L.setlists.begin(), L.setlists.end(), [] (const Setlist& a, const Setlist& b) { return a.date > b.date; });
    ed.core.store.save(); savedMsg_ = "saved " + ed.core.store.lastSavedTime; repaint();
    ed.core.live().refreshSetlist (working_);   // v0.7: arrangement / notes edits reach Live (and the band) right away
}

std::vector<int> SetlistView::filteredSongs() const {
    std::vector<int> v; auto q = search_.getText().toLowerCase(); auto& songs = ed.core.store.lib.songs;
    for (int i = 0; i < (int) songs.size(); ++i) { juce::String hay = (juce::String (songs[(size_t) i].chart.title) + " " + songs[(size_t) i].chart.key + " " + songs[(size_t) i].chart.artist).toLowerCase(); if (q.isEmpty() || hay.contains (q)) v.push_back (i); }
    return v;
}

void SetlistView::importChart() {
    // Parent is this view so iPad presents a page sheet. A top-level chooser becomes an unanchored popover and UIKit throws before the picker appears.
    chooser_ = std::make_unique<juce::FileChooser> ("Import chart", juce::File(), "*.cho;*.chopro;*.chordpro;*.txt;*.pdf;*.crd", true, false, this);
    juce::Component::SafePointer<SetlistView> safe (this);
    chooser_->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe] (const juce::FileChooser& fc) {
        if (safe == nullptr) return;
        auto fail = [] (const juce::String& m) { juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Import", m); };
        try {
            auto url = fc.getURLResult();
            if (url.isEmpty()) { if (! fc.getResult().existsAsFile()) return; url = juce::URL (fc.getResult()); }
            juce::String text; juce::String title = url.getFileName().upToLastOccurrenceOf (".", false, false);
            const bool pdf = url.getFileName().endsWithIgnoreCase (".pdf");
            if (pdf && url.isLocalFile()) text = platform::extractPdfText (url.getLocalFile());
            else if (auto in = url.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress))) {
                if (in->getTotalLength() > 4 * 1024 * 1024) { fail ("That file is larger than 4 MB."); return; }
                text = in->readEntireStreamAsString();
            } else if (url.isLocalFile()) text = url.getLocalFile().loadFileAsString();
            if (text.trim().isEmpty()) { fail (pdf ? "This PDF has no text layer (it's a scan or photo). Use a text PDF, a .txt or a ChordPro file." : "Couldn't read that file. Use ChordPro, plain text, or a text PDF."); return; }
            safe->ed.openImportPreview (text, title.isEmpty() ? juce::String ("Imported chart") : title, [safe] (juce::String id) {
                if (safe != nullptr) { safe->working_.entries.push_back ({ id.toStdString(), "", 0, "lead" }); safe->repaint(); } });
        } catch (const std::exception& ex) { fail (juce::String (ex.what())); }
        catch (...) { fail ("Couldn't read that file."); }
    });
}

void SetlistView::pasteChart() {   // v0.7: only what the user copied - nothing is fetched from the web
    auto text = platform::clipboardText();
    if (text.trim().isEmpty()) { juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Paste chart", "The clipboard is empty. Select the chords on a page (or in Notes / Mail), tap Copy, then Paste chart."); return; }
    ed.openImportPreview (text, "Pasted chart", [this] (juce::String id) { working_.entries.push_back ({ id.toStdString(), "", 0, "lead" }); repaint(); });
}

void SetlistView::resized() {
    auto r = getLocalBounds().reduced (18, 14);
    libArea_ = r.removeFromLeft ((int) (r.getWidth() * 0.34f)); r.removeFromLeft (16);
    ctlArea_ = r.removeFromBottom (150); r.removeFromBottom (12); setArea_ = r;
    auto l = libArea_.withTrimmedTop (24); search_.setBounds (l.removeFromTop (40)); { auto b = libArea_.removeFromBottom (44); const int w3 = (b.getWidth() - 16) / 3; hymnal_.setBounds (b.removeFromRight (w3)); b.removeFromRight (8); paste_.setBounds (b.removeFromRight (w3)); b.removeFromRight (8); import_.setBounds (b); }
    auto top = setArea_.removeFromTop (40); name_.setBounds (top.removeFromLeft (180)); top.removeFromLeft (8); date_.setBounds (top.removeFromLeft (130)); top.removeFromLeft (8); sets_.setBounds (top.removeFromLeft (130));
    goLive_.setBounds (top.removeFromRight (130)); top.removeFromRight (8); save_.setBounds (top.removeFromRight (110));
    top.removeFromRight (8); export_.setBounds (top.removeFromRight (90)); top.removeFromRight (6); players_.setBounds (top.removeFromRight (96)); top.removeFromRight (6); backups_.setBounds (top.removeFromRight (96));
    setArea_.removeFromTop (8);
}

void SetlistView::paintTopBar (juce::Graphics& g, juce::Rectangle<int> r) {
    g.setColour (dim); g.setFont (font (13)); g.drawText ("Setlist builder", r.getX(), 12, 400, 16, juce::Justification::left);
    g.setColour (juce::Colours::white); g.setFont (font (19, true));
    g.drawText (juce::String (working_.name) + " - " + juce::String (working_.date) + " - " + juce::String ((int) working_.entries.size()) + " songs", r.getX(), 28, r.getWidth(), 24, juce::Justification::left);
}

void SetlistView::paint (juce::Graphics& g) {
    g.fillAll (bg); auto& lib = ed.core.store.lib; libRows_.clear(); setRows_.clear(); learnBtns_.clear();
    // ---- library
    label (g, "SONG LIBRARY", libArea_.withHeight (18)); label (g, juce::String ((int) lib.songs.size()) + " songs", libArea_.withHeight (18), dimmer, 12, juce::Justification::centredRight);
    auto lr = libArea_.withTrimmedTop (72).withTrimmedBottom (52); int y = lr.getY();
    for (int i : filteredSongs()) {
        auto& s = lib.songs[(size_t) i]; juce::Rectangle<int> row (lr.getX(), y, lr.getWidth(), 52); if (row.getBottom() > lr.getBottom()) break;
        pill (g, row.toFloat().reduced (0, 3), card, cardBorder, 10);
        g.setColour (text); g.setFont (font (15, true)); g.drawText (s.chart.title, row.reduced (12, 0).withTrimmedRight (120), juce::Justification::centredLeft, true);
        g.setColour (dim); g.setFont (font (11, true)); g.drawText ("KEY " + juce::String (s.chart.key) + "  BPM " + juce::String ((int) s.chart.bpm), row.reduced (12, 0).withTrimmedRight (30), juce::Justification::centredRight);
        bool inSet = std::any_of (working_.entries.begin(), working_.entries.end(), [&] (const SetlistEntry& e) { return e.songId == s.id; });
        g.setColour (inSet ? green : orange); g.setFont (font (18, true)); g.drawText (inSet ? "v" : "+", row.withTrimmedLeft (row.getWidth() - 30), juce::Justification::centred);
        libRows_.push_back ({ row, i }); y += 52;
    }
    // ---- set
    label (g, juce::String (working_.date).toUpperCase() + "  -  DRAG TO REORDER, TAP FOR KEY / ROLE", setArea_.withHeight (18)); label (g, savedMsg_, setArea_.withHeight (18), green, 12, juce::Justification::centredRight);
    y = setArea_.getY() + 26;
    for (int i = 0; i < (int) working_.entries.size(); ++i) {
        auto& e = working_.entries[(size_t) i]; auto* s = lib.song (e.songId); juce::Rectangle<int> row (setArea_.getX(), y, setArea_.getWidth(), 58);
        bool dragging = i == dragFrom_; if (dragTo_ >= 0 && dragFrom_ >= 0 && i == dragTo_) { g.setColour (pink); g.fillRect (row.getX(), row.getY() - 2, row.getWidth(), 3); }
        pill (g, row.toFloat().reduced (0, 4), dragging ? juce::Colour (0xff2b2210) : card, dragging ? orange : cardBorder, 12);
        g.setColour (dimmer); g.setFont (font (18, true)); g.drawText ("=", row.withWidth (34), juce::Justification::centred);
        g.setColour (dim); g.setFont (font (14, true)); g.drawText (juce::String (i + 1), row.withX (row.getX() + 34).withWidth (24), juce::Justification::centred);
        g.setColour (text); g.setFont (font (16, true)); g.drawText (s ? juce::String (s->chart.title) : juce::String ("(missing song)"), row.withTrimmedLeft (66).withTrimmedRight (300), juce::Justification::centredLeft, true);
        std::string key = ! e.keyOverride.empty() ? e.keyOverride : (s ? s->chart.key : "");
        auto tags = row.withTrimmedLeft (row.getWidth() - 300).reduced (8, 16); auto pl = [&] (const juce::String& k, const juce::String& v, int w, juce::Colour c) { auto b = tags.removeFromLeft (w); tags.removeFromLeft (6); pill (g, b.toFloat(), btn, btnBorder, 7); g.setColour (dim); g.setFont (font (11)); g.drawText (k, b.withTrimmedLeft (6), juce::Justification::centredLeft); g.setColour (c); g.setFont (font (12, true)); g.drawText (v, b.withTrimmedRight (6), juce::Justification::centredRight); };
        pl ("KEY", juce::String (key) + (e.keyOverride.empty() ? "" : "*"), 70, e.keyOverride.empty() ? text : orange);
        pl ("BPM", juce::String ((int) (e.bpmOverride > 0 ? e.bpmOverride : (s ? s->chart.bpm : 0))), 74, text);
        pl ("", e.role == "sing" ? "Sing+Play" : "Lead/Tabs", 100, e.role == "sing" ? blue : pink);
        { auto rt = wr::recentText (lib, e.songId, working_.date.empty() ? CoreProcessor::today() : working_.date);   // v0.8 song stats: played recently
          if (! rt.empty()) { g.setColour (orange); g.setFont (font (11, true)); g.drawText (juce::String (rt).toUpperCase(), row.withTrimmedLeft (66).withTrimmedRight (300).withTrimmedTop (34).withHeight (16), juce::Justification::centredLeft, true); } }
        setRows_.push_back ({ row, i }); y += 58;
    }
    if (working_.entries.empty()) { g.setColour (dim); g.setFont (font (15)); g.drawText ("Tap + on a library song to add it.", setArea_.withTrimmedTop (30).withHeight (60), juce::Justification::centred); }
    // ---- controller mapping (both units) with MIDI learn
    pill (g, ctlArea_.toFloat(), panel, cardBorder, 12); auto c = ctlArea_.reduced (14, 10);
    auto& ctl = lib.controllers;
    label (g, "FOOTSWITCH MAPPINGS - tap a switch to MIDI-learn it (saved, so a spare unit is a 1-minute swap)" + juce::String (ed.core.midiLearning() ? "   ...LEARNING: press the switch" : ""), c.removeFromTop (18), ed.core.midiLearning() ? pink : dimmer, 11);
    for (int u = 0; u < (int) ctl.size() && u < 2; ++u) {
        auto row = c.removeFromTop (54); c.removeFromTop (6); auto nm = row.removeFromLeft (190);
        g.setColour (text); g.setFont (font (13, true)); g.drawText (ctl[(size_t) u].name, nm.removeFromTop (28), juce::Justification::centredLeft, true);
        g.setColour (dim); g.setFont (font (11)); g.drawText ("MIDI ch " + juce::String (ctl[(size_t) u].channel) + " - momentary CC", nm, juce::Justification::centredLeft);
        float w = (row.getWidth() - 18) / 4.0f;
        for (int s = 0; s < 4; ++s) {
            auto& sw = ctl[(size_t) u].sw[s]; juce::Rectangle<int> b ((int) (row.getX() + s * (w + 6)), row.getY(), (int) w, row.getHeight());
            pill (g, b.toFloat(), juce::Colour (0xff171b25), cardBorder, 10);
            juce::String bind = sw.press.kind == MidiBinding::CC ? "CC " + juce::String (sw.press.number) : sw.press.kind == MidiBinding::PC ? "PC " + juce::String (sw.press.number) : sw.press.kind == MidiBinding::Note ? "Note " + juce::String (sw.press.number) : juce::String ("unmapped");
            g.setColour (orange); g.setFont (font (12, true)); g.drawText (juce::String::charToString ((juce::juce_wchar) ('A' + s)) + "  " + juce::String (sw.action), b.reduced (8, 4).removeFromTop (22), juce::Justification::centredLeft, true);
            g.setColour (dim); g.setFont (font (10, true)); g.drawText (bind + (sw.longAction.empty() || sw.longAction == sw.action ? juce::String() : "  - long: " + juce::String (sw.longAction)), b.reduced (8, 4).removeFromBottom (18), juce::Justification::centredLeft, true);
            learnBtns_.push_back ({ b, u * 4 + s });
        }
    }
}

void SetlistView::entryMenu (int idx) {
    if (! ed.requireAccess (wr::Feature::Edit)) return;   // v0.12 read-only
    auto& e = working_.entries[(size_t) idx]; auto* s = ed.core.store.lib.song (e.songId); juce::PopupMenu m, keys;
    std::string orig = s ? s->chart.key : ""; bool minor = ! orig.empty() && orig.back() == 'm';
    const char* ks[] = { "C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    for (auto* k : ks) { std::string key = std::string (k) + (minor ? "m" : ""); keys.addItem (juce::String (key) + (key == orig ? "  (original)" : ""), true, (e.keyOverride.empty() ? orig : e.keyOverride) == key, [this, idx, key, orig] { working_.entries[(size_t) idx].keyOverride = key == orig ? "" : key; repaint(); }); }
    m.addSubMenu ("Key for this set", keys);
    m.addItem ("Lead / tabs", true, e.role != "sing", [this, idx] { working_.entries[(size_t) idx].role = "lead"; repaint(); });
    m.addItem ("Sing + play", true, e.role == "sing", [this, idx] { working_.entries[(size_t) idx].role = "sing"; repaint(); });
    // v0.7 arrangement (this set or the song) + service notes
    if (s) { auto arr = e.arrangement.empty() ? s->chart.arrangement : e.arrangement; std::string sid = s->id;
        m.addItem ("Arrangement...  (" + juce::String (arr.empty() ? std::string ("as written") : formatArrangement (arr)) + (e.arrangement.empty() ? "" : ", this set") + ")", [this, idx, arr, sid] {
            auto* so = ed.core.store.lib.song (sid); if (! so) return;
            ed.arrangementEditor().open (so->chart, arr, true, [this, idx, sid] (std::vector<std::string> t, int scope) {
                if (idx >= (int) working_.entries.size()) return;
                if (scope == 1) working_.entries[(size_t) idx].arrangement = t;
                else { if (auto* so2 = ed.core.store.lib.song (sid)) so2->chart.arrangement = t; working_.entries[(size_t) idx].arrangement.clear(); }
                saveSet(); }, sid); });
        m.addItem ("Service notes... (" + juce::String ((int) e.notes.size()) + ")", [this, idx, sid] {
            auto* so = ed.core.store.lib.song (sid); if (! so || idx >= (int) working_.entries.size()) return;
            wr::ui::editServiceNotes (working_.entries[(size_t) idx].notes, so->chart, this, [this, idx] (std::vector<ServiceNote> n) { if (idx < (int) working_.entries.size()) { working_.entries[(size_t) idx].notes = n; saveSet(); } }); }); }
    m.addSeparator(); m.addItem ("Remove from set", [this, idx] { working_.entries.erase (working_.entries.begin() + idx); repaint(); });
    m.showMenuAsync ({});
}

void SetlistView::mouseDown (const juce::MouseEvent& e) { dragFrom_ = -1; dragTo_ = -1; if (! ed.core.allows (wr::Feature::Edit)) return; for (auto& r : setRows_) if (r.first.contains (e.getPosition()) && e.x < r.first.getX() + 66) dragFrom_ = r.second; }
void SetlistView::mouseDrag (const juce::MouseEvent& e) {
    if (dragFrom_ < 0) { for (auto& r : setRows_) if (r.first.contains (e.getMouseDownPosition())) dragFrom_ = r.second; if (dragFrom_ < 0) return; }
    dragTo_ = (int) working_.entries.size(); for (auto& r : setRows_) if (e.y < r.first.getCentreY()) { dragTo_ = r.second; break; }
    repaint();
}
void SetlistView::mouseUp (const juce::MouseEvent& e) {
    if (dragFrom_ >= 0 && dragTo_ >= 0 && e.getDistanceFromDragStart() > 6) {
        auto item = working_.entries[(size_t) dragFrom_]; working_.entries.erase (working_.entries.begin() + dragFrom_);
        int to = dragTo_ > dragFrom_ ? dragTo_ - 1 : dragTo_; working_.entries.insert (working_.entries.begin() + std::clamp (to, 0, (int) working_.entries.size()), item);
        dragFrom_ = dragTo_ = -1; repaint(); return;
    }
    dragFrom_ = dragTo_ = -1;
    if (e.getDistanceFromDragStart() > 6) { repaint(); return; }
    for (auto& r : libRows_) if (r.first.contains (e.getPosition())) { if (! ed.requireAccess (wr::Feature::Edit)) return; working_.entries.push_back ({ ed.core.store.lib.songs[(size_t) r.second].id, "", 0, "lead" }); repaint(); return; }
    for (auto& r : setRows_) if (r.first.contains (e.getPosition())) { entryMenu (r.second); return; }
    for (auto& b : learnBtns_) if (b.first.contains (e.getPosition())) {
        int u = b.second / 4, s = b.second % 4; juce::PopupMenu m;
        m.addItem ("Learn switch " + juce::String::charToString ((juce::juce_wchar) ('A' + s)) + " (press it now)", [this, u, s] { ed.core.beginMidiLearn (u, s, false); repaint(); });
        m.addItem ("Learn a separate LONG-press message", [this, u, s] { ed.core.beginMidiLearn (u, s, true); repaint(); });
        m.addSeparator(); wr::ui::addFootswitchMenu (ed, m, u, s); m.addSeparator();
        m.addItem ("Reset unit to Chocolate Plus defaults", [this, u] { auto& c = ed.core.store.lib.controllers[(size_t) u]; c = c.role == "section" ? defaultSectionUnit() : defaultGuitarUnit(); ed.core.pushControllerProfiles(); ed.core.store.save(); repaint(); });
        m.showMenuAsync ({}); return;
    }
}

// ------------------------------------------------------------------ sharing, players, backups
void SetlistView::exportMenu() {
    saveSet(); juce::PopupMenu m;
    auto go = [this] (std::vector<int> who, ExportFormat f) { // -1 = band chart
        std::vector<const Player*> ps; for (int i : who) ps.push_back (i < 0 ? nullptr : &working_.players[(size_t) i]);
        auto files = ed.core.store.exportSetlist (working_, ps, f); platform::shareFiles (files, &export_);
        savedMsg_ = "exported " + juce::String (files.size()) + " file(s)"; repaint(); };
    for (auto f : { ExportFormat::Pdf, ExportFormat::ChordPro }) {
        juce::PopupMenu sm; const juce::String fn = f == ExportFormat::Pdf ? "PDF" : "ChordPro";
        sm.addItem ("Band chart (setlist keys)", [go, f] { go ({ -1 }, f); });
        for (int i = 0; i < (int) working_.players.size(); ++i) { auto& p = working_.players[(size_t) i]; sm.addItem (juce::String (p.name) + " - " + juce::String (p.instrument) + (p.capo > 0 ? " (capo " + juce::String (p.capo) + ")" : juce::String()), [go, f, i] { go ({ i }, f); }); }
        if (! working_.players.empty()) { std::vector<int> all { -1 }; for (int i = 0; i < (int) working_.players.size(); ++i) all.push_back (i); sm.addItem ("Everyone (one file each)", [go, f, all] { go (all, f); }); }
        m.addSubMenu ("Share as " + fn, sm);
    }
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&export_));
}
void SetlistView::playersMenu() {
    juce::PopupMenu m;
    m.addItem ("Add player...", [this] {
        auto* w = new juce::AlertWindow ("Add player", "Name, instrument and default capo", juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("name", "", "Name"); w->addTextEditor ("inst", "Guitar", "Instrument"); w->addTextEditor ("capo", "0", "Capo (0 = none)");
        w->addButton ("Add", 1, juce::KeyPress (juce::KeyPress::returnKey)); w->addButton ("Cancel", 0);
        w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int r) { if (r != 1) return; Player p; p.name = w->getTextEditorContents ("name").trim().toStdString(); if (p.name.empty()) return;
            p.instrument = w->getTextEditorContents ("inst").trim().toStdString(); p.capo = juce::jlimit (0, 11, w->getTextEditorContents ("capo").getIntValue()); working_.players.push_back (p); saveSet(); }), true); });
    for (int i = 0; i < (int) working_.players.size(); ++i) {
        auto& p = working_.players[(size_t) i]; juce::PopupMenu pm;
        juce::PopupMenu dc; for (int c = 0; c <= 7; ++c) dc.addItem (c == 0 ? juce::String ("No capo") : "Capo " + juce::String (c), true, p.capo == c, [this, i, c] { working_.players[(size_t) i].capo = c; saveSet(); });
        pm.addSubMenu ("Default capo", dc);
        for (auto& e : working_.entries) {
            auto* s = ed.core.store.lib.song (e.songId); if (! s) continue; std::string sid = e.songId; auto part = p.songs.count (sid) ? p.songs[sid] : PlayerPart();
            std::string sounding = e.keyOverride.empty() ? s->chart.key : e.keyOverride;
            juce::PopupMenu sm; const char* keys[] = { "C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" }; bool minor = ! sounding.empty() && sounding.back() == 'm';
            sm.addItem ("Setlist key (" + juce::String (sounding) + ")", true, part.key.empty(), [this, i, sid] { working_.players[(size_t) i].songs[sid].key.clear(); saveSet(); });
            juce::PopupMenu km; for (auto* k : keys) { std::string kk = std::string (k) + (minor ? "m" : ""); km.addItem (kk, true, part.key == kk, [this, i, sid, kk] { working_.players[(size_t) i].songs[sid].key = kk; saveSet(); }); }
            sm.addSubMenu ("Own key", km);
            juce::PopupMenu cp; cp.addItem ("Player default", true, part.capo < 0, [this, i, sid] { working_.players[(size_t) i].songs[sid].capo = -1; saveSet(); });
            for (auto& o : capoOptions (part.key.empty() ? sounding : part.key)) { int c = o.capo; cp.addItem (c == 0 ? juce::String ("No capo") : "Capo " + juce::String (c) + " - " + juce::String (o.shapes) + " shapes", true, part.capo == c, [this, i, sid, c] { working_.players[(size_t) i].songs[sid].capo = c; saveSet(); }); }
            sm.addSubMenu ("Capo", cp);
            pm.addSubMenu (juce::String (s->chart.title), sm);
        }
        pm.addSeparator(); pm.addItem ("Remove player", [this, i] { working_.players.erase (working_.players.begin() + i); saveSet(); });
        m.addSubMenu (juce::String (p.name) + " - " + juce::String (p.instrument), pm);
    }
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&players_));
}
void showBackupsMenu (CoreEditor& ed, juce::Component* anchor, std::function<void()> after) {
    juce::PopupMenu m; auto list = ed.core.store.backups();
    if (list.empty()) m.addItem ("No backups yet (made automatically as you edit)", false, false, [] {});
    for (auto& b : list) {
        auto t = juce::Time ((juce::int64) b.unixTime * 1000); auto info = b;
        m.addItem ("Restore " + t.formatted ("%a %d %b  %H:%M:%S") + "  (" + juce::String ((int) (b.bytes / 1024)) + " KB)", [&ed, info, after] {
            juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Restore backup", "Replace songs, setlists and rigs with this backup? The current state is backed up first, so you can undo this.", "Restore", "Cancel", nullptr,
                juce::ModalCallbackFunction::create ([&ed, info, after] (int r) { if (r != 1) return; if (ed.core.store.restoreBackup (info)) { ed.core.applySettings(); if (! ed.core.store.lib.setlists.empty()) ed.core.goLive (ed.core.store.lib.setlists.front().date, 0); if (after) after(); } })); });
    }
    m.addSeparator(); m.addItem ("Backups folder: " + ed.core.store.file().getParentDirectory().getChildFile ("backups").getFullPathName(), false, false, [] {});
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (anchor));
}
void SetlistView::backupsMenu() { showBackupsMenu (ed, &backups_, [this] { refresh(); }); }
