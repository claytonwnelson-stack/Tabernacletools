#include "LiveSession.h"
#include "../midi/MidiClock.h"
#include <cmath>

namespace wr {

const Song* LiveSession::song() const {
    if (songIdx_ < 0 || songIdx_ >= (int) set_.entries.size()) return nullptr;
    return const_cast<Library&> (lib_).song (set_.entries[(size_t) songIdx_].songId);
}

void LiveSession::start (const Setlist& set, int songIndex, double now) { set_ = set; rigName_.clear(); loadSong (songIndex, now); }

void LiveSession::loadSong (int idx, double now) {
    if (set_.entries.empty()) { chart_ = Chart(); ++version_; return; }
    songIdx_ = std::clamp (idx, 0, (int) set_.entries.size() - 1);
    const auto& e = set_.entries[(size_t) songIdx_]; const Song* s = song();
    if (! s) { chart_ = Chart(); ++version_; return; }
    rebuildChart();
    if (s->rigPreset != rigName_) { if (auto* r = lib_.rig (s->rigPreset)) { sink_.loadRig (*r); rigName_ = s->rigPreset; } }
    boost_ = swC_ = swD_ = false; sink_.setPedal ("boost", false);
    if (! s->switchC.pedalUid.empty()) sink_.setPedal (s->switchC.pedalUid, false);
    if (! s->switchD.pedalUid.empty()) sink_.setPedal (s->switchD.pedalUid, false);
    songBpm_ = clampBpm (e.bpmOverride > 0 ? e.bpmOverride : (s->chart.bpm > 0 ? s->chart.bpm : songBpm_)); if (songBpm_ <= 0) songBpm_ = kDefaultBpm;
    tap_.reset(); applyTempo();
    view_ = View::Full; sceneName_.clear(); pendingIdx_ = -1; songEnded_ = false;
    sink_.setMeter (beatsPerBar()); setClickOn (s->clickDefault); playing_ = false; beatPos_ = 0;
    enterSection (0, 1, now);
}

void LiveSession::enterSection (int idx, int pass, double) {
    const Song* s = song(); if (! s || chart_.sections.empty()) { ++version_; return; }
    secIdx_ = std::clamp (idx, 0, (int) chart_.sections.size() - 1); pass_ = pass; songEnded_ = false;
    if (const Scene* sc = s->sceneForSection (chart_.sections[(size_t) secIdx_])) { if (sc->name != sceneName_) { sink_.applyScene (*sc); sceneName_ = sc->name; } }
    if (playing_) ++midiLocateGen_; // v0.17: section jump while playing -> MIDI SPP
    ++version_;
}

void LiveSession::gotoSection (int idx, double now) { if (following_) return; if (idx < 0 || idx >= (int) chart_.sections.size()) return; sectionCommand (now); requestSection (idx, 1, now); }
void LiveSession::requestSection (int idx, int pass, double now) {
    if (! quantizing()) { pendingIdx_ = -1; enterSection (idx, pass, now); return; }
    pendingIdx_ = idx; pendingPass_ = pass; ++cueSeq_;
    const auto& sec = chart_.sections[(size_t) idx];
    (void) sec; sink_.announceSection (lib_.settings.voiceCues ? cueName (idx, pass) : std::string(), cueSeq_);
    ++version_;
}
std::string LiveSession::cueName (int idx, int pass) const {
    if (idx < 0 || idx >= (int) chart_.sections.size()) return {}; const auto& sec = chart_.sections[(size_t) idx];
    return (pass > 1 && sec.repeat > 1) || (pass == 1 && sec.occurrence > 1) ? sec.name + " again" : sec.name;
}
void LiveSession::loadSetlist (const Setlist& set, int songIndex, double now) { set_ = set; loadSong (songIndex, now); }
void LiveSession::followSection (int idx, int pass, double now) {
    if (chart_.sections.empty()) return; pendingIdx_ = -1; view_ = View::Section; lastSectionCmd_ = now;
    enterSection (idx, pass, now); if (beatPos_ >= 0) beatPos_ = 0;
}
void LiveSession::followCue (int idx, int pass, bool speak) {
    if (idx < 0 || idx >= (int) chart_.sections.size()) return;
    pendingIdx_ = idx; pendingPass_ = pass; ++cueSeq_;
    if (speak) sink_.announceSection (lib_.settings.voiceCues ? cueName (idx, pass) : std::string(), cueSeq_);
    ++version_;
}
void LiveSession::followPlay (bool on, int ci, double now) {
    if (playing_ && ! on && inLastSection()) songEnded_ = true;   // the leader's song ran off its end
    playing_ = on; lastTick_ = now;
    if (on) { if (! clickOn_) { clickHeldOff_ = true; setClickOn (true); } beatPos_ = -(double) ci; if (ci > 0) sink_.countIn (ci, lib_.settings.countInSpoken); }
    else { sink_.countIn (0, false); if (clickHeldOff_) { clickHeldOff_ = false; setClickOn (false); } }
    ++version_;
}
int LiveSession::countInBeatsForSong() const {
    const Song* s = song(); int code = s && s->countIn >= 0 ? s->countIn : lib_.settings.countIn; return countInBeats (code, beatsPerBar());
}
void LiveSession::gotoSong (int idx, double now) { if (! following_) loadSong (idx, now); }
void LiveSession::setBpm (double b) { b = clampBpm (b); if (b <= 0) return; songBpm_ = b; applyTempo(); ++version_; }
void LiveSession::tapTempo (double now) { double b = tap_.tap (now); if (b > 0) setBpm (b); }
double LiveSession::storedSongBpm() const {
    if (set_.entries.empty() || ! song()) return 0; const auto& e = set_.entries[(size_t) songIdx_];
    return clampBpm (e.bpmOverride > 0 ? e.bpmOverride : song()->chart.bpm);
}
bool LiveSession::saveTempoToSong() {
    const Song* cs = song(); if (! cs) return false; Song* s = lib_.song (cs->id); if (! s) return false;
    s->chart.bpm = songBpm_; chart_.bpm = songBpm_;
    set_.entries[(size_t) songIdx_].bpmOverride = 0;
    if (auto* sl = lib_.setlist (set_.date)) if (songIdx_ < (int) sl->entries.size() && sl->entries[(size_t) songIdx_].songId == cs->id) sl->entries[(size_t) songIdx_].bpmOverride = 0;
    ++version_; return true;
}
void LiveSession::setKeyOverride (const std::string& key) {
    if (set_.entries.empty()) return; set_.entries[(size_t) songIdx_].keyOverride = key;
    if (auto* sl = lib_.setlist (set_.date)) if (songIdx_ < (int) sl->entries.size()) sl->entries[(size_t) songIdx_].keyOverride = key;
    rebuildChart(); ++version_;
}

int beatsPerBarFor (const std::string& t) { int n = std::atoi (t.c_str()); return n >= 1 && n <= 16 ? n : 4; }
void LiveSession::rebuildChart() {
    const Song* s = song(); if (! s) { chart_ = Chart(); return; } const auto& e = set_.entries[(size_t) songIdx_];
    Chart written = s->chart;
    if (following_ && followColors_) { auto it = followSongColors_.find (s->id); clearSectionColorOverrides (written);
        if (it != followSongColors_.end()) for (size_t i = 0; i < written.sections.size() && i < it->second.size(); ++i) written.sections[i].customColor = it->second[i]; }
    chart_ = arrangedChart (chartForPart (written, e.keyOverride, e.capoOverride), e.arrangement.empty() ? s->chart.arrangement : e.arrangement);
    applySectionColors (chart_, sectionColorDefaults());
}
std::vector<std::string> LiveSession::arrangement() const {
    const Song* s = song(); if (! s) return {}; const auto& e = set_.entries[(size_t) songIdx_]; return e.arrangement.empty() ? s->chart.arrangement : e.arrangement;
}
void LiveSession::setArrangement (const std::vector<std::string>& a, bool forSetlist) {
    const Song* cs = song(); if (! cs) return;
    if (forSetlist) { set_.entries[(size_t) songIdx_].arrangement = a;
        if (auto* sl = lib_.setlist (set_.date)) if (songIdx_ < (int) sl->entries.size() && sl->entries[(size_t) songIdx_].songId == cs->id) sl->entries[(size_t) songIdx_].arrangement = a; }
    else if (Song* s = lib_.song (cs->id)) s->chart.arrangement = a;
    rebuildChart(); pendingIdx_ = -1; secIdx_ = std::clamp (secIdx_, 0, std::max (0, (int) chart_.sections.size() - 1)); pass_ = 1; ++version_;
}
void LiveSession::practiceSection (int idx, double now) {
    if (following_ || idx < 0 || idx >= (int) chart_.sections.size() || (idx == secIdx_ && pendingIdx_ < 0)) return;
    pendingIdx_ = -1; songEnded_ = false; lastSectionCmd_ = now; enterSection (idx, 1, now);
}
void LiveSession::setNotes (const std::vector<ServiceNote>& n) {
    if (set_.entries.empty()) return; set_.entries[(size_t) songIdx_].notes = n;
    if (auto* sl = lib_.setlist (set_.date)) if (songIdx_ < (int) sl->entries.size() && sl->entries[(size_t) songIdx_].songId == set_.entries[(size_t) songIdx_].songId) sl->entries[(size_t) songIdx_].notes = n;
    ++version_;
}
void LiveSession::refreshSetlist (const Setlist& s) {
    if (s.date != set_.date || s.name != set_.name) return;
    bool same = s.entries.size() == set_.entries.size(); for (size_t i = 0; same && i < s.entries.size(); ++i) same = s.entries[i].songId == set_.entries[i].songId;
    const bool sameArr = same && songIdx_ < (int) s.entries.size() && s.entries[(size_t) songIdx_].arrangement == set_.entries[(size_t) songIdx_].arrangement;
    set_ = s;
    if (! same) { loadSong (std::min (songIdx_, (int) set_.entries.size() - 1), 0); return; }
    rebuildChart(); if (! sameArr) { pendingIdx_ = -1; pass_ = 1; } secIdx_ = std::clamp (secIdx_, 0, std::max (0, (int) chart_.sections.size() - 1)); ++version_;
}
NextSongInfo LiveSession::nextSongInfo() const {
    NextSongInfo n; const int i = songIdx_ + 1; if (i >= (int) set_.entries.size()) return n;
    const auto& e = set_.entries[(size_t) i]; const Song* s = const_cast<Library&> (lib_).song (e.songId); if (! s) return n;
    Chart c = arrangedChart (chartForPart (s->chart, e.keyOverride, e.capoOverride), e.arrangement.empty() ? s->chart.arrangement : e.arrangement);
    n.valid = true; n.index = i; n.title = c.title; n.key = c.key; n.capo = c.capo; n.capoText = capoLabel (c.key, c.capo); n.time = c.time.empty() ? "4/4" : c.time;
    n.bpm = clampBpm (e.bpmOverride > 0 ? e.bpmOverride : s->chart.bpm); n.firstChord = firstChord (c); return n;
}
std::vector<ServiceNote> LiveSession::activeNotes() const {
    std::vector<ServiceNote> out; if (songIdx_ < 0 || songIdx_ >= (int) set_.entries.size()) return out;
    const auto& e = set_.entries[(size_t) songIdx_]; const int n = (int) chart_.sections.size();
    for (auto& note : e.notes) {
        const std::string at = note.at.empty() ? std::string ("start") : note.at; bool on = false;
        if (at == "start") on = ! songEnded_ && secIdx_ == 0 && pass_ == 1;
        else if (at == "end") on = songEnded_ || inLastSection();
        else if (at == "after") on = songEnded_;
        else if (n && ! songEnded_) { const auto& cur = chart_.sections[(size_t) secIdx_]; const Song* s = song();
            int src = s ? resolveArrangementToken (s->chart, at) : -1; on = src >= 0 ? cur.source == src : false; }
        if (on) out.push_back (note);
    }
    return out;
}
void LiveSession::setCapoOverride (int capo) {
    if (set_.entries.empty()) return; capo = std::clamp (capo, -1, 11); set_.entries[(size_t) songIdx_].capoOverride = capo;
    if (auto* sl = lib_.setlist (set_.date)) if (songIdx_ < (int) sl->entries.size()) sl->entries[(size_t) songIdx_].capoOverride = capo;
    rebuildChart(); ++version_;
}
void LiveSession::setClickOn (bool on) { clickOn_ = on; sink_.setClick (on, lib_.settings.clickMuted); ++version_; }
void LiveSession::toggleClickMute() { lib_.settings.clickMuted = ! lib_.settings.clickMuted; settingsDirty_ = true; sink_.setClick (clickOn_, lib_.settings.clickMuted); ++version_; }
void LiveSession::setTunerOpen (bool open) { if (open == tunerOpen_) return; tunerOpen_ = open; sink_.setTunerOpen (open); ++version_; }
void LiveSession::setPlaying (bool p, double now) {
    playing_ = p; lastTick_ = now;
    // Count-in clicks even when the metronome switch is off, then that override ends.
    // If the switch was off, Play turns it on for the song and Stop puts it back.
    if (p) { if (! clickOn_) { clickHeldOff_ = true; setClickOn (true); } const int ci = countInBeatsForSong(); beatPos_ = -(double) ci; sink_.restartBeat(); if (ci > 0) sink_.countIn (ci, lib_.settings.countInSpoken); }
    else { sink_.countIn (0, false); if (clickHeldOff_) { clickHeldOff_ = false; setClickOn (false); } }
    ++version_;
}

void LiveSession::handle (const Action& a, double now) {
    const Song* s = song(); const int nSec = (int) chart_.sections.size();
    if (following_) switch (a.type) { // the leader drives position, tempo and transport
        case Action::NextSection: case Action::PrevSection: case Action::JumpChorus: case Action::GotoSection: case Action::NextSong:
        case Action::PrevSong: case Action::Tap: case Action::Play: case Action::PlayStart: case Action::TransportStart: case Action::ClockBpm: return;
        default: break; }
    switch (a.type) {
        case Action::NextSection: {
            sectionCommand (now); int ci = pendingIdx_ >= 0 ? pendingIdx_ : secIdx_, cp = pendingIdx_ >= 0 ? pendingPass_ : pass_;
            if (nSec && cp < chart_.sections[(size_t) ci].repeat) requestSection (ci, cp + 1, now);
            else if (ci + 1 < nSec) requestSection (ci + 1, 1, now);
            break; }
        case Action::PrevSection: { sectionCommand (now); int ci = pendingIdx_ >= 0 ? pendingIdx_ : secIdx_, cp = pendingIdx_ >= 0 ? pendingPass_ : pass_;
            if (cp > 1) requestSection (ci, cp - 1, now); else if (ci > 0) requestSection (ci - 1, 1, now); break; }
        case Action::JumpChorus: {
            sectionCommand (now); int target = -1, from = pendingIdx_ >= 0 ? pendingIdx_ : secIdx_;
            for (int i = from + 1; i < nSec && target < 0; ++i) if (chart_.sections[(size_t) i].kind == "chorus") target = i;
            for (int i = nSec - 1; i >= 0 && target < 0; --i) if (chart_.sections[(size_t) i].kind == "chorus") target = i;
            if (target >= 0) requestSection (target, 1, now);
            break; }
        case Action::GotoSection: if (a.value >= 0 && a.value < nSec) { sectionCommand (now); requestSection (a.value, 1, now); } break;
        case Action::NextSong: if (songIdx_ + 1 < (int) set_.entries.size()) loadSong (songIdx_ + 1, now); break;
        case Action::PrevSong: if (songIdx_ > 0) loadSong (songIdx_ - 1, now); break;
        case Action::Tap: tapTempo (now); break;
        case Action::ClockBpm: {
            double b = clampBpm (a.bpm); if (b <= 0) break;
            bool changed = ! clockActive() || std::abs (b - clockBpm_) >= 0.05; clockBpm_ = b; lastClockT_ = now;
            if (changed) { applyTempo(); ++version_; } break; }
        case Action::Boost: boost_ = ! boost_; sink_.setPedal ("boost", boost_); ++version_; break;
        case Action::SwitchC: if (s && ! s->switchC.pedalUid.empty()) { swC_ = ! swC_; sink_.setPedal (s->switchC.pedalUid, swC_); ++version_; } break;
        case Action::SwitchD: if (s && ! s->switchD.pedalUid.empty()) { swD_ = ! swD_; sink_.setPedal (s->switchD.pedalUid, swD_); ++version_; } break;
        case Action::TunerToggle: setTunerOpen (! tunerOpen_); break;
        case Action::ClickMute: toggleClickMute(); break;
        case Action::ClickToggle: setClickOn (! clickOn_); break;
        case Action::Play: setPlaying (! playing_, now); break;
        case Action::PlayStart: if (! playing_) setPlaying (true, now); break; // short press starts; it does not stop
        case Action::TransportStart: // external MIDI Start/Continue: follow transport + align the click
            if (! playing_) setPlaying (true, now); else { sink_.restartBeat(); if (beatPos_ < 0) beatPos_ = 0; lastTick_ = now; }
            break;
        case Action::TransportStop: if (playing_) setPlaying (false, now); break;
        case Action::SongPosition: seekAbsoluteBeats (sppToBeats (a.value), now); break;
        case Action::ToggleView: view_ = view_ == View::Full ? View::Section : View::Full; lastSectionCmd_ = now; ++version_; break;
        default: break;
    }
}

void LiveSession::tick (double now) {
    if (pendingIdx_ >= 0 && ! following_ && ! tracksDrive_ && sink_.cueDue (cueSeq_)) { int i = pendingIdx_, p = pendingPass_; pendingIdx_ = -1; enterSection (i, p, now); if (beatPos_ >= 0) beatPos_ = 0; } // the change lands on a bar line
    // beat-based auto-advance: only without MIDI clock (Cubase sends its own section cues), after Play
    if (playing_ && ! following_ && ! tracksDrive_ && lastTick_ >= 0 && ! clockActive() && lib_.settings.autoAdvance && ! chart_.sections.empty()) {
        beatPos_ += std::max (0.0, now - lastTick_) * bpm() / 60.0;
        for (int guard = 0; guard < 4; ++guard) {
            const auto& sec = chart_.sections[(size_t) secIdx_]; if (sec.bars <= 0) break;
            const double len = (double) sec.bars * beatsPerBar(); if (beatPos_ + 1e-9 < len) break;
            beatPos_ -= len; const int n = (int) chart_.sections.size();
            if (pass_ < sec.repeat) enterSection (secIdx_, pass_ + 1, now);
            else if (secIdx_ + 1 < n) enterSection (secIdx_ + 1, 1, now);
            else { playing_ = false; songEnded_ = true; ++version_; break; }
        }
    }
    lastTick_ = now;
    if (clockActive() && now - lastClockT_ > clockTimeoutSec (clockBpm_)) { lastClockT_ = -1; applyTempo(); ++version_; } // clock stopped: back to the song tempo
    if (view_ == View::Section && lib_.settings.autoReturnSec > 0 && now - lastSectionCmd_ > lib_.settings.autoReturnSec) { view_ = View::Full; ++version_; }
}

std::string LiveSession::nextSongTitle() const {
    if (songIdx_ + 1 >= (int) set_.entries.size()) return {};
    auto* s = const_cast<Library&> (lib_).song (set_.entries[(size_t) songIdx_ + 1].songId); return s ? s->chart.title : std::string();
}
std::string LiveSession::performerCue (int i) const {
    const Song* s = song(); if (! s || i < 0 || i >= (int) chart_.sections.size()) return {};
    auto it = s->cues.find (chart_.sections[(size_t) i].name); return it == s->cues.end() ? std::string() : it->second;
}

double LiveSession::absoluteBeats() const {
    const int bpb = beatsPerBar(); double b = 0;
    for (int i = 0; i < secIdx_ && i < (int) chart_.sections.size(); ++i) {
        const int bars = chart_.sections[(size_t) i].bars; if (bars > 0) b += (double) bars * bpb * std::max (1, chart_.sections[(size_t) i].repeat);
    }
    // within the current pass only (repeat passes restart the section's bars)
    if (secIdx_ >= 0 && secIdx_ < (int) chart_.sections.size()) {
        const int bars = chart_.sections[(size_t) secIdx_].bars;
        if (bars > 0 && pass_ > 1) b += (double) bars * bpb * (pass_ - 1);
    }
    return b + std::max (0.0, beatPos_);
}
void LiveSession::seekAbsoluteBeats (double beats, double now) {
    beats = std::max (0.0, beats); const int bpb = std::max (1, beatsPerBar());
    double rem = beats; int idx = 0, pass = 1;
    for (; idx < (int) chart_.sections.size(); ++idx) {
        const auto& sec = chart_.sections[(size_t) idx]; const int bars = sec.bars > 0 ? sec.bars : 0;
        if (bars <= 0) { if (idx + 1 >= (int) chart_.sections.size()) break; continue; }
        const double secLen = (double) bars * bpb; const int reps = std::max (1, sec.repeat);
        for (pass = 1; pass <= reps; ++pass) {
            if (rem < secLen - 1e-9) { enterSection (idx, pass, now); beatPos_ = rem; lastTick_ = now; sink_.restartBeat(); ++version_; return; }
            rem -= secLen;
        }
    }
    if (! chart_.sections.empty()) { enterSection ((int) chart_.sections.size() - 1, pass, now); beatPos_ = rem; lastTick_ = now; ++version_; }
}
Cue LiveSession::nextCue() const {
    Cue c; const int n = (int) chart_.sections.size();
    if (n && pass_ < chart_.sections[(size_t) secIdx_].repeat) { c.unit = "section"; c.sw = 'B'; c.text = "Next: " + chart_.sections[(size_t) secIdx_].label + " again (" + std::to_string (pass_ + 1) + "/" + std::to_string (chart_.sections[(size_t) secIdx_].repeat) + ")"; return c; }
    if (secIdx_ + 1 < n) { auto& nx = chart_.sections[(size_t) secIdx_ + 1]; c.unit = "section"; c.sw = 'B'; c.text = "Next: " + nx.name; auto pc = performerCue (secIdx_ + 1); if (! pc.empty()) c.text += "  -  " + pc; return c; }
    auto t = nextSongTitle(); if (! t.empty()) { c.unit = "section"; c.sw = 'D'; c.text = "Next song: " + t; return c; }
    c.text = "End of set"; return c;
}
} // namespace wr
