// v0.8 editable section colours: palette, defaults, ChordPro {x_section_color:} round trip, JSON, live chart, band sync.
#include "sync/Sync.h"
#include "model/Chart.h"
#include "model/Projector.h"
#include <cstdio>
using namespace wr; using namespace wr::sync;
static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (! (c)) { ++fails; std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_EQ(a, b) do { ++checks; auto _a = (a); auto _b = (b); if (! (_a == _b)) { ++fails; std::printf ("FAIL %s:%d  %s == %s\n", __FILE__, __LINE__, #a, #b); } } while (0)
using S = std::string;

static const char* kSong = "{title: Morning Song}\n{key: G}\n{tempo: 120}\n{arrangement: V1 C V2 C}\n{x_section_color: Chorus=teal}\n{x_unknown_thing: whatever}\n{meta: foo bar}\n"
    "{start_of_verse: Verse 1}\n{bars: 2}\n[G]Light breaks\n{end_of_verse}\n{start_of_chorus: Chorus}\n{bars: 2}\n[G]Rise up\n{end_of_chorus}\n"
    "{start_of_verse: Verse 2}\n{x_section_color: #f80}\n[Em]Every heart\n{end_of_verse}\n{start_of_bridge: Bridge}\n{x_section_color: notacolour}\n[C]Even when\n{end_of_bridge}\n";

struct Sink : RigSink { void loadRig (const RigPreset&) override {} void applyScene (const Scene&) override {} void setPedal (const S&, bool) override {}
    void setBpm (double) override {} void announceSection (const S&, int) override {} bool cueDue (int) override { return false; } };
static Library lib() { Library l = seedLibrary(); Song a; a.id = "m"; a.chart = parseChart (kSong); l.songs.push_back (a);
    Setlist set; set.name = "Sun"; set.date = "2026-11-08"; SetlistEntry e; e.songId = "m"; set.entries = { e }; l.setlists = { set }; return l; }

static void testPalette() {
    CHECK_EQ ((int) sectionPalette().size(), 12);
    CHECK_EQ (normalizeColor ("Teal"), S ("teal")); CHECK_EQ (normalizeColor ("gray"), S ("grey")); CHECK_EQ (normalizeColor ("#ff8800"), S ("#FF8800"));
    CHECK_EQ (normalizeColor ("#f80"), S ("#FF8800")); CHECK_EQ (normalizeColor ("#ff88"), S ()); CHECK_EQ (normalizeColor ("banana"), S ()); CHECK_EQ (normalizeColor ("#gg0000"), S ());
    uint32_t v = 0; CHECK (colorArgb ("#102030", v)); CHECK_EQ (v, 0xff102030u); CHECK (colorArgb ("orange", v)); CHECK_EQ (v, 0xfff5a524u); CHECK (! colorArgb ("x", v));
    CHECK_EQ (builtinKindColor ("verse"), S ("orange")); CHECK_EQ (builtinKindColor ("chorus"), S ("blue")); CHECK_EQ (builtinKindColor ("bridge"), S ("purple")); CHECK_EQ (builtinKindColor ("outro"), S ("grey"));
    SectionColorMap d { { "chorus", "red" }, { "verse", "bogus" } };
    CHECK_EQ (kindColor (d, "chorus"), S ("red")); CHECK_EQ (kindColor (d, "verse"), S ("orange")); CHECK_EQ (kindColor (d, "bridge"), S ("purple"));
    CHECK_EQ (kindDisplayName ("prechorus"), S ("Pre-Chorus")); CHECK_EQ ((int) sectionKinds().size(), 9);
}
static void testChordPro() {
    Chart c = parseChart (kSong); CHECK_EQ ((int) c.sections.size(), 4);
    CHECK_EQ (c.sections[0].customColor, S ()); CHECK_EQ (c.sections[0].color, S ("orange"));
    CHECK_EQ (c.sections[1].customColor, S ("teal")); CHECK_EQ (c.sections[1].color, S ("teal"));
    CHECK_EQ (c.sections[2].customColor, S ("#FF8800")); CHECK_EQ (c.sections[3].customColor, S ()); CHECK_EQ (c.sections[3].color, S ("purple"));
    CHECK_EQ (c.title, S ("Morning Song")); CHECK_EQ (formatArrangement (c.arrangement), S ("V1 C V2 C"));   // unknown directives ignored
    auto cp = chartToChordPro (c); CHECK (cp.find ("{x_section_color: Chorus=teal}") != S::npos);
    Chart b = parseChart (cp); for (size_t i = 0; i < 4; ++i) CHECK_EQ (b.sections[i].customColor, c.sections[i].customColor);
    // duplicate names write by position
    Chart d = parseChart ("{start_of_chorus: Chorus}\nA\n{start_of_chorus: Chorus}\nB\n"); CHECK (setSectionColorOverride (d, 1, "lime"));
    auto dp = chartToChordPro (d); CHECK (dp.find ("@2=lime") != S::npos); Chart d2 = parseChart (dp); CHECK_EQ (d2.sections[0].customColor, S ()); CHECK_EQ (d2.sections[1].customColor, S ("lime"));
    // label / shorthand tokens, bad index, bad colour
    Chart e = parseChart ("{x_section_color: V2=green}\n{section_color: @9=red}\n{start_of_verse: Verse 1}\nA\n{start_of_verse: Verse 2}\nB\n"); CHECK_EQ (e.sections[1].customColor, S ("green")); CHECK_EQ (e.sections[0].customColor, S ());
    CHECK (! setSectionColorOverride (e, 7, "red")); CHECK (! setSectionColorOverride (e, 0, "nope")); CHECK (setSectionColorOverride (e, 1, "")); CHECK_EQ (e.sections[1].customColor, S ());
    clearSectionColorOverrides (c); for (auto& s : c.sections) CHECK (s.customColor.empty());
    // projector slides ignore colours
    Chart p1 = parseChart (kSong), p2 = p1; clearSectionColorOverrides (p2); applySectionColors (p2, { { "verse", "red" } });
    ProjectorSettings ps; auto s1 = songSlides (p1, ps), s2 = songSlides (p2, ps); CHECK_EQ (s1.size(), s2.size());
}
static void testJsonAndLive() {
    Library L = lib(); L.settings.sectionColors["verse"] = "yellow";
    Library L2 = libraryFromJson (toJson (L)); CHECK_EQ (L2.settings.sectionColors["verse"], S ("yellow")); CHECK_EQ (L2.song ("m")->chart.sections[1].customColor, S ("teal"));
    CHECK_EQ (L2.song ("m")->chart.sections[1].color, S ("teal"));
    Sink sk; LiveSession live (L, sk); live.start (L.setlists[0], 0, 0);
    const auto& ch = live.chart(); CHECK_EQ ((int) ch.sections.size(), 4);
    CHECK_EQ (ch.sections[0].color, S ("yellow")); CHECK_EQ (ch.sections[1].color, S ("teal")); CHECK_EQ (ch.sections[3].color, S ("teal")); CHECK_EQ (ch.sections[2].color, S ("#FF8800"));
    L.settings.sectionColors["chorus"] = "red"; L.song ("m")->chart.sections[1].customColor.clear(); live.refreshChart();
    CHECK_EQ (live.chart().sections[1].color, S ("red")); CHECK_EQ (live.chart().sections[3].color, S ("red"));
    L.settings.sectionColors.clear(); live.refreshChart(); CHECK_EQ (live.chart().sections[1].color, S ("blue")); CHECK_EQ (live.chart().sections[0].color, S ("orange"));
}
static void testSync() {
    Library LL = lib(), FL = seedLibrary(); FL.settings.sectionColors["verse"] = "white";   // follower's own default differs
    Sink a, b; LiveSession lead (LL, a), fol (FL, b); FakeClock clock; Leader leader ("S"); std::vector<S> up;
    Follower f ([&] (const S& l) { up.push_back (l); }, "F");
    auto step = [&] (double t) { for (auto& l : leader.update (lead, clock, t)) f.onLine (l, fol, FL, clock, t);
        auto u = up; up.clear(); for (auto& l : u) { auto r = leader.onFollowerLine (l, lead, clock, t); if (! r.empty()) f.onLine (r, fol, FL, clock, t); } };
    LL.settings.sectionColors["verse"] = "pink"; lead.start (LL.setlists[0], 0, 0); step (0); f.onConnected(); step (0.1);
    CHECK (f.synced()); CHECK_EQ ((int) fol.chart().sections.size(), 4);
    CHECK_EQ (fol.chart().sections[0].color, S ("pink")); CHECK_EQ (fol.chart().sections[1].color, S ("teal"));
    // leader changes a default and a song override: snapshot carries both
    LL.settings.sectionColors["verse"] = "lime"; LL.song ("m")->chart.sections[1].customColor = "indigo"; lead.refreshChart(); step (1);
    CHECK_EQ (fol.chart().sections[0].color, S ("lime")); CHECK_EQ (fol.chart().sections[1].color, S ("indigo")); CHECK_EQ (fol.chart().sections[3].color, S ("indigo"));
    CHECK_EQ (FL.song ("m")->chart.sections[1].customColor, S ("teal"));   // follower's library not rewritten
    // leaving the session -> own colours again
    fol.setFollowing (false); CHECK_EQ (fol.chart().sections[0].color, S ("white"));
    // state JSON round trip + old leader (no colours field)
    State st = leader.capture (lead, clock, 2); State st2 = stateFromJson (toJson (st)); CHECK (st2.hasColors); CHECK_EQ (st2.sectionColors["verse"], S ("lime"));
    Json j = toJson (st); Json j2 = Json::object(); for (auto& kv : j.members()) if (kv.first != "sectionColors") j2.set (kv.first, kv.second); CHECK (! stateFromJson (j2).hasColors);
}
int main() { testPalette(); testChordPro(); testJsonAndLive(); testSync(); std::printf ("%d checks, %d failures\n", checks, fails); return fails ? 1 : 0; }
