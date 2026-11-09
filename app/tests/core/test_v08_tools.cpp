// v0.8: setlist overrides over sync, auto-scroll, footswitch banks, capo helper, song stats, backup bundle, stage mode, recorder.
#include "sync/Sync.h"
#include "model/StageTools.h"
#include "audio/Recorder.h"
#include "midi/MidiRouter.h"
#include <cstdio>
#include <cmath>
using namespace wr; using namespace wr::sync;
static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (! (c)) { ++fails; std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_EQ(a, b) do { ++checks; auto _a = (a); auto _b = (b); if (! (_a == _b)) { ++fails; std::printf ("FAIL %s:%d  %s == %s\n", __FILE__, __LINE__, #a, #b); } } while (0)
using S = std::string;
static const char* kSong = "{title: Morning}\n{key: A}\n{tempo: 120}\n{start_of_verse: Verse 1}\n{bars: 4}\n[A]One [D]two [E]three [F#m]four\n{end_of_verse}\n{start_of_chorus: Chorus}\n{bars: 4}\n[D]Rise [A]up [E]now\n{end_of_chorus}\n";
struct Sink : RigSink { void loadRig (const RigPreset&) override {} void applyScene (const Scene&) override {} void setPedal (const S&, bool) override {}
    void setBpm (double) override {} void announceSection (const S&, int) override {} bool cueDue (int) override { return false; } };
static Library lib() { Library l = seedLibrary(); Song a; a.id = "m"; a.chart = parseChart (kSong); l.songs.push_back (a); Song b; b.id = "n"; b.chart = parseChart ("{title: Night}\n{key: E}\n[E]x\n"); l.songs.push_back (b);
    Setlist set; set.name = "Sun"; set.date = "2026-11-08"; SetlistEntry e; e.songId = "m"; e.keyOverride = "Bb"; e.bpmOverride = 96; SetlistEntry e2; e2.songId = "n"; set.entries = { e, e2 }; l.setlists = { set }; return l; }

static void testSetlistSync() {
    Library LL = lib(), FL = seedLibrary(); Sink a, b; LiveSession lead (LL, a), fol (FL, b); FakeClock clock; Leader leader ("S"); std::vector<S> up;
    Follower f ([&] (const S& l) { up.push_back (l); }, "F");
    auto step = [&] (double t) { for (auto& l : leader.update (lead, clock, t)) f.onLine (l, fol, FL, clock, t);
        auto u = up; up.clear(); for (auto& l : u) { auto r = leader.onFollowerLine (l, lead, clock, t); if (! r.empty()) f.onLine (r, fol, FL, clock, t); } };
    lead.start (LL.setlists[0], 0, 0); step (0); f.onConnected(); step (0.1);
    CHECK_EQ ((int) fol.setlist().entries.size(), 2); CHECK_EQ (fol.setlist().entries[0].songId, S ("m")); CHECK_EQ (fol.key(), S ("Bb")); CHECK (std::abs (fol.bpm() - 96) < 0.01);
    // reorder + new override on the leader: followers get the new order
    auto set = LL.setlists[0]; std::swap (set.entries[0], set.entries[1]); set.entries[1].keyOverride = "C"; LL.setlists[0] = set; lead.refreshSetlist (set); step (1);
    CHECK_EQ (fol.setlist().entries[0].songId, S ("n")); CHECK_EQ (fol.setlist().entries[1].keyOverride, S ("C"));
}
static void testAutoScroll() {
    CHECK_EQ (sectionProgress (0, 4, 4), 0.0); CHECK_EQ (sectionProgress (8, 4, 4), 0.5); CHECK_EQ (sectionProgress (20, 4, 4), 1.0); CHECK_EQ (sectionProgress (3, 4, 0), 0.0);
    CHECK_EQ (sectionProgress (8, 4, 4, 2), 0.25);
    CHECK_EQ (autoScrollOffset (500, 600, 0, 400, 3000), 460.0f);                 // section start: header in view
    float mid = autoScrollOffset (500, 600, 0.5, 400, 3000); CHECK (std::abs (mid - (800 - 400 * 0.33f)) < 0.01f);
    CHECK (autoScrollOffset (500, 600, 0.9, 400, 3000) > mid);                    // keeps moving with the bar
    CHECK_EQ (autoScrollOffset (2900, 100, 1.0, 400, 3000), 2600.0f);             // clamped to the end
    CHECK_EQ (autoScrollOffset (0, 100, 0.0, 400, 3000), 0.0f);
    Library L = lib(); Sink s; LiveSession live (L, s); L.settings.countIn = 0; live.start (L.setlists[0], 0, 0); live.setPlaying (true, 0);
    for (double t = 0; t < 2.05; t += 0.05) live.tick (t);   // 96 BPM -> ~3.2 beats
    const double p = sectionProgress (live.beatInSection(), live.beatsPerBar(), live.chart().sections[0].bars); CHECK (p > 0.15 && p < 0.25);
}
static void testBanks() {
    ControllerProfile g = defaultGuitarUnit(); enableDefaultBanks (g); CHECK_EQ ((int) g.banks.size(), 2); CHECK_EQ (g.banks[0].action[0], S ("tap")); CHECK_EQ (g.banks[0].longAction[1], S ("bankNext"));
    ControllerProfile sct = defaultSectionUnit(); enableDefaultBanks (sct); CHECK_EQ (sct.banks[1].action[1], S ("nextSong")); CHECK_EQ (sct.banks[0].longAction[2], S ("bankNext"));
    enableDefaultBanks (sct); CHECK_EQ ((int) sct.banks.size(), 2);   // idempotent
    // JSON (saved per user / device)
    sct.bank = 1; auto back = controllerFromJson (toJson (sct)); CHECK_EQ ((int) back.banks.size(), 2); CHECK_EQ (back.bank, 1); CHECK_EQ (back.banks[1].name, S ("Songs"));
    CHECK (controllerFromJson (toJson (defaultSectionUnit())).banks.empty());
    // router: bank 1 -> press B = nextSection; long-press C -> bank 2; press B = nextSong; bankNext wraps
    sct.bank = 0; MidiRouter r; r.setProfiles ({ sct }); Action out[8];
    const uint8_t cc = 0xB0 | 1;   // channel 2
    auto press = [&] (int sw, double t, double hold) { r.handle (cc, (uint8_t) (80 + sw), 127, t); r.tick (t + hold); r.handle (cc, (uint8_t) (80 + sw), 0, t + hold); r.tick (t + hold + 1); };
    press (1, 0, 10); int n = r.pop (out, 8); CHECK_EQ (n, 1); if (n) CHECK (out[0].type == Action::NextSection);
    press (2, 100, 800); n = r.pop (out, 8); bool changed = false; for (int i = 0; i < n; ++i) if (out[i].type == Action::BankChanged && out[i].value == 1) changed = true; CHECK (changed); CHECK_EQ (r.bank (0), 1);
    press (1, 2000, 10); n = r.pop (out, 8); CHECK_EQ (n, 1); if (n) CHECK (out[0].type == Action::NextSong);
    press (2, 3000, 800); n = r.pop (out, 8); CHECK_EQ (r.bank (0), 0);   // bank 2's C long = bankNext -> wraps to bank 1
    // no banks: legacy mapping, bankNext ignored
    MidiRouter r2; ControllerProfile p = defaultSectionUnit(); p.sw[0].action = "bankNext"; r2.setProfiles ({ p }); r2.handle (cc, 80, 127, 0); r2.handle (cc, 80, 0, 5); r2.tick (10); CHECK_EQ (r2.pop (out, 8), 0);
    CHECK (actionFromName ("bankPrev") == Action::BankPrev); CHECK_EQ (S (actionName (Action::BankChanged)), S ("bankChanged"));
}
static void testCapo() {
    Chart c = parseChart (kSong); auto h = capoHelper (c, "Bb"); CHECK_EQ ((int) h.size(), 8);
    CHECK (! h.empty() && h[0].capo == 3);   // G shapes (G C D Em: all open) beat capo 1 A shapes (F#m is a barre)
    for (auto& x : h) if (x.capo == 1) { CHECK_EQ (x.shapesKey, S ("A")); CHECK_EQ (x.openShapes, 3); CHECK_EQ (x.shapes[0], S ("A")); CHECK (shapesText (x).rfind ("Capo 1 - A shapes: A D E", 0) == 0); }
    bool sawG = false; for (auto& x : h) if (x.capo == 3) { sawG = true; CHECK_EQ (x.shapesKey, S ("G")); CHECK_EQ (x.shapes[0], S ("G")); CHECK_EQ (x.openShapes, 4); } CHECK (sawG);
    for (auto& x : h) if (x.capo == 0) CHECK_EQ (x.shapes[0], S ("Bb"));
    CHECK (capoHelper (c, "H").empty());
}
static void testStats() {
    Library L = lib(); CHECK_EQ (daysFromCivil ("1970-01-01"), 0); CHECK_EQ (daysFromCivil ("2026-11-08") - daysFromCivil ("2026-10-25"), 14); CHECK_EQ (daysFromCivil ("2024-03-01") - daysFromCivil ("2024-02-28"), 2);
    CHECK (daysFromCivil ("bad") < -100000);
    CHECK (logPlay (L, "m", "2026-10-25")); CHECK (! logPlay (L, "m", "2026-10-25")); CHECK (logPlay (L, "m", "2026-09-06")); CHECK (logPlay (L, "n", "2026-01-04")); CHECK (! logPlay (L, "m", "nope"));
    auto h = playHistory (L, "m"); CHECK_EQ ((int) h.size(), 2); CHECK_EQ (h[0], S ("2026-10-25"));
    CHECK_EQ (daysSinceLastPlayed (L, "m", "2026-11-08"), 14); CHECK_EQ (daysSinceLastPlayed (L, "x", "2026-11-08"), -1);
    CHECK (playedRecently (L, "m", "2026-11-08")); CHECK (! playedRecently (L, "n", "2026-11-08")); CHECK_EQ (recentText (L, "m", "2026-11-08"), S ("Played 2 weeks ago"));
    L.settings.repeatWarnDays = 7; CHECK (! playedRecently (L, "m", "2026-11-08")); L.settings.repeatWarnDays = 21;
    logPlay (L, "m", "2026-11-08"); CHECK_EQ (daysSinceLastPlayed (L, "m", "2026-11-08"), 14);   // today doesn't count as "recent repeat"
    CHECK_EQ (playsWithin (L, "m", "2026-11-08", 30), 2); auto st = songStats (L); CHECK_EQ (st[0].songId, S ("m")); CHECK_EQ (st[0].plays, 3);
    auto L2 = libraryFromJson (toJson (L)); CHECK_EQ ((int) L2.playLog.size(), 4); CHECK_EQ (L2.settings.repeatWarnDays, 21);
}
static void testBundle() {
    Library L = lib(); L.settings.stageMode = true; L.settings.autoScroll = false; L.settings.sectionColors["verse"] = "teal"; logPlay (L, "m", "2026-10-25"); enableDefaultBanks (L.controllers[0]);
    auto txt = exportBundle (L, "Tabernacle Tools - Electric Guitar", 1790000000);
    CHECK (txt.find ("\"format\":\"worship-rig-backup\"") != S::npos || txt.find ("worship-rig-backup") != S::npos);
    Library R = seedLibrary(); auto rep = importBundle (txt, R, ImportMode::Replace); CHECK (rep.ok); CHECK_EQ (rep.songs, (int) L.songs.size());
    CHECK (! R.settings.stageMode); CHECK (! R.settings.autoScroll); CHECK_EQ (R.settings.sectionColors["verse"], S ("teal")); CHECK_EQ ((int) R.playLog.size(), 1); CHECK_EQ ((int) R.controllers[0].banks.size(), 2);
    CHECK_EQ (R.setlists[0].entries[0].keyOverride, S ("Bb"));
    // merge keeps local, adds missing, skips existing
    Library M = seedLibrary(); M.settings.stageMode = false; Song mine; mine.id = "m"; mine.chart.title = "Mine"; M.songs.push_back (mine);
    auto rm = importBundle (txt, M, ImportMode::Merge); CHECK (rm.ok); CHECK_EQ (M.song ("m")->chart.title, S ("Mine")); CHECK (M.song ("n") != nullptr); CHECK (rm.skipped >= 1); CHECK (! M.settings.stageMode);
    CHECK (M.setlist ("2026-11-08") != nullptr);
    // bare library.json + junk
    Library B = seedLibrary(); CHECK (importBundle (toJson (L).dump (0), B, ImportMode::Replace).ok);
    Library J = seedLibrary(); auto bad = importBundle ("{\"hello\":1}", J, ImportMode::Replace); CHECK (! bad.ok); CHECK (! bad.error.empty()); CHECK (! importBundle ("not json", J, ImportMode::Merge).ok);
    CHECK (! importBundle ("{\"format\":\"worship-rig-backup\",\"bundleVersion\":9,\"library\":{}}", J, ImportMode::Merge).ok);
    CHECK_EQ ((int) J.songs.size(), (int) seedLibrary().songs.size());   // failed import leaves the library alone
    CHECK_EQ (stageTextScale (L.settings), 1.0f); CHECK_EQ (stageTextScale (AppSettings()), 1.0f);
}
static unsigned rd (FILE* f, int n) { unsigned v = 0; for (int i = 0; i < n; ++i) v |= (unsigned) std::fgetc (f) << (8 * i); return v; }
static void testRecorder() {
    const S path = "/tmp/wr_v08_take.wav"; RehearsalRecorder r (4096);
    CHECK (r.start (path, 48000, 2, 24)); CHECK (r.recording());
    std::vector<float> a (480), c (480); for (int i = 0; i < 480; ++i) { a[(size_t) i] = 0.5f * std::sin (i * 0.1f); c[(size_t) i] = i % 120 == 0 ? 0.9f : 0.0f; }
    for (int k = 0; k < 100; ++k) { r.push (a.data(), c.data(), 480); if (k % 4 == 3) r.drain(); }   // 1 s
    r.stop(); CHECK (! r.recording()); CHECK_EQ (r.framesWritten(), 48000LL); CHECK_EQ (r.dropped(), 0LL); CHECK (std::abs (r.seconds() - 1.0) < 1e-9); CHECK (r.peak() > 0.49f);
    FILE* f = std::fopen (path.c_str(), "rb"); CHECK (f != nullptr); if (f) { char id[5] = {}; std::fread (id, 1, 4, f); CHECK_EQ (S (id), S ("RIFF")); unsigned riff = rd (f, 4);
        std::fseek (f, 22, SEEK_SET); CHECK_EQ (rd (f, 2), 2u); CHECK_EQ (rd (f, 4), 48000u); std::fseek (f, 34, SEEK_SET); CHECK_EQ (rd (f, 2), 24u);
        std::fseek (f, 40, SEEK_SET); unsigned data = rd (f, 4); CHECK_EQ (data, 48000u * 6); CHECK_EQ (riff, 36 + data);
        int s0 = (int) rd (f, 3); int c0 = (int) rd (f, 3); CHECK_EQ (s0, 0); CHECK (c0 > 7000000);   // frame 0: input 0, click 0.9
        std::fseek (f, 0, SEEK_END); CHECK_EQ ((long) std::ftell (f), (long) (44 + data)); std::fclose (f); }
    // overflow: push without draining -> drops counted, no crash; mono 16-bit
    RehearsalRecorder m (1024); CHECK (m.start ("/tmp/wr_v08_mono.wav", 44100, 1, 16)); for (int k = 0; k < 10; ++k) m.push (a.data(), nullptr, 480); CHECK (m.dropped() > 0); m.stop();
    CHECK_EQ (m.framesWritten(), 1024LL); r.push (a.data(), nullptr, 480); CHECK_EQ (r.framesWritten(), 48000LL);   // not recording: ignored
    CHECK (! RehearsalRecorder().start ("/nonexistent/dir/x.wav", 48000, 1));
}
int main() { testSetlistSync(); testAutoScroll(); testBanks(); testCapo(); testStats(); testBundle(); testRecorder(); std::printf ("%d checks, %d failures\n", checks, fails); return fails ? 1 : 0; }
