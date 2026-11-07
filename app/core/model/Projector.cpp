#include "Projector.h"
#include <cctype>
#include <cmath>

namespace wr {

static std::string squash (const std::string& in) {
    std::string o; bool sp = false;
    for (char c : in) { if (c == ' ' || c == '\t') { sp = ! o.empty(); continue; } if (sp) o += ' '; sp = false; o += c; }
    return o;
}
std::string lyricOnly (const ChartLine& l) {
    std::string s; int depth = 0;
    for (char c : l.lyric) { if (c == '[') { ++depth; continue; } if (c == ']' && depth) { --depth; continue; } if (! depth) s += c; } // stray inline chords
    s = squash (s);
    // lines made only of chord tokens / bar marks ("G  D/F#  Em  C", "| G | D |") are not lyrics
    bool any = false, allChords = true; std::string tok;
    auto flush = [&] { if (tok.empty()) return; any = true; if (! (isChordToken (tok) || tok == "|" || tok == "/" || tok == "-" || tok == "x2" || tok == "x3" || tok == "x4" || tok == "N.C.")) allChords = false; tok.clear(); };
    for (char c : s) { if (c == ' ') flush(); else tok += c; } flush();
    if (! any || allChords) return {};
    return s;
}
std::vector<std::string> sectionLyrics (const Section& sec) {
    std::vector<std::string> v;
    for (auto& it : sec.items) if (it.kind == ChartItem::Line) { auto t = lyricOnly (it.line); if (! t.empty()) v.push_back (t); }
    return v;
}
std::vector<Slide> sectionSlides (const Section& sec, int si, int n) {
    n = std::clamp (n, 1, 8); auto lines = sectionLyrics (sec); std::vector<Slide> out; if (lines.empty()) return out;
    const int count = ((int) lines.size() + n - 1) / n, base = (int) lines.size() / count, extra = (int) lines.size() % count; // balanced: 5 lines / 4 -> 3+2, 5 / 2 -> 2+2+1
    size_t p = 0;
    for (int i = 0; i < count; ++i) { Slide s; s.kind = Slide::Lyrics; s.section = si; s.index = i; s.count = count;
        int take = std::min (n, base + (i < extra ? 1 : 0)); for (int k = 0; k < take && p < lines.size(); ++k) s.lines.push_back (lines[p++]); out.push_back (std::move (s)); }
    while (p < lines.size()) out.back().lines.push_back (lines[p++]);
    return out;
}
std::vector<Slide> songSlides (const Chart& c, const ProjectorSettings& st) {
    std::vector<Slide> v;
    if (st.titleSlide) { Slide t; t.kind = Slide::Title; t.lines.push_back (c.title); if (! c.artist.empty()) t.lines.push_back (c.artist); v.push_back (t); }
    for (size_t i = 0; i < c.sections.size(); ++i) for (auto& s : sectionSlides (c.sections[i], (int) i, st.linesPerSlide)) v.push_back (s);
    return v;
}

// ---------------------------------------------------------------- state
void ProjectorState::nextSlide() { if (! started_) { started_ = true; manual_ = 0; return; } ++manual_; }
void ProjectorState::prevSlide() { if (manual_ > 0) --manual_; }
void ProjectorState::update (const LiveSession& live, const ProjectorSettings& st) {
    const Song* so = live.song(); const std::string id = so ? so->id : std::string();
    if (id != songId_) { songId_ = id; section_ = live.sectionIndex(); pass_ = live.pass(); manual_ = 0; started_ = ended_ = false; wasPlaying_ = live.playing(); }
    chart_ = live.chart();
    if (live.sectionIndex() != section_ || live.pass() != pass_) { section_ = live.sectionIndex(); pass_ = live.pass(); manual_ = 0; started_ = true; ended_ = false; }
    if (live.playing() && live.beatInSection() >= 0) started_ = true;
    if (wasPlaying_ && ! live.playing() && started_ && live.sectionIndex() + 1 >= (int) chart_.sections.size()) ended_ = true; // ran off the end
    wasPlaying_ = live.playing();

    Slide blank; blank.kind = Slide::Blank;
    if (blank_ || ! so) { cur_ = blank; return; }
    if (! started_) { if (st.titleSlide) { cur_ = Slide(); cur_.kind = Slide::Title; cur_.lines.push_back (chart_.title); if (! chart_.artist.empty()) cur_.lines.push_back (chart_.artist); } else cur_ = blank; return; }
    if (ended_ && st.blankBetweenSongs) { cur_ = blank; return; }
    if (section_ < 0 || section_ >= (int) chart_.sections.size()) { cur_ = blank; return; }
    const auto& sec = chart_.sections[(size_t) section_];
    auto slides = sectionSlides (sec, section_, st.linesPerSlide);
    if (slides.empty()) { cur_ = blank; return; }                    // instrumental section: blank screen
    int i = manual_;
    if (live.playing() && sec.bars > 0 && live.beatInSection() >= 0) { // spread the slides over the section's bars
        const double len = (double) sec.bars * live.beatsPerBar();
        i = std::max (i, (int) std::floor (live.beatInSection() / len * (double) slides.size()));
    }
    cur_ = slides[(size_t) std::clamp (i, 0, (int) slides.size() - 1)];
    if (st.uppercase) for (auto& l : cur_.lines) for (auto& c : l) c = (char) std::toupper ((unsigned char) c);
}

// ---------------------------------------------------------------- layout
SlideLayout layoutSlide (const Slide& s, float w, float h, const ProjectorSettings& st, const MeasureFn& measure) {
    SlideLayout L; if (s.kind == Slide::Blank || s.lines.empty()) return L;
    static const float scale[] = { 0.065f, 0.08f, 0.095f, 0.115f };
    const float base = h * scale[std::clamp (st.fontSize, 0, 3)] * (s.kind == Slide::Title ? 1.25f : 1.0f);
    const float maxW = w * 0.9f, maxH = h * 0.86f;
    auto wrap = [&] (float px) {
        std::vector<std::string> out;
        for (auto& line : s.lines) {
            std::string cur, word; std::vector<std::string> words; for (char c : line) { if (c == ' ') { if (! word.empty()) words.push_back (word); word.clear(); } else word += c; } if (! word.empty()) words.push_back (word);
            for (auto& wd : words) { std::string t = cur.empty() ? wd : cur + " " + wd; if (! cur.empty() && measure (t, px) > maxW) { out.push_back (cur); cur = wd; } else cur = t; }
            if (! cur.empty()) out.push_back (cur);
        }
        return out;
    };
    float px = base;
    for (int guard = 0; guard < 40; ++guard) {
        auto lines = wrap (px); float widest = 0; for (auto& l : lines) widest = std::max (widest, measure (l, px));
        if ((lines.size() * px * 1.25f <= maxH && widest <= maxW) || px <= base * 0.35f) { L.lines = lines; break; }
        px *= 0.93f; L.shrunk = true;
    }
    L.fontPx = px; const float lh = px * 1.25f, total = lh * (float) L.lines.size(), top = (h - total) * 0.5f;
    for (size_t i = 0; i < L.lines.size(); ++i) L.baselines.push_back (top + lh * (float) i + px * 0.95f);
    return L;
}

} // namespace wr
