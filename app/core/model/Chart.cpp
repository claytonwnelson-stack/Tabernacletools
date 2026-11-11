#include "Chart.h"
#include <cctype>
#include <cstdio>
#include <sstream>
#include <algorithm>

namespace wr {
namespace {
std::string trim (const std::string& s) { size_t a = s.find_first_not_of (" \t\r\n"), b = s.find_last_not_of (" \t\r\n"); return a == std::string::npos ? std::string() : s.substr (a, b - a + 1); }
std::string lower (std::string s) { for (auto& c : s) c = (char) std::tolower ((unsigned char) c); return s; }
bool startsWith (const std::string& s, const std::string& p) { return s.size() >= p.size() && s.compare (0, p.size(), p) == 0; }
std::vector<std::string> splitWs (const std::string& s) { std::istringstream is (s); std::vector<std::string> v; std::string t; while (is >> t) v.push_back (t); return v; }
// normalise unicode sharps/flats and smart quotes into ASCII
std::string normalise (std::string s) {
    auto rep = [&] (const std::string& a, const std::string& b) { size_t p = 0; while ((p = s.find (a, p)) != std::string::npos) { s.replace (p, a.size(), b); p += b.size(); } };
    rep ("\xE2\x99\xAF", "#"); rep ("\xE2\x99\xAD", "b"); rep ("\xE2\x80\x99", "'"); rep ("\xE2\x80\x98", "'"); rep ("\xE2\x80\x9C", "\""); rep ("\xE2\x80\x9D", "\""); rep ("\t", "    "); rep ("\r", "");
    return s;
}
}

// ------------------------------------------------------------------ chords
int noteIndex (const std::string& n) {
    if (n.empty()) return -1; static const int base[7] = { 9, 11, 0, 2, 4, 5, 7 }; // A B C D E F G
    char c = (char) std::toupper ((unsigned char) n[0]); if (c < 'A' || c > 'G') return -1;
    int v = base[c - 'A'];
    for (size_t i = 1; i < n.size(); ++i) { if (n[i] == '#') ++v; else if (n[i] == 'b') --v; else return -1; }
    return ((v % 12) + 12) % 12;
}
// split "F#m7/C#" -> root "F#", rest "m7", bass "C#"
static bool splitChord (const std::string& ch, std::string& root, std::string& qual, std::string& bass) {
    if (ch.empty() || ch[0] < 'A' || ch[0] > 'G') return false;
    size_t i = 1; while (i < ch.size() && (ch[i] == '#' || ch[i] == 'b')) ++i;
    root = ch.substr (0, i); std::string rest = ch.substr (i); bass.clear();
    size_t sl = rest.rfind ('/');
    if (sl != std::string::npos) { std::string b = rest.substr (sl + 1); if (noteIndex (b) >= 0) { bass = b; rest = rest.substr (0, sl); } }
    qual = rest; return true;
}
bool isChordToken (const std::string& tokIn) {
    std::string tok = tokIn;
    if (tok == "N.C." || tok == "NC" || tok == "|" || tok == "/" || tok == "-" || tok == "%") return true;
    while (! tok.empty() && (tok.front() == '(' || tok.front() == '|')) tok.erase (0, 1);
    while (! tok.empty() && (tok.back() == ')' || tok.back() == '|' || tok.back() == '*')) tok.pop_back();
    std::string r, q, b; if (! splitChord (tok, r, q, b)) return false;
    // allowed quality vocabulary
    static const char* ok[] = { "", "m", "maj", "min", "M", "dim", "aug", "+", "sus", "add", "o", "ø", "-", "5", "6", "7", "9", "11", "13", "2", "4", "b5", "#5", "b9", "#9", "#11", "b13", "(", ")", "/" };
    size_t i = 0;
    while (i < q.size()) {
        bool m = false;
        for (int len = 4; len >= 1 && ! m; --len) {
            if (i + (size_t) len > q.size()) continue; std::string part = q.substr (i, (size_t) len);
            for (auto* o : ok) if (*o && part == o) { m = true; i += (size_t) len; break; }
        }
        if (! m) return false;
    }
    return true;
}
bool keyPrefersFlats (const std::string& key) {
    std::string k = trim (key); bool minor = k.size() > 1 && k.back() == 'm'; std::string root = minor ? k.substr (0, k.size() - 1) : k;
    if (root.size() > 1 && root[1] == 'b') return true;
    int i = noteIndex (root); if (i < 0) return false;
    if (! minor) return i == 5;                       // F (others with b handled above)
    return i == 2 || i == 7 || i == 0 || i == 5;      // Dm Gm Cm Fm
}
std::string spellNote (int idx, bool flats) {
    static const char* sh[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    static const char* fl[12] = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };
    idx = ((idx % 12) + 12) % 12; return flats ? fl[idx] : sh[idx];
}
std::string transposeKey (const std::string& key, int semis) {
    std::string k = trim (key); bool minor = k.size() > 1 && k.back() == 'm'; std::string root = minor ? k.substr (0, k.size() - 1) : k;
    int i = noteIndex (root); if (i < 0) return key; int n = ((i + semis) % 12 + 12) % 12;
    static const char* maj[12] = { "C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    static const char* mnr[12] = { "Cm", "C#m", "Dm", "Ebm", "Em", "Fm", "F#m", "Gm", "G#m", "Am", "Bbm", "Bm" };
    return minor ? mnr[n] : maj[n];
}
int semitonesBetween (const std::string& a, const std::string& b) {
    auto rootOf = [] (std::string k) { k = trim (k); if (k.size() > 1 && k.back() == 'm') k.pop_back(); return noteIndex (k); };
    int x = rootOf (a), y = rootOf (b); if (x < 0 || y < 0) return 0; int d = ((y - x) % 12 + 12) % 12; return d > 6 ? d - 12 : d;
}
std::string transposeChord (const std::string& chord, int semis, const std::string& targetKey) {
    // keep surrounding punctuation like "(" ")"
    size_t a = 0, b = chord.size(); while (a < b && (chord[a] == '(' || chord[a] == '|')) ++a; while (b > a && (chord[b - 1] == ')' || chord[b - 1] == '|' || chord[b - 1] == '*')) --b;
    std::string core = chord.substr (a, b - a), r, q, bs;
    if (! splitChord (core, r, q, bs)) return chord;
    bool fl = keyPrefersFlats (targetKey);
    std::string out = spellNote (noteIndex (r) + semis, fl) + q; if (! bs.empty()) out += "/" + spellNote (noteIndex (bs) + semis, fl);
    return chord.substr (0, a) + out + chord.substr (b);
}

// ------------------------------------------------------------------ tabs
namespace {
struct TabTok { int col, len, fret; };
std::vector<TabTok> tabTokens (const std::string& s) {
    std::vector<TabTok> v; size_t i = 0; size_t bar = s.find ('|'); if (bar != std::string::npos && bar < 4) i = bar + 1;
    while (i < s.size()) { if (std::isdigit ((unsigned char) s[i])) { size_t j = i; while (j < s.size() && std::isdigit ((unsigned char) s[j])) ++j; v.push_back ({ (int) i, (int) (j - i), std::atoi (s.substr (i, j - i).c_str()) }); i = j; } else ++i; }
    return v;
}
bool isTabLine (const std::string& raw) {
    std::string s = trim (raw); if (s.size() < 4) return false;
    const std::string names = "eBGDAEbgdaFC#";
    size_t i = 0; while (i < s.size() && i < 3 && names.find (s[i]) != std::string::npos) ++i;
    if (i == 0 || i >= s.size() || (s[i] != '|' && s[i] != ':')) return false;
    int dashes = 0; for (char c : s) if (c == '-') ++dashes;
    return dashes >= 3;
}
}

TabTransposeResult transposeTab (const TabBlock& tab, int semis, int maxFret) {
    TabTransposeResult res; res.tab = tab;
    std::vector<std::vector<TabTok>> toks; int mn = 999, mx = -999;
    for (auto& s : tab.strings) { toks.push_back (tabTokens (s)); for (auto& t : toks.back()) { mn = std::min (mn, t.fret); mx = std::max (mx, t.fret); } }
    if (mn == 999 || semis == 0) { res.fretShift = 0; return res; }
    int cands[3] = { semis, semis > 0 ? semis - 12 : semis + 12, semis > 0 ? semis + 12 : semis - 12 };
    int shift = 0; bool found = false;
    for (int c : cands) if (mn + c >= 0 && mx + c <= maxFret) { shift = c; found = true; break; }
    if (! found) {
        res.playable = false; res.fretShift = 0;
        int up = ((semis % 12) + 12) % 12; res.capo = up;
        res.advice = up <= 7 ? "Riff won't fit the neck in this key: play the original tab with capo " + std::to_string (up) + "."
                             : "Riff won't fit the neck in this key: tune down " + std::to_string (12 - up) + " semitone(s) or play it as written.";
        res.tab.note = res.advice; return res;
    }
    res.fretShift = shift; if (shift != semis) res.advice = std::string ("Moved ") + (shift > semis ? "up" : "down") + " an octave to stay on the neck.";
    // rebuild each line; widen columns where numbers gain digits so strings stay aligned
    const size_t ns = tab.strings.size();
    std::vector<std::string> out (ns); std::vector<size_t> pos (ns, 0);
    std::vector<int> evCols; for (auto& tv : toks) for (auto& t : tv) evCols.push_back (t.col);
    std::sort (evCols.begin(), evCols.end()); evCols.erase (std::unique (evCols.begin(), evCols.end()), evCols.end());
    std::vector<size_t> ti (ns, 0);
    size_t maxLen = 0; for (auto& s : tab.strings) maxLen = std::max (maxLen, s.size());
    size_t col = 0; size_t ei = 0;
    while (col < maxLen) {
        if (ei < evCols.size() && (int) col == evCols[ei]) {
            int span = 1, width = 1;
            std::vector<int> hit (ns, -1);
            for (size_t s = 0; s < ns; ++s) if (ti[s] < toks[s].size() && toks[s][ti[s]].col == (int) col) { hit[s] = (int) ti[s]; span = std::max (span, toks[s][ti[s]].len); width = std::max (width, (int) std::to_string (toks[s][ti[s]].fret + shift).size()); }
            width = std::max (width, span);
            // absorb following filler dashes so the bar length stays the same when a number gains a digit
            int extra = width - span, absorbed = 0;
            while (absorbed < extra) {
                size_t c2 = col + (size_t) span + (size_t) absorbed; bool allDash = true;
                for (size_t s = 0; s < ns; ++s) if (c2 >= tab.strings[s].size() || tab.strings[s][c2] != '-' || (c2 + 1 < tab.strings[s].size() && std::isdigit ((unsigned char) tab.strings[s][c2 + 1]) && false)) allDash = false;
                if (! allDash || std::binary_search (evCols.begin(), evCols.end(), (int) c2)) break;
                ++absorbed;
            }
            for (size_t s = 0; s < ns; ++s) {
                if (hit[s] >= 0) { std::string num = std::to_string (toks[s][(size_t) hit[s]].fret + shift); out[s] += num; out[s].append ((size_t) (width - (int) num.size()), '-'); ++ti[s]; }
                else { for (int k = 0; k < span; ++k) out[s] += col + (size_t) k < tab.strings[s].size() ? tab.strings[s][col + (size_t) k] : ' '; out[s].append ((size_t) (width - span), '-'); }
            }
            col += (size_t) (span + absorbed); ++ei;
            while (ei < evCols.size() && evCols[ei] < (int) col) ++ei; // tokens starting inside the span were consumed
            for (size_t s = 0; s < ns; ++s) while (ti[s] < toks[s].size() && toks[s][ti[s]].col < (int) col) ++ti[s];
        } else { for (size_t s = 0; s < ns; ++s) out[s] += col < tab.strings[s].size() ? tab.strings[s][col] : ' '; ++col; }
    }
    for (auto& s : out) { while (! s.empty() && s.back() == ' ') s.pop_back(); }
    res.tab.strings = out; res.tab.note = res.advice; return res;
}

Chart transposeChart (const Chart& c, const std::string& targetKey) {
    Chart o = c; if (c.key.empty() || targetKey.empty()) return o;
    int semis = semitonesBetween (c.key, targetKey); if (semis == 0) { o.key = targetKey; return o; }
    o.key = targetKey;
    for (auto& s : o.sections) for (auto& it : s.items) {
        if (it.kind == ChartItem::Line) for (auto& ch : it.line.chords) ch.chord = transposeChord (ch.chord, semis, targetKey);
        else if (it.kind == ChartItem::Tab) it.tab = transposeTab (it.tab, semis).tab;
    }
    return o;
}

// ------------------------------------------------------------------ section naming
std::string sectionLabelFor (const std::string& nameIn, std::string* kindOut, std::string* colorOut) {
    std::string n = lower (trim (nameIn)), num; for (char c : n) if (std::isdigit ((unsigned char) c)) num += c;
    std::string kind = "other", lab, col = "grey";
    auto has = [&] (const char* w) { return n.find (w) != std::string::npos; };
    if (has ("pre")) { kind = "prechorus"; lab = "PC"; col = "pink"; }
    else if (has ("chorus") || has ("refrain")) { kind = "chorus"; lab = "CH"; col = "blue"; }
    else if (has ("verse")) { kind = "verse"; lab = "V"; col = "orange"; }
    else if (has ("bridge")) { kind = "bridge"; lab = "BR"; col = "purple"; }
    else if (has ("tag")) { kind = "tag"; lab = "TAG"; col = "blue"; }
    else if (has ("intro")) { kind = "intro"; lab = "IN"; col = "grey"; }
    else if (has ("outro") || has ("ending") || has ("end")) { kind = "outro"; lab = "OUT"; col = "grey"; }
    else if (has ("inst") || has ("interlude") || has ("turn")) { kind = "interlude"; lab = "INT"; col = "grey"; }
    else { lab = n.empty() ? "S" : std::string (1, (char) std::toupper ((unsigned char) n[0])); }
    if (kind == "verse") lab += num.empty() ? "" : num; else if (! num.empty() && kind != "other") lab += num;
    if (kindOut) *kindOut = kind; if (colorOut) *colorOut = col; return lab;
}

// ------------------------------------------------------------------ parser
namespace {
struct Parser {
    std::vector<std::pair<std::string, std::string>> colorSpecs; // v0.8 {x_section_color: token=colour}, resolved after parsing
    Chart c; Section* cur = nullptr; std::string hl; bool inTab = false; TabBlock tab; std::vector<std::string> pendingChordLine; bool havePending = false; std::string pendingRaw;
    bool gap = false, holding = false; std::string heldRaw;
    bool chorusOpener (const std::string& lyric) const {
        auto want = lower (trim (lyric)); if (want.size() < 2) return false;
        for (auto& s : c.sections) if (s.kind == "chorus") for (auto& it : s.items) if (it.kind == ChartItem::Line && ! trim (it.line.lyric).empty())
            return lower (trim (it.line.lyric)) == want;
        return false;
    }
    void ensureSection() { if (! cur) startSection ("Intro"); }
    void startSection (std::string name, int repeat = 1) {
        flushPending(); Section s; s.name = trim (name); s.label = sectionLabelFor (s.name, &s.kind, &s.color); s.repeat = repeat; c.sections.push_back (s); cur = &c.sections.back();
    }
    void addLine (ChartLine l) { ensureSection(); ChartItem it; it.kind = ChartItem::Line; l.highlight = hl; it.line = std::move (l); cur->items.push_back (std::move (it)); }
    void addTab (TabBlock t) { ensureSection(); ChartItem it; it.kind = ChartItem::Tab; it.tab = std::move (t); cur->items.push_back (std::move (it)); }
    void addComment (const std::string& t) { ensureSection(); ChartItem it; it.kind = ChartItem::Comment; it.text = t; cur->items.push_back (std::move (it)); }
    void flushPending() { // chord line with no lyric under it
        if (! havePending) return; havePending = false; ChartLine l; int p = 0; size_t i = 0; const std::string& s = pendingRaw;
        while (i < s.size()) { if (s[i] == ' ') { ++i; continue; } size_t j = i; while (j < s.size() && s[j] != ' ') ++j; l.chords.push_back ({ (int) i, s.substr (i, j - i) }); i = j; ++p; }
        // chords-only line: keep spacing as spaces in the lyric so positions render
        addLine (l);
    }
    void flushTab() { if (! tab.strings.empty()) addTab (tab); tab = TabBlock(); }
    static std::string attr (const std::string& s, const std::string& key) { auto p = s.find (key + "="); if (p == std::string::npos) return {}; p += key.size() + 1; size_t e = p; if (e < s.size() && s[e] == '"') { ++p; e = s.find ('"', p); } else while (e < s.size() && s[e] != ' ') ++e; return s.substr (p, e - p); }
    static std::string stripAttrs (std::string s) { auto p = s.find ('='); if (p == std::string::npos) return trim (s); auto sp = s.rfind (' ', p); return trim (s.substr (0, sp == std::string::npos ? 0 : sp)); }

    bool directive (const std::string& t) {
        if (t.size() < 2 || t.front() != '{' || t.back() != '}') return false;
        std::string in = t.substr (1, t.size() - 2), k, v; auto colon = in.find (':');
        if (colon != std::string::npos) { k = lower (trim (in.substr (0, colon))); v = trim (in.substr (colon + 1)); } else { auto sp = in.find (' '); k = lower (trim (in.substr (0, sp))); v = sp == std::string::npos ? "" : trim (in.substr (sp + 1)); }
        if (k == "title" || k == "t") c.title = v; else if (k == "artist" || k == "subtitle" || k == "st") c.artist = v; else if (k == "key") c.key = v;
        else if (k == "tempo" || k == "bpm") c.bpm = clampBpm (std::atof (v.c_str())); else if (k == "time") c.time = v; else if (k == "capo") c.capo = std::clamp (std::atoi (v.c_str()), 0, 11);
        else if (k == "arrangement" || k == "x_arrangement" || k == "order") c.arrangement = parseArrangement (v);
        else if (k == "x_section_color" || k == "section_color" || k == "x_section_colour" || k == "section_colour") { // v0.8
            auto eq = v.rfind ('=');
            if (eq != std::string::npos) colorSpecs.push_back ({ trim (v.substr (0, eq)), trim (v.substr (eq + 1)) });
            else if (cur) { auto n = normalizeColor (v); if (! n.empty()) cur->customColor = n; } }
        else if (k == "bars") { if (cur) cur->bars = std::max (0, std::atoi (v.c_str())); }
        else if (k == "start_of_verse" || k == "sov") startSection (v.empty() ? "Verse" : v);
        else if (k == "start_of_chorus" || k == "soc") startSection (v.empty() ? "Chorus" : v);
        else if (k == "start_of_bridge" || k == "sob") startSection (v.empty() ? "Bridge" : v);
        else if (k == "start_of_part" || k == "section") { std::string nm = stripAttrs (v); int rep = 1; auto r = attr (v, "repeat"); if (! r.empty()) rep = std::atoi (r.c_str()); startSection (nm.empty() ? "Part" : nm, std::max (1, rep));
            auto bz = attr (v, "bars"); if (! bz.empty() && cur) cur->bars = std::max (0, std::atoi (bz.c_str())); }
        else if (k == "start_of_tab" || k == "sot") { flushPending(); inTab = true; tab = TabBlock(); tab.title = stripAttrs (v); tab.color = attr (v, "color"); if (tab.title.empty()) tab.title = "Tab"; }
        else if (k == "end_of_tab" || k == "eot") { inTab = false; flushTab(); }
        else if (k == "start_of_highlight" || k == "soh") hl = v.empty() ? "orange" : v;
        else if (k == "end_of_highlight" || k == "eoh") hl.clear();
        else if (k == "comment" || k == "c" || k == "ci" || k == "comment_italic") addComment (v);
        else if (k == "repeat") { if (cur) cur->repeat = std::max (1, std::atoi (v.c_str())); }
        // end_of_verse / end_of_chorus etc. need nothing
        return true;
    }
    bool header (const std::string& t) { // [Verse 1] x2  |  Chorus:  |  VERSE 2  |  Verse 2 (soft)  |  V2
        std::string s = t; int rep = 1;
        auto xr = lower (s); auto xp = xr.rfind ('x');
        if (xp != std::string::npos && xp + 1 < xr.size() && std::all_of (xr.begin() + (long) xp + 1, xr.end(), ::isdigit) && (xp == 0 || xr[xp - 1] == ' ' || xr[xp - 1] == ']' || xr[xp - 1] == '(')) {
            rep = std::atoi (xr.c_str() + xp + 1); s = trim (s.substr (0, xp)); if (! s.empty() && s.back() == '(') s.pop_back(); s = trim (s); }
        auto paren = s.rfind ('('); if (paren != std::string::npos && ! s.empty() && s.back() == ')') s = trim (s.substr (0, paren));
        if (s.size() > 2 && s.front() == '[' && s.back() == ']') { std::string in = trim (s.substr (1, s.size() - 2)); if (! isChordToken (in) && s.find (']') == s.size() - 1) { startSection (in, rep); return true; } }
        while (! s.empty() && ! std::isalnum ((unsigned char) s.front())) s.erase (s.begin());
        std::string l = lower (s); if (! l.empty() && (l.back() == ':' || l.back() == '.')) l.pop_back(); l = trim (l);
        if (l.empty() || isChordToken (l)) return false;
        auto wordNum = [] (const std::string& w) { if (w == "one") return 1; if (w == "two") return 2; if (w == "three") return 3; if (w == "four") return 4; if (w == "five") return 5; return 0; };
        static const char* words[] = { "verse", "chorus", "pre-chorus", "prechorus", "pre chorus", "bridge", "tag", "intro", "outro", "interlude", "instrumental", "ending", "turnaround", "refrain" };
        for (auto* w : words) if (startsWith (l, w)) {
            std::string rest = trim (l.substr (std::string (w).size()));
            std::string compact; for (char ch : rest) if (ch != ' ') compact += ch;
            int wn = wordNum (compact);
            bool digits = ! compact.empty() && std::all_of (compact.begin(), compact.end(), [] (char ch) { return std::isdigit ((unsigned char) ch); });
            if (compact.empty() || digits || wn > 0) {
                std::string nm = s; if (! nm.empty() && (nm.back() == ':' || nm.back() == '.')) nm.pop_back(); nm = trim (nm);
                if (wn > 0) { std::string base = w; if (! base.empty()) base[0] = (char) std::toupper ((unsigned char) base[0]); nm = base + " " + std::to_string (wn); }
                startSection (nm, rep); return true;
            }
        }
        auto named = [&] (const char* key, const char* nm) { if (l == key) { startSection (nm, rep); return true; } return false; };
        if (named ("v1", "Verse 1") || named ("v2", "Verse 2") || named ("v3", "Verse 3") || named ("v4", "Verse 4")
            || named ("ch", "Chorus") || named ("ch2", "Chorus 2") || named ("pc", "Pre-Chorus") || named ("br", "Bridge")) return true;
        return false;
    }
    static bool isChordLine (const std::string& t) { auto toks = splitWs (t); if (toks.empty()) return false; int n = 0; for (auto& x : toks) { if (! isChordToken (x)) return false; if (x != "|" && x != "/" && x != "-") ++n; } return n > 0; }
    static bool inlineChords (const std::string& t, ChartLine& out) {
        if (t.find ('[') == std::string::npos) return false; std::string lyr; std::vector<ChordAt> ch; size_t i = 0;
        while (i < t.size()) { if (t[i] == '[') { auto e = t.find (']', i); if (e == std::string::npos) return false; std::string tok = t.substr (i + 1, e - i - 1); if (! isChordToken (tok)) return false; ch.push_back ({ (int) lyr.size(), tok }); i = e + 1; } else lyr += t[i++]; }
        out.chords = ch; out.lyric = lyr; return ! ch.empty();
    }
    void line (const std::string& raw) {
        std::string t = trim (raw);
        if (inTab && t.size() > 1 && t.front() == '{') { directive (t); return; }
        if (inTab) { if (isTabLine (t)) tab.strings.push_back (t); else if (! t.empty()) { flushTab(); tab.title = "Tab"; } return; }
        if (t.empty()) { flushPending(); gap = true; holding = false; return; }
        if (t[0] == '#') return; // ChordPro comment
        if (directive (t)) return;
        if (isTabLine (t)) { flushPending(); if (tab.strings.empty()) { tab.title = "Tab"; } tab.strings.push_back (t); if (tab.strings.size() == 6) flushTab(); return; }
        if (! tab.strings.empty()) flushTab();
        if (header (t)) { gap = false; holding = false; return; }
        if (c.title.empty() && c.sections.empty() && ! havePending && ! isChordLine (t) && t.find ('[') == std::string::npos) { c.title = t; return; } // plain text: first line = title
        ChartLine il; if (inlineChords (t, il)) { flushPending(); addLine (il); return; }
        std::string rr = raw; while (! rr.empty() && (rr.back() == ' ' || rr.back() == '\r')) rr.pop_back();
        if (isChordLine (t)) {
            flushPending();
            if (gap) { holding = true; heldRaw = rr; gap = false; return; }
            havePending = true; pendingRaw = rr; return;
        }
        // lyric line; a blank line then a repeat of a chorus's first line starts another chorus
        if ((holding || gap) && chorusOpener (trim (rr))) startSection ("Chorus");
        gap = false;
        if (holding) { havePending = true; pendingRaw = heldRaw; holding = false; }
        ChartLine l; l.lyric = rr;
        size_t lead = l.lyric.find_first_not_of (' '); if (lead != std::string::npos && lead > 0) l.lyric = l.lyric.substr (lead); else lead = 0;
        if (havePending) {
            havePending = false; const std::string& s = pendingRaw; size_t i = 0;
            while (i < s.size()) { if (s[i] == ' ') { ++i; continue; } size_t j = i; while (j < s.size() && s[j] != ' ') ++j; int col = (int) i - (int) lead; l.chords.push_back ({ std::max (0, col), s.substr (i, j - i) }); i = j; }
        }
        addLine (l);
    }
};
}

Chart parseChart (const std::string& textIn, const std::string& fallbackTitle) {
    Parser p; std::string text = normalise (textIn); std::istringstream is (text); std::string ln;
    while (std::getline (is, ln)) p.line (ln);
    p.flushPending(); p.flushTab();
    if (p.c.title.empty()) p.c.title = fallbackTitle;
    for (auto& cs : p.colorSpecs) { // v0.8: exact name first, then label / arrangement shorthand; unknown sections or colours are ignored
        auto col = normalizeColor (cs.second); if (col.empty()) continue; int idx = -1;
        if (cs.first.size() > 1 && cs.first[0] == '@') { int n = std::atoi (cs.first.c_str() + 1); if (n >= 1 && n <= (int) p.c.sections.size()) idx = n - 1; else continue; }
        for (int i = 0; i < (int) p.c.sections.size() && idx < 0; ++i) if (lower (p.c.sections[(size_t) i].name) == lower (cs.first)) idx = i;
        if (idx < 0) idx = resolveArrangementToken (p.c, cs.first);
        if (idx >= 0) p.c.sections[(size_t) idx].customColor = col; }
    applySectionColors (p.c, {});
    if (p.c.key.empty()) { // guess key from first chord
        for (auto& s : p.c.sections) { for (auto& it : s.items) if (it.kind == ChartItem::Line && ! it.line.chords.empty()) { std::string r, q, b; if (splitChord (it.line.chords[0].chord, r, q, b)) { p.c.key = r + (startsWith (q, "m") && ! startsWith (q, "maj") ? "m" : ""); p.c.warnings.push_back ("Key not given; guessed " + p.c.key + " from the first chord."); } break; }
            if (! p.c.key.empty()) break; }
    }
    // make labels unique-ish for chips where needed (V1, V2 stay; repeated CH stays CH)
    for (auto& s : p.c.sections) s.items.erase (std::remove_if (s.items.begin(), s.items.end(), [] (const ChartItem& i) { return i.kind == ChartItem::Line && i.line.chords.empty() && trim (i.line.lyric).empty(); }), s.items.end());
    p.c.sections.erase (std::remove_if (p.c.sections.begin(), p.c.sections.end(), [] (const Section& s) { return s.items.empty() && s.name == "Intro"; }), p.c.sections.end());
    // ChordPro capo convention: {key:} is the sounding key, chords are written as capo shapes. Store chords as sounding.
    if (p.c.capo > 0 && ! p.c.key.empty()) {
        bool guessed = ! p.c.warnings.empty() && p.c.warnings.back().rfind ("Key not given", 0) == 0;
        if (guessed) p.c.key = transposeKey (p.c.key, p.c.capo);
        Chart shapes = p.c; shapes.key = shapesKey (p.c.key, p.c.capo); int capo = p.c.capo; std::string k = p.c.key; auto w = p.c.warnings;
        p.c = transposeChart (shapes, k); p.c.capo = capo; p.c.warnings = w;
    }
    return p.c;
}


// ------------------------------------------------------------------ ChordPro export
std::string formatBpm (double b) {
    b = clampBpm (b); if (b <= 0) return {};
    char buf[16]; long long t = (long long) (b * 10.0 + 0.5);
    if (t % 10 == 0) std::snprintf (buf, sizeof buf, "%lld", t / 10); else std::snprintf (buf, sizeof buf, "%lld.%lld", t / 10, t % 10);
    return buf;
}

std::string setChordProTempo (const std::string& text, double bpm) {
    std::istringstream is (text); std::string ln, out, tempoLine = bpm > 0 ? "{tempo: " + formatBpm (bpm) + "}" : std::string();
    bool done = false; std::vector<std::string> lines; while (std::getline (is, ln)) lines.push_back (ln);
    int insertAfter = -1;
    for (size_t i = 0; i < lines.size(); ++i) {
        std::string t = lower (trim (lines[i]));
        if (startsWith (t, "{tempo") || startsWith (t, "{bpm")) { if (! done && ! tempoLine.empty()) { lines[i] = tempoLine; done = true; } else lines[i] = "\x01"; continue; }
        if (startsWith (t, "{title") || startsWith (t, "{t:") || startsWith (t, "{key") || startsWith (t, "{artist") || startsWith (t, "{subtitle")) insertAfter = (int) i;
    }
    if (! done && ! tempoLine.empty()) lines.insert (lines.begin() + (insertAfter + 1), tempoLine);
    for (auto& l : lines) if (l != "\x01") { out += l; out += '\n'; }
    return out;
}

static std::string inlineChords (const ChartLine& l) {
    std::string out; size_t pos = 0; auto chords = l.chords; std::stable_sort (chords.begin(), chords.end(), [] (const ChordAt& a, const ChordAt& b) { return a.pos < b.pos; });
    for (auto& c : chords) { size_t p = (size_t) std::max (0, c.pos); if (p > l.lyric.size()) { out += l.lyric.substr (std::min (pos, l.lyric.size())); if (p > std::max (pos, l.lyric.size())) out += std::string (p - std::max (pos, l.lyric.size()), ' '); pos = p; }
        else if (p >= pos) { out += l.lyric.substr (pos, p - pos); pos = p; }
        out += "[" + c.chord + "]"; }
    if (pos < l.lyric.size()) out += l.lyric.substr (pos);
    return out;
}

std::string chartToChordPro (const Chart& cIn) {
    // capo: write chords as shapes (key stays the sounding key)
    Chart c = cIn; if (cIn.capo > 0 && ! cIn.key.empty()) { c = transposeChart (cIn, shapesKey (cIn.key, cIn.capo)); c.key = cIn.key; c.capo = cIn.capo; for (auto& s : c.sections) for (auto& it : s.items) if (it.kind == ChartItem::Tab) it.tab.note.clear(); }
    std::string o;
    if (! c.title.empty()) o += "{title: " + c.title + "}\n";
    if (! c.artist.empty()) o += "{artist: " + c.artist + "}\n";
    if (! c.key.empty()) o += "{key: " + c.key + "}\n";
    if (c.bpm > 0) o += "{tempo: " + formatBpm (c.bpm) + "}\n";
    if (! c.time.empty()) o += "{time: " + c.time + "}\n";
    if (c.capo > 0) o += "{capo: " + std::to_string (c.capo) + "}\n";
    if (! c.arrangement.empty()) o += "{arrangement: " + formatArrangement (c.arrangement) + "}\n";
    for (size_t i = 0; i < c.sections.size(); ++i) { const auto& s = c.sections[i]; if (s.customColor.empty()) continue; // v0.8
        int same = 0; for (auto& o2 : c.sections) if (lower (o2.name) == lower (s.name)) ++same;
        o += "{x_section_color: " + (same == 1 && s.name.find ('=') == std::string::npos ? s.name : "@" + std::to_string (i + 1)) + "=" + s.customColor + "}\n"; }
    for (auto& s : c.sections) {
        o += "\n{start_of_part: " + s.name + (s.repeat > 1 ? " repeat=" + std::to_string (s.repeat) : std::string()) + (s.bars > 0 ? " bars=" + std::to_string (s.bars) : std::string()) + "}\n";
        std::string hl;
        for (auto& it : s.items) {
            if (it.kind == ChartItem::Line) {
                if (it.line.highlight != hl) { if (! hl.empty()) o += "{end_of_highlight}\n"; hl = it.line.highlight; if (! hl.empty()) o += "{start_of_highlight: " + hl + "}\n"; }
                o += inlineChords (it.line) + "\n";
            } else {
                if (! hl.empty()) { o += "{end_of_highlight}\n"; hl.clear(); }
                if (it.kind == ChartItem::Comment) o += "{comment: " + it.text + "}\n";
                else { o += "{start_of_tab: " + (it.tab.title.empty() ? std::string ("Tab") : it.tab.title) + (it.tab.color.empty() ? std::string() : " color=" + it.tab.color) + "}\n";
                       for (auto& st : it.tab.strings) o += st + "\n"; o += "{end_of_tab}\n"; }
            }
        }
        if (! hl.empty()) o += "{end_of_highlight}\n";
        o += "{end_of_part}\n";
    }
    return o;
}


// ------------------------------------------------------------------ capo
std::string shapesKey (const std::string& soundingKey, int capo) { return capo > 0 ? transposeKey (soundingKey, -capo) : soundingKey; }

std::vector<CapoOption> capoOptions (const std::string& soundingKey, int maxCapo) {
    std::vector<CapoOption> out; if (soundingKey.empty() || noteIndex (soundingKey.substr (0, soundingKey.size() > 1 && (soundingKey[1] == '#' || soundingKey[1] == 'b') ? 2 : 1)) < 0) return out;
    const bool minor = soundingKey.back() == 'm';
    // open-chord friendliness of the shapes key (guitar): lower is better
    auto rank = [&] (const std::string& k) {
        static const char* maj[] = { "G", "C", "D", "A", "E" }; static const char* mn[] = { "Em", "Am", "Dm", "Bm" };
        if (minor) { for (int i = 0; i < 4; ++i) if (k == mn[i]) return i; } else for (int i = 0; i < 5; ++i) if (k == maj[i]) return i;
        return 20; };
    for (int capo = 0; capo <= maxCapo; ++capo) { std::string sk = shapesKey (soundingKey, capo); int r = rank (sk); if (r >= 20 && capo > 0) continue; out.push_back ({ capo, sk, r * 2 + capo }); }
    std::stable_sort (out.begin(), out.end(), [] (const CapoOption& a, const CapoOption& b) { return a.score < b.score; });
    return out;
}

Chart chartForPart (const Chart& song, const std::string& soundingKey, int capo) {
    Chart sounding = soundingKey.empty() || song.key.empty() ? song : transposeChart (song, soundingKey);
    if (capo < 0) capo = song.capo;
    if (capo <= 0 || sounding.key.empty()) { sounding.capo = 0; return sounding; }
    Chart shapes = transposeChart (sounding, shapesKey (sounding.key, capo)); shapes.key = sounding.key; shapes.capo = capo; // chords now show shapes
    return shapes;
}

std::string capoLabel (const std::string& soundingKey, int capo) {
    if (capo <= 0) return "Key " + soundingKey;
    return "Capo " + std::to_string (capo) + ": play " + shapesKey (soundingKey, capo) + " shapes (sounds " + soundingKey + ")";
}


// ------------------------------------------------------------------ v0.7 arrangement
std::vector<std::string> parseArrangement (const std::string& text) {
    std::vector<std::string> out; std::string t;
    for (char ch : text + " ") { if (ch == ' ' || ch == ',' || ch == '\t' || ch == ';' || ch == '|') { if (! t.empty()) out.push_back (t); t.clear(); } else t += ch; }
    return out;
}
std::string formatArrangement (const std::vector<std::string>& a) { std::string o; for (auto& t : a) { if (! o.empty()) o += ' '; o += t; } return o; }
static std::string lowerS (std::string s) { for (auto& ch : s) ch = (char) std::tolower ((unsigned char) ch); return s; }
int resolveArrangementToken (const Chart& c, const std::string& tokIn) {
    const std::string tok = lowerS (tokIn); const int n = (int) c.sections.size();
    for (int i = 0; i < n; ++i) if (lowerS (c.sections[(size_t) i].label) == tok) return i;
    for (int i = 0; i < n; ++i) if (lowerS (c.sections[(size_t) i].name) == tok) return i;
    // shorthand: letters + optional number -> n-th section of that kind (C = chorus, V2 = second verse, PC, B, I, O/E, T, INT)
    size_t d = 0; while (d < tok.size() && std::isalpha ((unsigned char) tok[d])) ++d;
    const std::string pre = tok.substr (0, d); const int num = d < tok.size() ? std::atoi (tok.c_str() + d) : 1;
    static const std::pair<const char*, const char*> kinds[] = { { "c", "chorus" }, { "ch", "chorus" }, { "chorus", "chorus" }, { "v", "verse" }, { "verse", "verse" },
        { "pc", "prechorus" }, { "pre", "prechorus" }, { "b", "bridge" }, { "br", "bridge" }, { "bridge", "bridge" }, { "i", "intro" }, { "in", "intro" }, { "intro", "intro" },
        { "o", "outro" }, { "e", "outro" }, { "end", "outro" }, { "outro", "outro" }, { "t", "tag" }, { "tag", "tag" }, { "int", "interlude" }, { "inst", "interlude" }, { "interlude", "interlude" } };
    for (auto& k : kinds) if (pre == k.first) {
        int seen = 0, last = -1;
        for (int i = 0; i < n; ++i) if (c.sections[(size_t) i].kind == k.second) { last = i; if (++seen == std::max (1, num)) return i; }
        return d == tok.size() ? last : -1;   // "C" with one written chorus -> it; "V3" with two verses -> not found
    }
    return -1;
}
Chart arrangedChart (const Chart& c, const std::vector<std::string>& arr, std::vector<std::string>* unresolved) {
    Chart out = c; for (size_t i = 0; i < out.sections.size(); ++i) { out.sections[i].occurrence = 1; out.sections[i].source = (int) i; }
    if (arr.empty()) return out;
    std::vector<Section> secs; std::map<int, int> count;
    for (auto& t : arr) {
        const int i = resolveArrangementToken (c, t);
        if (i < 0) { if (unresolved) unresolved->push_back (t); continue; }
        Section s = c.sections[(size_t) i]; s.source = i; s.occurrence = ++count[i]; secs.push_back (std::move (s));
    }
    if (! secs.empty()) out.sections = std::move (secs);
    return out;
}
std::string firstChord (const Chart& c) {
    for (auto& s : c.sections) for (auto& it : s.items) if (it.kind == ChartItem::Line && ! it.line.chords.empty()) return it.line.chords.front().chord;
    return {};
}

int practiceSectionAt (const std::vector<double>& marks, double sec) {
    int best = -1; double bt = -1;
    for (int i = 0; i < (int) marks.size(); ++i) if (marks[(size_t) i] >= 0 && marks[(size_t) i] <= sec + 1e-6 && marks[(size_t) i] >= bt) { best = i; bt = marks[(size_t) i]; }
    return best;
}
namespace {
std::vector<ChartItem> parseBodyItems (const std::string& text) {
    std::vector<ChartItem> items; bool pending = false; std::string raw; bool inTab = false; TabBlock tab;
    auto flushTab = [&] { if (! tab.strings.empty()) { ChartItem it; it.kind = ChartItem::Tab; if (tab.title.empty()) tab.title = "Tab"; it.tab = tab; items.push_back (std::move (it)); } tab = TabBlock(); inTab = false; };
    auto flushChord = [&] {
        if (! pending) return; pending = false; ChartLine l; size_t i = 0;
        while (i < raw.size()) { if (raw[i] == ' ') { ++i; continue; } size_t j = i; while (j < raw.size() && raw[j] != ' ') ++j; l.chords.push_back ({ (int) i, raw.substr (i, j - i) }); i = j; }
        ChartItem it; it.kind = ChartItem::Line; it.line = std::move (l); items.push_back (std::move (it));
    };
    std::istringstream is (text); std::string ln;
    while (std::getline (is, ln)) {
        if (! ln.empty() && ln.back() == '\r') ln.pop_back();
        std::string t = trim (ln);
        if (t.size() > 1 && t.front() == '{' && t.back() == '}') {
            std::string in = t.substr (1, t.size() - 2), k, v; auto colon = in.find (':');
            k = lower (trim (colon == std::string::npos ? in : in.substr (0, colon))); v = colon == std::string::npos ? std::string() : trim (in.substr (colon + 1));
            if (k == "comment" || k == "c") { flushChord(); flushTab(); ChartItem it; it.kind = ChartItem::Comment; it.text = v; items.push_back (std::move (it)); }
            else if (k == "start_of_tab" || k == "sot") { flushChord(); inTab = true; tab = TabBlock(); tab.title = v.empty() ? "Tab" : v; }
            else if (k == "end_of_tab" || k == "eot") flushTab();
            continue;
        }
        if (t.empty()) { flushChord(); if (inTab) flushTab(); continue; }
        if (inTab || isTabLine (t)) { flushChord(); inTab = true; if (tab.title.empty()) tab.title = "Tab"; tab.strings.push_back (t); continue; }
        auto toks = splitWs (t); bool chordLine = ! toks.empty(); int chords = 0;
        for (auto& x : toks) { if (! isChordToken (x)) { chordLine = false; break; } if (x != "|" && x != "/" && x != "-") ++chords; }
        if (chordLine && chords > 0) { flushChord(); pending = true; raw = ln; while (! raw.empty() && (raw.back() == ' ' || raw.back() == '\r')) raw.pop_back(); continue; }
        ChartLine l; std::string rr = ln; while (! rr.empty() && (rr.back() == ' ' || rr.back() == '\r')) rr.pop_back();
        if (t.find ('[') != std::string::npos) {
            std::string lyr; std::vector<ChordAt> ch; size_t i = 0; bool ok = true;
            while (i < t.size()) { if (t[i] == '[') { auto e = t.find (']', i); if (e == std::string::npos) { ok = false; break; } std::string tok = t.substr (i + 1, e - i - 1); if (! isChordToken (tok)) { ok = false; break; } ch.push_back ({ (int) lyr.size(), tok }); i = e + 1; } else lyr += t[i++]; }
            if (ok && ! ch.empty()) { flushChord(); l.chords = ch; l.lyric = lyr; ChartItem it; it.kind = ChartItem::Line; it.line = std::move (l); items.push_back (std::move (it)); continue; }
        }
        l.lyric = rr; size_t lead = l.lyric.find_first_not_of (' '); if (lead != std::string::npos && lead > 0) l.lyric = l.lyric.substr (lead); else lead = 0;
        if (pending) { pending = false; size_t i = 0; while (i < raw.size()) { if (raw[i] == ' ') { ++i; continue; } size_t j = i; while (j < raw.size() && raw[j] != ' ') ++j; l.chords.push_back ({ std::max (0, (int) i - (int) lead), raw.substr (i, j - i) }); i = j; } }
        ChartItem it; it.kind = ChartItem::Line; it.line = std::move (l); items.push_back (std::move (it));
    }
    flushChord(); flushTab();
    items.erase (std::remove_if (items.begin(), items.end(), [] (const ChartItem& i) { return i.kind == ChartItem::Line && i.line.chords.empty() && trim (i.line.lyric).empty(); }), items.end());
    return items;
}
int nonTabCount (const Section& s) { int n = 0; for (auto& it : s.items) if (it.kind != ChartItem::Tab) ++n; return n; }
int nonTabAt (const Section& s, int line) {
    int seen = 0; for (int i = 0; i < (int) s.items.size(); ++i) if (s.items[(size_t) i].kind != ChartItem::Tab) { if (seen == line) return i; ++seen; } return -1;
}
void labelNew (Section& s, const std::string& name) {
    s.name = trim (name); if (s.name.empty()) s.name = "New section";
    s.label = sectionLabelFor (s.name, &s.kind, &s.color); s.customColor.clear(); s.occurrence = 1; s.source = -1;
}
} // namespace

std::string sectionBodyText (const Section& s) {
    std::string o;
    for (auto& it : s.items) {
        if (it.kind == ChartItem::Tab) continue;
        if (it.kind == ChartItem::Comment) { o += "{comment: " + it.text + "}\n"; continue; }
        o += inlineChords (it.line) + "\n";
    }
    return o;
}
bool setSectionBody (Section& s, const std::string& text) {
    auto parsed = parseBodyItems (text); bool anyTab = false; for (auto& it : parsed) if (it.kind == ChartItem::Tab) anyTab = true;
    if (anyTab) { s.items = std::move (parsed); return true; }
    std::vector<ChartItem> tabs; for (auto& it : s.items) if (it.kind == ChartItem::Tab) tabs.push_back (it);
    s.items = std::move (parsed); for (auto& t : tabs) s.items.push_back (std::move (t)); return true;
}
ChartEdit duplicateSection (Chart& c, int index) {
    ChartEdit e; const int n = (int) c.sections.size(); if (index < 0 || index >= n) return e;
    Section copy = c.sections[(size_t) index]; copy.occurrence = 1; copy.source = -1;
    c.sections.insert (c.sections.begin() + index + 1, std::move (copy));
    e.oldToNew.resize ((size_t) n); for (int i = 0; i < n; ++i) e.oldToNew[(size_t) i] = i <= index ? i : i + 1; e.ok = true; return e;
}
ChartEdit splitSection (Chart& c, int index, int firstLineOfNew, const std::string& newName) {
    ChartEdit e; const int n = (int) c.sections.size(); if (index < 0 || index >= n || firstLineOfNew < 1) return e;
    Section& src = c.sections[(size_t) index]; if (firstLineOfNew >= nonTabCount (src)) return e;
    const int at = nonTabAt (src, firstLineOfNew); if (at <= 0) return e;
    Section neu; labelNew (neu, newName); neu.repeat = 1; neu.bars = 0;
    neu.items.assign (src.items.begin() + at, src.items.end()); src.items.erase (src.items.begin() + at, src.items.end());
    c.sections.insert (c.sections.begin() + index + 1, std::move (neu));
    e.oldToNew.resize ((size_t) n); for (int i = 0; i < n; ++i) e.oldToNew[(size_t) i] = i <= index ? i : i + 1; e.ok = true; return e;
}
ChartEdit mergeSections (Chart& c, int index) {
    ChartEdit e; const int n = (int) c.sections.size(); if (index < 0 || index + 1 >= n) return e;
    auto& a = c.sections[(size_t) index]; auto& b = c.sections[(size_t) index + 1];
    a.items.insert (a.items.end(), b.items.begin(), b.items.end()); if (a.bars > 0 && b.bars > 0) a.bars += b.bars;
    c.sections.erase (c.sections.begin() + index + 1);
    e.oldToNew.resize ((size_t) n); for (int i = 0; i < n; ++i) e.oldToNew[(size_t) i] = i < index ? i : i == index || i == index + 1 ? index : i - 1; e.ok = true; return e;
}
ChartEdit moveSection (Chart& c, int from, int to) {
    ChartEdit e; const int n = (int) c.sections.size(); if (from < 0 || to < 0 || from >= n || to >= n) return e;
    if (from != to) { Section s = std::move (c.sections[(size_t) from]); c.sections.erase (c.sections.begin() + from); c.sections.insert (c.sections.begin() + to, std::move (s)); }
    std::vector<int> id ((size_t) n); for (int i = 0; i < n; ++i) id[(size_t) i] = i;
    if (from != to) { int moving = id[(size_t) from]; id.erase (id.begin() + from); id.insert (id.begin() + to, moving); }
    e.oldToNew.assign ((size_t) n, 0); for (int i = 0; i < n; ++i) e.oldToNew[(size_t) id[(size_t) i]] = i; e.ok = true; return e;
}
ChartEdit deleteSection (Chart& c, int index) {
    ChartEdit e; const int n = (int) c.sections.size(); if (index < 0 || index >= n) return e;
    c.sections.erase (c.sections.begin() + index);
    e.oldToNew.resize ((size_t) n); for (int i = 0; i < n; ++i) e.oldToNew[(size_t) i] = i < index ? i : i == index ? -1 : i - 1; e.ok = true; return e;
}
ChartEdit renameSection (Chart& c, int index, const std::string& name) {
    ChartEdit e; const int n = (int) c.sections.size(); if (index < 0 || index >= n || trim (name).empty()) return e;
    labelNew (c.sections[(size_t) index], name); e.oldToNew.resize ((size_t) n); for (int i = 0; i < n; ++i) e.oldToNew[(size_t) i] = i; e.ok = true; return e;
}
void remapArrangement (std::vector<std::string>& tokens, const Chart& before, const Chart& after, const std::vector<int>& oldToNew) {
    if (tokens.empty() || oldToNew.empty()) return;
    std::vector<std::string> out;
    for (auto& t : tokens) {
        const int i = resolveArrangementToken (before, t);
        if (i < 0 || i >= (int) oldToNew.size()) { out.push_back (t); continue; }
        const int j = oldToNew[(size_t) i]; if (j < 0 || j >= (int) after.sections.size()) continue;
        const auto& s = after.sections[(size_t) j];
        out.push_back (resolveArrangementToken (after, s.label) == j ? s.label : s.name);
    }
    tokens.swap (out);
}
void commitChartEdit (Library& lib, const std::string& songId, const Chart& before, Chart after, const std::vector<int>& oldToNew) {
    Song* s = lib.song (songId); if (! s) return;
    applySectionColors (after, lib.settings.sectionColors);
    auto songTokens = before.arrangement; remapArrangement (songTokens, before, after, oldToNew); after.arrangement = songTokens;
    for (auto& set : lib.setlists) for (auto& e : set.entries) if (e.songId == songId && ! e.arrangement.empty()) remapArrangement (e.arrangement, before, after, oldToNew);
    s->chart = std::move (after);
}
std::vector<TabPart> parseTabText (const std::string& text) {
    struct Acc { std::string title; std::vector<std::string> strings; };
    std::vector<Acc> parts; Acc cur;
    auto flush = [&] { if (! cur.strings.empty()) { if (cur.title.empty()) cur.title = "Tab"; parts.push_back (cur); } cur.strings.clear(); };
    std::istringstream is (text); std::string ln;
    while (std::getline (is, ln)) {
        if (! ln.empty() && ln.back() == '\r') ln.pop_back();
        std::string t = trim (ln);
        if (t.empty()) { flush(); continue; }
        if (isTabLine (t)) cur.strings.push_back (t);
        else { flush(); cur.title = t; }
    }
    flush();
    std::vector<TabPart> out;
    for (auto& p : parts) {
        if ((int) p.strings.size() >= 12 && p.strings.size() % 6 == 0) {
            for (size_t i = 0; i < p.strings.size(); i += 6) {
                TabPart part; part.title = p.title + (i == 0 ? std::string() : " " + std::to_string (i / 6 + 1));
                part.tab.title = part.title; part.tab.strings.assign (p.strings.begin() + (long) i, p.strings.begin() + (long) i + 6); out.push_back (std::move (part));
            }
        } else { TabPart part; part.title = p.title; part.tab.title = p.title; part.tab.strings = p.strings; out.push_back (std::move (part)); }
    }
    return out;
}
static int tabIndexAt (const Section& s, int tabIndex) {
    int seen = 0; for (int i = 0; i < (int) s.items.size(); ++i) if (s.items[(size_t) i].kind == ChartItem::Tab) { if (seen == tabIndex) return i; ++seen; } return -1;
}
int sectionTabCount (const Section& s) { int n = 0; for (auto& it : s.items) if (it.kind == ChartItem::Tab) ++n; return n; }
bool addSectionTab (Section& s, TabBlock tab) {
    if (tab.strings.empty()) return false; if (tab.title.empty()) tab.title = "Tab";
    ChartItem it; it.kind = ChartItem::Tab; it.tab = std::move (tab); s.items.push_back (std::move (it)); return true;
}
bool removeSectionTab (Section& s, int tabIndex) { int i = tabIndexAt (s, tabIndex); if (i < 0) return false; s.items.erase (s.items.begin() + i); return true; }
bool replaceSectionTab (Section& s, int tabIndex, TabBlock tab) {
    int i = tabIndexAt (s, tabIndex); if (i < 0 || tab.strings.empty()) return false; if (tab.title.empty()) tab.title = s.items[(size_t) i].tab.title;
    s.items[(size_t) i].tab = std::move (tab); return true;
}

std::vector<double> estimatePracticeMarks (const Chart& c, double bpm, double t0) {
    std::vector<double> m (c.sections.size(), -1.0); if (bpm <= 0) return m;
    int bpb = std::atoi (c.time.c_str()); if (bpb < 1 || bpb > 16) bpb = 4; double t = t0;
    for (size_t i = 0; i < c.sections.size(); ++i) { m[i] = t; if (c.sections[i].bars <= 0) break; t += c.sections[i].bars * std::max (1, c.sections[i].repeat) * bpb * 60.0 / bpm; }
    return m;
}

} // namespace wr
