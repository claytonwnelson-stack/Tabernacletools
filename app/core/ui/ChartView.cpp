#include "ChartView.h"
#include "LiveExtras.h"
#include "model/StageTools.h"
using namespace wr; using namespace wr::theme;

static const float kLyric = 20.0f, kChord = 14.0f;

// ================================================================== chart
static juce::Colour hlColour (const std::string& h) { return h == "pink" ? pink.withAlpha (0.20f) : h.empty() ? juce::Colours::transparentBlack : accent (h).withAlpha (0.18f); }

float ChartView::sectionHeight (const Section& s, float w, bool compact) const {
    float h = 34; int shown = 0;
    for (auto& it : s.items) {
        if (compact && shown >= 1) break;
        if (it.kind == ChartItem::Line) { h += (it.line.chords.empty() ? 0 : 17) + (it.line.lyric.empty() ? 4 : 26); ++shown; }
        else if (it.kind == ChartItem::Tab) { if (! compact) h += 22 + 15.0f * (float) it.tab.strings.size() + (it.tab.note.empty() ? 0 : 16) + 8; }
        else h += 20;
    }
    juce::ignoreUnused (w); return h + 10;
}

float ChartView::drawSection (juce::Graphics& g, const Section& s, int idx, juce::Rectangle<float> a, bool cur, bool dimmed, bool compact) {
    auto& L = ed.core.live(); const Song* song = L.song();
    float h = sectionHeight (s, a.getWidth(), compact); auto box = a.withHeight (h);
    g.setOpacity (1.0f);
    if (cur) { g.setColour (orange.withAlpha (0.08f)); g.fillRoundedRectangle (box.expanded (4), 14); pill (g, box, juce::Colour (0xff1a1810), orange, 12); g.setColour (orange); g.drawRoundedRectangle (box.reduced (1), 12, 2); }
    else pill (g, box, panel, juce::Colour (0xff222838), 12);
    const float alpha = dimmed ? 0.45f : 1.0f;
    const auto secCol = accent (juce::String (s.color));   // v0.8 editable section colour
    g.setColour (secCol.withMultipliedAlpha (alpha)); g.fillRoundedRectangle (box.getX() + 3, box.getY() + 8, 4, std::max (8.0f, h - 16), 2);
    auto hdr = box.reduced (14, 8).removeFromTop (20);
    g.setColour (secCol.interpolatedWith (juce::Colour (0xffaab2c6), 0.25f).withMultipliedAlpha (alpha)); g.setFont (font (14, true));
    juce::String nm = juce::String (s.name).toUpperCase(); if (s.repeat > 1) nm << "  x" << s.repeat << (cur ? "  (" + juce::String (L.pass()) + "/" + juce::String (s.repeat) + ")" : juce::String());
    g.drawText (nm, hdr, juce::Justification::centredLeft); float hx = hdr.getX() + juce::GlyphArrangement::getStringWidth (font (14, true), nm) + 10;
    auto cue = [&] (const juce::String& t, juce::Colour c) { float w = juce::GlyphArrangement::getStringWidth (font (12, true), t) + 18; juce::Rectangle<float> r (hx, hdr.getY(), w, 20); g.setColour (c.withMultipliedAlpha (alpha)); g.fillRoundedRectangle (r, 10); g.setColour (juce::Colour (0xff111111)); g.setFont (font (12, true)); g.drawText (t, r, juce::Justification::centred); hx += w + 6; };
    if (song) if (auto* sc = song->sceneForSection (s)) cue ("Scene - " + juce::String (sc->name), accent (s.color));
    auto pc = L.performerCue (idx); if (! pc.empty()) cue (pc, blue);
    if (cur) { g.setColour (orange); g.setFont (font (12, true)); g.drawText ("NOW", hdr, juce::Justification::centredRight); }

    float y = box.getY() + 36; const float x0 = box.getX() + 16; int shown = 0;
    for (auto& it : s.items) {
        if (compact && shown >= 1) { g.setColour (dimmer); g.setFont (font (13)); g.drawText ("...", juce::Rectangle<float> (x0, y - 6, 200, 14), juce::Justification::left); break; }
        if (it.kind == ChartItem::Line) {
            auto& ln = it.line; const auto lf = font (kLyric), cf = mono (kChord, true);
            float lyricW = juce::GlyphArrangement::getStringWidth (lf, ln.lyric);
            if (! ln.chords.empty()) {
                float lastEnd = -1;
                for (auto& ch : ln.chords) {
                    float cx = x0 + juce::GlyphArrangement::getStringWidth (lf, juce::String (ln.lyric).substring (0, ch.pos));
                    if (ln.lyric.empty()) cx = x0 + (float) ch.pos * 9.0f; cx = std::max (cx, lastEnd + 6);
                    g.setColour ((s.color == "pink" ? juce::Colour (0xffff8fc8) : s.color == "blue" ? juce::Colour (0xff7dd3fc) : orange).withMultipliedAlpha (alpha)); g.setFont (cf);
                    g.drawText (ch.chord, juce::Rectangle<float> (cx, y, 200, 17), juce::Justification::topLeft); lastEnd = cx + juce::GlyphArrangement::getStringWidth (cf, ch.chord);
                }
                y += 17;
            }
            if (! ln.lyric.empty()) {
                if (! ln.highlight.empty()) { g.setColour (hlColour (ln.highlight).withMultipliedAlpha (alpha)); g.fillRoundedRectangle (x0 - 4, y, lyricW + 8, 24, 4); }
                g.setColour (text.withMultipliedAlpha (alpha)); g.setFont (lf); g.drawText (ln.lyric, juce::Rectangle<float> (x0, y, box.getRight() - x0 - 8, 24), juce::Justification::centredLeft, true); y += 26;
            } else y += 4;
            ++shown;
        } else if (it.kind == ChartItem::Tab && ! compact) {
            auto col = accent (it.tab.color); float th = 22 + 15.0f * (float) it.tab.strings.size() + (it.tab.note.empty() ? 0 : 16);
            float tw = 0; for (auto& l : it.tab.strings) tw = std::max (tw, juce::GlyphArrangement::getStringWidth (mono (13), l));
            juce::Rectangle<float> tr (x0, y + 4, std::min (tw + 22, box.getRight() - x0 - 12), th);
            g.setColour (well); g.fillRoundedRectangle (tr, 8); g.setColour (col.withMultipliedAlpha (alpha)); g.drawRoundedRectangle (tr, 8, 2);
            g.setFont (font (10, true)); g.drawText ("TAB - " + juce::String (it.tab.title).toUpperCase(), tr.reduced (10, 4).removeFromTop (14), juce::Justification::left);
            g.setColour ((it.tab.color == "pink" ? juce::Colour (0xffffc0df) : juce::Colour (0xffffd08a)).withMultipliedAlpha (alpha)); g.setFont (mono (13));
            float ly = tr.getY() + 20; for (auto& l : it.tab.strings) { g.drawText (l, juce::Rectangle<float> (tr.getX() + 10, ly, tr.getWidth() - 12, 15), juce::Justification::left, false); ly += 15; }
            if (! it.tab.note.empty()) { g.setColour (blue); g.setFont (font (11, true)); g.drawText (it.tab.note, juce::Rectangle<float> (tr.getX() + 10, ly, tr.getWidth(), 14), juce::Justification::left); }
            y += th + 8;
        } else if (it.kind == ChartItem::Comment) { g.setColour (dim.withMultipliedAlpha (alpha)); g.setFont (font (14).italicised()); g.drawText (it.text, juce::Rectangle<float> (x0, y, box.getWidth() - 30, 18), juce::Justification::left); y += 20; }
    }
    return h;
}

void ChartView::paint (juce::Graphics& g) {
    { juce::Graphics::ScopedSaveState ss (g); paintChart (g); }
    // v0.7 overlays: service notes under the NEXT banner, next-song card at the bottom (last section + gap between songs)
    auto& L = ed.core.live(); auto area = getLocalBounds().toFloat().reduced (18, 14);
    auto notes = L.activeNotes(); if (! notes.empty()) wr::ui::paintNotesBanner (g, area.withTrimmedTop (46).withHeight (32), notes);
    if (L.showNextSong()) { const float h = std::min (190.0f, area.getHeight() * 0.45f); wr::ui::paintNextSongCard (g, area.removeFromBottom (h), L.nextSongInfo(), L.songEnded()); }
}
void ChartView::paintChart (juce::Graphics& g) {
    auto& L = ed.core.live(); const Chart& c = L.chart(); const auto& st = ed.core.store.lib.settings;
    const float k = stageTextScale (st);   // v0.8 dark stage mode: black, larger text (everything drawn at k x)
    g.fillAll (st.stageMode ? juce::Colours::black : bg);
    if (k != 1.0f) g.addTransform (juce::AffineTransform::scale (k));
    auto area = (getLocalBounds().toFloat() / k).reduced (18, 14);
    // NEXT banner with the switch to press
    Cue cue = L.nextCue();
    juce::Rectangle<float> ban = area.removeFromTop (40); area.removeFromTop (12);
    juce::ColourGradient gr (juce::Colour (0xff3a1630), ban.getX(), 0, juce::Colour (0xff1c1424), ban.getRight(), 0, false); g.setGradientFill (gr); g.fillRoundedRectangle (ban, 10);
    g.setColour (pink); g.drawRoundedRectangle (ban, 10, 1);
    if (L.sectionPending() && L.pendingSection() < (int) c.sections.size()) cue.text = "ON THE BAR LINE: " + c.sections[(size_t) L.pendingSection()].name; // queued, being announced
    { const int ni = L.sectionPending() ? L.pendingSection() : L.sectionIndex() + 1;   // v0.8 next section's colour
      if (ni >= 0 && ni < (int) c.sections.size()) { g.setColour (accent (juce::String (c.sections[(size_t) ni].color))); g.fillRoundedRectangle (ban.getX() + 6, ban.getY() + 8, 5, ban.getHeight() - 16, 2); } }
    g.setColour (text); g.setFont (font (15, true)); g.drawText (cue.text, ban.reduced (14, 0).withTrimmedRight (90), juce::Justification::centredLeft, true);
    if (cue.sw) { juce::Rectangle<float> sw (ban.getRight() - 84, ban.getY() + 9, 70, 22); g.setColour (pink); g.fillRoundedRectangle (sw, 6); g.setColour (juce::Colour (0xff111111)); g.setFont (font (12, true));
                  g.drawText ((cue.unit == "section" ? "SEC " : "GTR ") + juce::String::charToString ((juce::juce_wchar) cue.sw), sw, juce::Justification::centred); }
    if (c.sections.empty()) { g.setColour (dim); g.setFont (font (16)); g.drawText ("Add songs on the SETLIST screen, then Go Live.", area, juce::Justification::centred); return; }

    g.reduceClipRegion (area.toNearestInt());
    const int cur = L.sectionIndex();
    if (L.view() == LiveSession::View::Section) {
        // section mode: current section large, next one dimmed underneath
        float y = area.getY();
        y += drawSection (g, c.sections[(size_t) cur], cur, area.withY (y), true, false, false) + 10;
        if (cur + 1 < (int) c.sections.size()) drawSection (g, c.sections[(size_t) cur + 1], cur + 1, area.withY (y), false, true, false);
        g.setColour (dimmer); g.setFont (font (11, true)); g.drawText ("SECTION VIEW - returns to full song automatically", area.removeFromBottom (16), juce::Justification::centredRight);
        return;
    }
    // full song view: whole chart, scrolled so the current section stays in view
    secY_.clear(); float y = 0;
    for (auto& s : c.sections) { float h = sectionHeight (s, area.getWidth(), false); secY_.push_back ({ y, y + h }); y += h + 10; }
    contentH_ = y;
    if (followCurrent_ && cur < (int) secY_.size()) { float target = secY_[(size_t) cur].first - 40; scroll_ = juce::jlimit (0.0f, std::max (0.0f, contentH_ - area.getHeight()), target); followCurrent_ = false; }
    // v0.8 auto-scroll: while playing (click grid / PLAY / follower), the view glides with the bar position inside the section
    if (st.autoScroll && L.playing() && cur < (int) secY_.size() && ! c.sections.empty()) {
        const auto& sec = c.sections[(size_t) cur]; const double p = sectionProgress (L.beatInSection(), L.beatsPerBar(), sec.bars, 1);
        if (sec.bars > 0) { const float t = autoScrollOffset (secY_[(size_t) cur].first, secY_[(size_t) cur].second - secY_[(size_t) cur].first, p, area.getHeight(), contentH_);
            scroll_ += (t - scroll_) * 0.25f; } }
    for (int i = 0; i < (int) c.sections.size(); ++i) {
        float top = area.getY() + secY_[(size_t) i].first - scroll_;
        if (top > area.getBottom() || top + (secY_[(size_t) i].second - secY_[(size_t) i].first) < area.getY()) continue;
        drawSection (g, c.sections[(size_t) i], i, area.withY (top), i == cur, i < cur, false);
    }
}

void ChartView::mouseDrag (const juce::MouseEvent& e) { if (ed.core.live().view() == LiveSession::View::Full) { scroll_ = juce::jlimit (0.0f, std::max (0.0f, contentH_ - (float) getHeight() + 60), dragStart_ - (float) e.getDistanceFromDragStartY()); repaint(); } }
void ChartView::mouseUp (const juce::MouseEvent& e) {
    if (e.getDistanceFromDragStart() > 8) return;
    auto& L = ed.core.live();
    if (L.view() == LiveSession::View::Section) { Action a; a.type = Action::ToggleView; L.handle (a, ed.core.nowSec()); return; }
    float cy = (float) e.y / stageTextScale (ed.core.store.lib.settings) - 14 - 52 + scroll_;
    for (int i = 0; i < (int) secY_.size(); ++i) if (cy >= secY_[(size_t) i].first && cy < secY_[(size_t) i].second) { L.gotoSection (i, ed.core.nowSec()); Action a; a.type = Action::ToggleView; L.handle (a, ed.core.nowSec()); return; }
}

