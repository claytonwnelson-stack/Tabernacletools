#include "LiveExtras.h"
#include "model/Chart.h"
#include "Platform.h"
using namespace wr; using namespace wr::theme;

// ================================================================== song editor
SongEditor::SongEditor() {
    addAndMakeVisible (name_); addAndMakeVisible (body_);
    body_.setMultiLine (true, true); body_.setReturnKeyStartsNewLine (true);
    name_.setFont (font (18)); body_.setFont (font (16));
    for (auto* t : { &name_, &body_ }) {
        t->setColour (juce::TextEditor::backgroundColourId, well);
        t->setColour (juce::TextEditor::textColourId, juce::Colours::white);
        t->setColour (juce::TextEditor::outlineColourId, cardBorder);
        t->setColour (juce::TextEditor::focusedOutlineColourId, accentText);
    }
    name_.onFocusLost = [this] { if (isVisible() && sel_ >= 0 && sel_ < (int) chart_.sections.size()) { auto n = name_.getText().trim().toStdString(); if (! n.empty() && n != chart_.sections[(size_t) sel_].name) { pushUndo(); run (renameSection (chart_, sel_, n)); } } };
    body_.onFocusLost = [this] { if (isVisible()) storeBody(); };
}
int SongEditor::nonTabs (const Section& s) const { int n = 0; for (auto& it : s.items) if (it.kind != ChartItem::Tab) ++n; return n; }
void SongEditor::pushUndo() { undo_.push_back (chart_); undoOrigin_.push_back (origin_); if (undo_.size() > 40) { undo_.erase (undo_.begin()); undoOrigin_.erase (undoOrigin_.begin()); } }
void SongEditor::undo() { if (undo_.empty()) return; chart_ = undo_.back(); origin_ = undoOrigin_.back(); undo_.pop_back(); undoOrigin_.pop_back(); if (sel_ >= (int) chart_.sections.size()) sel_ = std::max (0, (int) chart_.sections.size() - 1); loadSel(); repaint(); }
void SongEditor::run (const ChartEdit& e) {
    if (! e.ok) { if (! undo_.empty()) { undo_.pop_back(); undoOrigin_.pop_back(); } return; }
    std::vector<int> next (chart_.sections.size(), -1);
    for (int i = 0; i < (int) origin_.size() && i < (int) e.oldToNew.size(); ++i) { int j = e.oldToNew[(size_t) i]; if (j >= 0 && j < (int) next.size() && next[(size_t) j] < 0) next[(size_t) j] = origin_[(size_t) i]; }
    origin_ = std::move (next); loadSel(); repaint();
}
void SongEditor::storeBody() {
    if (sel_ < 0 || sel_ >= (int) chart_.sections.size()) return;
    auto& s = chart_.sections[(size_t) sel_]; auto text = body_.getText().toStdString();
    if (text != sectionBodyText (s)) setSectionBody (s, text);
}
void SongEditor::loadSel() {
    if (chart_.sections.empty()) { name_.setText ({}, false); body_.setText ({}, false); return; }
    sel_ = std::clamp (sel_, 0, (int) chart_.sections.size() - 1);
    auto& s = chart_.sections[(size_t) sel_]; name_.setText (juce::String (s.name), false); body_.setText (juce::String (sectionBodyText (s)), false);
    splitAt_ = std::clamp (splitAt_, 1, std::max (1, nonTabs (s) - 1));
}
void SongEditor::open (CoreEditor* ed, const std::string& songId, Chart chart, std::function<void (Chart)> onSave) {
    ed_ = ed; songId_ = songId; opened_ = chart; chart_ = std::move (chart); onSave_ = std::move (onSave);
    autoOn_ = true; if (ed_) if (auto* s = ed_->core.store.lib.song (songId_)) autoOn_ = s->autoAdvance;
    origin_.assign (chart_.sections.size(), 0); for (int i = 0; i < (int) origin_.size(); ++i) origin_[(size_t) i] = i;
    undo_.clear(); undoOrigin_.clear(); pending_.clear(); sel_ = 0; splitAt_ = 1; dragFrom_ = -1; loadSel();
    setVisible (true); toFront (true); resized();
}
void SongEditor::addBlank() {
    storeBody(); pushUndo(); Section s; s.name = "New section"; s.label = sectionLabelFor (s.name, &s.kind, &s.color); s.customColor.clear();
    ChartLine line; line.lyric = ""; ChartItem it; it.kind = ChartItem::Line; it.line.lyric = " "; s.items.push_back (it);
    chart_.sections.push_back (std::move (s)); origin_.push_back (-1); sel_ = (int) chart_.sections.size() - 1; loadSel(); repaint();
}
void SongEditor::chooseKind (const std::string& kind) {
    if (sel_ < 0 || sel_ >= (int) chart_.sections.size()) return;
    storeBody();
    int n = 1; for (int i = 0; i < (int) chart_.sections.size(); ++i) if (i != sel_ && chart_.sections[(size_t) i].kind == kind) ++n;
    std::string name = kind == "verse" ? "Verse " + std::to_string (n) : kind == "prechorus" ? "Pre-Chorus" : kind == "other" ? "Part" : kindDisplayName (kind);
    if (kind != "verse" && n > 1) name += " " + std::to_string (n);
    pushUndo(); run (renameSection (chart_, sel_, name));
}
void SongEditor::takeTabText (const juce::String& text) {
    auto parts = parseTabText (text.toStdString()); if (parts.empty()) return;
    if (parts.size() == 1 && sel_ >= 0 && sel_ < (int) chart_.sections.size()) { storeBody(); pushUndo(); addSectionTab (chart_.sections[(size_t) sel_], parts[0].tab); loadSel(); repaint(); return; }
    pending_ = std::move (parts); repaint();
}
void SongEditor::importTabFile() {
    auto safe = juce::Component::SafePointer<SongEditor> (this);
    platform::pickChartFile (this, [safe] (const juce::File& f, const juce::String&) { if (safe == nullptr || ! f.existsAsFile()) return; safe->takeTabText (f.loadFileAsString()); });
}
void SongEditor::save() {
    storeBody();
    if (sel_ >= 0 && sel_ < (int) chart_.sections.size()) { auto n = name_.getText().trim().toStdString(); if (! n.empty() && n != chart_.sections[(size_t) sel_].name) { pushUndo(); run (renameSection (chart_, sel_, n)); } }
    const SectionColorMap* defs = ed_ ? &ed_->core.store.lib.settings.sectionColors : nullptr;
    auto remapTokens = [&] (std::vector<std::string>& tokens) {
        if (tokens.empty()) return; std::vector<std::string> next;
        for (auto& t : tokens) { int old = resolveArrangementToken (opened_, t); if (old < 0) { next.push_back (t); continue; }
            int found = -1; for (int i = 0; i < (int) origin_.size(); ++i) if (origin_[(size_t) i] == old) { found = i; break; }
            if (found < 0 || found >= (int) chart_.sections.size()) continue;
            auto& s = chart_.sections[(size_t) found]; next.push_back (resolveArrangementToken (chart_, s.label) == found ? s.label : s.name); }
        tokens.swap (next);
    };
    if (opened_.arrangement.empty()) chart_.arrangement.clear(); else remapTokens (chart_.arrangement = opened_.arrangement);
    applySectionColors (chart_, defs ? *defs : SectionColorMap());
    if (ed_ && ! songId_.empty()) {
        if (auto* s = ed_->core.store.lib.song (songId_)) {
            s->chart = chart_; s->autoAdvance = autoOn_;
            for (auto& set : ed_->core.store.lib.setlists) for (auto& e : set.entries) if (e.songId == songId_ && ! e.arrangement.empty()) remapTokens (e.arrangement);
            ed_->core.store.save();
            if (ed_->core.live().song() && ed_->core.live().song()->id == songId_) ed_->core.live().refreshChart();
        }
    }
    setVisible (false); if (onSave_) onSave_ (chart_);
}
void SongEditor::resized() {
    auto area = getLocalBounds().reduced (16);
    auto top = area.removeFromTop (48);
    close_ = top.removeFromRight (112); top.removeFromRight (8); save_ = top.removeFromRight (120); top.removeFromRight (8); undoR_ = top.removeFromRight (100);
    area.removeFromTop (12);
    list_ = area.removeFromLeft (juce::jmin (340, area.getWidth() / 3)); area.removeFromLeft (14);
    auto side = list_.removeFromBottom (196);
    actions_.clear();
    auto row = side.removeFromTop (46); actions_.push_back ({ row.removeFromLeft (row.getWidth() / 2 - 4), 1 }); row.removeFromLeft (8); actions_.push_back ({ row, 2 });
    side.removeFromTop (8); row = side.removeFromTop (46); actions_.push_back ({ row.removeFromLeft (row.getWidth() / 3 - 4), 3 }); row.removeFromLeft (6); actions_.push_back ({ row.removeFromLeft (row.getWidth() / 2 - 3), 4 }); row.removeFromLeft (6); actions_.push_back ({ row, 5 });
    side.removeFromTop (8); actions_.push_back ({ side.removeFromTop (46), 6 });
    name_.setBounds (area.removeFromTop (44)); area.removeFromTop (8);
    kindArea_ = area.removeFromTop (92); area.removeFromTop (8);
    auto split = area.removeFromTop (46); splitDec_ = split.removeFromLeft (52); split.removeFromLeft (8); splitInc_ = split.removeFromLeft (52); split.removeFromLeft (8); splitGo_ = split.removeFromLeft (150);
    area.removeFromTop (8);
    auto tabs = area.removeFromBottom (std::min (210, area.getHeight() / 3)); area.removeFromBottom (8);
    paste_ = tabs.removeFromTop (44).removeFromLeft (150); tabs.removeFromLeft (8); importB_ = tabs.removeFromTop (44).removeFromLeft (170);
    tabArea_ = tabs; body_.setBounds (area);
}
void SongEditor::paint (juce::Graphics& g) {
    g.fillAll (bg);
    g.setColour (text); g.setFont (font (22, true)); g.drawText (songId_.empty() ? "Edit sections" : "Edit chart", 20, 16, 280, 40, juce::Justification::centredLeft);
    autoR_ = juce::Rectangle<int> (300, 18, 210, 36);
    pill (g, autoR_.toFloat(), autoOn_ ? accentFill : card, autoOn_ ? accentFill : cardBorder, 10);
    g.setColour (autoOn_ ? onAccent : text); g.setFont (font (13, true)); g.drawText (autoOn_ ? "Auto-advance on" : "Auto-advance off", autoR_, juce::Justification::centred);
    auto drawBtn = [&] (juce::Rectangle<int> r, const juce::String& t, juce::Colour fill) { pill (g, r.toFloat(), fill, cardBorder, 10); g.setColour (text); g.setFont (font (15, true)); g.drawText (t, r, juce::Justification::centred); };
    drawBtn (undoR_, "Undo", undo_.empty() ? juce::Colour (0xff1a2744) : card); drawBtn (save_, "Save", accentFill); drawBtn (close_, "Close", card);
    const SectionColorMap* defs = ed_ ? &ed_->core.store.lib.settings.sectionColors : nullptr;
    rows_.clear(); barBtns_.clear(); timeBtns_.clear(); passRows_.clear(); auto list = list_;
    if (! chart_.arrangement.empty()) {
        auto passes = list.removeFromBottom (78);
        g.setColour (dim); g.setFont (font (11, true)); g.drawText ("ARRANGEMENT PASSES - tap to set bars (0 = section)", passes.removeFromTop (16), juce::Justification::centredLeft);
        int px = passes.getX();
        for (int i = 0; i < (int) chart_.arrangement.size(); ++i) {
            int bars = i < (int) chart_.arrangementBars.size() ? chart_.arrangementBars[(size_t) i] : 0;
            juce::String t = juce::String (chart_.arrangement[(size_t) i]) + (bars > 0 ? "  " + juce::String (bars) : juce::String ("  -"));
            int w = (int) juce::GlyphArrangement::getStringWidth (font (12, true), t) + 16;
            if (px + w > list_.getRight()) break;
            juce::Rectangle<int> c (px, passes.getY(), w, 28); passRows_.push_back (c); px += w + 6;
            pill (g, c.toFloat(), card, cardBorder, 8); g.setColour (text); g.setFont (font (12, true)); g.drawText (t, c, juce::Justification::centred);
        }
    }
    for (int i = 0; i < (int) chart_.sections.size(); ++i) {
        auto row = list.removeFromTop (58); if (row.getHeight() < 40) break; rows_.push_back (row.reduced (0, 4));
        auto& s = chart_.sections[(size_t) i]; auto col = accent (juce::String (effectiveSectionColor (s, defs ? *defs : SectionColorMap())));
        pill (g, rows_.back().toFloat(), i == sel_ ? accentFill.withAlpha (0.35f) : card, i == sel_ ? accentText : cardBorder, 10);
        auto box = rows_.back();
        auto plus = box.removeFromRight (26); auto num = box.removeFromRight (36); auto minus = box.removeFromRight (26); auto meter = box.removeFromRight (46);
        barBtns_.push_back ({ minus.reduced (2), i * 2 }); barBtns_.push_back ({ plus.reduced (2), i * 2 + 1 }); timeBtns_.push_back ({ meter.reduced (2), i });
        g.setColour (col); g.fillRoundedRectangle ((float) rows_.back().getX() + 8, (float) rows_.back().getY() + 10, 6, (float) rows_.back().getHeight() - 20, 2);
        g.setColour (text); g.setFont (font (15, true)); g.drawText (juce::String (s.name), box.reduced (22, 6), juce::Justification::centredLeft, true);
        g.setColour (dim); g.setFont (font (12, true)); g.drawText ("-", minus, juce::Justification::centred); g.drawText ("+", plus, juce::Justification::centred);
        g.setColour (text); g.drawText (s.bars > 0 ? juce::String (s.bars) : juce::String ("cue"), num, juce::Justification::centred);
        g.setColour (s.time.empty() ? dim : accentText); g.drawText (s.time.empty() ? juce::String ("song") : juce::String (s.time), meter, juce::Justification::centred);
        if (dragged_ && dragTo_ == i) { g.setColour (accentText); g.fillRect (rows_.back().getX(), rows_.back().getY() - 3, rows_.back().getWidth(), 3); }
    }
    const char* names[] = { "Add section", "Duplicate", "Up", "Down", "Merge next", "Delete" };
    for (size_t i = 0; i < actions_.size() && i < 6; ++i) drawBtn (actions_[i].first, names[i], i == 5 ? juce::Colour (0xff5a2740) : card);
    g.setColour (dim); g.setFont (font (13)); g.drawText ("Chords and lyrics", name_.getBounds().getX(), kindArea_.getBottom() + 52, 200, 16, juce::Justification::left);
    kinds_.clear(); const char* ks[] = { "verse", "chorus", "prechorus", "bridge", "tag", "intro", "outro", "other" };
    const char* kl[] = { "Verse", "Chorus", "Pre-Chorus", "Bridge", "Tag", "Intro", "Outro", "Other" };
    auto ka = kindArea_; int x = ka.getX(), y = ka.getY();
    for (int i = 0; i < 8; ++i) {
        juce::Rectangle<int> r (x, y, 118, 40); if (r.getRight() > ka.getRight()) { x = ka.getX(); y += 46; r.setPosition (x, y); }
        kinds_.push_back (r); auto col = accent (juce::String (builtinKindColor (ks[i])));
        bool on = sel_ >= 0 && sel_ < (int) chart_.sections.size() && chart_.sections[(size_t) sel_].kind == ks[i];
        pill (g, r.toFloat(), on ? col.withAlpha (0.85f) : card, col, 8); g.setColour (on ? inkOn (col) : text); g.setFont (font (14, true)); g.drawText (kl[i], r, juce::Justification::centred);
        x += 124;
    }
    drawBtn (splitDec_, "-", card); drawBtn (splitInc_, "+", card);
    int lines = sel_ >= 0 && sel_ < (int) chart_.sections.size() ? nonTabs (chart_.sections[(size_t) sel_]) : 0;
    drawBtn (splitGo_, "Split at line " + juce::String (std::min (splitAt_, std::max (1, lines - 1))), card);
    drawBtn (paste_, "Paste tab", card); drawBtn (importB_, "Import tab file", card);
    tabRows_.clear(); partRows_.clear(); auto ta = tabArea_;
    if (sel_ >= 0 && sel_ < (int) chart_.sections.size()) {
        int ti = 0; for (auto& it : chart_.sections[(size_t) sel_].items) if (it.kind == ChartItem::Tab) {
            auto row = ta.removeFromTop (36); if (row.getHeight() < 28) break; tabRows_.push_back (row);
            g.setColour (text); g.setFont (font (14)); g.drawText (juce::String (it.tab.title) + "  (" + juce::String ((int) it.tab.strings.size()) + " lines)", row.withTrimmedRight (80), juce::Justification::centredLeft, true);
            auto rm = row.removeFromRight (72); pill (g, rm.toFloat(), card, cardBorder, 8); g.setColour (juce::Colour (0xffff8080)); g.drawText ("Remove", rm, juce::Justification::centred); ++ti;
        }
    }
    for (int i = 0; i < (int) pending_.size(); ++i) {
        auto row = ta.removeFromTop (36); if (row.getHeight() < 28) break; partRows_.push_back (row);
        g.setColour (accentText); g.setFont (font (14, true)); g.drawText ("Add \"" + juce::String (pending_[(size_t) i].title) + "\" to this section", row, juce::Justification::centredLeft, true);
    }
}
void SongEditor::mouseDown (const juce::MouseEvent& e) { dragged_ = false; dragFrom_ = -1; for (int i = 0; i < (int) rows_.size(); ++i) if (rows_[(size_t) i].contains (e.getPosition())) dragFrom_ = i; }
void SongEditor::mouseDrag (const juce::MouseEvent& e) {
    if (dragFrom_ < 0 || e.getDistanceFromDragStart() < 10) return; dragged_ = true; dragTo_ = (int) rows_.size() - 1;
    for (int i = 0; i < (int) rows_.size(); ++i) if (e.y < rows_[(size_t) i].getCentreY()) { dragTo_ = i; break; }
    repaint();
}
void SongEditor::mouseUp (const juce::MouseEvent& e) {
    if (dragged_ && dragFrom_ >= 0 && dragTo_ >= 0 && dragFrom_ != dragTo_) { storeBody(); pushUndo(); int from = dragFrom_; int to = dragTo_; run (moveSection (chart_, from, to)); sel_ = to; loadSel(); dragFrom_ = -1; dragged_ = false; repaint(); return; }
    dragFrom_ = -1; dragged_ = false;
    auto p = e.getPosition();
    if (autoR_.contains (p)) { autoOn_ = ! autoOn_; repaint(); return; }
    for (auto& b : barBtns_) if (b.first.contains (p)) {
        int i = b.second / 2; if (i < 0 || i >= (int) chart_.sections.size()) return;
        pushUndo(); int& bars = chart_.sections[(size_t) i].bars; bars = b.second % 2 ? std::min (64, bars + 1) : std::max (0, bars - 1); sel_ = i; repaint(); return;
    }
    for (auto& b : timeBtns_) if (b.first.contains (p)) {
        int i = b.second; if (i < 0 || i >= (int) chart_.sections.size()) return;
        pushUndo(); auto& tm = chart_.sections[(size_t) i].time;
        tm = tm.empty() ? "4/4" : tm == "4/4" ? "3/4" : tm == "3/4" ? "6/8" : tm == "6/8" ? "2/4" : std::string();
        sel_ = i; repaint(); return;
    }
    for (int i = 0; i < (int) passRows_.size() && i < (int) chart_.arrangement.size(); ++i) if (passRows_[(size_t) i].contains (p)) {
        pushUndo(); if ((int) chart_.arrangementBars.size() < (int) chart_.arrangement.size()) chart_.arrangementBars.resize (chart_.arrangement.size());
        int& bars = chart_.arrangementBars[(size_t) i]; const int steps[] = { 0, 2, 4, 8, 12, 16 };
        int at = 0; for (int s = 0; s < 6; ++s) if (steps[s] == bars) at = s;
        bars = steps[(at + 1) % 6]; repaint(); return;
    }
    if (close_.contains (p)) { setVisible (false); return; }
    if (undoR_.contains (p)) { undo(); return; }
    if (save_.contains (p)) { save(); return; }
    for (int i = 0; i < (int) rows_.size(); ++i) if (rows_[(size_t) i].contains (p)) { storeBody(); sel_ = i; loadSel(); repaint(); return; }
    for (auto& a : actions_) if (a.first.contains (p)) {
        storeBody(); if (chart_.sections.empty() && a.second != 1) return;
        if (a.second == 1) { addBlank(); return; }
        if (a.second == 6) {
            int idx = sel_; juce::String nm = idx >= 0 && idx < (int) chart_.sections.size() ? juce::String (chart_.sections[(size_t) idx].name) : juce::String();
            juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::WarningIcon, "Delete " + nm + "?", "This removes the section from the chart. Undo can bring it back before you save.", "Delete", "Cancel", this,
                juce::ModalCallbackFunction::create ([this, idx] (int r) { if (r != 1 || idx < 0 || idx >= (int) chart_.sections.size()) return; pushUndo(); run (deleteSection (chart_, idx)); if (sel_ >= (int) chart_.sections.size()) sel_ = std::max (0, (int) chart_.sections.size() - 1); loadSel(); }));
            return;
        }
        pushUndo();
        if (a.second == 2) { run (duplicateSection (chart_, sel_)); sel_ = std::min (sel_ + 1, (int) chart_.sections.size() - 1); loadSel(); }
        else if (a.second == 3 && sel_ > 0) { run (moveSection (chart_, sel_, sel_ - 1)); --sel_; loadSel(); }
        else if (a.second == 4 && sel_ + 1 < (int) chart_.sections.size()) { run (moveSection (chart_, sel_, sel_ + 1)); ++sel_; loadSel(); }
        else if (a.second == 5) run (mergeSections (chart_, sel_));
        else { undo_.pop_back(); undoOrigin_.pop_back(); }
        return;
    }
    const char* ks[] = { "verse", "chorus", "prechorus", "bridge", "tag", "intro", "outro", "other" };
    for (int i = 0; i < (int) kinds_.size(); ++i) if (kinds_[(size_t) i].contains (p)) { chooseKind (ks[i]); return; }
    if (splitDec_.contains (p)) { splitAt_ = std::max (1, splitAt_ - 1); repaint(); return; }
    if (splitInc_.contains (p)) { int n = sel_ >= 0 && sel_ < (int) chart_.sections.size() ? nonTabs (chart_.sections[(size_t) sel_]) : 1; splitAt_ = std::min (std::max (1, n - 1), splitAt_ + 1); repaint(); return; }
    if (splitGo_.contains (p)) { storeBody(); int n = sel_ >= 0 && sel_ < (int) chart_.sections.size() ? nonTabs (chart_.sections[(size_t) sel_]) : 0; if (n < 2) return; pushUndo(); int at = std::clamp (splitAt_, 1, n - 1); run (splitSection (chart_, sel_, at, "New section")); sel_ = std::min (sel_ + 1, (int) chart_.sections.size() - 1); loadSel(); return; }
    if (paste_.contains (p)) { takeTabText (platform::clipboardText()); return; }
    if (importB_.contains (p)) { importTabFile(); return; }
    if (sel_ >= 0 && sel_ < (int) chart_.sections.size()) for (int i = 0; i < (int) tabRows_.size(); ++i) if (tabRows_[(size_t) i].contains (p) && p.x > tabRows_[(size_t) i].getRight() - 80) {
        storeBody(); pushUndo(); removeSectionTab (chart_.sections[(size_t) sel_], i); loadSel(); repaint(); return; }
    for (int i = 0; i < (int) partRows_.size() && i < (int) pending_.size(); ++i) if (partRows_[(size_t) i].contains (p) && sel_ >= 0 && sel_ < (int) chart_.sections.size()) {
        storeBody(); pushUndo(); addSectionTab (chart_.sections[(size_t) sel_], pending_[(size_t) i].tab); pending_.erase (pending_.begin() + i); loadSel(); repaint(); return; }
}

// ================================================================== arrangement editor
void ArrangementEditor::open (const Chart& written, std::vector<std::string> tokens, bool setlistScope, Done done, std::string songId, std::vector<int> passBars) {
    written_ = written; written_.arrangement.clear(); setlistScope_ = setlistScope; done_ = std::move (done); songId_ = std::move (songId); coloursDirty_ = false; recolour();
    asWritten_ = tokens.empty();
    if (asWritten_) { for (int i = 0; i < (int) written_.sections.size(); ++i) tokens.push_back (tokenFor (i)); passBars.clear(); }
    tok_ = std::move (tokens); passBars_ = std::move (passBars); passBars_.resize (tok_.size()); dragFrom_ = -1; setVisible (true); toFront (true); repaint();
}
std::string ArrangementEditor::tokenFor (int i) const {   // label if unique, else the full name
    const auto& s = written_.sections[(size_t) i]; int same = 0; for (auto& o : written_.sections) if (o.label == s.label) ++same;
    return ! s.label.empty() && same == 1 ? s.label : s.name;
}
int ArrangementEditor::chipAt (juce::Point<int> p) const { for (int i = 0; i < (int) chips_.size(); ++i) if (chips_[(size_t) i].expanded (3).contains (p)) return i; return -1; }
void ArrangementEditor::paint (juce::Graphics& g) {
    g.fillAll (bg.withAlpha (0.72f));
    box_ = getLocalBounds().withSizeKeepingCentre (std::min (900, getWidth() - 40), std::min (520, getHeight() - 40));
    pill (g, box_.toFloat(), bar, cardBorder, 14);
    auto r = box_.reduced (22, 16);
    g.setColour (juce::Colours::white); g.setFont (font (18, true)); g.drawText ("ARRANGEMENT - " + juce::String (written_.title), r.removeFromTop (30), juce::Justification::centredLeft, true);
    label (g, "PLAY ORDER - drag to reorder, tap a section to duplicate or remove. Next / Prev, cues, auto-advance, projector and band sync follow this order.", r.removeFromTop (18), dimmer, 11);
    r.removeFromTop (8);
    chips_.clear(); int x = r.getX(), y = r.getY(); const int h = 36;
    for (int i = 0; i < (int) tok_.size(); ++i) {
        const int idx = resolveArrangementToken (written_, tok_[(size_t) i]);
        const int bars = i < (int) passBars_.size() ? passBars_[(size_t) i] : 0;
        juce::String t (tok_[(size_t) i]); if (bars > 0) t << "  " << bars;
        int w = (int) juce::GlyphArrangement::getStringWidth (font (14, true), t) + 30; if (x + w > r.getRight()) { x = r.getX(); y += h + 8; }
        juce::Rectangle<int> c (x, y, w, h); chips_.push_back (c); x += w + 8;
        const bool bad = idx < 0, drag = i == dragFrom_ && dragged_;
        auto col = bad ? juce::Colour (0xffff4d4d) : accent (juce::String (written_.sections[(size_t) idx].color));
        pill (g, c.toFloat(), drag ? col.withAlpha (0.35f) : card, col, 10);
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
        const bool primary = i >= 2; pill (g, b.toFloat(), primary ? accentFill : btn, primary ? accentFill : btnBorder, 8); g.setColour (primary ? onAccent : text); g.setFont (font (13, true)); g.drawText (names[i], b, juce::Justification::centred); buttons_.push_back ({ b, i }); }
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
    if (dragged_ && dragFrom_ >= 0) { int to = std::clamp (dragTo_, 0, (int) tok_.size()); auto t = tok_[(size_t) dragFrom_]; int bars = dragFrom_ < (int) passBars_.size() ? passBars_[(size_t) dragFrom_] : 0;
        tok_.erase (tok_.begin() + dragFrom_); if (dragFrom_ < (int) passBars_.size()) passBars_.erase (passBars_.begin() + dragFrom_); if (to > dragFrom_) --to;
        tok_.insert (tok_.begin() + to, t); passBars_.insert (passBars_.begin() + std::min (to, (int) passBars_.size()), bars); asWritten_ = false; dragFrom_ = -1; dragged_ = false; repaint(); return; }
    dragFrom_ = -1;
    if (! box_.contains (p)) return;
    for (auto& b : buttons_) if (b.first.contains (p)) {
        if (b.second == 0) { setVisible (false); return; }
        if (b.second == 1) { tok_.clear(); passBars_.clear(); for (int i = 0; i < (int) written_.sections.size(); ++i) { tok_.push_back (tokenFor (i)); passBars_.push_back (0); } asWritten_ = true; repaint(); return; }
        std::vector<std::string> out; std::vector<int> bars;
        if (! asWritten_) for (size_t i = 0; i < tok_.size(); ++i) if (resolveArrangementToken (written_, tok_[i]) >= 0) { out.push_back (tok_[i]); bars.push_back (i < passBars_.size() ? passBars_[i] : 0); }
        if (coloursDirty_ && onColours && ! songId_.empty()) { std::vector<std::string> cc; for (auto& sec : written_.sections) cc.push_back (sec.customColor); onColours (songId_, cc); }
        setVisible (false); if (done_) done_ (out, b.second == 3 ? 1 : 0, asWritten_ ? std::vector<int>() : bars); return;
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
    for (auto& pc : palette_) if (pc.first.contains (p)) { tok_.push_back (tokenFor (pc.second)); passBars_.push_back (0); asWritten_ = false; repaint(); return; }
    const int c = chipAt (p); if (c < 0) return;
    juce::PopupMenu m;
    m.addItem ("Duplicate (play it again right after)", [this, c] { int bars = c < (int) passBars_.size() ? passBars_[(size_t) c] : 0; tok_.insert (tok_.begin() + c + 1, tok_[(size_t) c]); passBars_.insert (passBars_.begin() + std::min (c + 1, (int) passBars_.size()), bars); asWritten_ = false; repaint(); });
    m.addItem ("Move earlier", c > 0, false, [this, c] { std::swap (tok_[(size_t) c], tok_[(size_t) c - 1]); if (c < (int) passBars_.size() && c - 1 < (int) passBars_.size()) std::swap (passBars_[(size_t) c], passBars_[(size_t) c - 1]); asWritten_ = false; repaint(); });
    m.addItem ("Move later", c + 1 < (int) tok_.size(), false, [this, c] { std::swap (tok_[(size_t) c], tok_[(size_t) c + 1]); if (c + 1 < (int) passBars_.size()) std::swap (passBars_[(size_t) c], passBars_[(size_t) c + 1]); asWritten_ = false; repaint(); });
    m.addItem ("Remove", [this, c] { tok_.erase (tok_.begin() + c); if (c < (int) passBars_.size()) passBars_.erase (passBars_.begin() + c); asWritten_ = false; repaint(); });
    m.addSeparator();
    auto setBars = [this, c] (int bars) { if ((int) passBars_.size() < (int) tok_.size()) passBars_.resize (tok_.size()); passBars_[(size_t) c] = bars; asWritten_ = false; repaint(); };
    const int cur = c < (int) passBars_.size() ? passBars_[(size_t) c] : 0;
    m.addItem ("Bars: use the section's count", true, cur == 0, [setBars] { setBars (0); });
    for (int bars : { 2, 4, 8, 12, 16 }) m.addItem (juce::String (bars) + " bars this pass", true, cur == bars, [setBars, bars] { setBars (bars); });
    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (chips_[(size_t) c])));
}

// ================================================================== practice bar
static juce::String mmss (double s) { if (s < 0) s = 0; int t = (int) s; return juce::String (t / 60) + ":" + juce::String (t % 60).paddedLeft ('0', 2); }
void PracticeBar::paint (juce::Graphics& g) {
    auto& P = ed.core; auto& pl = P.practice; btn_.clear();
    g.fillAll (panel); g.setColour (accentText.withAlpha (0.6f)); g.drawHorizontalLine (0, 0, (float) getWidth());
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
    m.addItem ("Edit chart...", ! following, false, [&ed, id = s->id] { ed.openSongEditor (id); });
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
