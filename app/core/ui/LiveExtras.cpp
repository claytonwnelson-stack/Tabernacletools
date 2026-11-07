#include "LiveExtras.h"
#include "model/Chart.h"
using namespace wr; using namespace wr::theme;

// ================================================================== arrangement editor
void ArrangementEditor::open (const Chart& written, std::vector<std::string> tokens, bool setlistScope, Done done, std::string songId) {
    written_ = written; written_.arrangement.clear(); setlistScope_ = setlistScope; done_ = std::move (done); songId_ = std::move (songId); coloursDirty_ = false; recolour();
    asWritten_ = tokens.empty();
    if (asWritten_) for (int i = 0; i < (int) written_.sections.size(); ++i) tokens.push_back (tokenFor (i));
    tok_ = std::move (tokens); dragFrom_ = -1; setVisible (true); toFront (true); repaint();
}
std::string ArrangementEditor::tokenFor (int i) const {   // label if unique, else the full name
    const auto& s = written_.sections[(size_t) i]; int same = 0; for (auto& o : written_.sections) if (o.label == s.label) ++same;
    return ! s.label.empty() && same == 1 ? s.label : s.name;
}
int ArrangementEditor::chipAt (juce::Point<int> p) const { for (int i = 0; i < (int) chips_.size(); ++i) if (chips_[(size_t) i].expanded (3).contains (p)) return i; return -1; }
void ArrangementEditor::paint (juce::Graphics& g) {
    g.fillAll (juce::Colours::black.withAlpha (0.6f));
    box_ = getLocalBounds().withSizeKeepingCentre (std::min (900, getWidth() - 40), std::min (520, getHeight() - 40));
    pill (g, box_.toFloat(), bar, cardBorder, 14);
    auto r = box_.reduced (22, 16);
    g.setColour (juce::Colours::white); g.setFont (font (18, true)); g.drawText ("ARRANGEMENT - " + juce::String (written_.title), r.removeFromTop (30), juce::Justification::centredLeft, true);
    label (g, "PLAY ORDER - drag to reorder, tap a section to duplicate or remove. Next / Prev, cues, auto-advance, projector and band sync follow this order.", r.removeFromTop (18), dimmer, 11);
    r.removeFromTop (8);
    chips_.clear(); int x = r.getX(), y = r.getY(); const int h = 36;
    for (int i = 0; i < (int) tok_.size(); ++i) {
        const int idx = resolveArrangementToken (written_, tok_[(size_t) i]); juce::String t (tok_[(size_t) i]);
        int w = (int) juce::GlyphArrangement::getStringWidth (font (14, true), t) + 30; if (x + w > r.getRight()) { x = r.getX(); y += h + 8; }
        juce::Rectangle<int> c (x, y, w, h); chips_.push_back (c); x += w + 8;
        const bool bad = idx < 0, drag = i == dragFrom_ && dragged_;
        auto col = bad ? juce::Colour (0xffff4d4d) : accent (juce::String (written_.sections[(size_t) idx].color));
        pill (g, c.toFloat(), drag ? col.withAlpha (0.35f) : juce::Colour (0xff1d2230), col, 10);
        g.setColour (bad ? juce::Colour (0xffff8080) : text); g.setFont (font (14, true)); g.drawText (t, c, juce::Justification::centred);
        g.setColour (dimmer); g.setFont (font (9, true)); g.drawText (juce::String (i + 1), c.withTrimmedLeft (4).withHeight (12), juce::Justification::topLeft);
    }
    if (dragged_ && dragTo_ >= 0 && dragTo_ < (int) chips_.size()) { auto c = chips_[(size_t) dragTo_]; g.setColour (pink); g.fillRect (c.getX() - 6, c.getY(), 3, c.getHeight()); }
    if (tok_.empty()) { g.setColour (dim); g.setFont (font (14)); g.drawText ("Empty - add sections below (or tap WRITTEN ORDER)", r.withY (y).withHeight (h), juce::Justification::centredLeft); }
    r.setTop (y + h + 22);
    label (g, "ADD A SECTION (tap to append)", r.removeFromTop (18), dimmer, 11); r.removeFromTop (6);
    palette_.clear(); x = r.getX(); y = r.getY();
    for (int i = 0; i < (int) written_.sections.size(); ++i) {
        juce::String t = juce::String (written_.sections[(size_t) i].name) + "  (" + juce::String (tokenFor (i)) + ")";
        int w = (int) juce::GlyphArrangement::getStringWidth (font (13), t) + 26; if (x + w > r.getRight()) { x = r.getX(); y += 40; }
        juce::Rectangle<int> c (x, y, w, 32); palette_.push_back ({ c, i }); x += w + 8;
        pill (g, c.toFloat(), btn, btnBorder, 8); g.setColour (text); g.setFont (font (13)); g.drawText (t, c, juce::Justification::centred);
    }
    // v0.8 section colours: tap a swatch to pick (palette / custom / default); RESET COLOURS = back to the kind defaults
    swatches_.clear(); resetColours_ = {};
    if (! songId_.empty() && onColours) {
        r.setTop (y + 32 + 18);
        auto hdrR = r.removeFromTop (18); label (g, "SECTION COLOURS (this song - tap to change; defaults per section type in AUDIO/MIDI > Section colours)", hdrR, dimmer, 11);
        resetColours_ = juce::Rectangle<int> (hdrR.getRight() - 130, hdrR.getY() - 4, 130, 24);
        pill (g, resetColours_.toFloat(), btn, btnBorder, 8); g.setColour (text); g.setFont (font (11, true)); g.drawText ("RESET COLOURS", resetColours_, juce::Justification::centred);
        r.removeFromTop (8); x = r.getX(); y = r.getY();
        for (int i = 0; i < (int) written_.sections.size(); ++i) {
            const auto& sec = written_.sections[(size_t) i]; juce::String t (tokenFor (i)); const auto col = accent (juce::String (sec.color));
            int w = (int) juce::GlyphArrangement::getStringWidth (font (13, true), t) + 40; if (x + w > r.getRight()) { x = r.getX(); y += 38; }
            juce::Rectangle<int> c (x, y, w, 30); swatches_.push_back ({ c, i }); x += w + 8;
            pill (g, c.toFloat(), col, sec.customColor.empty() ? col : juce::Colours::white, 8); g.setColour (inkOn (col)); g.setFont (font (13, true));
            g.drawText (t + (sec.customColor.empty() ? "" : " *"), c, juce::Justification::centred);
        }
    }
    // buttons
    buttons_.clear(); auto row = box_.reduced (22, 16).removeFromBottom (40); const char* names[] = { "CANCEL", "WRITTEN ORDER", "SAVE TO SONG", "SAVE FOR THIS SETLIST" };
    int bx = row.getRight();
    for (int i = 3; i >= 0; --i) { if (i == 3 && ! setlistScope_) continue; int w = (int) juce::GlyphArrangement::getStringWidth (font (13, true), names[i]) + 30; bx -= w; juce::Rectangle<int> b (bx, row.getY(), w, 40); bx -= 10;
        const bool primary = i >= 2; pill (g, b.toFloat(), primary ? orange : btn, primary ? orange : btnBorder, 8); g.setColour (primary ? juce::Colour (0xff111111) : text); g.setFont (font (13, true)); g.drawText (names[i], b, juce::Justification::centred); buttons_.push_back ({ b, i }); }
    g.setColour (dim); g.setFont (font (12)); g.drawText ("ChordPro: {arrangement: " + juce::String (formatArrangement (tok_)) + "}", row.withRight (bx), juce::Justification::centredLeft, true);
}
void ArrangementEditor::mouseDown (const juce::MouseEvent& e) { dragFrom_ = chipAt (e.getPosition()); dragTo_ = dragFrom_; dragged_ = false; }
void ArrangementEditor::mouseDrag (const juce::MouseEvent& e) {
    if (dragFrom_ < 0 || e.getDistanceFromDragStart() < 8) return; dragged_ = true;
    int best = -1; double bd = 1e9; for (int i = 0; i < (int) chips_.size(); ++i) { double d = chips_[(size_t) i].getCentre().getDistanceFrom (e.getPosition()); if (d < bd) { bd = d; best = i; } }
    if (best >= 0 && e.x > chips_[(size_t) best].getCentreX()) ++best; dragTo_ = best; repaint();
}
void ArrangementEditor::mouseUp (const juce::MouseEvent& e) {
    auto p = e.getPosition();
    if (dragged_ && dragFrom_ >= 0) { int to = std::clamp (dragTo_, 0, (int) tok_.size()); auto t = tok_[(size_t) dragFrom_]; tok_.erase (tok_.begin() + dragFrom_); if (to > dragFrom_) --to;
        tok_.insert (tok_.begin() + to, t); asWritten_ = false; dragFrom_ = -1; dragged_ = false; repaint(); return; }
    dragFrom_ = -1;
    if (! box_.contains (p)) return;
    for (auto& b : buttons_) if (b.first.contains (p)) {
        if (b.second == 0) { setVisible (false); return; }
        if (b.second == 1) { tok_.clear(); for (int i = 0; i < (int) written_.sections.size(); ++i) tok_.push_back (tokenFor (i)); asWritten_ = true; repaint(); return; }
        std::vector<std::string> out; if (! asWritten_) for (auto& t : tok_) if (resolveArrangementToken (written_, t) >= 0) out.push_back (t);
        if (coloursDirty_ && onColours && ! songId_.empty()) { std::vector<std::string> cc; for (auto& sec : written_.sections) cc.push_back (sec.customColor); onColours (songId_, cc); }
        setVisible (false); if (done_) done_ (out, b.second == 3 ? 1 : 0); return;
    }
    if (! resetColours_.isEmpty() && resetColours_.contains (p)) { clearSectionColorOverrides (written_); recolour(); coloursDirty_ = true; repaint(); return; }
    for (auto& sw : swatches_) if (sw.first.contains (p)) {
        const int i = sw.second; const auto& sec = written_.sections[(size_t) i];
        const auto def = kindColor (defaults ? *defaults : SectionColorMap(), sec.kind);
        juce::Component::SafePointer<ArrangementEditor> self (this);
        wr::ui::pickSectionColour (this, sw.first, sec.customColor.empty() ? def : sec.customColor,
            "Use " + juce::String (kindDisplayName (sec.kind)) + " default (" + wr::ui::colourName (def) + ")", [self, i] (std::string c) {
                if (! self) return; setSectionColorOverride (self->written_, i, c); self->recolour(); self->coloursDirty_ = true; self->repaint(); });
        return; }
    for (auto& pc : palette_) if (pc.first.contains (p)) { tok_.push_back (tokenFor (pc.second)); asWritten_ = false; repaint(); return; }
    const int c = chipAt (p); if (c < 0) return;
    juce::PopupMenu m;
    m.addItem ("Duplicate (play it again right after)", [this, c] { tok_.insert (tok_.begin() + c + 1, tok_[(size_t) c]); asWritten_ = false; repaint(); });
    m.addItem ("Move earlier", c > 0, false, [this, c] { std::swap (tok_[(size_t) c], tok_[(size_t) c - 1]); asWritten_ = false; repaint(); });
    m.addItem ("Move later", c + 1 < (int) tok_.size(), false, [this, c] { std::swap (tok_[(size_t) c], tok_[(size_t) c + 1]); asWritten_ = false; repaint(); });
    m.addItem ("Remove", [this, c] { tok_.erase (tok_.begin() + c); asWritten_ = false; repaint(); });
    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (chips_[(size_t) c])));
}

// ================================================================== practice bar
static juce::String mmss (double s) { if (s < 0) s = 0; int t = (int) s; return juce::String (t / 60) + ":" + juce::String (t % 60).paddedLeft ('0', 2); }
void PracticeBar::paint (juce::Graphics& g) {
    auto& P = ed.core; auto& pl = P.practice; btn_.clear();
    g.fillAll (juce::Colour (0xff121a24)); g.setColour (blue.withAlpha (0.6f)); g.drawHorizontalLine (0, 0, (float) getWidth());
    auto r = getLocalBounds().reduced (10, 8); int x = r.getX();
    auto button = [&] (const juce::String& t, int id, bool lit = false, int w = 0) { if (w == 0) w = (int) juce::GlyphArrangement::getStringWidth (font (12, true), t) + 22;
        juce::Rectangle<int> b (x, r.getY(), w, r.getHeight()); pill (g, b.toFloat(), lit ? blue.withAlpha (0.25f) : btn, lit ? blue : btnBorder, 8);
        g.setColour (lit ? blue : text); g.setFont (font (12, true)); g.drawText (t, b, juce::Justification::centred); btn_.push_back ({ b, id }); x += w + 6; return b; };
    g.setColour (blue); g.setFont (font (11, true)); g.drawText ("PRACTICE", x, r.getY(), 70, r.getHeight(), juce::Justification::centredLeft); x += 74;
    if (! pl.hasTrack()) { button ("ATTACH PRACTICE TRACK (mp3 / m4a / wav)...", 20); g.setColour (dim); g.setFont (font (12));
        g.drawText ("Practice tracks play on outs 1/2. Not used while following a leader.", x + 6, r.getY(), r.getRight() - x - 70, r.getHeight(), juce::Justification::centredLeft, true); }
    else {
        button (pl.playing() ? "PAUSE" : "PLAY", 1, pl.playing(), 70); button ("-5s", 2); button ("+5s", 3);
        g.setColour (text); g.setFont (mono (13, true)); g.drawText (mmss (pl.position()) + " / " + mmss (pl.length()), x, r.getY(), 100, r.getHeight(), juce::Justification::centred); x += 104;
        button ("SPEED " + juce::String ((int) std::lround (pl.speed() * 100)) + "%", 4, std::abs (pl.speed() - 1.0) > 0.01);
        button ("LOOP SECTION", 5, pl.looping()); button ("MARK SECTION HERE", 6); button ("MARKS...", 7);
        auto* s = P.practiceSong(); int marked = 0; if (s) for (double m : s->practice.marks) if (m >= 0) ++marked;
        g.setColour (dim); g.setFont (font (11)); g.drawText (juce::String (marked) + "/" + juce::String ((int) P.live().chart().sections.size()) + " marked", x, r.getY(), 90, r.getHeight(), juce::Justification::centredLeft); x += 90;
    }
    x = r.getRight() - 40; button ("X", 9, false, 40);
}
void PracticeBar::mouseUp (const juce::MouseEvent& e) {
    auto& P = ed.core; auto& pl = P.practice;
    for (auto& b : btn_) if (b.first.contains (e.getPosition())) {
        switch (b.second) {
            case 1: pl.play (! pl.playing()); break;
            case 2: pl.seek (pl.position() - 5); break;
            case 3: pl.seek (pl.position() + 5); break;
            case 4: { juce::PopupMenu m; for (int pc : { 50, 60, 70, 75, 80, 85, 90, 95, 100, 105, 110, 120 }) m.addItem (juce::String (pc) + "%" + (pc == 100 ? "  (original)" : ""), true, (int) std::lround (pl.speed() * 100) == pc, [&pl, pc] { pl.setSpeed (pc / 100.0); });
                      m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (b.first))); break; }
            case 5: P.loopCurrentSection (! pl.looping()); break;
            case 6: P.markSectionHere(); break;
            case 7: { juce::PopupMenu m; auto* s = P.practiceSong(); const auto& c = P.live().chart();
                      m.addItem ("Auto: every section from bars x tempo, first one starting now (" + mmss (pl.position()) + ")", [&P] { P.autoMarks(); });
                      m.addSeparator();
                      for (int i = 0; s && i < (int) c.sections.size(); ++i) { const double t = i < (int) s->practice.marks.size() ? s->practice.marks[(size_t) i] : -1.0;
                          juce::PopupMenu sub; sub.addItem ("Jump there", t >= 0, false, [&pl, t] { pl.seek (t); });
                          sub.addItem ("Clear this marker", t >= 0, false, [&P, i] { if (auto* so = P.practiceSong()) if (i < (int) so->practice.marks.size()) { so->practice.marks[(size_t) i] = -1; P.store.saveSoon(); } });
                          m.addSubMenu (juce::String (i + 1) + ". " + juce::String (c.sections[(size_t) i].name) + "   " + (t >= 0 ? mmss (t) : juce::String ("not marked")), sub); }
                      m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (b.first))); break; }
            case 9: P.setPracticeMode (false); break;
            case 20: ed.choosePracticeTrack(); break;
            default: break;
        }
        repaint(); return;
    }
}

// ================================================================== next song card + notes banner
namespace wr::ui {
void paintNextSongCard (juce::Graphics& g, juce::Rectangle<float> a, const NextSongInfo& n, bool ended) {
    pill (g, a, juce::Colour (0xf0141c2a), blue, 14);
    auto r = a.reduced (20, 12);
    g.setColour (blue); g.setFont (font (13, true)); g.drawText (ended ? "NEXT SONG" : "NEXT SONG - after this section", r.removeFromTop (18), juce::Justification::centredLeft);
    g.setColour (juce::Colours::white); g.setFont (font (34, true)); g.drawText (juce::String (n.title), r.removeFromTop (44), juce::Justification::centredLeft, true);
    auto row = r.removeFromTop (std::min (56.0f, r.getHeight()));
    auto stat = [&] (const juce::String& k, const juce::String& v, float w) { auto b = row.removeFromLeft (w); g.setColour (dim); g.setFont (font (11, true)); g.drawText (k, b.removeFromTop (16), juce::Justification::centredLeft);
        g.setColour (text); g.setFont (font (24, true)); g.drawText (v, b, juce::Justification::centredLeft, true); row.removeFromLeft (14); };
    const float w = row.getWidth() / 5.2f;
    stat ("KEY", juce::String (n.key.empty() ? "-" : n.key), w * 0.8f);
    stat ("CAPO", n.capo > 0 ? juce::String (n.capo) + " (" + juce::String (shapesKey (n.key, n.capo)) + " shapes)" : juce::String ("none"), w * 1.5f);
    stat ("TEMPO", n.bpm > 0 ? juce::String (formatBpm (n.bpm)) : juce::String ("-"), w * 0.8f);
    stat ("TIME", juce::String (n.time), w * 0.7f);
    stat ("FIRST CHORD", juce::String (n.firstChord.empty() ? "-" : n.firstChord), w);
}
void paintNotesBanner (juce::Graphics& g, juce::Rectangle<float> a, const std::vector<ServiceNote>& notes) {
    if (notes.empty()) return; juce::String t; for (auto& n : notes) { if (t.isNotEmpty()) t << "   |   "; t << juce::String (n.text); }
    g.setColour (juce::Colour (0xfff5d524)); g.fillRoundedRectangle (a, 8); g.setColour (juce::Colour (0xff111111)); g.setFont (font (15, true));
    g.drawText ("NOTE: " + t, a.reduced (12, 0), juce::Justification::centredLeft, true);
}
juce::String noteWhenText (const std::string& at) {
    if (at == "start" || at.empty()) return "when the song starts"; if (at == "end") return "last section + after"; if (at == "after") return "after the song";
    return "during " + juce::String (at);
}
void editServiceNotes (std::vector<ServiceNote> notes, const Chart& written, juce::Component* anchor, std::function<void (std::vector<ServiceNote>)> done) {
    juce::PopupMenu m;
    m.addItem ("Add note...", [notes, written, done] {
        auto* w = new juce::AlertWindow ("Service note", "Shown on the Live screen (and the band's iPads) at the right moment", juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("text", "", "Note"); juce::StringArray when { "When the song starts", "Last section + after the song", "After the song (gap)" };
        for (auto& s : written.sections) when.add ("During " + juce::String (s.name)); w->addComboBox ("when", when, "When"); w->getComboBoxComponent ("when")->setSelectedItemIndex (2);
        w->addButton ("Add", 1, juce::KeyPress (juce::KeyPress::returnKey)); w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create ([w, notes, written, done] (int r) {
            if (r != 1) return; auto t = w->getTextEditorContents ("text").trim(); if (t.isEmpty()) return; const int i = w->getComboBoxComponent ("when")->getSelectedItemIndex();
            std::string at = i == 0 ? "start" : i == 1 ? "end" : i == 2 ? "after" : written.sections[(size_t) (i - 3)].name;
            auto n = notes; n.push_back ({ t.toStdString(), at }); done (n); }), true); });
    if (! notes.empty()) m.addSeparator();
    for (size_t i = 0; i < notes.size(); ++i) m.addItem ("Remove: \"" + juce::String (notes[i].text) + "\"  (" + noteWhenText (notes[i].at) + ")", [notes, i, done] { auto n = notes; n.erase (n.begin() + (long) i); done (n); });
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (anchor));
}
void showSongMenu (CoreEditor& ed, juce::Rectangle<int> anchor) {
    auto& P = ed.core; auto& L = P.live(); const Song* s = L.song(); if (! s) return;
    const bool following = L.following(); juce::PopupMenu m;
    m.addItem ("Arrangement...  (" + juce::String (L.arrangement().empty() ? std::string ("as written") : formatArrangement (L.arrangement())) + ")", ! following, false, [&ed] { ed.openLiveArrangement(); });
    const auto& notes = L.setlist().entries.empty() ? std::vector<ServiceNote>() : L.setlist().entries[(size_t) L.songIndex()].notes;
    m.addItem ("Service notes for this song... (" + juce::String ((int) notes.size()) + ")", ! following, false, [&ed, notes] {
        auto* so = ed.core.live().song(); if (! so) return; editServiceNotes (notes, so->chart, &ed, [&ed] (std::vector<ServiceNote> n) { ed.core.live().setNotes (n); ed.core.store.save(); }); });
    if (following) m.addItem ("(Arrangement and notes come from the leader)", false, false, [] {});
    m.addSeparator();
    m.addItem ("Practice mode (play along with a recording)", ! following, P.practiceMode(), [&P] { P.setPracticeMode (! P.practiceMode()); });
    m.addItem (s->practice.file.empty() ? juce::String ("Attach practice track...") : "Practice track: " + juce::File (s->practice.file).getFileName() + " - replace...", [&ed] { ed.choosePracticeTrack(); });
    m.addItem ("CCLI details..." + juce::String (s->ccli.publicDomain ? "  (public domain)" : s->ccli.number.empty() ? "  (no CCLI #)" : "  (#" + juce::String (s->ccli.number) + ")"), ! following, false, [&P] {
        auto* so = P.tracksSong(); if (! so) return; auto id = so->id;
        auto* w = new juce::AlertWindow ("CCLI details", "Optional. Used only in the usage export you enter on CCLI's site.", juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("num", juce::String (so->ccli.number), "CCLI song #"); w->addTextEditor ("title", juce::String (so->ccli.title.empty() ? so->chart.title : so->ccli.title), "Title");
        w->addTextEditor ("auth", juce::String (so->ccli.authors), "Authors"); w->addComboBox ("pd", { "Not public domain", "Public domain (left out of exports)" }, "");
        w->getComboBoxComponent ("pd")->setSelectedItemIndex (so->ccli.publicDomain ? 1 : 0);
        w->addButton ("Save", 1); w->addButton ("Cancel", 0);
        w->enterModalState (true, juce::ModalCallbackFunction::create ([&P, w, id] (int r) { if (r != 1) return; auto* x = P.store.lib.song (id); if (! x) return;
            x->ccli.number = w->getTextEditorContents ("num").trim().retainCharacters ("0123456789").toStdString(); x->ccli.title = w->getTextEditorContents ("title").trim().toStdString();
            x->ccli.authors = w->getTextEditorContents ("auth").trim().toStdString(); x->ccli.publicDomain = w->getComboBoxComponent ("pd")->getSelectedItemIndex() == 1; P.store.save(); }), true); });
    m.addSeparator();   // v0.14 backing tracks
    m.addItem (s->multitrack.empty() ? juce::String ("Backing tracks: import stems...") : "Backing tracks: " + juce::String ((int) s->multitrack.stems.size()) + " stems - add more...", ! following, false, [&ed] { ed.wantTracksBar = true; ed.chooseStems(); });
    m.addItem ("Show backing-tracks bar", ! following, ed.wantTracksBar, [&ed] { ed.wantTracksBar = ! ed.wantTracksBar; });
    if (! s->multitrack.empty()) m.addItem ("Remove backing tracks from this song", ! following, false, [&P] { if (auto* so = P.tracksSong()) {
        for (auto& st : so->multitrack.stems) juce::File (st.file).deleteFile(); so->multitrack = mt::MultitrackSet(); P.store.save(); P.reloadTracks(); } });
    if (! s->practice.file.empty()) m.addItem ("Remove practice track", [&P] { if (auto* so = P.practiceSong()) { so->practice = PracticeTrack(); P.store.save(); P.loadPracticeTrack(); } });
    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (ed.localAreaToGlobal (anchor)));
}
} // namespace wr::ui

// ================================================================== v0.8 section colour picker + settings
namespace {
// full colour wheel in a call-out; the colour is delivered when the call-out closes
class CustomColourPick : public juce::Component, private juce::ChangeListener {
public:
    CustomColourPick (juce::Colour start, std::function<void (std::string)> done) : done_ (std::move (done)) {
        sel_.setCurrentColour (start, juce::dontSendNotification); sel_.addChangeListener (this); addAndMakeVisible (sel_); setSize (300, 320); }
    ~CustomColourPick() override { sel_.removeChangeListener (this); if (changed_ && done_) done_ ("#" + sel_.getCurrentColour().withAlpha (1.0f).toDisplayString (false).toStdString()); }
    void resized() override { sel_.setBounds (getLocalBounds()); }
private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { changed_ = true; }
    juce::ColourSelector sel_ { juce::ColourSelector::showColourspace | juce::ColourSelector::showSliders | juce::ColourSelector::showColourAtTop };
    std::function<void (std::string)> done_; bool changed_ = false;
};
}
juce::String wr::ui::colourName (const std::string& c) {
    auto n = wr::normalizeColor (c); for (auto& p : wr::sectionPalette()) if (n == p.name) return p.label; return juce::String (n.empty() ? std::string ("Grey") : n);
}
void wr::ui::pickSectionColour (juce::Component* anchor, juce::Rectangle<int> local, const std::string& current, const juce::String& resetLabel, std::function<void (std::string)> done) {
    const auto cur = wr::normalizeColor (current); juce::PopupMenu m; const auto& pal = wr::sectionPalette();
    m.addSectionHeader ("Section colour");
    for (int i = 0; i < (int) pal.size(); ++i) m.addColouredItem (i + 1, juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xa0  ")) + pal[(size_t) i].label, juce::Colour (pal[(size_t) i].argb), true, cur == pal[(size_t) i].name);
    m.addSeparator();
    m.addItem (100, "Custom colour..." + (cur.size() == 7 && cur[0] == '#' ? "  (" + juce::String (cur) + ")" : juce::String()), true, cur.size() == 7 && cur[0] == '#');
    if (resetLabel.isNotEmpty()) m.addItem (101, resetLabel);
    juce::Component::SafePointer<juce::Component> sp (anchor); const auto screen = anchor->localAreaToGlobal (local);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (screen), [sp, local, cur, done] (int r) {
        if (r <= 0 || ! sp) return; const auto& pal2 = wr::sectionPalette();
        if (r >= 1 && r <= (int) pal2.size()) { done (pal2[(size_t) r - 1].name); return; }
        if (r == 101) { done (""); return; }
        if (r == 100) juce::CallOutBox::launchAsynchronously (std::make_unique<CustomColourPick> (wr::theme::colourOf (cur), done), local, sp.getComponent());
    });
}
void wr::ui::addSectionColourSettings (CoreEditor& ed, juce::PopupMenu& m, juce::Component* anchor) {
    juce::PopupMenu sc; auto& lib = ed.core.store.lib; CoreEditor* e = &ed;
    sc.addSectionHeader ("Default colour per section type (songs can override in Arrangement)");
    for (auto& kind : wr::sectionKinds()) {
        const auto c = wr::kindColor (lib.settings.sectionColors, kind);
        juce::PopupMenu k; const auto& pal = wr::sectionPalette();
        for (auto& p : pal) { std::string nm = p.name; juce::PopupMenu::Item it (juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xa0  ")) + p.label);
            it.setColour (juce::Colour (p.argb)).setTicked (c == nm).setAction ([e, kind, nm] { e->core.store.lib.settings.sectionColors[kind] = nm; e->core.live().refreshChart(); e->core.store.save(); e->repaint(); });
            k.addItem (std::move (it)); }
        k.addSeparator();
        juce::Component::SafePointer<juce::Component> sp (anchor);
        k.addItem ("Custom colour..." + (c[0] == '#' ? "  (" + juce::String (c) + ")" : juce::String()), true, c[0] == '#', [e, kind, c, sp] {
            if (! sp) return;
            juce::CallOutBox::launchAsynchronously (std::make_unique<CustomColourPick> (wr::theme::colourOf (c), [e, kind] (std::string hex) {
                e->core.store.lib.settings.sectionColors[kind] = hex; e->core.live().refreshChart(); e->core.store.save(); e->repaint(); }), sp->getLocalBounds(), sp.getComponent()); });
        k.addItem ("Reset to default (" + wr::ui::colourName (wr::builtinKindColor (kind)) + ")", lib.settings.sectionColors.count (kind) > 0, false, [e, kind] {
            e->core.store.lib.settings.sectionColors.erase (kind); e->core.live().refreshChart(); e->core.store.save(); e->repaint(); });
        sc.addSubMenu (juce::String (wr::kindDisplayName (kind)) + "  -  " + wr::ui::colourName (c), k, true, nullptr, false, 0);
    }
    sc.addSeparator();
    sc.addItem ("Reset all section types to defaults", ! lib.settings.sectionColors.empty(), false, [e] { e->core.store.lib.settings.sectionColors.clear(); e->core.live().refreshChart(); e->core.store.save(); e->repaint(); });
    sc.addItem ("Clear this song's colour overrides", ed.core.live().song() != nullptr, false, [e] {
        if (auto* s = e->core.live().song()) if (auto* so = e->core.store.lib.song (s->id)) { wr::clearSectionColorOverrides (so->chart); wr::applySectionColors (so->chart, {}); e->core.live().refreshChart(); e->core.store.save(); e->repaint(); } });
    m.addSubMenu ("Section colours", sc);
}
