#include "StageTools.h"
#include <algorithm>
#include <cstdlib>
#include <set>

namespace wr {
int daysFromCivil (const std::string& d) {
    if (d.size() != 10 || d[4] != '-' || d[7] != '-') return -1000000;
    int y = std::atoi (d.substr (0, 4).c_str()), m = std::atoi (d.substr (5, 2).c_str()), dd = std::atoi (d.substr (8, 2).c_str());
    if (m < 1 || m > 12 || dd < 1 || dd > 31) return -1000000;
    y -= m <= 2; const int era = (y >= 0 ? y : y - 399) / 400; const unsigned yoe = (unsigned) (y - era * 400);
    const unsigned doy = (unsigned) ((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + dd - 1); const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int) doe - 719468;
}
bool logPlay (Library& l, const std::string& id, const std::string& date) {
    if (id.empty() || daysFromCivil (date) == -1000000) return false;
    for (auto& e : l.playLog) if (e.songId == id && e.date == date) return false;
    l.playLog.push_back ({ id, date }); return true;
}
std::vector<std::string> playHistory (const Library& l, const std::string& id) {
    std::vector<std::string> v; for (auto& e : l.playLog) if (e.songId == id) v.push_back (e.date);
    std::sort (v.begin(), v.end(), std::greater<std::string>()); return v;
}
int daysSinceLastPlayed (const Library& l, const std::string& id, const std::string& today, bool excl) {
    const int t = daysFromCivil (today); int best = -1;
    for (auto& e : l.playLog) if (e.songId == id) { int d = t - daysFromCivil (e.date); if (d < 0 || (excl && d == 0)) continue; if (best < 0 || d < best) best = d; }
    return best;
}
int playsWithin (const Library& l, const std::string& id, const std::string& today, int days) {
    const int t = daysFromCivil (today); int n = 0; for (auto& e : l.playLog) if (e.songId == id) { int d = t - daysFromCivil (e.date); if (d >= 0 && d <= days) ++n; } return n;
}
bool playedRecently (const Library& l, const std::string& id, const std::string& today) { int d = daysSinceLastPlayed (l, id, today); return d > 0 && d <= l.settings.repeatWarnDays; }
std::string recentText (const Library& l, const std::string& id, const std::string& today) {
    if (! playedRecently (l, id, today)) return {}; int d = daysSinceLastPlayed (l, id, today);
    return d == 1 ? "Played yesterday" : d % 7 == 0 ? "Played " + std::to_string (d / 7) + (d == 7 ? " week ago" : " weeks ago") : "Played " + std::to_string (d) + " days ago";
}
std::vector<SongStat> songStats (const Library& l) {
    std::vector<SongStat> v;
    for (auto& s : l.songs) { SongStat st; st.songId = s.id; st.title = s.chart.title; auto h = playHistory (l, s.id); st.plays = (int) h.size(); st.last = h.empty() ? "" : h.front(); v.push_back (st); }
    std::stable_sort (v.begin(), v.end(), [] (const SongStat& a, const SongStat& b) { return a.plays != b.plays ? a.plays > b.plays : a.last > b.last; });
    return v;
}

// ---------------------------------------------------------------- bundle
std::string exportBundle (const Library& l, const std::string& app, long long now) {
    Json j = Json::object(); j.set ("format", "worship-rig-backup").set ("bundleVersion", 1).set ("app", app).set ("created", (double) now).set ("library", toJson (l));
    return j.dump (1);
}
ImportReport importBundle (const std::string& text, Library& lib, ImportMode mode) {
    ImportReport r; bool ok = false; Json j = Json::parse (text, &ok);
    if (! ok || ! j.isObject()) { r.error = "Not a Bearing Live backup (not JSON)"; return r; }
    Json L;
    if (j["format"].str() == "worship-rig-backup") { if (j["bundleVersion"].integer (0) > 1) { r.error = "Backup is from a newer version of the app"; return r; } L = j["library"]; }
    else if (j.has ("songs") && j.has ("settings")) L = j;   // bare library.json
    else { r.error = "Not a Bearing Live backup"; return r; }
    if (! L.isObject() || ! L.has ("songs")) { r.error = "Backup has no library"; return r; }
    Library in = libraryFromJson (L);
    if (mode == ImportMode::Replace) { r.songs = (int) in.songs.size(); r.setlists = (int) in.setlists.size(); r.rigs = (int) in.rigs.size();
        if (in.controllers.empty()) in.controllers = lib.controllers; lib = std::move (in); r.ok = true; return r; }
    for (auto& s : in.songs) { if (lib.song (s.id)) { ++r.skipped; continue; } lib.songs.push_back (s); ++r.songs; }
    for (auto& s : in.setlists) { if (lib.setlist (s.date)) { ++r.skipped; continue; } lib.setlists.push_back (s); ++r.setlists; }
    for (auto& g : in.rigs) { if (lib.rig (g.name)) { ++r.skipped; continue; } lib.rigs.push_back (g); ++r.rigs; }
    for (auto& e : in.playLog) logPlay (lib, e.songId, e.date);
    r.ok = true; return r;
}

// ---------------------------------------------------------------- capo helper
static bool openShape (const std::string& ch) {
    static const std::set<std::string> open = { "C", "D", "E", "G", "A", "Am", "Em", "Dm", "E7", "A7", "D7", "G7", "C7", "B7", "Cadd9", "Dsus4", "Dsus2", "Asus4", "Asus2", "Esus4", "Am7", "Em7", "Dm7", "Fmaj7", "Cmaj7", "Gmaj7", "Dmaj7", "Amaj7" };
    auto base = ch.substr (0, ch.find ('/')); return open.count (base) > 0;
}
std::vector<CapoHelp> capoHelper (const Chart& c, const std::string& target, int maxCapo) {
    std::vector<CapoHelp> out; if (noteIndex (target.substr (0, target.size() > 1 && (target[1] == '#' || target[1] == 'b') ? 2 : 1)) < 0) return out;
    for (int capo = 0; capo <= maxCapo; ++capo) {
        Chart t = c.key.empty() ? c : transposeChart (c, target); t.key = target; t.capo = 0; Chart shp = chartForPart (t, target, capo);
        CapoHelp h; h.capo = capo; h.shapesKey = shapesKey (target, capo); std::set<std::string> seen;
        for (auto& s : shp.sections) for (auto& it : s.items) if (it.kind == ChartItem::Line) for (auto& ch : it.line.chords) if (isChordToken (ch.chord) && seen.insert (ch.chord).second) h.shapes.push_back (ch.chord);
        for (auto& x : h.shapes) if (openShape (x)) ++h.openShapes;
        const int n = std::max (1, (int) h.shapes.size()); h.score = h.openShapes * 100 / n - capo * 3; out.push_back (h);
    }
    std::stable_sort (out.begin(), out.end(), [] (const CapoHelp& a, const CapoHelp& b) { return a.score > b.score; });
    return out;
}
std::string shapesText (const CapoHelp& h) {
    std::string s = (h.capo == 0 ? std::string ("No capo") : "Capo " + std::to_string (h.capo)) + " - " + h.shapesKey + " shapes:";
    for (size_t i = 0; i < h.shapes.size() && i < 8; ++i) s += " " + h.shapes[i]; if (h.shapes.size() > 8) s += " ..."; return s;
}

// ---------------------------------------------------------------- auto-scroll
double sectionProgress (double beat, int bpb, int bars, int repeat) {
    if (bars <= 0 || bpb <= 0) return 0; const double total = (double) bars * bpb * std::max (1, repeat); return std::clamp (beat / total, 0.0, 1.0);
}
float autoScrollOffset (float top, float h, double p, float viewH, float contentH, float lead) {
    // keep the "now" line (top + p * h) at about a third of the view; at the start of a section show its header
    const float now = top + (float) p * h; const float target = p <= 0 ? top - lead : std::max (top - lead, now - viewH * 0.33f);
    return std::clamp (target, 0.0f, std::max (0.0f, contentH - viewH));
}
} // namespace wr
