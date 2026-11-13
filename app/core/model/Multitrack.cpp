#include "Multitrack.h"
#include "Song.h"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>

namespace wr::mt {

static std::string lower (std::string s) { for (auto& c : s) c = (char) std::tolower ((unsigned char) c); return s; }
static std::string trim (const std::string& s) { size_t a = s.find_first_not_of (" \t\r\n"), b = s.find_last_not_of (" \t\r\n"); return a == std::string::npos ? std::string() : s.substr (a, b - a + 1); }

// ---------------------------------------------------------------- tempo map (piecewise constant)
double TempoMap::bpmAt (double beat) const {
    if (pts.empty()) return 120; double b = pts.front().bpm;
    for (auto& p : pts) { if (p.beat <= beat + 1e-9) b = p.bpm; else break; }
    return b > 0 ? b : 120;
}
double TempoMap::secAt (double beat) const {
    if (pts.empty()) return offsetSec + beat * 0.5;
    if (beat <= pts.front().beat) return offsetSec + beat * 60.0 / std::max (1.0, pts.front().bpm);
    double t = offsetSec + pts.front().beat * 60.0 / std::max (1.0, pts.front().bpm);
    for (size_t i = 0; i < pts.size(); ++i) {
        const double b0 = pts[i].beat, b1 = i + 1 < pts.size() ? pts[i + 1].beat : 1e18, spb = 60.0 / std::max (1.0, pts[i].bpm);
        if (beat <= b1) return t + (beat - b0) * spb;
        t += (b1 - b0) * spb;
    }
    return t;
}
double TempoMap::beatAt (double sec) const {
    if (pts.empty()) return (sec - offsetSec) * 2.0;
    const double first = secAt (pts.front().beat);
    if (sec <= first) return (sec - offsetSec) * std::max (1.0, pts.front().bpm) / 60.0;
    double t = first;
    for (size_t i = 0; i < pts.size(); ++i) {
        const double b0 = pts[i].beat, spb = 60.0 / std::max (1.0, pts[i].bpm);
        const double segT = i + 1 < pts.size() ? (pts[i + 1].beat - b0) * spb : 1e18;
        if (sec <= t + segT) return b0 + (sec - t) / spb;
        t += segT;
    }
    return 0;
}

void setTempoAt (TempoMap& t, double beat, double bpm) {
    beat = std::max (0.0, std::round (beat)); bpm = std::clamp (bpm, 20.0, 400.0);
    for (auto& p : t.pts) if (std::abs (p.beat - beat) < 1e-6) { p.bpm = bpm; return; }
    t.pts.push_back ({ beat, bpm }); std::sort (t.pts.begin(), t.pts.end(), [] (auto& a, auto& b) { return a.beat < b.beat; });
}
bool removeTempoPoint (TempoMap& t, size_t i) { if (t.pts.size() <= 1 || i >= t.pts.size()) return false; t.pts.erase (t.pts.begin() + (long) i); return true; }
std::string tempoMapText (const TempoMap& t, int bpb) {
    std::string s; char b[64];
    for (auto& p : t.pts) { std::snprintf (b, sizeof b, "%sBar %d: %g BPM", s.empty() ? "" : " - ", (int) std::floor (p.beat / std::max (1, bpb)) + 1, p.bpm); s += b; }
    return s.empty() ? std::string ("120 BPM") : s;
}

// ---------------------------------------------------------------- JSON
Json toJson (const MultitrackSet& m) {
    Json st = Json::array(), tp = Json::array(), sb = Json::array();
    for (auto& s : m.stems) { Json o = Json::object().set ("id", s.id).set ("name", s.name).set ("file", s.file).set ("color", s.color)
        .set ("vol", s.volumeDb).set ("pan", s.pan).set ("mute", s.mute).set ("solo", s.solo).set ("bus", s.bus == Bus::Cue ? "cue" : "main");
        if (! s.sharedSession.empty()) o.set ("shared", s.sharedSession); st.push (std::move (o)); }
    for (auto& p : m.tempo.pts) tp.push (Json::object().set ("beat", p.beat).set ("bpm", p.bpm));
    for (double b : m.sectionBeats) sb.push (b);
    return Json::object().set ("stems", st).set ("offset", m.tempo.offsetSec).set ("tempo", tp).set ("bpb", m.beatsPerBar)
        .set ("sections", sb).set ("length", m.lengthSec).set ("rate", m.sampleRate);
}
MultitrackSet multitrackFromJson (const Json& j) {
    MultitrackSet m;
    for (auto& o : j["stems"].items()) { if ((int) m.stems.size() >= kMaxStems) break; Stem s; s.id = o["id"].str(); s.name = o["name"].str(); s.file = o["file"].str();
        s.color = o["color"].str(); s.volumeDb = (float) std::clamp (o["vol"].num(), -60.0, 12.0); s.pan = (float) std::clamp (o["pan"].num(), -1.0, 1.0);
        s.mute = o["mute"].boolean(); s.solo = o["solo"].boolean(); s.bus = o["bus"].str() == "cue" ? Bus::Cue : Bus::Main; s.sharedSession = o["shared"].str(); m.stems.push_back (s); }
    m.tempo.offsetSec = j["offset"].num(); for (auto& p : j["tempo"].items()) m.tempo.pts.push_back ({ p["beat"].num(), std::clamp (p["bpm"].num (120), 20.0, 400.0) });
    std::sort (m.tempo.pts.begin(), m.tempo.pts.end(), [] (auto& a, auto& b) { return a.beat < b.beat; });
    m.beatsPerBar = std::clamp (j["bpb"].integer (4), 1, 16);
    for (auto& b : j["sections"].items()) m.sectionBeats.push_back (b.num (-1));
    m.lengthSec = j["length"].num(); m.sampleRate = j["rate"].num (48000);
    return m;
}

// ---------------------------------------------------------------- mix helpers
bool stemAudible (const MultitrackSet& m, size_t i) {
    if (i >= m.stems.size() || m.stems[i].mute) return false;
    bool anySolo = false; for (auto& s : m.stems) anySolo |= s.solo;
    return ! anySolo || m.stems[i].solo;
}
float stemGain (const MultitrackSet& m, size_t i) { return stemAudible (m, i) ? std::pow (10.0f, m.stems[i].volumeDb / 20.0f) : 0.0f; }
void panGains (float pan, float& l, float& r) { const float a = (std::clamp (pan, -1.0f, 1.0f) + 1.0f) * 0.25f * 3.14159265f; l = std::cos (a) * 1.41421356f; r = std::sin (a) * 1.41421356f; } // centre = unity
std::vector<std::string> mutedStemNames (const MultitrackSet& m) { std::vector<std::string> v; for (auto& s : m.stems) if (s.mute) v.push_back (s.name); return v; }
int muteStemsMatching (MultitrackSet& m, const std::string& w) { int n = 0; const auto lw = lower (w); if (lw.empty()) return 0;
    for (auto& s : m.stems) if (lower (s.name).find (lw) != std::string::npos) { s.mute = true; ++n; } return n; }
std::string guessStemColor (const std::string& name) {
    const auto n = lower (name); auto has = [&] (const char* k) { return n.find (k) != std::string::npos; };
    if (has ("click") || has ("cue") || has ("guide")) return "grey";
    if (has ("drum") || has ("kick") || has ("snare") || has ("perc") || has ("loop")) return "red";
    if (has ("bass") || has ("sub")) return "purple";
    if (has ("key") || has ("piano") || has ("pad") || has ("synth") || has ("organ") || has ("rhodes")) return "blue";
    if (has ("gtr") || has ("guitar") || has ("eg") || has ("ag")) return "orange";
    if (has ("vox") || has ("vocal") || has ("bgv") || has ("choir")) return "pink";
    if (has ("string") || has ("orch")) return "teal";
    return "green";
}
std::string stemNameFromFile (const std::string& file) {
    std::string s = file; auto sl = s.find_last_of ("/\\"); if (sl != std::string::npos) s = s.substr (sl + 1);
    auto dot = s.find_last_of ('.'); if (dot != std::string::npos && dot > 0) s = s.substr (0, dot);
    size_t i = 0; while (i < s.size() && (std::isdigit ((unsigned char) s[i]) || s[i] == ' ' || s[i] == '-' || s[i] == '_' || s[i] == '.')) ++i;
    if (i < s.size()) s = s.substr (i);
    for (auto& c : s) if (c == '_') c = ' ';
    s = trim (s); return s.empty() ? std::string ("Track") : s;
}

// ---------------------------------------------------------------- markers
int applyFileMarkers (MultitrackSet& m, const Chart& w, const std::vector<FileMarker>& marks) {
    m.sectionBeats.resize (w.sections.size(), -1.0); int n = 0; std::vector<bool> used (w.sections.size(), false);
    for (auto& fm : marks) { const auto l = lower (trim (fm.label)); if (l.empty()) continue;
        for (size_t i = 0; i < w.sections.size(); ++i) if (! used[i] && (lower (w.sections[i].name) == l || lower (w.sections[i].label) == l)) {
            used[i] = true; m.sectionBeats[i] = m.tempo.beatAt (fm.sec); ++n; break; } }
    return n;
}
void fillMissingSectionBeats (MultitrackSet& m, const Chart& w) {
    m.sectionBeats.resize (w.sections.size(), -1.0);
    for (size_t i = 0; i < w.sections.size(); ++i) {
        if (m.sectionBeats[i] >= 0) continue;
        if (i == 0) { m.sectionBeats[0] = 0; continue; }
        const auto& p = w.sections[i - 1];
        if (m.sectionBeats[i - 1] >= 0 && p.bars > 0) m.sectionBeats[i] = m.sectionBeats[i - 1] + (double) p.bars * std::max (1, p.repeat) * m.beatsPerBar;
    }
}

// ---------------------------------------------------------------- sequence
std::vector<Segment> buildSequence (const MultitrackSet& m, const Chart& w, const Chart& arr, std::string* warning) {
    std::vector<Segment> out; const double fs = m.sampleRate > 0 ? m.sampleRate : 48000; int missing = 0;
    const double endBeat = m.tempo.beatAt (m.lengthSec);
    auto region = [&] (int wi, double& a, double& b) {
        if (wi < 0 || wi >= (int) m.sectionBeats.size() || m.sectionBeats[(size_t) wi] < 0) return false;
        a = m.sectionBeats[(size_t) wi]; b = endBeat;
        for (double x : m.sectionBeats) if (x > a + 1e-6 && x < b) b = x;
        return b > a + 1e-6;
    };
    int64_t o = 0;
    for (size_t k = 0; k < arr.sections.size(); ++k) {
        const int wi = arr.sections[k].source >= 0 ? arr.sections[k].source : (int) k;
        double a, b; if (wi >= (int) w.sections.size() || ! region (wi, a, b)) { ++missing; continue; }
        Segment s; s.arrIdx = (int) k; s.written = wi; s.startBeat = a; s.endBeat = b;
        s.srcStart = std::max<int64_t> (0, (int64_t) std::llround (m.tempo.secAt (a) * fs)); s.srcEnd = std::max<int64_t> (s.srcStart + 1, (int64_t) std::llround (m.tempo.secAt (b) * fs));
        s.outStart = o; o += s.srcEnd - s.srcStart; out.push_back (s);
    }
    if (warning) *warning = missing ? std::to_string (missing) + " section(s) have no marker on the tracks - they are skipped" : std::string();
    return out;
}

// ---------------------------------------------------------------- sequencer
static int findArr (const std::vector<Segment>& s, int arr) { for (size_t i = 0; i < s.size(); ++i) if (s[i].arrIdx >= arr) return (int) i; return -1; }
void Sequencer::setSequence (std::vector<Segment> seq, const TempoMap& t, int bpb, double fs) {
    tempo_ = t; bpb_ = std::max (1, bpb); fs_ = fs > 0 ? fs : 48000; xf_ = std::max (16, (int) std::lround (kCrossfadeSec * fs_));
    resequence (std::move (seq));
}
void Sequencer::resequence (std::vector<Segment>&& seq) {
    const int oldArr = cur_ >= 0 && cur_ < (int) seq_.size() ? seq_[(size_t) cur_].arrIdx : -1;
    const int64_t off = cur_ >= 0 && cur_ < (int) seq_.size() ? src_ - seq_[(size_t) cur_].srcStart : 0;
    seq_.swap (seq);   // the old vector goes back to the caller (freed off the audio thread)
    jumpTo_ = -1; loop_ = -1; fadeOut_ = fadeIn_ = 0;
    if (seq_.empty()) { cur_ = -1; playing_ = false; return; }
    int c = oldArr >= 0 ? findArr (seq_, oldArr) : 0; if (c < 0) c = (int) seq_.size() - 1;
    const auto& s = seq_[(size_t) c]; const int64_t ns = std::clamp<int64_t> (s.srcStart + (oldArr >= 0 ? off : 0), s.srcStart, s.srcEnd - 1);
    if (playing_ && ns != src_) { fadeSrc_ = src_; fadeOut_ = fadeIn_ = xf_; }
    cur_ = c; src_ = ns; ++changes_;
}
void Sequencer::play (int64_t pre) {
    if (seq_.empty()) return;
    if (cur_ < 0 || ended_) { cur_ = 0; src_ = seq_[0].srcStart; ++changes_; }
    ended_ = false; playing_ = true; preroll_ = std::max<int64_t> (0, pre); fadeOut_ = 0; fadeIn_ = 0;
}
void Sequencer::locate (int i) {
    if (i < 0 || i >= (int) seq_.size()) return;
    jumpTo_ = -1; ended_ = false;
    if (! playing_) { cur_ = i; src_ = seq_[(size_t) i].srcStart; ++changes_; return; }
    splice (i, seq_[(size_t) i].srcStart);
}
void Sequencer::splice (int to, int64_t toSrc) {
    if (toSrc != src_) { fadeSrc_ = src_; fadeOut_ = xf_; fadeIn_ = xf_; }
    src_ = toSrc; cur_ = to; ++changes_;
}
int64_t Sequencer::barLen (int64_t src) const {
    if (cur_ < 0) return 0; const auto& s = seq_[(size_t) cur_];
    const double beat = tempo_.beatAt ((double) src / fs_), rel = beat - s.startBeat;
    const double next = s.startBeat + (std::floor (rel / bpb_ + 1e-6) + 1.0) * bpb_;
    return std::max<int64_t> (1, (int64_t) std::llround (tempo_.secAt (next) * fs_) - src);
}
void Sequencer::requestJump (int i, Quant q) {
    if (i < 0 || i >= (int) seq_.size()) return;
    if (! playing_ || q == Quant::Now) { locate (i); return; }
    jumpTo_ = i; jumpQ_ = q; jumpAt_ = q == Quant::NextBar ? src_ + barLen (src_) : -1;
}
double Sequencer::beat() const { return tempo_.beatAt ((double) src_ / fs_); }
double Sequencer::barInSection() const { if (cur_ < 0) return 0; return std::floor ((beat() - seq_[(size_t) cur_].startBeat) / bpb_ + 1e-6) + 1; }
int64_t Sequencer::nextTargetSrc() const {
    if (cur_ < 0) return -1;
    if (jumpTo_ >= 0) return seq_[(size_t) jumpTo_].srcStart;
    if (loop_ == cur_) return seq_[(size_t) cur_].srcStart;
    if (cur_ + 1 < (int) seq_.size() && seq_[(size_t) cur_ + 1].srcStart != seq_[(size_t) cur_].srcEnd) return seq_[(size_t) cur_ + 1].srcStart;
    return -1;
}
int Sequencer::render (int n, Span* out, int maxSpans) {
    int dst = 0, ns = 0; if (! playing_ || cur_ < 0) return 0;
    if (preroll_ > 0) { const int k = (int) std::min<int64_t> (n, preroll_); preroll_ -= k; dst += k; }
    while (playing_ && ns + 2 <= maxSpans) {
        const auto& s = seq_[(size_t) cur_];
        const bool atEnd = src_ >= s.srcEnd, atJump = jumpTo_ >= 0 && jumpQ_ == Quant::NextBar && src_ == jumpAt_;
        if (atEnd || atJump) {
            int to;
            if (jumpTo_ >= 0) { to = jumpTo_; jumpTo_ = -1; }
            else if (loop_ == cur_) to = cur_;
            else { to = cur_ + 1; if (to >= (int) seq_.size()) { ended_ = true; playing_ = false; break; } }
            const int64_t ts = seq_[(size_t) to].srcStart;
            if (ts == src_) { cur_ = to; ++changes_; } else splice (to, ts);
            if (loop_ >= 0 && loop_ != cur_ && to != loop_) loop_ = -1;  // jumped away from a vamp: release it
            continue;
        }
        if (dst >= n) break;
        int64_t d = s.srcEnd - src_;
        if (jumpTo_ >= 0 && jumpQ_ == Quant::NextBar && jumpAt_ > src_ && jumpAt_ < s.srcEnd) d = jumpAt_ - src_;
        int k = (int) std::min<int64_t> (n - dst, d); if (fadeIn_ > 0) k = std::min (k, fadeIn_);
        float g0 = 1, g1 = 1;
        if (fadeIn_ > 0) { g0 = 1.0f - (float) fadeIn_ / xf_; g1 = 1.0f - (float) (fadeIn_ - k) / xf_; }
        out[ns++] = { src_, k, g0, g1, dst };
        if (fadeOut_ > 0) { out[ns++] = { fadeSrc_, k, (float) fadeOut_ / xf_, (float) (fadeOut_ - k) / xf_, dst }; fadeSrc_ += k; fadeOut_ -= k; }
        if (fadeIn_ > 0) fadeIn_ -= k;
        src_ += k; dst += k;
    }
    return ns;
}

// ---------------------------------------------------------------- sync + health
Json toJson (const Transport& t) { return Json::object().set ("loaded", t.loaded).set ("on", t.playing).set ("sec", t.section).set ("loop", t.loop).set ("pending", t.pending).set ("beat", t.beat); }
Transport transportFromJson (const Json& j) { Transport t; t.loaded = j["loaded"].boolean(); t.playing = j["on"].boolean(); t.section = j["sec"].integer (-1);
    t.loop = j["loop"].integer (-1); t.pending = j["pending"].integer (-1); t.beat = j["beat"].num(); return t; }
std::string loadWarning (const Load& l) {
    if (l.rateMismatch) return "Backing tracks are at a different sample rate - re-import them";
    if (l.underruns > 0) return "Backing tracks: the iPad's storage can't keep up - mute or remove some stems";
    if (l.cpu > 0.85) return "Backing tracks: CPU very high - expect dropouts";
    if (l.stems > kMaxStems) return "Too many stems - only the first " + std::to_string (kMaxStems) + " play";
    return {};
}

} // namespace wr::mt
