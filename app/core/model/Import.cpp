#include "Import.h"
#include <cctype>
#include <sstream>
#include <map>
#include <algorithm>

namespace wr {
namespace {
std::string trimS (const std::string& s) { size_t a = s.find_first_not_of (" \t\r\n"); if (a == std::string::npos) return {}; size_t b = s.find_last_not_of (" \t\r\n"); return s.substr (a, b - a + 1); }
std::string lowerS (std::string s) { for (auto& c : s) c = (char) std::tolower ((unsigned char) c); return s; }
bool startsWithI (const std::string& s, const std::string& p) { return lowerS (s.substr (0, p.size())) == p; }
void replaceAll (std::string& s, const std::string& a, const std::string& b) { for (size_t p = 0; (p = s.find (a, p)) != std::string::npos; p += b.size()) s.replace (p, a.size(), b); }
std::vector<std::string> splitLines (const std::string& t) { std::vector<std::string> v; std::string l; std::istringstream is (t); while (std::getline (is, l)) { if (! l.empty() && l.back() == '\r') l.pop_back(); v.push_back (l); } return v; }
std::vector<std::string> words (const std::string& s) { std::vector<std::string> w; std::istringstream is (s); std::string x; while (is >> x) w.push_back (x); return w; }
bool isChordLine (const std::string& l) {   // every token is a chord (allow "|", "-", "x2", "(", ")" decorations)
    auto w = words (l); if (w.empty()) return false; int chords = 0;
    for (auto t : w) { while (! t.empty() && (t.front() == '(' || t.front() == '|')) t.erase (0, 1); while (! t.empty() && (t.back() == ')' || t.back() == '|')) t.pop_back();
        if (t.empty() || t == "-" || t == "/" || t == "%" || (t.size() >= 2 && (t[0] == 'x' || t[0] == 'X') && std::isdigit ((unsigned char) t[1]))) continue;
        if (! isChordToken (t)) return false; ++chords; }
    return chords > 0;
}
bool isSectionTag (const std::string& l) {   // [Verse 1], [Chorus], [Pre-Chorus], [Intro], [Bridge]...
    auto t = trimS (l); if (t.size() < 3 || t.front() != '[' || t.back() != ']') return false;
    auto in = lowerS (t.substr (1, t.size() - 2)); if (isChordToken (t.substr (1, t.size() - 2))) return false;
    for (const char* k : { "verse", "chorus", "pre", "bridge", "intro", "outro", "tag", "interlude", "instrumental", "solo", "refrain", "ending", "break", "hook", "vamp", "riff", "turnaround", "coda" })
        if (in.find (k) != std::string::npos) return true;
    return false;
}
bool isTabLine (const std::string& l) { auto t = trimS (l); return t.size() > 4 && std::string ("eBGDAEbgda").find (t[0]) != std::string::npos && (t[1] == '|' || (t.size() > 2 && t[2] == '|')) && std::count (t.begin(), t.end(), '-') >= 3; }
bool isChordPro (const std::string& t) { return t.find ("{title") != std::string::npos || t.find ("{t:") != std::string::npos || t.find ("{start_of_") != std::string::npos || t.find ("{soc}") != std::string::npos; }
} // namespace

std::string decodeEntities (const std::string& in) {
    std::string s = in;
    replaceAll (s, "&nbsp;", " "); replaceAll (s, "&quot;", "\""); replaceAll (s, "&#39;", "'"); replaceAll (s, "&apos;", "'"); replaceAll (s, "&lt;", "<"); replaceAll (s, "&gt;", ">");
    for (size_t p; (p = s.find ("&#")) != std::string::npos; ) { size_t e = s.find (';', p); if (e == std::string::npos || e - p > 8) break; int code = std::atoi (s.c_str() + p + 2);
        std::string rep = code == 8217 || code == 8216 ? "'" : code == 8220 || code == 8221 ? "\"" : code == 8211 || code == 8212 ? "-" : code > 0 && code < 128 ? std::string (1, (char) code) : std::string (" "); s.replace (p, e - p + 1, rep); }
    replaceAll (s, "&amp;", "&");
    replaceAll (s, "\xC2\xA0", " ");                                         // UTF-8 no-break space
    replaceAll (s, "\xE2\x80\x99", "'"); replaceAll (s, "\xE2\x80\x98", "'"); replaceAll (s, "\xE2\x80\x9C", "\""); replaceAll (s, "\xE2\x80\x9D", "\"");
    return s;
}

std::string normalizeSharedText (const std::string& raw, std::vector<std::string>* notes) {
    std::string s = decodeEntities (raw); replaceAll (s, "\t", "    ");
    if (isChordPro (s)) return s;
    // UG markup: [ch]G[/ch] -> G (keeps the column so chords stay above the right syllable); [tab]/[/tab] wrappers dropped
    bool hadMarkup = s.find ("[ch]") != std::string::npos || s.find ("[tab]") != std::string::npos;
    replaceAll (s, "[ch]", ""); replaceAll (s, "[/ch]", ""); replaceAll (s, "[tab]", ""); replaceAll (s, "[/tab]", "");
    if (hadMarkup && notes) notes->push_back ("converted Ultimate-Guitar style [ch] / [tab] markup");
    auto lines = splitLines (s);
    // find the chart inside a shared page: from the first section tag / chord line to the last one (+ its lyric line)
    int first = -1, last = -1;
    for (int i = 0; i < (int) lines.size(); ++i) if (isSectionTag (lines[(size_t) i]) || isChordLine (lines[(size_t) i]) || isTabLine (lines[(size_t) i])) { if (first < 0) first = i; last = i; }
    if (first < 0) return s;
    int end = last; if (isChordLine (lines[(size_t) last]) && last + 1 < (int) lines.size() && ! trimS (lines[(size_t) last + 1]).empty() && ! isSectionTag (lines[(size_t) last + 1])) end = last + 1;
    // metadata above the chart (kept as ChordPro directives so the normal parser picks them up)
    std::string head;
    for (int i = 0; i < first; ++i) {
        auto t = trimS (lines[(size_t) i]); if (t.empty()) continue; auto lt = lowerS (t);
        auto val = [&] (size_t from) { return trimS (t.substr (from)); };
        if (startsWithI (t, "key:")) { auto w = words (val (4)); if (! w.empty()) head += "{key: " + w.front() + "}\n"; }
        else if (startsWithI (t, "capo:")) { int c = std::atoi (val (5).c_str()); if (c == 0 && lt.find ("no capo") == std::string::npos) { for (char ch : t) if (std::isdigit ((unsigned char) ch)) { c = ch - '0'; break; } } if (c > 0) head += "{capo: " + std::to_string (c) + "}\n"; }
        else if (startsWithI (t, "tempo:") || startsWithI (t, "bpm:")) head += "{tempo: " + val (t.find (':') + 1) + "}\n";
        else if (startsWithI (t, "time:") || startsWithI (t, "time signature:")) head += "{time: " + val (t.find (':') + 1) + "}\n";
        else if (head.find ("{title") == std::string::npos) {
            auto pc = lt.find (" chords by "); if (pc == std::string::npos) pc = lt.find (" tab by "); if (pc == std::string::npos) pc = lt.find (" chords - ");
            if (pc != std::string::npos) { auto by = lt.find (" by ", pc); std::string title = trimS (t.substr (0, pc)), artist = by != std::string::npos ? trimS (t.substr (by + 4)) : std::string();
                head += "{title: " + title + "}\n"; if (! artist.empty()) head += "{artist: " + artist + "}\n"; }
            else { auto ch = lt.rfind (" chords"); auto dash = t.find (" - ");
                if (ch != std::string::npos && ch + 7 == lt.size() && dash != std::string::npos && dash < ch) { head += "{title: " + trimS (t.substr (dash + 3, ch - dash - 3)) + "}\n{artist: " + trimS (t.substr (0, dash)) + "}\n"; } }
        }
    }
    const int dropped = first + ((int) lines.size() - 1 - end);
    std::string body; for (int i = first; i <= end; ++i) body += lines[(size_t) i] + "\n";
    if (dropped > 0 && notes) {
        int nonEmpty = 0; for (int i = 0; i < (int) lines.size(); ++i) if ((i < first || i > end) && ! trimS (lines[(size_t) i]).empty()) ++nonEmpty;
        if (nonEmpty > 0) notes->push_back ("left out " + std::to_string (nonEmpty) + " line(s) of page text around the chart");
    }
    // UG tags are already "[Verse 1]" headers for the text parser; "Key: G" lines inside the chart are dropped
    return head.empty() ? body : head + body;
}

std::string guessKey (const Chart& c) {
    std::vector<std::string> chords; for (auto& s : c.sections) for (auto& it : s.items) if (it.kind == ChartItem::Line) for (auto& ch : it.line.chords) chords.push_back (ch.chord);
    if (chords.empty()) return {};
    auto root = [] (const std::string& ch, bool& minor) { std::string r = ch.substr (0, ch.size() > 1 && (ch[1] == '#' || ch[1] == 'b') ? 2 : 1); auto rest = ch.substr (r.size());
        minor = ! rest.empty() && rest[0] == 'm' && rest.compare (0, 3, "maj") != 0; return noteIndex (r); };
    double best = -1e9; std::string bestKey;
    for (int k = 0; k < 12; ++k) for (int mode = 0; mode < 2; ++mode) {   // 0 major, 1 relative minor tonic
        static const int majDeg[7] = { 0, 2, 4, 5, 7, 9, 11 }; static const bool majMin[7] = { false, true, true, false, false, true, true };
        double sc = 0;
        for (size_t i = 0; i < chords.size(); ++i) { bool mi = false; int r = root (chords[i], mi); if (r < 0) continue; int deg = ((r - k) % 12 + 12) % 12; double w = 0;
            for (int d = 0; d < 7; ++d) if (majDeg[d] == deg) w = majMin[d] == mi ? 1.0 : 0.3;
            if (deg == 10 && ! mi) w = 0.4;   // bVII is common in worship songs
            const int tonic = mode == 0 ? 0 : 9; if (deg == tonic && mi == (mode == 1)) w += (i == 0 || i + 1 == chords.size()) ? 1.5 : 0.4;
            sc += w; }
        if (sc > best) { best = sc; const int t = mode == 0 ? k : (k + 9) % 12; const std::string m = mode == 1 ? "m" : "";
            const std::string fl = spellNote (t, true) + m; bestKey = keyPrefersFlats (fl) ? fl : spellNote (t, false) + m; }
    }
    return bestKey;
}

ImportPreview previewImport (const std::string& raw, const std::string& fallbackTitle) {
    ImportPreview p; auto t = trimS (raw);
    if (! t.empty() && t.find ('\n') == std::string::npos && (startsWithI (t, "http://") || startsWithI (t, "https://"))) {
        p.urlOnly = true; p.notes.push_back ("Only a link was shared. Tabernacle Tools doesn't download web pages: select the chords on the page and share or copy the text instead."); return p; }
    p.chart = parseChart (normalizeSharedText (raw, &p.notes), fallbackTitle);
    for (auto& s : p.chart.sections) for (auto& it : s.items) if (it.kind == ChartItem::Line) { p.chordCount += (int) it.line.chords.size(); if (! trimS (it.line.lyric).empty()) ++p.lyricLines; }
    {   // the text parser guesses from the first chord; use the better whole-song guess instead
        auto& w = p.chart.warnings; auto it = std::find_if (w.begin(), w.end(), [] (const std::string& x) { return x.rfind ("Key not given", 0) == 0; });
        if (it != w.end()) { w.erase (it); p.chart.key.clear(); } }
    if (p.chart.capo > 0 && ! p.chart.key.empty()) {   // UG "Key:" is often the shapes key; the chart stores sounding keys
        const auto g = guessKey (p.chart), up = transposeKey (p.chart.key, p.chart.capo);
        if (! g.empty() && g == up && g != p.chart.key) { p.notes.push_back ("Key " + p.chart.key + " is the capo " + std::to_string (p.chart.capo) + " shapes key; the song sounds in " + g); p.chart.key = g; } }
    if (p.chart.key.empty()) { p.chart.key = guessKey (p.chart); p.keyGuessed = ! p.chart.key.empty(); }
    if (p.chart.title.empty()) p.chart.title = fallbackTitle.empty() ? "Imported song" : fallbackTitle;
    return p;
}

} // namespace wr
