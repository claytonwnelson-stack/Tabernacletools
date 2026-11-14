// v0.7 tests (core, every app): arrangements, next-song card, service notes, device health in the roster, practice player.
#include "sync/Sync.h"
#include "sync/Roster.h"
#include "model/Projector.h"
#include "model/Chart.h"
#include "audio/PracticePlayer.h"
#include <cstdio>
using namespace wr; using namespace wr::sync;

static int fails = 0, checks = 0;
template <typename T> static std::string toS (const T& v) { if constexpr (std::is_same_v<T, std::string>) return v; else if constexpr (std::is_arithmetic_v<T>) return std::to_string (v); else return "?"; }
#define CHECK(c) do { ++checks; if (! (c)) { ++fails; std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_EQ(a, b) do { ++checks; auto _a = (a); auto _b = (b); if (! (_a == _b)) { ++fails; std::printf ("FAIL %s:%d  %s == %s\n   got [%s] want [%s]\n", __FILE__, __LINE__, #a, #b, toS (_a).c_str(), toS (_b).c_str()); } } while (0)

static const char* kSong = R"({title: Morning Song}
{key: G}
{tempo: 120}
{time: 4/4}
{arrangement: V1 C V2 C B C C}
{start_of_verse: Verse 1}
{bars: 2}
[G]Light breaks over the [D]hills
[Em]We lift our [C]voices high
{end_of_verse}
{start_of_chorus: Chorus}
{bars: 2}
[G]Rise up, morning is here
[D]Open the door
{end_of_chorus}
{start_of_verse: Verse 2}
{bars: 2}
[Em]Every heart awake again
{end_of_verse}
{start_of_bridge: Bridge}
{bars: 2}
[C]Even when the night is long
{end_of_bridge}
)";
static const char* kSong2 = "{title: Evening Hymn}\n{key: E}\n{capo: 4}\n{tempo: 66}\n{time: 6/8}\n{start_of_verse: Verse 1}\n[E]Quiet now the [B]day is done\n{end_of_verse}\n";

struct Sink : RigSink {
    std::vector<std::string> said; void loadRig (const RigPreset&) override {} void applyScene (const Scene&) override {} void setPedal (const std::string&, bool) override {}
    void setBpm (double) override {} void announceSection (const std::string& n, int) override { said.push_back (n); }
    bool cueDue (int seq) override { return dueSeq >= seq; } int dueSeq = 1 << 30;
};
static Library lib() {
    Library l = seedLibrary(); Song a; a.id = "morning"; a.chart = parseChart (kSong); Song b; b.id = "evening"; b.chart = parseChart (kSong2); l.songs.push_back (a); l.songs.push_back (b);
    Setlist set; set.name = "Sunday"; set.date = "2026-11-08"; SetlistEntry e1; e1.songId = "morning"; SetlistEntry e2; e2.songId = "evening";
    e1.notes = { { "Welcome - pastor speaks", "start" }, { "Hold the bridge for prayer", "B" }, { "Pastor prays after this song", "after" } };
    set.entries = { e1, e2 }; l.setlists = { set }; return l;
}
static std::vector<std::string> names (const Chart& c) { std::vector<std::string> v; for (auto& s : c.sections) v.push_back (s.name); return v; }

static void testArrangementModel() {
    Chart c = parseChart (kSong);
    CHECK_EQ ((int) c.sections.size(), 4);
    CHECK_EQ (formatArrangement (c.arrangement), std::string ("V1 C V2 C B C C"));
    CHECK_EQ (formatArrangement (parseArrangement ("V1, C | V2;C  B")), std::string ("V1 C V2 C B"));
    CHECK_EQ (resolveArrangementToken (c, "C"), 1); CHECK_EQ (resolveArrangementToken (c, "v2"), 2); CHECK_EQ (resolveArrangementToken (c, "Bridge"), 3);
    CHECK_EQ (resolveArrangementToken (c, c.sections[2].label), 2); CHECK_EQ (resolveArrangementToken (c, "V3"), -1); CHECK_EQ (resolveArrangementToken (c, "X"), -1);
    std::vector<std::string> bad; Chart a = arrangedChart (c, parseArrangement ("V1 C V2 C B C C X"), &bad);
    CHECK_EQ ((int) a.sections.size(), 7); CHECK_EQ ((int) bad.size(), 1);
    std::vector<std::string> want = { "Verse 1", "Chorus", "Verse 2", "Chorus", "Bridge", "Chorus", "Chorus" }; CHECK (names (a) == want);
    CHECK_EQ (a.sections[3].occurrence, 2); CHECK_EQ (a.sections[6].occurrence, 4); CHECK_EQ (a.sections[3].source, 1); CHECK_EQ (a.sections[0].occurrence, 1);
    CHECK_EQ ((int) arrangedChart (c, {}).sections.size(), 4);
    // ChordPro + JSON round trip
    Chart back = parseChart (chartToChordPro (c)); CHECK (back.arrangement == c.arrangement); CHECK_EQ ((int) back.sections.size(), 4);
    CHECK (chartFromJson (toJson (c)).arrangement == c.arrangement);
    Library L = lib(); L.setlists[0].entries[0].arrangement = { "V1", "C", "C" };
    Library L2 = libraryFromJson (toJson (L)); const auto& e = L2.setlists[0].entries[0];
    CHECK ((e.arrangement == std::vector<std::string> { "V1", "C", "C" })); CHECK_EQ ((int) e.notes.size(), 3); CHECK_EQ (e.notes[1].at, std::string ("B"));
    // practice track JSON
    L.songs.back().practice = { "/x/evening.m4a", { 0.5, 12.0, -1 } }; Library L3 = libraryFromJson (toJson (L));
    CHECK_EQ (L3.songs.back().practice.file, std::string ("/x/evening.m4a")); CHECK_EQ ((int) L3.songs.back().practice.marks.size(), 3); CHECK_EQ (L3.songs.back().practice.marks[2], -1.0);
}

static void testArrangementLive() {
    Library L = lib(); Sink s; LiveSession live (L, s); L.settings.quantizeSections = true; L.settings.voiceCues = true;
    live.start (L.setlists[0], 0, 0);
    CHECK_EQ ((int) live.chart().sections.size(), 7);
    Action nx; nx.type = Action::NextSection; Action pv; pv.type = Action::PrevSection;
    live.handle (nx, 1); CHECK_EQ (live.chart().sections[(size_t) live.sectionIndex()].name, std::string ("Chorus"));
    live.handle (nx, 2); live.handle (nx, 3); CHECK_EQ (live.sectionIndex(), 3);
    live.handle (pv, 4); CHECK_EQ (live.sectionIndex(), 2);
    // voice cues: 2nd chorus = "Chorus again" (click on -> quantised)
    live.setClickOn (true); s.dueSeq = -1; live.handle (nx, 5); CHECK_EQ (s.said.back(), std::string ("Chorus again")); s.dueSeq = 1 << 30; live.tick (5.1);
    CHECK_EQ (live.sectionIndex(), 3); live.setClickOn (false);
    // auto-advance follows the arrangement: 7 sections x 2 bars x 2 s (120 BPM 4/4) = 28 s
    live.gotoSection (0, 6); live.setPlaying (true, 6); L.settings.countIn = 0;
    live.setPlaying (false, 6); live.setPlaying (true, 6);
    std::vector<std::string> seen; int last = -1;
    for (double t = 6; t < 6 + 40; t += 0.05) { live.tick (t); if (live.sectionIndex() != last) { last = live.sectionIndex(); seen.push_back (live.chart().sections[(size_t) last].name); } }
    std::vector<std::string> want = { "Verse 1", "Chorus", "Verse 2", "Chorus", "Bridge", "Chorus", "Chorus" }; CHECK (seen == want);
    CHECK (live.songEnded()); CHECK (! live.playing());
    // projector slides follow the arranged order
    auto slides = songSlides (live.chart(), L.settings.projector); int lyricSecs = 0, lastSec = -1; for (auto& sl : slides) if (sl.kind == Slide::Lyrics && sl.section != lastSec) { lastSec = sl.section; ++lyricSecs; }
    CHECK_EQ (lyricSecs, 7);
    // per-setlist override + editing the song arrangement
    live.setArrangement ({ "C", "V1", "C" }, true); CHECK_EQ ((int) live.chart().sections.size(), 3); CHECK_EQ (L.setlists[0].entries[0].arrangement.size(), (size_t) 3);
    CHECK_EQ (formatArrangement (L.songs[L.songs.size() - 2].chart.arrangement), std::string ("V1 C V2 C B C C"));   // song untouched
    live.setArrangement ({}, true); CHECK_EQ ((int) live.chart().sections.size(), 7);
    live.setArrangement ({ "V1", "B" }, false); CHECK_EQ ((int) live.chart().sections.size(), 2); CHECK_EQ (formatArrangement (L.song ("morning")->chart.arrangement), std::string ("V1 B"));
}

static void testNextSongAndNotes() {
    Library L = lib(); Sink s; LiveSession live (L, s); L.settings.quantizeSections = false;
    L.setlists[0].entries[1].capoOverride = 2;   // this player's own capo for song 2
    live.start (L.setlists[0], 0, 0);
    auto n = live.nextSongInfo(); CHECK (n.valid); CHECK_EQ (n.title, std::string ("Evening Hymn")); CHECK_EQ (n.key, std::string ("E")); CHECK_EQ (n.capo, 2);
    CHECK_EQ (n.bpm, 66.0); CHECK_EQ (n.time, std::string ("6/8")); CHECK_EQ (n.firstChord, std::string ("F#")); // chart written in capo-4 shapes ([E] sounds G#); this player: capo 2 -> F# shape
    CHECK (! live.showNextSong());
    // notes: start note while the song starts
    auto an = live.activeNotes(); CHECK_EQ ((int) an.size(), 1); if (! an.empty()) CHECK_EQ (an[0].text, std::string ("Welcome - pastor speaks"));
    live.gotoSection (4, 1); an = live.activeNotes(); CHECK_EQ ((int) an.size(), 1); if (! an.empty()) CHECK_EQ (an[0].at, std::string ("B"));
    live.gotoSection (5, 2); CHECK (live.activeNotes().empty()); CHECK (! live.showNextSong());
    live.gotoSection (6, 3); CHECK (live.showNextSong()); CHECK (live.activeNotes().empty());   // last section: card, "after" note not yet
    // song runs off the end -> gap: card + "after" note
    L.settings.countIn = 0; live.setPlaying (true, 10); for (double t = 10; t < 20; t += 0.1) live.tick (t);
    CHECK (live.songEnded()); CHECK (live.showNextSong()); an = live.activeNotes(); CHECK_EQ ((int) an.size(), 1); if (! an.empty()) CHECK_EQ (an[0].at, std::string ("after"));
    Action ns; ns.type = Action::NextSong; live.handle (ns, 21); CHECK (! live.songEnded()); CHECK (! live.nextSongInfo().valid); CHECK (! live.showNextSong());
    // unresolvable token: never shown
    L.setlists[0].entries[0].notes = { { "x", "Coda" } }; live.start (L.setlists[0], 0, 30); for (int i = 0; i < 7; ++i) { live.gotoSection (i, 31 + i); CHECK (live.activeNotes().empty()); }
}

struct Band {
    Library LL = lib(), FL = seedLibrary(); Sink ls, fs; LiveSession lead { LL, ls }, fol { FL, fs }; FakeClock clock;
    Leader leader { "S1" }; std::vector<std::string> toLeader; Follower follower { [this] (const std::string& l) { toLeader.push_back (l); }, "Keys" };
    void flushUp (double now) { auto up = toLeader; toLeader.clear(); for (auto& l : up) { if (l.find ("\"hb\"") != std::string::npos) hbs.push_back (l); auto r = leader.onFollowerLine (l, lead, clock, now); if (! r.empty()) follower.onLine (r, fol, FL, clock, now); } }
    void step (double now) { lead.tick (now); leader.tick (lead, clock, now); for (auto& l : leader.update (lead, clock, now)) follower.onLine (l, fol, FL, clock, now); flushUp (now); follower.tick (fol, clock, now); fol.tick (now); }
    std::vector<std::string> hbs;
};

static void testSync() {
    Band b; b.LL.settings.quantizeSections = false; b.FL.settings.quantizeSections = false;
    b.lead.start (b.LL.setlists[0], 0, 0); b.step (0); b.follower.onConnected(); b.flushUp (0); b.step (0.1);
    CHECK (b.follower.synced()); CHECK_EQ ((int) b.fol.chart().sections.size(), 7);
    b.lead.gotoSection (3, 1); b.step (1.1); CHECK_EQ (b.fol.sectionIndex(), 3); CHECK_EQ (b.fol.chart().sections[3].occurrence, 2);
    CHECK_EQ (b.fol.cueName (3, 1), std::string ("Chorus again"));
    // notes reach the follower
    b.lead.gotoSection (4, 2); b.step (2.1); auto an = b.fol.activeNotes(); CHECK_EQ ((int) an.size(), 1);
    // leader edits the arrangement for this setlist: snapshot, follower's indices follow
    b.lead.setArrangement ({ "V1", "C", "B", "C" }, true); b.step (3.1); CHECK_EQ ((int) b.fol.chart().sections.size(), 4);
    b.lead.gotoSection (2, 4); b.step (4.1); CHECK_EQ (b.fol.chart().sections[(size_t) b.fol.sectionIndex()].name, std::string ("Bridge"));
    // next-song card on the follower in the last section (its own capo)
    b.follower.localKey["evening"] = { "E", 4 };
    b.lead.gotoSection (3, 5); b.step (5.1); CHECK (b.fol.showNextSong());
    // practice file is not sent
    b.LL.song ("morning")->practice.file = "/leader/only.m4a"; auto st = b.leader.capture (b.lead, b.clock, 6); CHECK (st.songs[0].song.practice.file.empty());
    // device health in heartbeats
    b.follower.health = { 14, false, 2 }; b.step (7.5); b.step (9.0);
    CHECK (! b.hbs.empty()); Msg m; CHECK (decode (b.hbs.back(), m)); CHECK_EQ (m.body["bat"].integer(), 14); CHECK_EQ (m.body["therm"].integer(), 2);
    Roster r; r.connected (1, 0); r.line (1, m, "", 0, 0, 120, 1); auto ms = r.members(); CHECK_EQ ((int) ms.size(), 1);
    CHECK_EQ (ms[0].health.batteryPct, 14); CHECK_EQ (ms[0].healthSev, 1); CHECK_EQ (ms[0].warning(), std::string ("Hot - iPad may slow down"));
    auto toasts = r.takeToasts(); bool warned = false; for (auto& t : toasts) if (t.severity == 1 && t.text.find ("Hot") != std::string::npos) warned = true; CHECK (warned);
    r.line (1, m, "", 0, 0, 120, 2); for (auto& t : r.takeToasts()) CHECK (t.text.find ("Hot") == std::string::npos);   // once, not every heartbeat
}

static void testHealth() {
    CHECK_EQ (healthWarning ({ 50, false, 0 }), std::string());
    CHECK_EQ (healthWarning ({ 19, false, 0 }), std::string ("Battery 19% - plug in"));
    CHECK_EQ (healthWarning ({ 19, true, 0 }), std::string());
    CHECK_EQ (healthWarning ({ 20, false, 0 }), std::string());
    CHECK_EQ (healthWarning ({ 80, true, 2 }), std::string ("Hot - iPad may slow down"));
    CHECK_EQ (healthWarning ({ 80, true, 3 }), std::string ("Overheating - iPad may shut down"));
    CHECK_EQ (healthWarning ({ -1, false, 1 }), std::string());
    CHECK_EQ (healthSeverity ({ 5, false, 0 }), 1); CHECK_EQ (healthSeverity ({ 5, false, 3 }), 2); CHECK_EQ (healthSeverity ({ 90, false, 1 }), 0);
    CHECK_EQ (healthText ({ 87, true, 1 }), std::string ("87% charging - warm"));
}

// zero-crossing frequency of a mono signal
static double zcFreq (const std::vector<float>& x, double fs, size_t from, size_t to) {
    int z = 0; double first = -1, last = -1;
    for (size_t i = from + 1; i < to; ++i) if (x[i - 1] < 0 && x[i] >= 0) { double t = (double) i; if (first < 0) first = t; last = t; ++z; }
    return z > 1 ? (z - 1) * fs / (last - first) : 0;
}
static void testPractice() {
    // marks
    std::vector<double> marks = { 0.0, 8.0, -1, 16.0 };
    CHECK_EQ (practiceSectionAt (marks, 0.0), 0); CHECK_EQ (practiceSectionAt (marks, 7.9), 0); CHECK_EQ (practiceSectionAt (marks, 8.0), 1);
    CHECK_EQ (practiceSectionAt (marks, 15.0), 1); CHECK_EQ (practiceSectionAt (marks, 30.0), 3); CHECK_EQ (practiceSectionAt ({ 2.0 }, 1.0), -1);
    Chart a = arrangedChart (parseChart (kSong), parseChart (kSong).arrangement); auto est = estimatePracticeMarks (a, 120, 1.0);
    CHECK_EQ ((int) est.size(), 7); CHECK_EQ (est[0], 1.0); CHECK_EQ (est[1], 5.0); CHECK_EQ (est[6], 25.0);
    // chart following: playhead -> section
    { Library L = lib(); Sink s; LiveSession live (L, s); live.start (L.setlists[0], 0, 0);
      for (double t : { 0.5, 5.5, 13.0, 21.0, 26.0 }) live.practiceSection (practiceSectionAt (est, t), t);
      CHECK_EQ (live.sectionIndex(), 6); CHECK (s.said.empty()); }   // no voice cues while following a recording

    const double fs = 48000; auto t = std::make_shared<PracticeAudio>(); t->sampleRate = 44100;
    const int N = (int) (t->sampleRate * 6); t->left.resize ((size_t) N); t->right.resize ((size_t) N);
    for (int i = 0; i < N; ++i) { t->left[(size_t) i] = 0.5f * (float) std::sin (2 * 3.14159265358979 * 440.0 * i / t->sampleRate); t->right[(size_t) i] = t->left[(size_t) i]; }
    auto render = [&] (PracticePlayer& p, double sec, std::vector<float>& out, double* posAt2 = nullptr) {
        const int blk = 256; out.assign ((size_t) (sec * fs), 0.0f); std::vector<float> r ((size_t) blk);
        for (size_t o = 0; o < out.size(); o += blk) { int n = (int) std::min ((size_t) blk, out.size() - o); std::fill (r.begin(), r.end(), 0.0f); p.process (out.data() + o, r.data(), n);
            if (posAt2 && o <= (size_t) (2 * fs) && o + blk > (size_t) (2 * fs)) *posAt2 = p.position(); } };
    { // speed 1: pitch + timing intact, resampled 44.1 -> 48 kHz
      PracticePlayer p; p.prepare (fs); p.setTrack (t); CHECK (p.hasTrack()); CHECK (std::abs (p.length() - 6.0) < 1e-6); p.play (true);
      std::vector<float> o; double pos2 = 0; render (p, 3.0, o, &pos2);
      double f = zcFreq (o, fs, (size_t) (0.5 * fs), (size_t) (2.5 * fs)); CHECK (std::abs (f - 440.0) < 2.0);
      float peak = 0; for (size_t i = (size_t) (0.2 * fs); i < o.size(); ++i) peak = std::max (peak, std::abs (o[i])); CHECK (std::abs (peak - 0.5f) < 0.03f);
      CHECK (std::abs (pos2 - 2.0) < 0.05); std::printf ("   speed 1.0: %.2f Hz, peak %.3f, position at 2 s = %.3f\n", f, peak, pos2); }
    { // speed 0.75: pitch kept, material advances at 0.75 s per second
      PracticePlayer p; p.prepare (fs); p.setTrack (t); p.setSpeed (0.75); p.play (true);
      std::vector<float> o; double pos2 = 0; render (p, 3.0, o, &pos2);
      double f = zcFreq (o, fs, (size_t) (0.5 * fs), (size_t) (2.5 * fs)); CHECK (std::abs (f - 440.0) < 440.0 * 0.02);
      CHECK (std::abs (pos2 - 1.5) < 0.08); std::printf ("   speed 0.75: %.2f Hz, position at 2 s = %.3f\n", f, pos2);
      float rmsMin = 1; for (size_t w = (size_t) (0.5 * fs); w + 2400 < o.size(); w += 2400) { double e = 0; for (size_t i = w; i < w + 2400; ++i) e += o[i] * o[i]; rmsMin = std::min (rmsMin, (float) std::sqrt (e / 2400)); }
      CHECK (rmsMin > 0.25f); }   // no dropouts (sine RMS 0.354)
    { // speed 1.25
      PracticePlayer p; p.prepare (fs); p.setTrack (t); p.setSpeed (1.25); p.play (true); std::vector<float> o; double pos2 = 0; render (p, 3.0, o, &pos2);
      CHECK (std::abs (zcFreq (o, fs, (size_t) (0.5 * fs), (size_t) (2.5 * fs)) - 440.0) < 440.0 * 0.02); CHECK (std::abs (pos2 - 2.5) < 0.08); }
    { // loop a section + seek + stop at the end
      PracticePlayer p; p.prepare (fs); p.setTrack (t); p.setLoop (1.0, 2.0); p.seek (1.0); p.play (true); double mn = 99, mx = 0; std::vector<float> l (256), r (256);
      for (int k = 0; k < (int) (4 * fs / 256); ++k) { p.process (l.data(), r.data(), 256); if (k > 4) { mn = std::min (mn, p.position()); mx = std::max (mx, p.position()); } }
      CHECK (mn >= 0.98 && mx <= 2.06); CHECK (p.looping()); std::printf ("   loop 1-2 s: position stayed in [%.3f, %.3f]\n", mn, mx);
      p.clearLoop(); p.seek (5.5); for (int k = 0; k < (int) (1.5 * fs / 256); ++k) p.process (l.data(), r.data(), 256);
      CHECK (! p.playing()); CHECK (std::abs (p.position() - 6.0) < 0.01); }
    { // stopped / no track = silent and harmless
      PracticePlayer p; p.prepare (fs); std::vector<float> l (256, 0.0f); p.process (l.data(), nullptr, 256); CHECK (l[10] == 0.0f); p.play (true); CHECK (! p.playing()); }
}

int main() {
    setvbuf (stdout, nullptr, _IONBF, 0);
    testArrangementModel(); testArrangementLive(); testNextSongAndNotes(); testSync(); testHealth(); testPractice();
    std::printf ("%d checks, %d failures\n", checks, fails); return fails ? 1 : 0;
}
