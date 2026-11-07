#include "Ccli.h"
#include <algorithm>
#include <map>
#include <cctype>

namespace wr::ccli {

std::string effectiveTitle (const Song& s) { return ! s.ccli.title.empty() ? s.ccli.title : s.chart.title.empty() ? s.id : s.chart.title; }
bool validDate (const std::string& d) {
    if (d.size() != 10 || d[4] != '-' || d[7] != '-') return false;
    for (int i : { 0, 1, 2, 3, 5, 6, 8, 9 }) if (! std::isdigit ((unsigned char) d[(size_t) i])) return false;
    const int m = std::stoi (d.substr (5, 2)), day = std::stoi (d.substr (8, 2)); return m >= 1 && m <= 12 && day >= 1 && day <= 31;
}
bool logUsage (Library& lib, const std::string& id, const std::string& date, const std::string& setlist, bool projected, bool chart) {
    const Song* s = lib.song (id); if (! s || ! validDate (date) || (! projected && ! chart)) return false;
    for (auto& e : lib.usageLog) if (e.songId == id && e.date == date && e.setlist == setlist) {
        const bool ch = (projected && ! e.projected) || (chart && ! e.chart); e.projected |= projected; e.chart |= chart; return ch; }
    UsageEntry u; u.date = date; u.songId = id; u.setlist = setlist; u.title = effectiveTitle (*s); u.ccliNumber = s->ccli.number; u.authors = s->ccli.authors;
    u.publicDomain = s->ccli.publicDomain; u.projected = projected; u.chart = chart; lib.usageLog.push_back (u); return true;
}
std::vector<UsageEntry> usageInRange (const Library& lib, const ExportOptions& o) {
    std::vector<UsageEntry> v;
    for (auto e : lib.usageLog) {
        if (! o.from.empty() && e.date < o.from) continue; if (! o.to.empty() && e.date > o.to) continue;
        if (auto* s = const_cast<Library&> (lib).song (e.songId)) { if (e.ccliNumber.empty()) e.ccliNumber = s->ccli.number; if (e.authors.empty()) e.authors = s->ccli.authors; e.publicDomain |= s->ccli.publicDomain; }
        if (e.publicDomain && ! o.includePublicDomain) continue;
        v.push_back (e);
    }
    std::stable_sort (v.begin(), v.end(), [] (auto& a, auto& b) { return a.date < b.date; });
    return v;
}
std::string csvField (const std::string& s) {
    std::string t = s; if (! t.empty() && (t[0] == '=' || t[0] == '+' || t[0] == '-' || t[0] == '@')) t = "'" + t;   // no spreadsheet formulas
    if (t.find_first_of (",\"\n\r") == std::string::npos) return t;
    std::string o = "\""; for (char c : t) { if (c == '"') o += '"'; o += c; } return o + "\"";
}
std::string exportCsv (const Library& lib, const ExportOptions& o) {
    std::string out = "Date,Song Title,CCLI Song Number,Authors,Public Domain,Lyrics Projected,Chart Used,Setlist\r\n";
    for (auto& e : usageInRange (lib, o))
        out += csvField (e.date) + "," + csvField (e.title) + "," + csvField (e.ccliNumber) + "," + csvField (e.authors) + "," + (e.publicDomain ? "Yes" : "No") + ","
            + (e.projected ? "Yes" : "No") + "," + (e.chart ? "Yes" : "No") + "," + csvField (e.setlist) + "\r\n";
    return out;
}
std::string exportTotalsCsv (const Library& lib, const ExportOptions& o) {
    struct T { std::string title, number, authors, first, last; int uses = 0, projected = 0, charts = 0; };
    std::map<std::string, T> per; std::vector<std::string> order;
    for (auto& e : usageInRange (lib, o)) { auto it = per.find (e.songId); if (it == per.end()) { order.push_back (e.songId); it = per.emplace (e.songId, T()).first; it->second.first = e.date; }
        auto& t = it->second; t.title = e.title; t.number = e.ccliNumber; t.authors = e.authors; t.last = e.date; ++t.uses; t.projected += e.projected; t.charts += e.chart; }
    std::sort (order.begin(), order.end(), [&] (auto& a, auto& b) { return per[a].uses != per[b].uses ? per[a].uses > per[b].uses : per[a].title < per[b].title; });
    std::string out = "CCLI Song Number,Title,Authors,Times Used,Digital (Lyrics Projected),Charts Displayed,First Used,Last Used\r\n";
    for (auto& id : order) { auto& t = per[id];
        out += csvField (t.number) + "," + csvField (t.title) + "," + csvField (t.authors) + "," + std::to_string (t.uses) + "," + std::to_string (t.projected) + ","
            + std::to_string (t.charts) + "," + t.first + "," + t.last + "\r\n"; }
    return out;
}
std::string exportSummary (const Library& lib, const ExportOptions& o) {
    auto v = usageInRange (lib, o);
    struct T { std::string title, number, authors; int uses = 0, projected = 0, charts = 0; };
    std::map<std::string, T> per; for (auto& e : v) { auto& t = per[e.songId]; t.title = e.title; t.number = e.ccliNumber; t.authors = e.authors; ++t.uses; t.projected += e.projected; t.charts += e.chart; }
    std::vector<T> rows; for (auto& kv : per) rows.push_back (kv.second);
    std::sort (rows.begin(), rows.end(), [] (auto& a, auto& b) { return a.uses != b.uses ? a.uses > b.uses : a.title < b.title; });
    std::string s = "Tabernacle Tools - song usage for CCLI\nPeriod: " + (o.from.empty() ? std::string ("start") : o.from) + " to " + (o.to.empty() ? std::string ("today") : o.to) + "\n";
    s += std::to_string (v.size()) + " uses of " + std::to_string (rows.size()) + " songs" + (o.includePublicDomain ? " (public-domain songs included)" : " (public-domain songs left out)") + "\n\n";
    for (auto& r : rows) s += "- " + r.title + "  |  CCLI # " + (r.number.empty() ? std::string ("MISSING") : r.number) + "  |  used " + std::to_string (r.uses)
        + "x  |  lyrics projected " + std::to_string (r.projected) + "x  |  charts " + std::to_string (r.charts) + "x" + (r.authors.empty() ? std::string() : "  |  " + r.authors) + "\n";
    int missing = 0; for (auto& r : rows) missing += r.number.empty();
    if (missing) s += "\n" + std::to_string (missing) + " song(s) have no CCLI number - look them up in SongSelect / CCLI and add them in the song's CCLI details.\n";
    s += "\nHow to enter: sign in at ccli.com > Reporting > Search & Report. Search each song by its CCLI # (or title + author),\n"
         "press REPORT SONG, and use (+) for each additional use. 'Times used' = services; lyrics projected count as Digital.\n"
         "Print, recording and translation uses aren't tracked here - add them by hand.\n"
         "Tabernacle Tools does not send anything to CCLI.\n";
    return s;
}

} // namespace wr::ccli
