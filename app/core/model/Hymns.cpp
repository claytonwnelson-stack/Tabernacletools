#include "model/Hymns.h"
#include "model/Chart.h"
#include <algorithm>
#include <cctype>

namespace wr {
static std::string fold (const std::string& s) { // lowercase ASCII, map common accented UTF-8 letters, drop punctuation
    std::string o; o.reserve (s.size());
    for (size_t i = 0; i < s.size(); ++i) { unsigned char c = (unsigned char) s[i];
        if (c == 0xC3 && i + 1 < s.size()) { unsigned char d = (unsigned char) s[++i]; const char* m = "aaaaaaaceeeeiiiidnooooo/ouuuuyty"; // U+00E0..U+00FF
            if (d >= 0xA0 && d <= 0xBF) o += m[d - 0xA0]; else if (d >= 0x80 && d <= 0x9F) o += m[d - 0x80]; continue; }
        if (std::isalnum (c)) o += (char) std::tolower (c); else if (c == ' ' || c == '-' || c == '\n') { if (! o.empty() && o.back() != ' ') o += ' '; } }
    return o;
}
const Hymn* findHymn (const std::string& id) { for (auto& h : hymnLibrary()) if (h.id == id) return &h; return nullptr; }
std::string hymnFirstLine (const Hymn& h) {
    auto c = hymnChart (h); for (auto& s : c.sections) for (auto& it : s.items) if (it.kind == ChartItem::Line && ! it.line.lyric.empty()) return it.line.lyric;
    return {};
}
Chart hymnChart (const Hymn& h) { return parseChart (h.chordPro, h.title); }
std::vector<const Hymn*> searchHymns (const std::string& query) {
    std::vector<std::pair<int, const Hymn*>> r; const std::string q = fold (query);
    for (auto& h : hymnLibrary()) {
        int score = 0;
        if (q.empty()) score = 1;
        else { auto t = fold (h.title); auto fl = fold (hymnFirstLine (h));
            if (t.rfind (q, 0) == 0) score = 100; else if (t.find (q) != std::string::npos) score = 80; else if (fl.find (q) != std::string::npos) score = 60;
            else if (fold (h.textAuthor + " " + h.tuneSource).find (q) != std::string::npos || fold (h.tune).find (q) != std::string::npos) score = 40;
            else { std::string lyr; for (auto& s : hymnChart (h).sections) for (auto& it : s.items) if (it.kind == ChartItem::Line) lyr += it.line.lyric + " "; if (fold (lyr).find (q) != std::string::npos) score = 20; } }
        if (score) r.push_back ({ score, &h });
    }
    std::stable_sort (r.begin(), r.end(), [] (auto& a, auto& b) { return a.first != b.first ? a.first > b.first : a.second->title < b.second->title; });
    std::vector<const Hymn*> o; for (auto& x : r) o.push_back (x.second); return o;
}
std::string copyHymnToLibrary (Library& lib, const std::string& hymnId) {
    auto* h = findHymn (hymnId); if (! h) return {};
    Song s; s.chart = hymnChart (*h); std::string id = h->id; int n = 2; while (lib.song (id)) id = h->id + "-" + std::to_string (n++);
    s.ccli.publicDomain = true; s.ccli.title = h->title; s.ccli.authors = h->textAuthor + (h->tune.empty() ? std::string() : " (tune: " + h->tune + ")");
    s.id = id; s.rigPreset = lib.rigs.empty() ? "Worship Ambient" : lib.rigs.front().name;
    lib.songs.push_back (s); return id;
}
} // namespace wr
