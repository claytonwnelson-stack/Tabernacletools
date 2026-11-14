#include "model/SectionColors.h"
#include "LiveView.h"
#include "BandPanel.h"
#include "audio/PitchTracker.h"
using namespace wr; using namespace wr::theme;


// ================================================================== Live view
LiveView::LiveView (TabernacleGuitarEditor& e) : ed (e), chart_ (e), rig_ (e), top_ (e) { addAndMakeVisible (chart_); addAndMakeVisible (rig_); }
void LiveView::resized() { auto r = getLocalBounds(); chart_.setBounds (r.removeFromLeft ((int) (r.getWidth() * 0.56f))); rig_.setBounds (r); }
void LiveView::paint (juce::Graphics& g) { g.fillAll (bg); g.setColour (line); g.drawVerticalLine (chart_.getRight(), 0, (float) getHeight()); }
void LiveView::tick() { if (++meterTick_ % 3 == 0) rig_.repaint(); float hz = ed.proc.rig.tunerHz.load(); tunerHzSm_ = hz > 0 ? (tunerHzSm_ > 0 ? tunerHzSm_ * 0.7f + hz * 0.3f : hz) : 0; }

void LiveView::paintTopBar (juce::Graphics& g, juce::Rectangle<int> r) {
    top_.paint (g, r, ed.tunerShown ? 230 : 8);
    if (ed.tunerShown) { // tuner
        juce::Rectangle<int> t (r.getRight() - 222, 12, 214, 40); pill (g, t.toFloat(), card, line, 10);
        auto rd = tunerReading (tunerHzSm_, ed.proc.rig.tunerConf.load(), ed.proc.store.lib.settings.tunerRefHz);
        g.setColour (green); g.setFont (font (24, true)); g.drawText (rd.valid ? juce::String (rd.note) : juce::String ("-"), t.withWidth (56).withTrimmedLeft (10), juce::Justification::centredLeft);
        juce::Rectangle<float> barR ((float) t.getX() + 60, (float) t.getCentreY() - 4, 70, 8); g.setColour (bg); g.fillRoundedRectangle (barR, 4);
        if (rd.valid) { float px = barR.getCentreX() + juce::jlimit (-1.0f, 1.0f, rd.cents / 50.0f) * 35.0f; g.setColour (std::abs (rd.cents) < 3 ? green : warn); g.fillRoundedRectangle (px - 2, barR.getY() - 4, 4, 16, 2); }
        g.setColour (dim); g.setFont (font (11)); g.drawText (rd.valid ? juce::String (rd.cents, 0) + "c " + juce::String (rd.hz, 1) + " Hz" : juce::String ("tuner"), t.withTrimmedLeft (136), juce::Justification::centredLeft);
    }
}

// ================================================================== rig panel
void RigPanel::paint (juce::Graphics& g) {
    auto& P = ed.proc; auto& L = P.live(); g.fillAll (bg); tiles_.clear(); switches_.clear();
    auto r = getLocalBounds().toFloat().reduced (16, 14);
    auto hdr = r.removeFromTop (18); r.removeFromTop (8);
    g.setColour (dimmer); g.setFont (font (12, true)); g.drawText ("RIG - SCENE: " + juce::String (L.currentSceneName().empty() ? "-" : L.currentSceneName()).toUpperCase(), hdr, juce::Justification::centredLeft);
    double fs = P.getSampleRate() > 0 ? P.getSampleRate() : 48000; int blk = P.getBlockSize() > 0 ? P.getBlockSize() : 128;
    g.drawText (juce::String (fs / 1000.0, 0) + " kHz - " + juce::String (blk) + " smp - " + juce::String (blk / fs * 1000.0, 1) + " ms" + (P.rig.routingNow.load() == 0 ? "  - 4-cable" : "  - 2-cable"), hdr, juce::Justification::centredRight);
    if (P.rig.fellBackToTwoCable.load()) { g.setColour (warn); g.drawText ("only 2 outputs: running 2-cable", hdr.translated (0, 14), juce::Justification::centredRight); }

    // control strip: CLICK (on/off, beat LEDs) | MUTE | TUNER | PLAY (bar grid for auto-advance)
    ctl_.clear();
    { auto strip = r.removeFromTop (40); r.removeFromTop (8); const float w4 = (strip.getWidth() - 3 * 8) / 4;
      auto cell = [&] (int i) { return juce::Rectangle<float> (strip.getX() + (float) i * (w4 + 8), strip.getY(), w4, strip.getHeight()); };
      const bool on = L.clickOn(), muted = L.clickMuted(); const int bpb = L.beatsPerBar(), beat = P.rig.click.clickBeat.load();
      auto c0 = cell (0); pill (g, c0, on ? card : btn, on ? accentText : btnBorder, 10);
      g.setColour (text); g.setFont (font (13, true)); g.drawText (on ? "CLICK ON" : "CLICK OFF", c0.withTrimmedBottom (14), juce::Justification::centred);
      for (int b = 0; b < std::min (bpb, 12); ++b) { float d = 7, x = c0.getCentreX() - (float) std::min (bpb, 12) * 5.5f + (float) b * 11; g.setColour (on && b == beat ? (b == 0 ? accentText : juce::Colours::white) : line); g.fillEllipse (x, c0.getBottom() - 12, d, d); }
      auto c1 = cell (1); pill (g, c1, muted ? card : btn, muted ? clip : btnBorder, 10);
      g.setColour (muted ? juce::Colour (0xffff8080) : text); g.drawText (muted ? "CLICK MUTED" : "MUTE CLICK", c1, juce::Justification::centred);
      auto c2 = cell (2); pill (g, c2, btn, btnBorder, 10); g.setColour (text); g.drawText ("TUNER", c2, juce::Justification::centred);
      auto c3 = cell (3); const bool pl = L.playing(); pill (g, c3, pl ? card : btn, pl ? accentText : btnBorder, 10);
      const auto& ch = L.chart(); int bars = ch.sections.empty() ? 0 : ch.sections[(size_t) L.sectionIndex()].bars;
      if (P.rig.click.countingIn.load()) { g.setColour (accentText); g.drawText ("COUNT-IN", c3, juce::Justification::centred); } else
      g.setColour (pl ? accentText : text), g.drawText (L.clockActive() ? juce::String ("MIDI CLOCK") : pl ? (bars > 0 ? "BAR " + juce::String (std::min (bars, L.barInSection())) + "/" + juce::String (bars) : juce::String ("PLAYING")) : juce::String ("PLAY"), c3, juce::Justification::centred);
      ctl_ = { { c0, 0 }, { c1, 1 }, { c2, 2 }, { c3, 3 } };
      takeLead_ = {};
      if (P.sync().canTakeLead()) { takeLead_ = juce::Rectangle<float> (strip.getRight() - 120, strip.getBottom() + 6, 120, 28);
          pill (g, takeLead_, accentFill, accentFill, 8); g.setColour (onAccent); g.setFont (font (13, true)); g.drawText ("TAKE LEAD", takeLead_, juce::Justification::centred); r.removeFromTop (36); }
      { auto b = P.sync().banner (P.nowSec()); if (! b.empty()) { g.setColour (juce::Colours::red.withAlpha (0.85f)); auto br = strip.translated (0, 38).withHeight (14); g.fillRect (br); g.setColour (juce::Colours::white); g.setFont (font (11, true)); g.drawText (juce::String (b), br, juce::Justification::centred); r.removeFromTop (8); } }
      if (P.rig.clickConflict.load()) { g.setColour (warn); g.setFont (font (10, true)); g.drawText ("click/cues share rig outputs (4-cable: use Out 2) - AUDIO/MIDI > Click track", strip.translated (0, 38).withHeight (12), juce::Justification::centredLeft); r.removeFromTop (6); }
    }

    // pedal tiles: before-amp lane then after-amp lane
    const auto* chain = P.layout(); std::vector<const PedalSlot*> slots; std::vector<int> laneOf;
    if (chain) for (int l = 0; l < 2; ++l) for (auto& s : chain->lanes[l]) { slots.push_back (&s); laneOf.push_back (l); }
    const int cols = 4; const float gap = 8, tw = (r.getWidth() - gap * (cols - 1)) / cols, th = 74;
    int rows = ((int) slots.size() + cols - 1) / cols; auto grid = r.removeFromTop (rows * (th + gap)); r.removeFromTop (8);
    for (size_t i = 0; i < slots.size(); ++i) {
        auto& s = *slots[i]; bool on = P.pedalOn (s.uid); int cI = (int) i % cols, rI = (int) i / cols;
        juce::Rectangle<float> t (grid.getX() + (float) cI * (tw + gap), grid.getY() + (float) rI * (th + gap), tw, th);
        juce::Colour tint = laneOf[i] == 0 ? card : panel;
        juce::ColourGradient gg (tint, 0, t.getY(), card, 0, t.getBottom(), false); g.setGradientFill (gg); g.fillRoundedRectangle (t, 12);
        g.setColour (on ? accentText : cardBorder); g.drawRoundedRectangle (t, 12, 1);
        g.setOpacity (on ? 1.0f : 0.55f);
        led (g, { t.getX() + 14, t.getY() + 15 }, on);
        g.setColour (text); g.setFont (font (13, true)); g.drawText (s.pedal->displayName, t.withTrimmedLeft (24).withHeight (30), juce::Justification::centredLeft, true);
        if (! s.pedal->implemented) { g.setColour (dimmer); g.setFont (font (9, true)); g.drawText ("TODO v0.2", t.reduced (8, 6), juce::Justification::topRight); }
        // two mini knobs = first two params
        for (int k = 0; k < std::min (2, s.pedal->numParams()); ++k) {
            auto& sp = s.pedal->specs()[(size_t) k]; float v = P.paramValue (s.uid, sp.id), norm = (v - sp.min) / (sp.max - sp.min);
            juce::Rectangle<float> kr (t.getX() + 12 + (float) k * (tw / 2 - 6), t.getY() + 34, 26, 26);
            g.setColour (bg); g.fillEllipse (kr); float ang = -2.4f + 4.8f * juce::jlimit (0.0f, 1.0f, norm);
            g.setColour (juce::Colours::white); g.drawLine (kr.getCentreX(), kr.getCentreY(), kr.getCentreX() + std::sin (ang) * 10, kr.getCentreY() - std::cos (ang) * 10, 2);
            g.setColour (dim); g.setFont (font (10)); g.drawText (juce::String (sp.name), juce::Rectangle<float> (kr.getRight() + 3, kr.getY(), tw / 2 - 40, 26), juce::Justification::centredLeft, true);
        }
        g.setOpacity (1.0f); tiles_.push_back ({ t, s.uid });
    }

    // footswitches: guitar unit (ch1) and section unit (ch2)
    const Song* song = L.song(); Cue cue = L.nextCue();
    auto unitHdr = [&] (const juce::String& left, const juce::String& right) { auto h = r.removeFromTop (16); g.setColour (dim); g.setFont (font (11, true)); g.drawText (left, h, juce::Justification::centredLeft); g.drawText (right, h, juce::Justification::centredRight); r.removeFromTop (4); };
    auto drawUnit = [&] (const char* role, const juce::String (&names)[4], const juce::String (&subs)[4], const bool (&lit)[4], const Action::Type (&acts)[4], const uint32_t* sectionArgb = nullptr) {
        float sh = std::max (64.0f, std::min (96.0f, (r.getHeight() - 60) / 2.0f)); auto row = r.removeFromTop (sh); r.removeFromTop (12); float w = (row.getWidth() - 3 * gap) / 4;
        for (int i = 0; i < 4; ++i) {
            juce::Rectangle<float> b (row.getX() + (float) i * (w + gap), row.getY(), w, sh);
            bool next = cue.unit == role && cue.sw == 'A' + i;
            g.setColour (lit[i] ? accentFill.withAlpha (0.45f) : card); g.fillRoundedRectangle (b, 14);
            if (lit[i]) { g.setColour (orange); g.drawRoundedRectangle (b, 14, 2); }
            else if (next) { juce::Path p; p.addRoundedRectangle (b.reduced (1.5f), 14); float dl[] = { 7, 5 }; juce::PathStrokeType (3).createDashedStroke (p, p, dl, 2); g.setColour (pink.withAlpha (0.18f)); g.fillRoundedRectangle (b.expanded (5), 18); g.setColour (pink); g.fillPath (p); }
            else { g.setColour (cardBorder); g.drawRoundedRectangle (b, 14, 2); }
            if (sectionArgb && sectionArgb[i]) {   // v0.12: the target section's colour (same palette as the chart) as a bar + tint
                const juce::Colour sc (sectionArgb[i]); g.setColour (sc.withAlpha (0.16f)); g.fillRoundedRectangle (b.reduced (2), 12);
                g.setColour (sc); g.fillRoundedRectangle (b.getX() + 10, b.getBottom() - 7, b.getWidth() - 20, 4, 2); }
            g.setColour (lit[i] ? orange : next ? pink : dimmer); g.setFont (font (12, true)); g.drawText (juce::String::charToString ((juce::juce_wchar) ('A' + i)), b.reduced (0, 6).removeFromTop (14), juce::Justification::centred);
            juce::Rectangle<float> ring (b.getCentreX() - 14, b.getY() + 22, 28, 28); g.setColour (bg); g.fillEllipse (ring); g.setColour (lit[i] ? accentText : line); g.drawEllipse (ring, 2.5f);
            g.setColour (text); g.setFont (font (15, true)); g.drawText (names[i], b.withTrimmedTop (sh - 38).withHeight (18), juce::Justification::centred, true);
            g.setColour (next ? pink : dimmer); g.setFont (font (9, true)); g.drawText (subs[i], b.withTrimmedTop (sh - 20).withHeight (14), juce::Justification::centred, true);
            switches_.push_back ({ b, acts[i] });
        }
    };
    auto& ctl = P.store.lib.controllers;
    auto chName = [&] (const char* role) {
        auto& st = P.store.lib.settings; const bool gtr = std::string (role) == "guitar";
        const auto& id = gtr ? st.guitarPedalId : st.sectionPedalId; const auto& nm = gtr ? st.guitarPedalName : st.sectionPedalName;
        if (! id.empty()) return juce::String (nm.empty() ? "USB pedal" : nm);
        for (auto& c : ctl) if (c.role == role) return "MIDI ch " + juce::String (c.channel);
        return juce::String ("not mapped"); };
    unitHdr ("FOOTSWITCHES - GUITAR (Chocolate Plus) - " + chName ("guitar"), juce::String (P.midiActivity.load() % 2 ? "* " : "  ") + "USB via hub");
    { juce::String n[4] = { "Tap", "Boost", song && ! song->switchC.label.empty() ? juce::String (song->switchC.label) : juce::String ("-"), song && ! song->switchD.label.empty() ? juce::String (song->switchD.label) : juce::String ("-") };
      juce::String s[4] = { juce::String (L.bpm(), 0) + " BPM - HOLD OR DOUBLE-TAP: TUNER", L.boostOn() ? "FIXED - ON" : "FIXED", "THIS SONG", L.clickMuted() ? "HOLD OR DOUBLE-TAP: UNMUTE" : "HOLD OR DOUBLE-TAP: MUTE" };
      bool lit[4] = { false, L.boostOn(), L.switchCOn(), L.switchDOn() }; Action::Type a[4] = { Action::Tap, Action::Boost, Action::SwitchC, Action::SwitchD }; drawUnit ("guitar", n, s, lit, a); }
    unitHdr ("SECTIONS (Chocolate Plus) - " + chName ("section"), "spare in bag");
    { const auto& c = L.chart(); int i = L.sectionIndex(); juce::String nx = i + 1 < (int) c.sections.size() ? "-> " + juce::String (c.sections[(size_t) i + 1].name).toUpperCase() : juce::String ("LAST SECTION");
      juce::String n[4] = { "Play", "Next >", "Chorus", "Next Song" }; juce::String s[4] = { "HOLD OR DOUBLE-TAP: STOP", nx, "JUMP / LONG", "HOLD = PREV" };
      bool lit[4] = { false, false, false, false }; Action::Type a[4] = { Action::PlayStart, Action::NextSection, Action::JumpChorus, Action::NextSong };
      const auto tg = wr::sectionSwitchTargets (c, i, L.pass()); uint32_t col[4] = { 0, 0, 0, 0 }; const int idx[3] = { tg.prev, tg.next, tg.chorus };
      for (int k = 0; k < 3; ++k) if (idx[k] >= 0) col[k] = wr::colorArgbOr (wr::effectiveSectionColor (c.sections[(size_t) idx[k]], P.store.lib.settings.sectionColors), 0xff8a93a6);
      if (tg.chorus < 0) s[2] = "NO CHORUS";
      drawUnit ("section", n, s, lit, a, col); }

    // meters
    auto m = r.removeFromBottom (18); auto bar = [&] (const juce::String& t, float lvl, juce::Rectangle<float> a) {
        g.setColour (dim); g.setFont (font (12)); g.drawText (t, a.removeFromLeft (56), juce::Justification::centredLeft);
        for (int k = 0; k < 8; ++k) { float th2 = (float) k / 8.0f; g.setColour (lvl > th2 * 0.9f ? (k > 6 ? clip : k > 5 ? meterHot : green) : line); g.fillRect (a.getX() + (float) k * 9.0f, a.getY() + 4.0f, 7.0f, 10.0f); }
        float db = juce::Decibels::gainToDecibels (lvl, -60.0f); g.setColour (dim); g.drawText (juce::String (db, 0) + " dB", a.withTrimmedLeft (78), juce::Justification::centredLeft); };
    bar ("Input", std::sqrt (P.rig.inLevel.load()), m.removeFromLeft (m.getWidth() / 2)); bar ("Output", std::sqrt (std::max (P.rig.outPeakPre.load(), P.rig.outPeakPost.load())), m);
}

void RigPanel::mouseDown (const juce::MouseEvent& e) {
    held_ = -1; heldLong_ = false;
    for (int i = 0; i < (int) switches_.size(); ++i)
        if (switches_[(size_t) i].first.contains (e.position) && switches_[(size_t) i].second == wr::Action::PlayStart) {
            held_ = i; heldAt_ = juce::Time::getMillisecondCounterHiRes(); startTimerHz (20); return;
        }
}
void RigPanel::timerCallback() {
    if (held_ < 0 || heldLong_) return;
    if (juce::Time::getMillisecondCounterHiRes() - heldAt_ >= 700.0) { // same 0.7 s as the section footswitch
        heldLong_ = true; wr::Action a; a.type = wr::Action::TransportStop; ed.proc.live().handle (a, ed.proc.nowSec()); repaint();
    }
}
void RigPanel::mouseUp (const juce::MouseEvent& e) {
    stopTimer();
    auto& P = ed.proc; auto p = e.position;
    for (auto& t : tiles_) if (t.first.contains (p)) { P.setPedal (t.second, ! P.pedalOn (t.second)); held_ = -1; repaint(); return; } // tap tile = toggle
    if (takeLead_.contains (p)) { P.sync().takeLead (P.nowSec()); held_ = -1; repaint(); return; }
    for (auto& c : ctl_) if (c.first.contains (p)) { auto& L = P.live();
        if (c.second == 0) L.setClickOn (! L.clickOn()); else if (c.second == 1) L.toggleClickMute(); else if (c.second == 2) L.setTunerOpen (true); else P.sync().requestPlay (! L.playing(), P.nowSec());
        held_ = -1; repaint(); return; }
    for (int i = 0; i < (int) switches_.size(); ++i) if (switches_[(size_t) i].first.contains (p)) {
        if (switches_[(size_t) i].second == wr::Action::PlayStart) {
            if (! heldLong_) { wr::Action a; a.type = wr::Action::PlayStart; P.live().handle (a, P.nowSec()); } // a long hold already sent Stop
        } else { wr::Action a; a.type = switches_[(size_t) i].second; P.live().handle (a, P.nowSec()); }
        held_ = -1; repaint(); return;
    }
    held_ = -1;
}
