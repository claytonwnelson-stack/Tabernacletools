#include "MultitrackLive.h"

namespace wr::mt {

std::string MultitrackLive::loadSong (const Song* s, const LiveSession& live, const Opener& open, bool allowed, bool sync) {
    unload();
    if (! s || s->multitrack.empty()) return {};
    if (! allowed) return "Backing tracks need a subscription";
    stemsWanted_ = (int) s->multitrack.stems.size();
    std::vector<std::unique_ptr<StemSource>> src; int missing = 0;
    for (size_t i = 0; i < s->multitrack.stems.size() && (int) i < kMaxStems; ++i) { auto x = open ? open (s->multitrack.stems[i].file) : nullptr; if (! x) ++missing; src.push_back (std::move (x)); }
    MultitrackSet set = s->multitrack; fillMissingSectionBeats (set, s->chart);
    auto seq = buildSequence (set, s->chart, live.chart(), &warning_);
    if (seq.empty()) { warning_ = "Mark where the sections start on the tracks first"; return warning_; }
    // drop the stems that failed to open but keep the mix indices aligned
    MultitrackSet mix = set; std::vector<std::unique_ptr<StemSource>> ok; mix.stems.clear();
    for (size_t i = 0; i < src.size(); ++i) if (src[i]) { mix.stems.push_back (set.stems[i]); ok.push_back (std::move (src[i])); }
    if (! p_.load (mix, std::move (ok), std::move (seq), sync)) return "Couldn't open the backing tracks";
    songId_ = s->id; lastChanges_ = -1; wasPlaying_ = false; started_ = false;
    if (missing) return std::to_string (missing) + " stem file(s) missing";
    return stemsWanted_ > kMaxStems ? loadWarning (load (0)) : std::string();
}
void MultitrackLive::arrangementChanged (const Song* s, const LiveSession& live) {
    if (! active() || ! s || s->id != songId_) return;
    MultitrackSet set = s->multitrack; fillMissingSectionBeats (set, s->chart);
    p_.setSequence (buildSequence (set, s->chart, live.chart(), &warning_));
}
bool MultitrackLive::handle (const Action& a, LiveSession& live, double now, bool allowed) {
    const bool tracksAction = a.type == Action::TracksNext || a.type == Action::TracksLoop || a.type == Action::TracksStop;
    if (! active() || live.following()) return tracksAction;   // nothing to do, but never pass a tracks action on
    if (! allowed) { if (p_.playing()) p_.stop(); return true; }
    const Quant q = live.library().settings.trackJumpAt == 1 ? Quant::SectionEnd : Quant::NextBar;
    const int n = (int) live.chart().sections.size(), cur = p_.current();
    auto jumpTo = [&] (int idx) {
        if (idx < 0 || idx >= n) return;
        if (! p_.playing()) { p_.locate (idx); live.practiceSection (idx, now); return; }
        p_.jump (idx, q); live.followCue (idx, 1, true);      // spoken 1-bar cue on the click bus; lands with the stems
    };
    switch (a.type) {
        case Action::TracksPlay: case Action::Play: return false;   // the normal PLAY path (band sync quantises it); stems start on the count-in hook
        case Action::TracksStop: live.setPlaying (false, now); p_.stop(); return true;
        case Action::TracksLoop: p_.loop (p_.loopSection() < 0); return true;
        case Action::TracksNext: case Action::NextSection: jumpTo (cur + 1); return true;
        case Action::PrevSection: jumpTo (cur - 1); return true;
        case Action::GotoSection: jumpTo (a.value); return true;
        case Action::JumpChorus: { for (int i = cur + 1; i < n; ++i) if (live.chart().sections[(size_t) i].kind == "chorus") { jumpTo (i); return true; }
                                   for (int i = 0; i < n; ++i) if (live.chart().sections[(size_t) i].kind == "chorus") { jumpTo (i); return true; } return true; }
        default: return false;
    }
}
void MultitrackLive::tick (LiveSession& live, double now) {
    live.setTracksDrive (active() && p_.playing() && ! live.following());
    if (! active()) return;
    const bool lp = live.playing();
    if (lp && ! wasPlaying_ && ! started_ && ! p_.playing()) p_.play (0);   // started without a count-in
    if (! lp && wasPlaying_) p_.stop();
    if (! lp) started_ = false;
    wasPlaying_ = lp;
    const int ch = p_.sectionChanges();
    if (ch != lastChanges_) { lastChanges_ = ch; const int c = p_.current(); if (c >= 0) live.practiceSection (c, now); }
    if (p_.ended() && lp) live.setPlaying (false, now);
}

} // namespace wr::mt
