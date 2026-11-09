#include "model/StageTools.h"
#include "TopBar.h"
#include "BandPanel.h"
#include "LiveExtras.h"
using namespace wr; using namespace wr::theme;

void TopBar::paint (juce::Graphics& g, juce::Rectangle<int> r, int rightReserve) {
    g.saveState(); g.reduceClipRegion (r);
    auto& L = ed.core.live(); const Song* s = L.song(); const Chart& c = L.chart();
    auto x = r.getX();
    g.setColour (dim); g.setFont (font (13)); g.drawText ("Setlist - " + juce::String (L.songIndex() + 1) + " of " + juce::String ((int) L.setlist().entries.size()), x, 12, 240, 16, juce::Justification::left);
    g.setColour (juce::Colours::white); g.setFont (font (16, true)); g.drawText (s ? juce::String (c.title) : juce::String ("No song - build a setlist"), x, 29, 240, 20, juce::Justification::left, true);
    titleR_ = { r.getX(), 8, 240, 48 };
    x += 248;
    auto badge = [&] (const juce::String& k, const juce::String& v, int w) { juce::Rectangle<int> b (x, 15, w, 34); pill (g, b.toFloat(), btn, btnBorder, 8); g.setColour (dim); g.setFont (font (11));
        g.drawText (k, b.withTrimmedLeft (9), juce::Justification::centredLeft); g.setColour (text); g.setFont (font (14, true)); g.drawText (v, b.withTrimmedRight (9), juce::Justification::centredRight); x += w + 8; return b; };
    keyR_ = badge ("KEY", c.key.empty() ? juce::String ("-") : juce::String (c.key) + (c.capo > 0 ? " c" + juce::String (c.capo) : juce::String()), c.capo > 0 ? 92 : 70);
    { // tempo: [-] BPM [+] TAP. Keep the cluster inside the bar so it cannot cover the tuner or the settings button.
        const int cluster = 30 + 4 + 84 + 4 + 30 + 4 + 44 + 8;
        const int limit = r.getRight() - rightReserve;
        if (x + cluster > limit) x = std::max (r.getX() + 160, limit - cluster);
        const bool clk = L.clockActive(); const bool clkSend = ed.core.store.lib.settings.midiClockMode == 1 && ! L.following();
        auto small = [&] (const juce::String& t, int w, juce::Colour fill, juce::Colour border, juce::Colour fg) {
            juce::Rectangle<int> b (x, 15, w, 34); pill (g, b.toFloat(), fill, border, 8); g.setColour (fg); g.setFont (font (13, true)); g.drawText (t, b, juce::Justification::centred); x += w + 4; return b; };
        bpmMinusR_ = small ("-", 30, btn, btnBorder, clk ? dimmer : text);
        double b = L.bpm(); juce::String v = std::abs (b - std::round (b)) < 0.05 ? juce::String ((int) std::round (b)) : juce::String (b, 1);
        bpmR_ = badge ("BPM", v, 84); x -= 4;
        bpmPlusR_ = small ("+", 30, btn, btnBorder, clk ? dimmer : text);
        tapR_ = small ("TAP", 44, btn, btnBorder, text);
        tempoSaveR_ = {};
        if (clk) small ("MIDI CLOCK", 86, card, accentText, accentText);
        else if (clkSend && L.playing()) small ("MIDI OUT", 72, card, accentText, accentText);
        else if (L.tempoEdited()) tempoSaveR_ = small ("SAVE " + juce::String ((int) std::round (L.storedSongBpm())) + juce::String::fromUTF8 ("→") + v, 96, accentFill, accentFill, onAccent);
        x += 4;
    }
    bandR_ = {};
    if (ed.core.sync().mode() == BandSync::Mode::Leading) { // BAND n + one dot per follower (green / yellow / red)
        auto ms = ed.core.sync().roster().members(); const int n = ed.core.sync().roster().connectedCount(), w = 62 + 10 * std::min (8, (int) ms.size());
        bandR_ = { x, 15, w, 34 }; pill (g, bandR_.toFloat(), btn, btnBorder, 8); g.setColour (text); g.setFont (font (12, true));
        g.drawText ("BAND " + juce::String (n), bandR_.withTrimmedLeft (9).withWidth (54), juce::Justification::centredLeft);
        for (int i = 0; i < std::min (8, (int) ms.size()); ++i) { g.setColour (BandPanel::statusColour (ms[(size_t) i].status)); g.fillEllipse ((float) bandR_.getX() + 60 + i * 10, (float) bandR_.getCentreY() - 3.5f, 7, 7); }
        x += w + 8;
    }
    chipR_.clear();
    for (int i = 0; i < (int) c.sections.size(); ++i) {
        auto& sec = c.sections[(size_t) i]; juce::String t (sec.label); int w = (int) juce::GlyphArrangement::getStringWidth (font (12, true), t) + 18;
        if (x + w > r.getRight() - rightReserve) break;
        juce::Rectangle<int> b (x, 20, w, 24); bool cur = i == L.sectionIndex(), nx = i == L.sectionIndex() + 1, done = i < L.sectionIndex();
        const auto sc = accent (juce::String (sec.color));   // v0.8 section colour: current = filled, next = bold ring, others = thin ring
        pill (g, b.toFloat(), cur ? sc : card, cur || nx ? sc : sc.withAlpha (done ? 0.35f : 0.6f), 12);
        if (nx) { g.setColour (sc); g.drawRoundedRectangle (b.toFloat().reduced (1), 12, 2.5f); }
        g.setColour (cur ? inkOn (sc) : nx ? sc.brighter (0.3f) : done ? juce::Colour (0xff9aa3b8) : dimmer); g.setFont (font (12, true)); g.drawText (t, b, juce::Justification::centred);
        chipR_.push_back ({ b, i }); x += w + 4;
    }
    g.restoreState();
}

void TopBar::click (juce::Point<int> p) {
    auto& L = ed.core.live();
    if (bandR_.contains (p)) { ed.showBandPanel(); return; }
    if (titleR_.contains (p)) { wr::ui::showSongMenu (ed, titleR_); return; }   // v0.7 arrangement / notes / practice
    if (keyR_.contains (p)) { // transpose: per-setlist key override
        juce::PopupMenu m; const char* keys[] = { "C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
        const Song* s = L.song(); std::string orig = s ? s->chart.key : std::string(); bool minor = ! orig.empty() && orig.back() == 'm';
        for (auto* k : keys) { std::string key = std::string (k) + (minor ? "m" : ""); m.addItem (juce::String (key) + (key == orig ? "  (original)" : ""), true, key == L.key(), [this, key, orig] {
            auto& LL = ed.core.live(); LL.setKeyOverride (key == orig ? std::string() : key); ed.core.store.save(); ed.core.syncLocalKey(); }); }
        // capo: chart shows shapes; suggestions for the sounding key
        juce::PopupMenu cm; cm.addItem ("Song default (capo " + juce::String (s ? s->chart.capo : 0) + ")", [this] { ed.core.live().setCapoOverride (-1); ed.core.syncLocalKey(); ed.core.store.saveSoon(); });
        cm.addItem ("No capo", true, L.capo() == 0, [this] { ed.core.live().setCapoOverride (0); ed.core.syncLocalKey(); ed.core.store.saveSoon(); });
        // v0.8 capo helper: positions ranked by how many open shapes you'd play, with the shapes listed
        if (s) { cm.addSectionHeader ("Capo helper - shapes for " + juce::String (L.key()));
          for (auto& h : capoHelper (s->chart, L.key())) if (h.capo > 0) { int cp = h.capo; cm.addItem (juce::String (shapesText (h)) + "  (" + juce::String (h.openShapes) + "/" + juce::String ((int) h.shapes.size()) + " open)", true, L.capo() == cp, [this, cp] { ed.core.live().setCapoOverride (cp); ed.core.syncLocalKey(); ed.core.store.saveSoon(); }); } }
        m.addSeparator(); m.addSubMenu ("Capo (" + juce::String (L.capoText()) + ")", cm);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (ed.localAreaToGlobal (keyR_)));
        return;
    }
    if (bpmMinusR_.contains (p)) { L.nudgeBpm (-1); return; }
    if (bpmPlusR_.contains (p)) { L.nudgeBpm (+1); return; }
    if (tapR_.contains (p)) { L.tapTempo (ed.core.nowSec()); return; }
    if (tempoSaveR_.contains (p)) { if (L.saveTempoToSong()) ed.core.store.save(); return; }
    if (bpmR_.contains (p)) { // type an exact tempo, save, or revert
        juce::PopupMenu m;
        if (L.clockActive()) m.addItem ("Following MIDI clock (" + juce::String (L.clockBpm(), 1) + " BPM) - song tempo " + juce::String (L.songBpm(), 0) + " resumes when it stops", false, false, [] {});
        else if (ed.core.store.lib.settings.midiClockMode == 1) m.addItem ("Sending MIDI clock (" + juce::String (L.bpm(), 1) + " BPM)", false, false, [] {});
        m.addItem ("Type tempo...", [this] {
            auto* w = new juce::AlertWindow ("Song tempo", "BPM (30-300)", juce::MessageBoxIconType::NoIcon);
            w->addTextEditor ("bpm", juce::String (ed.core.live().songBpm(), 0)); w->getTextEditor ("bpm")->setInputRestrictions (5, "0123456789.");
            w->addButton ("Set", 1, juce::KeyPress (juce::KeyPress::returnKey)); w->addButton ("Set + save to song", 2); w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int r) {
                if (r == 0) return; double b = w->getTextEditorContents ("bpm").getDoubleValue(); if (b <= 0) return;
                auto& LL = ed.core.live(); LL.setBpm (b); if (r == 2 && LL.saveTempoToSong()) ed.core.store.save(); }), true); });
        m.addItem ("Save " + juce::String (L.songBpm(), 0) + " BPM to this song", L.tempoEdited(), false, [this] { if (ed.core.live().saveTempoToSong()) ed.core.store.save(); });
        m.addItem ("Revert to song tempo (" + juce::String (L.storedSongBpm(), 0) + ")", L.tempoEdited(), false, [this] { auto& LL = ed.core.live(); LL.setBpm (LL.storedSongBpm()); });
        m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (ed.localAreaToGlobal (bpmR_)));
        return;
    }
    for (auto& c : chipR_) if (c.first.contains (p)) { L.gotoSection (c.second, ed.core.nowSec()); return; }
}

