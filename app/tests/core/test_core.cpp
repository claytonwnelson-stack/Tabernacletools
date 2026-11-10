// worship-core unit tests (Linux/macOS, no JUCE, no guitar DSP): chart parser, transpose, tabs, JSON, MIDI router, live session, click/cues.
#include "model/Backup.h"
#include "audio/Voice.h"
#include "audio/Click.h"
#include "audio/PitchTracker.h"
#include "model/Export.h"
#include <filesystem>
#include "model/Chart.h"
#include "model/LiveSession.h"
#include <cstdio>
#include <fstream>
#include <sstream>
using namespace wr;

static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (! (c)) { ++fails; std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_EQ(a, b) do { ++checks; auto _a = (a); auto _b = (b); if (! (_a == _b)) { ++fails; std::printf ("FAIL %s:%d  %s == %s\n   got [%s] want [%s]\n", __FILE__, __LINE__, #a, #b, toS (_a).c_str(), toS (_b).c_str()); } } while (0)
static std::string toS (const std::string& s) { return s; } static std::string toS (const char* s) { return s; } template <class T> static std::string toS (T v) { return std::to_string (v); }

static void testChords() {
    CHECK (isChordToken ("A")); CHECK (isChordToken ("Asus2")); CHECK (isChordToken ("F#m7")); CHECK (isChordToken ("A/C#")); CHECK (isChordToken ("Bbmaj7")); CHECK (isChordToken ("Dsus4"));
    CHECK (isChordToken ("C#m7b5")); CHECK (isChordToken ("Gadd9")); CHECK (! isChordToken ("He")); CHECK (! isChordToken ("Every")); CHECK (! isChordToken ("Amazing")); CHECK (! isChordToken ("Verse"));
    CHECK_EQ (transposeChord ("A", 2, "B"), "B"); CHECK_EQ (transposeChord ("A/C#", 2, "B"), "B/D#"); CHECK_EQ (transposeChord ("F#m7", 1, "Bb"), "Gm7");
    CHECK_EQ (transposeChord ("A/C#", 1, "Bb"), "Bb/D"); CHECK_EQ (transposeChord ("Dsus2", -2, "G"), "Csus2"); CHECK_EQ (transposeChord ("E", -4, "F"), "C");
    CHECK_EQ (transposeChord ("F#m", -4, "F"), "Dm"); CHECK_EQ (transposeChord ("C#", 3, "E"), "E"); CHECK_EQ (transposeChord ("Bm7", 4, "Db"), "Ebm7");
    CHECK_EQ (transposeChord ("A", 3, "C"), "C"); CHECK_EQ (transposeChord ("E", 2, "B"), "F#"); CHECK_EQ (transposeChord ("E", 1, "Bb"), "F"); CHECK_EQ (transposeChord ("B", 1, "Bb"), "C");
    CHECK_EQ (transposeKey ("A", 1), "Bb"); CHECK_EQ (transposeKey ("A", -3), "F#"); CHECK_EQ (transposeKey ("F#m", 1), "Gm");
    CHECK_EQ (semitonesBetween ("A", "G"), -2); CHECK_EQ (semitonesBetween ("A", "D"), 5); CHECK_EQ (semitonesBetween ("A", "E"), -5); CHECK_EQ (semitonesBetween ("G", "C#"), 6);
    CHECK (keyPrefersFlats ("F")); CHECK (keyPrefersFlats ("Eb")); CHECK (keyPrefersFlats ("Dm")); CHECK (! keyPrefersFlats ("D")); CHECK (! keyPrefersFlats ("F#")); CHECK (! keyPrefersFlats ("Em"));
}

static TabBlock orange() { TabBlock t; t.title = "Orange"; t.strings = { "e|-------------------|", "B|--------------5----|", "G|--6--4--6--7-----4-|", "D|-------------------|", "A|-------------------|", "E|-------------------|" }; return t; }
static void testTabs() {
    auto r = transposeTab (orange(), -2); // A -> G
    CHECK (r.playable); CHECK_EQ (r.fretShift, -2);
    CHECK_EQ (r.tab.strings[2], std::string ("G|--4--2--4--5-----2-|")); CHECK_EQ (r.tab.strings[1], std::string ("B|--------------3----|"));
    auto r2 = transposeTab (orange(), -5); // A -> E: 4-5 = -1 is off the neck -> octave up (+7)
    CHECK (r2.playable); CHECK_EQ (r2.fretShift, 7); CHECK_EQ (r2.tab.strings[2], std::string ("G|--13-11-13-14----11|")); CHECK (! r2.advice.empty());
    CHECK_EQ (r2.tab.strings[0].size(), r2.tab.strings[2].size()); // columns stay aligned
    auto r3 = transposeTab (orange(), 3); CHECK_EQ (r3.tab.strings[2], std::string ("G|--9--7--9--10----7-|")); CHECK_EQ (r3.tab.strings[1], std::string ("B|--------------8----|"));
    CHECK_EQ (r3.tab.strings[1].size(), r3.tab.strings[2].size());
    TabBlock wide; wide.strings = { "e|--0--22--|", "B|--------|" }; auto r4 = transposeTab (wide, 1, 22); // 0..22 can't move +1/-11/+13 -> capo advice
    CHECK (! r4.playable); CHECK_EQ (r4.capo, 1); CHECK (r4.advice.find ("capo 1") != std::string::npos);
}

static std::string readFile (const std::string& p) { std::ifstream f (p); std::stringstream ss; ss << f.rdbuf(); return ss.str(); }

static void testParser() {
    Chart c = parseChart (readFile (std::string (WR_SOURCE_DIR) + "/resources/he-who-was.cho"));
    CHECK_EQ (c.title, "He who was / He who is"); CHECK_EQ (c.key, "A"); CHECK_EQ ((int) c.bpm, 73);
    CHECK_EQ ((int) c.sections.size(), 10);
    std::string labels; for (auto& s : c.sections) labels += s.label + " ";
    CHECK_EQ (labels, "V1 V2 PC CH V3 CH V4 V5 CH CH ");
    CHECK_EQ (c.sections[4].repeat, 2);
    // verse 1 line 1: "There is a [A]day [Asus2]coming"
    auto& l0 = c.sections[0].items[0].line; CHECK_EQ (l0.lyric, "There is a day coming"); CHECK_EQ ((int) l0.chords.size(), 2); CHECK_EQ (l0.chords[0].pos, 11); CHECK_EQ (l0.chords[1].chord, "Asus2"); CHECK_EQ (l0.highlight, "orange");
    int tabs = 0; std::string colors; for (auto& s : c.sections) for (auto& it : s.items) if (it.kind == ChartItem::Tab) { ++tabs; colors += it.tab.color + " "; CHECK_EQ ((int) it.tab.strings.size(), 6); }
    CHECK_EQ (tabs, 2); CHECK_EQ (colors, "orange pink ");
    CHECK_EQ (c.sections[2].items[0].line.highlight, "pink");

    // chords-over-lyrics text (what you get from a text PDF / copy-paste)
    const char* plain =
        "Amazing Grace\n\n[Verse 1]\n        G           G7        C       G\nAmazing grace how sweet the sound\n            G          Em       D\nThat saved a wretch like me\n\n"
        "Chorus: x2\nC   G\nMy chains are gone\n\ne|--3--5--|\nB|-------|\nG|-------|\nD|-------|\nA|-------|\nE|-------|\n";
    Chart p = parseChart (plain, "Amazing Grace");
    CHECK_EQ ((int) p.sections.size(), 2); CHECK_EQ (p.sections[0].label, "V1"); CHECK_EQ (p.sections[1].kind, "chorus"); CHECK_EQ (p.sections[1].repeat, 2);
    auto& al = p.sections[0].items[0].line; CHECK_EQ (al.lyric, "Amazing grace how sweet the sound"); CHECK_EQ ((int) al.chords.size(), 4); if (al.chords.size() == 4) { CHECK_EQ (al.chords[0].pos, 8); CHECK_EQ (al.chords[1].chord, "G7"); }
    CHECK_EQ (p.key, "G"); // guessed
    CHECK (p.sections[1].items.back().kind == ChartItem::Tab);

    // whole-chart transpose A -> Bb (flat spelling) and per-tab shift
    Chart t = transposeChart (c, "Bb"); CHECK_EQ (t.key, "Bb");
    CHECK_EQ (t.sections[3].items[6].line.chords[0].chord, "Bb/D"); CHECK_EQ (t.sections[0].items[2].line.chords[0].chord, "Gm7");
    Chart tg = transposeChart (c, "G"); CHECK_EQ (tg.sections[3].items[6].line.chords[0].chord, "G/B");
}

static void testJson() {
    Library l = seedLibrary(); Json j = toJson (l); std::string s = j.dump(); bool ok = false; Json k = Json::parse (s, &ok); CHECK (ok);
    Library m = libraryFromJson (k); CHECK_EQ (toJson (m).dump(), s); // round-trip is lossless
    CHECK_EQ (m.songs[0].chart.title, "He who was / He who is"); CHECK_EQ (m.controllers[1].channel, 2); CHECK_EQ (m.controllers[0].sw[1].action, "boost");
    Json u = Json::parse ("{\"a\":\"\\u00e9\\n\",\"b\":[1,2.5,true,null]}", &ok); CHECK (ok); CHECK_EQ (u["a"].str(), "\xC3\xA9\n"); CHECK_EQ (u["b"][(size_t) 1].num(), 2.5);
    Json::parse ("{bad", &ok); CHECK (! ok);
}

static void testMidi() {
    MidiRouter r; r.setProfiles ({ defaultGuitarUnit(), defaultSectionUnit() }); r.setExternal (16, 36, true);
    Action a[16]; int n;
    // section unit (ch 2 => status 0xB1): B = CC81 next section, fires on press
    r.handle (0xB1, 81, 127, 0); n = r.pop (a, 16); CHECK_EQ (n, 1); CHECK (a[0].type == Action::NextSection);
    r.handle (0xB1, 81, 0, 50); CHECK_EQ (r.pop (a, 16), 0);
    // D short = next song (on release), D long = prev song (while held)
    r.handle (0xB1, 83, 127, 100); r.tick (200); CHECK_EQ (r.pop (a, 16), 0); r.handle (0xB1, 83, 0, 250); n = r.pop (a, 16); CHECK_EQ (n, 1); CHECK (a[0].type == Action::NextSong);
    r.handle (0xB1, 83, 127, 1000); r.tick (1400); CHECK_EQ (r.pop (a, 16), 0); r.tick (1510); n = r.pop (a, 16); CHECK_EQ (n, 1); CHECK (a[0].type == Action::PrevSong);
    r.handle (0xB1, 83, 0, 1700); CHECK_EQ (r.pop (a, 16), 0);
    // C = jump to chorus; guitar unit ch1: A tap, B boost
    r.handle (0xB1, 82, 127, 2000); n = r.pop (a, 16); CHECK (n == 1 && a[0].type == Action::JumpChorus);
    r.handle (0xB0, 80, 127, 2100); n = r.pop (a, 16); CHECK (n == 1 && a[0].type == Action::Tap);
    r.handle (0xB0, 81, 127, 2200); n = r.pop (a, 16); CHECK (n == 1 && a[0].type == Action::Boost);
    r.tick (3101); n = r.pop (a, 16); CHECK (n == 1 && a[0].type == Action::TunerToggle); // the tap above was still held
    r.handle (0xB0, 80, 0, 3200); CHECK_EQ (r.pop (a, 16), 0);
    // section A: tap starts, hold 0.7 s stops, and the release after a hold does not also start
    r.handle (0xB1, 80, 127, 6000); r.tick (6200); CHECK_EQ (r.pop (a, 16), 0); r.handle (0xB1, 80, 0, 6400); n = r.pop (a, 16); CHECK (n == 1 && a[0].type == Action::PlayStart);
    r.handle (0xB1, 80, 127, 7000); r.tick (7690); CHECK_EQ (r.pop (a, 16), 0); r.tick (7700); n = r.pop (a, 16); CHECK (n == 1 && a[0].type == Action::TransportStop);
    r.handle (0xB1, 80, 0, 8000); CHECK_EQ (r.pop (a, 16), 0);
    // Chocolate Manufacturer Control (CC 20-23) hits the same switches as CC 80-83
    r.handle (0xB0, 20, 127, 9000); n = r.pop (a, 16); CHECK (n == 1 && a[0].type == Action::Tap);
    { ControllerProfile old = defaultSectionUnit(); old.sw[0].action = "prevSection"; old.sw[0].longAction.clear(); old.sw[0].longMs = 0;
      ControllerProfile::Bank bk; bk.action[0] = "prevSection"; old.banks = { bk };
      auto up = controllerFromJson (toJson (old)); CHECK_EQ (up.sw[0].action, std::string ("playStart")); CHECK_EQ (up.sw[0].longAction, std::string ("stop")); CHECK_EQ (up.sw[0].longMs, 700);
      CHECK_EQ (up.banks[0].action[0], std::string ("playStart")); CHECK_EQ (up.banks[0].longAction[0], std::string ("stop")); }
    // same CC on the wrong channel does nothing
    r.handle (0xB5, 81, 127, 2300); CHECK_EQ (r.pop (a, 16), 0);
    // MIDI learn: spare unit sends PC 5 on ch 3 for guitar switch C
    r.beginLearn (0, 2, false); r.handle (0xC2, 5, 0, 3000); n = r.pop (a, 16); CHECK (n == 1 && a[0].type == Action::Learned);
    CHECK_EQ (r.profile (0).channel, 3); CHECK (r.profile (0).sw[2].press.kind == MidiBinding::PC);
    r.handle (0xC2, 5, 0, 3100); n = r.pop (a, 16); CHECK (n == 1 && a[0].type == Action::SwitchC);
    // external: Cubase PC 3 on ch 16 -> goto section 3; note 38 -> section 2
    r.handle (0xCF, 3, 0, 4000); n = r.pop (a, 16); CHECK (n == 1 && a[0].type == Action::GotoSection && a[0].value == 3);
    r.handle (0x9F, 38, 100, 4100); n = r.pop (a, 16); CHECK (n == 1 && a[0].type == Action::GotoSection && a[0].value == 2);
    // MIDI clock at 72 bpm: 24 ticks per beat
    double tick = 60000.0 / 72.0 / 24.0, t = 5000; for (int i = 0; i < 24 * 3 + 1; ++i, t += tick) r.handle (0xF8, 0, 0, t);
    n = r.pop (a, 16); CHECK (n >= 1 && a[n - 1].type == Action::ClockBpm); CHECK (std::abs (r.clockBpm() - 72.0) < 0.2);
    // tap tempo
    TapTempo tt; tt.tap (0); double b = 0; for (int i = 1; i < 5; ++i) b = tt.tap (i * 0.5); CHECK (std::abs (b - 120) < 0.01);
    // two factory Chocolates: same PC numbers, told apart by USB port. E = A+B, F = C+D, within 50 ms.
    { MidiRouter f; f.setProfiles ({ defaultGuitarUnit(), defaultSectionUnit() }); Action fa[8];
      f.handle (0xC0, 0, 0, 0, 0); f.flushFactory (60); int n = f.pop (fa, 8); CHECK (n == 1 && fa[0].type == Action::Tap);
      f.handle (0xC0, 0, 0, 0, 1); f.flushFactory (60); n = f.pop (fa, 8); CHECK (n == 1 && fa[0].type == Action::PlayStart);
      f.handle (0xC0, 0, 0, 1000, 0); f.handle (0xC0, 1, 0, 1020, 0); f.flushFactory (1100); n = f.pop (fa, 8); CHECK (n == 1 && fa[0].type == Action::TunerToggle);
      f.handle (0xC0, 2, 0, 2000, 1); f.handle (0xC0, 3, 0, 2010, 1); f.flushFactory (2100); n = f.pop (fa, 8); CHECK (n == 1 && fa[0].type == Action::TransportStop);
      f.handle (0xC0, 4, 0, 3000, 0); f.flushFactory (3060); n = f.pop (fa, 8); CHECK (n == 1 && fa[0].type == Action::Tap); // PC 4 is still switch A
      f.handle (0xC0, 0, 0, 4000); f.flushFactory (4100); CHECK_EQ (f.pop (fa, 8), 0); // merged buffer: no factory map
      f.handle (0xB5, 80, 127, 5000, 0); n = f.pop (fa, 8); CHECK (n == 1 && fa[0].type == Action::Tap); // port known: channel ignored
      f.handle (0xB0, 80, 0, 5100, 0); CHECK_EQ (f.pop (fa, 8), 0);
      f.handle (0xB0, 20, 127, 5200, 1); f.handle (0xB0, 20, 0, 5300, 1); n = f.pop (fa, 8); CHECK (n == 1 && fa[0].type == Action::PlayStart); // CC 20 on the section port
      bool jok = false;
      auto bare = controllerFromJson (Json::parse ("{\"role\":\"section\",\"switches\":[]}", &jok)); CHECK (jok); CHECK_EQ (bare.comboE, std::string ("prevSection")); CHECK_EQ (bare.comboF, std::string ("stop"));
      auto cleared = controllerFromJson (Json::parse ("{\"role\":\"guitar\",\"comboE\":\"\",\"comboF\":\"tap\",\"switches\":[]}", &jok)); CHECK (cleared.comboE.empty()); CHECK_EQ (cleared.comboF, std::string ("tap"));
      CHECK_EQ (defaultGuitarUnit().comboE, std::string ("tunerToggle")); CHECK_EQ (defaultSectionUnit().comboF, std::string ("stop"));
      auto saved = libraryFromJson (toJson (seedLibrary())); CHECK (saved.settings.guitarPedalId.empty()); CHECK_EQ (saved.controllers[0].comboF, std::string ("clickMute")); }
}

struct MockSink : RigSink {
    std::vector<std::string> log; double bpm = 0;
    void loadRig (const RigPreset& r) override { log.push_back ("rig:" + r.name); }
    void applyScene (const Scene& s) override { log.push_back ("scene:" + s.name); }
    void setPedal (const std::string& u, bool on) override { log.push_back (u + (on ? ":on" : ":off")); }
    void setBpm (double b) override { bpm = b; }
};

static void testLive() {
    Library lib = seedLibrary(); Song s2 = lib.songs[0]; s2.id = "second"; s2.chart.title = "Second Song"; s2.chart.bpm = 120; lib.songs.push_back (s2);
    Setlist set = lib.setlists[0]; set.entries.push_back ({ "second", "G", 0, "sing" });
    MockSink sink; LiveSession ls (lib, sink); ls.start (set, 0, 0);
    CHECK_EQ (sink.log[0], "rig:Worship Ambient"); CHECK_EQ (ls.currentSceneName(), "Verse"); CHECK (ls.view() == LiveSession::View::Full); CHECK_EQ ((int) sink.bpm, 73);
    CHECK (ls.nextCue().sw == 'B'); CHECK (ls.nextCue().text.find ("Verse 2") != std::string::npos);
    Action a; a.type = Action::NextSection; ls.handle (a, 1); ls.handle (a, 2); CHECK_EQ (ls.sectionIndex(), 2); CHECK_EQ (ls.currentSceneName(), "Pre-Chorus"); CHECK (ls.view() == LiveSession::View::Section);
    ls.tick (5); CHECK (ls.view() == LiveSession::View::Section); ls.tick (13); CHECK (ls.view() == LiveSession::View::Full); // auto-return after 10 s
    ls.handle (a, 14); ls.handle (a, 15); CHECK_EQ (ls.sectionIndex(), 4); CHECK_EQ (ls.pass(), 1); ls.handle (a, 16); CHECK_EQ (ls.sectionIndex(), 4); CHECK_EQ (ls.pass(), 2); // V3 x2
    Action jc; jc.type = Action::JumpChorus; ls.handle (jc, 17); CHECK_EQ (ls.sectionIndex(), 5); CHECK_EQ (ls.currentSceneName(), "Chorus");
    Action bo; bo.type = Action::Boost; ls.handle (bo, 18); CHECK (ls.boostOn()); CHECK_EQ (sink.log.back(), "boost:on");
    Action sc; sc.type = Action::SwitchC; ls.handle (sc, 18); CHECK_EQ (sink.log.back(), "oct:on");
    for (int i = 0; i < 10; ++i) ls.handle (a, 19 + i); CHECK_EQ (ls.sectionIndex(), 9); CHECK (ls.nextCue().sw == 'D'); CHECK (ls.nextCue().text.find ("Second Song") != std::string::npos);
    Action ns; ns.type = Action::NextSong; ls.handle (ns, 40); CHECK_EQ (ls.songIndex(), 1); CHECK_EQ (ls.key(), "G"); CHECK (! ls.boostOn()); CHECK_EQ ((int) sink.bpm, 120);
    CHECK_EQ (ls.chart().sections[0].items[0].line.chords[0].chord, "G"); // per-setlist key override
    CHECK (ls.nextSongTitle().empty()); for (int i = 0; i < 12; ++i) ls.handle (a, 42 + i); CHECK_EQ (ls.nextCue().text, "End of set");
    Action ps; ps.type = Action::PrevSong; ls.handle (ps, 60); CHECK_EQ (ls.key(), "A");
    ls.setKeyOverride ("B"); CHECK_EQ (ls.key(), "B"); CHECK_EQ (lib.songs[0].chart.key, "A"); // original chart untouched
    Action gs; gs.type = Action::GotoSection; gs.value = 3; ls.handle (gs, 50); CHECK_EQ (ls.sectionIndex(), 3);
}


static void testTempo() {
    // --- clamping
    CHECK_EQ (clampBpm (10), 30.0); CHECK_EQ (clampBpm (999), 300.0); CHECK_EQ (clampBpm (72), 72.0); CHECK_EQ (clampBpm (0), 0.0); CHECK_EQ (clampBpm (-5), 0.0);
    CHECK_EQ (clampBpm (72.04), 72.0); CHECK_EQ (formatBpm (72), std::string ("72")); CHECK_EQ (formatBpm (72.5), std::string ("72.5")); CHECK (formatBpm (0).empty());
    // --- parse
    CHECK_EQ (parseChart ("{title: X}\n{tempo: 84}\n").bpm, 84.0); CHECK_EQ (parseChart ("{bpm: 400}\n").bpm, 300.0); CHECK_EQ (parseChart ("{tempo: 12}\n").bpm, 30.0);
    CHECK_EQ (parseChart ("{title: X}\n").bpm, 0.0);
    Chart seed = parseChart (readFile (std::string (WR_SOURCE_DIR) + "/resources/he-who-was.cho")); CHECK_EQ (seed.bpm, 73.0); seed.bpm = 72; // seed is 73 since 6704b3f
    // --- serialize: full writer round-trips tempo, key, sections, chords, tabs
    std::string cp = chartToChordPro (seed); CHECK (cp.find ("{tempo: 72}") != std::string::npos);
    Chart back = parseChart (cp); CHECK_EQ (back.bpm, 72.0); CHECK_EQ (back.key, seed.key); CHECK_EQ (back.title, seed.title);
    CHECK_EQ (back.sections.size(), seed.sections.size());
    bool same = back.sections.size() == seed.sections.size();
    for (size_t i = 0; same && i < seed.sections.size(); ++i) { same = back.sections[i].label == seed.sections[i].label && back.sections[i].repeat == seed.sections[i].repeat && back.sections[i].items.size() == seed.sections[i].items.size(); }
    CHECK (same);
    seed.bpm = 91.5; CHECK_EQ (parseChart (chartToChordPro (seed)).bpm, 91.5);
    // --- in-place rewrite keeps the rest of the file
    std::string src = "{title: A}\n{key: G}\n{tempo: 72}\n\n[G]Hello\n";
    std::string w = setChordProTempo (src, 80); CHECK_EQ (w, std::string ("{title: A}\n{key: G}\n{tempo: 80}\n\n[G]Hello\n"));
    std::string ins = setChordProTempo ("{title: A}\n{key: G}\n[G]Hi\n", 66); CHECK_EQ (ins, std::string ("{title: A}\n{key: G}\n{tempo: 66}\n[G]Hi\n"));
    CHECK_EQ (parseChart (setChordProTempo (src, 1000)).bpm, 300.0);
    CHECK_EQ (setChordProTempo ("{tempo: 70}\n{bpm: 71}\nx\n", 90), std::string ("{tempo: 90}\nx\n")); // duplicate tempo lines collapse
    // --- tap tempo averaging (last 4 taps), reset after a 2 s gap, clamped
    { TapTempo tt; CHECK_EQ (tt.tap (0), 0.0); CHECK (std::abs (tt.tap (0.5) - 120) < 0.01);
      tt.tap (1.0); double b = tt.tap (1.6); CHECK (std::abs (b - 60.0 * 3 / 1.6) < 0.1);           // averages 0.5, 0.5, 0.6 s
      b = tt.tap (2.1); CHECK (std::abs (b - 60.0 * 3 / 1.6) < 0.1);                               // window slides: taps 0.5..2.1
      CHECK_EQ (tt.tap (10.0), 0.0); CHECK (std::abs (tt.tap (11.0) - 60) < 0.01);                  // gap resets
      TapTempo f; f.tap (0); CHECK_EQ (f.tap (0.1), 300.0); TapTempo sl; sl.tap (0); CHECK_EQ (sl.tap (1.99), 30.2); }
    // --- live session: nudge, clamp, tap via the guitar unit, save back to song
    Library lib = seedLibrary(); MockSink sink; LiveSession ls (lib, sink); lib.songs[0].chart.bpm = 72; ls.start (lib.setlists[0], 0, 0); // seed is 73; this block was written against 72
    CHECK_EQ (ls.bpm(), 72.0); CHECK_EQ (sink.bpm, 72.0); CHECK (! ls.tempoEdited());
    ls.nudgeBpm (+1); CHECK_EQ (ls.bpm(), 73.0); CHECK_EQ (sink.bpm, 73.0); CHECK (ls.tempoEdited()); CHECK_EQ (lib.songs[0].chart.bpm, 72.0);
    ls.setBpm (5); CHECK_EQ (ls.bpm(), 30.0); ls.setBpm (500); CHECK_EQ (ls.bpm(), 300.0); ls.nudgeBpm (+1); CHECK_EQ (ls.bpm(), 300.0);
    MidiRouter r; r.setProfiles (lib.controllers); Action a[16];
    for (int i = 0; i < 4; ++i) { r.handle (0xB0, 80, 127, 1000.0 + i * 600); r.handle (0xB0, 80, 0, 1010.0 + i * 600); int n = r.pop (a, 16); for (int k = 0; k < n; ++k) ls.handle (a[k], 1.0 + i * 0.6); }
    CHECK (std::abs (ls.bpm() - 100.0) < 0.1); CHECK (std::abs (sink.bpm - 100.0) < 0.1);   // guitar switch A = tap -> song tempo
    CHECK (ls.saveTempoToSong()); CHECK (std::abs (lib.songs[0].chart.bpm - 100.0) < 0.1); CHECK (! ls.tempoEdited());
    // --- MIDI clock override + fallback
    double tk = 60000.0 / 128.0 / 24.0, t = 10000; int got = 0;
    for (int i = 0; i < 24 * 4 + 1; ++i, t += tk) { r.handle (0xF8, 0, 0, t); int n = r.pop (a, 16); for (int k = 0; k < n; ++k) { ls.handle (a[k], t / 1000.0); ++got; } }
    CHECK (got >= 3); CHECK (ls.clockActive()); CHECK (std::abs (ls.bpm() - 128) < 0.3); CHECK (std::abs (sink.bpm - 128) < 0.3);
    ls.nudgeBpm (+1); CHECK (std::abs (ls.bpm() - 128) < 0.3); CHECK (std::abs (ls.songBpm() - 101) < 0.1); // clock wins while present
    ls.tick (t / 1000.0 + 0.3); CHECK (ls.clockActive());                                                     // short gap: still present
    ls.tick (t / 1000.0 + 1.5); CHECK (! ls.clockActive()); CHECK (std::abs (ls.bpm() - 101) < 0.1); CHECK (std::abs (sink.bpm - 101) < 0.1); // stopped: song tempo
    // clock restarts at a new tempo after a gap: router re-measures instead of averaging across the gap
    t += 5000; tk = 60000.0 / 90.0 / 24.0;
    for (int i = 0; i < 24 * 2 + 1; ++i, t += tk) { r.handle (0xF8, 0, 0, t); int n = r.pop (a, 16); for (int k = 0; k < n; ++k) ls.handle (a[k], t / 1000.0); }
    CHECK (ls.clockActive()); CHECK (std::abs (ls.bpm() - 90) < 0.3);
    CHECK_EQ (LiveSession::clockTimeoutSec (120), 1.25); CHECK_EQ (LiveSession::clockTimeoutSec (30), 5.0);
    // follow-clock off: clock ignored
    MidiRouter r2; r2.setProfiles (lib.controllers); r2.setExternal (16, 36, false); int n2 = 0;
    for (int i = 0; i < 24 * 3; ++i) { r2.handle (0xF8, 0, 0, 20000 + i * tk); n2 += r2.pop (a, 16); } CHECK_EQ (n2, 0);
    // setlist BPM override is the session tempo; saving clears it into the song
    Library lib2 = seedLibrary(); lib2.setlists[0].entries[0].bpmOverride = 80; MockSink s2; LiveSession l2 (lib2, s2); l2.start (lib2.setlists[0], 0, 0);
    CHECK_EQ (l2.bpm(), 80.0); l2.nudgeBpm (-1); l2.saveTempoToSong(); CHECK_EQ (lib2.songs[0].chart.bpm, 79.0); CHECK_EQ (lib2.setlists[0].entries[0].bpmOverride, 0.0);
}


struct MockSink2 : MockSink {
    bool clickOn = false, clickMuted = false, tuner = false; int bpb = 0, restarts = 0;
    void setClick (bool on, bool m) override { clickOn = on; clickMuted = m; }
    void setMeter (int b) override { bpb = b; }
    void restartBeat() override { ++restarts; }
    void setTunerOpen (bool o) override { tuner = o; }
};

static void testSwitchesClickTuner() {
    MidiRouter r; r.setProfiles ({ defaultGuitarUnit(), defaultSectionUnit() }); Action a[16]; int n;
    // guitar A: tap fires on press; hold 1 s -> tuner; release after hold doesn't add a second tap
    r.handle (0xB0, 80, 127, 0); n = r.pop (a, 16); CHECK (n == 1 && a[0].type == Action::Tap);
    r.tick (600); CHECK_EQ (r.pop (a, 16), 0); r.tick (1001); n = r.pop (a, 16); CHECK (n == 1 && a[0].type == Action::TunerToggle);
    r.handle (0xB0, 80, 0, 1200); CHECK_EQ (r.pop (a, 16), 0);
    r.handle (0xB0, 80, 127, 2000); r.handle (0xB0, 80, 0, 2100); r.tick (3200); n = r.pop (a, 16); CHECK (n == 1 && a[0].type == Action::Tap); // short = tap only
    // guitar D: short (release) = per-song D, hold 0.6 s = click mute
    r.handle (0xB0, 83, 127, 4000); CHECK_EQ (r.pop (a, 16), 0); r.handle (0xB0, 83, 0, 4100); n = r.pop (a, 16); CHECK (n == 1 && a[0].type == Action::SwitchD);
    r.handle (0xB0, 83, 127, 5000); r.tick (5500); CHECK_EQ (r.pop (a, 16), 0); r.tick (5601); n = r.pop (a, 16); CHECK (n == 1 && a[0].type == Action::ClickMute);
    r.handle (0xB0, 83, 0, 5800); CHECK_EQ (r.pop (a, 16), 0);
    CHECK_EQ (defaultGuitarUnit().sw[0].longAction, std::string ("tunerToggle")); CHECK_EQ (defaultGuitarUnit().sw[3].longAction, std::string ("clickMute"));
    CHECK (actionFromName ("clickMute") == Action::ClickMute); CHECK (actionFromName ("play") == Action::Play);
    // profile JSON keeps per-switch hold time
    auto pj = controllerFromJson (toJson (defaultGuitarUnit())); CHECK_EQ (pj.sw[0].longMs, 1000); CHECK_EQ (pj.sw[3].longMs, 600);

    // live session: click per-song default, mute remembered in settings, tuner open/close
    Library lib = seedLibrary(); lib.songs[0].clickDefault = true; Song s2 = lib.songs[0]; s2.id = "two"; s2.clickDefault = false; s2.chart.time = "6/8"; lib.songs.push_back (s2);
    Setlist set = lib.setlists[0]; set.entries.push_back ({ "two", "", 0, "lead" });
    MockSink2 sink; LiveSession ls (lib, sink); ls.start (set, 0, 0);
    CHECK (ls.clickOn()); CHECK (sink.clickOn); CHECK (! sink.clickMuted); CHECK_EQ (sink.bpb, 4);
    Action m; m.type = Action::ClickMute; ls.handle (m, 1); CHECK (ls.clickMuted()); CHECK (sink.clickMuted); CHECK (lib.settings.clickMuted); CHECK (ls.consumeSettingsDirty()); CHECK (! ls.consumeSettingsDirty());
    Action ns; ns.type = Action::NextSong; ls.handle (ns, 2); CHECK (! ls.clickOn()); CHECK (sink.clickMuted); CHECK_EQ (sink.bpb, 6); // mute survives song change
    ls.handle (m, 3); CHECK (! ls.clickMuted());
    CHECK (Json::parse (toJson (lib).dump()).isObject()); { auto l2 = libraryFromJson (toJson (lib)); CHECK (l2.songs[0].clickDefault); CHECK (! l2.songs[1].clickDefault); CHECK_EQ (l2.settings.clickOut, 2) /* v0.4 default: out 2 */; }
    { Library lx = lib; lx.settings.clickMuted = true; lx.settings.tunerRefHz = 442; CHECK (libraryFromJson (toJson (lx)).settings.clickMuted); CHECK_EQ (libraryFromJson (toJson (lx)).settings.tunerRefHz, 442.0f); }
    Action tu; tu.type = Action::TunerToggle; ls.handle (tu, 4); CHECK (ls.tunerOpen()); CHECK (sink.tuner); ls.handle (tu, 5); CHECK (! sink.tuner);
    // tuner reading with a different A reference
    auto t440 = tunerReading (440.0f, 1.0f), t442 = tunerReading (442.0f, 1.0f, 442.0f), off = tunerReading (442.0f, 1.0f);
    CHECK (t440.valid && t440.note == "A" && std::abs (t440.cents) < 0.01f); CHECK (std::abs (t442.cents) < 0.01f); CHECK (std::abs (off.cents - 7.85f) < 0.1f);
    CHECK_EQ (beatsPerBarFor ("3/4"), 3); CHECK_EQ (beatsPerBarFor (""), 4); CHECK_EQ (beatsPerBarFor ("12/8"), 12);
}

static void testCapo() {
    Chart c = parseChart ("{title: T}\n{key: A}\n{capo: 2}\n{start_of_verse: Verse 1}\n[G]Hello [D/F#]world [Em7]now\n");
    CHECK_EQ (c.capo, 2); CHECK_EQ (c.key, "A");
    CHECK_EQ (c.sections[0].items[0].line.chords[0].chord, "A"); CHECK_EQ (c.sections[0].items[0].line.chords[1].chord, "E/G#"); // stored as sounding chords
    std::string w = chartToChordPro (c); CHECK (w.find ("{capo: 2}") != std::string::npos); CHECK (w.find ("[G]Hello") != std::string::npos); // written as shapes
    Chart back = parseChart (w); CHECK_EQ (back.capo, 2); CHECK_EQ (back.sections[0].items[0].line.chords[1].chord, "E/G#");
    CHECK_EQ (shapesKey ("A", 2), "G"); CHECK_EQ (shapesKey ("Bb", 3), "G"); CHECK_EQ (shapesKey ("F#m", 2), "Em");
    auto opts = capoOptions ("Bb"); CHECK (! opts.empty()); CHECK_EQ (opts[0].capo, 3); CHECK_EQ (opts[0].shapes, "G"); // G shapes (capo 3) ranks first, A shapes (capo 1) also offered
    bool hasA = false; for (auto& o : opts) if (o.capo == 1 && o.shapes == "A") hasA = true; CHECK (hasA);
    CHECK_EQ (capoOptions ("G")[0].capo, 0); CHECK_EQ (capoOptions ("C#m")[0].shapes, "Am"); CHECK_EQ (capoOptions ("C#m")[0].capo, 4); CHECK (capoOptions ("").empty());
    Chart part = chartForPart (c, "B", 4); CHECK_EQ (part.key, "B"); CHECK_EQ (part.capo, 4); CHECK_EQ (part.sections[0].items[0].line.chords[0].chord, "G"); // B with capo 4 -> G shapes
    Chart nocapo = chartForPart (c, "", 0); CHECK_EQ (nocapo.sections[0].items[0].line.chords[0].chord, "A"); CHECK_EQ (nocapo.capo, 0);
    CHECK_EQ (capoLabel ("A", 2), std::string ("Capo 2: play G shapes (sounds A)"));
    // seed chart with tabs: capo shapes move the tab with the chords
    Chart seed = parseChart (readFile (std::string (WR_SOURCE_DIR) + "/resources/he-who-was.cho"));
    Chart sp = chartForPart (seed, "A", 2); CHECK_EQ (sp.capo, 2); CHECK_EQ (sp.sections[0].items[0].line.chords.empty() ? std::string ("G") : sp.sections[0].items[0].line.chords[0].chord.substr (0, 1), std::string ("G"));
    // JSON keeps capo + bars; live session shows capo and per-setlist override
    Chart cj = chartFromJson (toJson (c)); CHECK_EQ (cj.capo, 2);
    Library lib = seedLibrary(); lib.songs[0].chart.capo = 2; MockSink sink; LiveSession ls (lib, sink); ls.start (lib.setlists[0], 0, 0);
    CHECK_EQ (ls.capo(), 2); CHECK_EQ (ls.key(), "A"); CHECK_EQ (ls.capoText(), std::string ("Capo 2: play G shapes (sounds A)"));
    ls.setCapoOverride (0); CHECK_EQ (ls.capo(), 0); CHECK_EQ (lib.setlists[0].entries[0].capoOverride, 0); ls.setCapoOverride (-1); CHECK_EQ (ls.capo(), 2);
    ls.setKeyOverride ("B"); CHECK_EQ (ls.key(), "B"); CHECK_EQ (ls.capoText(), std::string ("Capo 2: play A shapes (sounds B)"));
    Setlist sl = setlistFromJson (toJson (lib.setlists[0])); CHECK_EQ (sl.entries[0].capoOverride, -1);
}

static void testAutoAdvance() {
    Library lib = seedLibrary(); auto& ch = lib.songs[0].chart; ch.time = "4/4"; ch.bpm = 120;
    lib.settings.countIn = 0; lib.settings.quantizeSections = false; // v0.3 behaviour; count-in + bar-quantised changes are tested in testCues
    for (auto& s : ch.sections) s.bars = 2; // 2 bars at 120 = 4 s per section (pass)
    MockSink2 sink; LiveSession ls (lib, sink); ls.start (lib.setlists[0], 0, 0);
    double t = 0; auto run = [&] (double until) { for (; t < until; t += 0.01) ls.tick (t); };
    run (5); CHECK_EQ (ls.sectionIndex(), 0); // not playing yet
    ls.setPlaying (true, t); CHECK_EQ (sink.restarts, 1); double t0 = t;
    run (t0 + 3.9); CHECK_EQ (ls.sectionIndex(), 0); run (t0 + 4.05); CHECK_EQ (ls.sectionIndex(), 1); CHECK (ls.view() == LiveSession::View::Full); // auto != manual: stays in full view
    run (t0 + 8.05); CHECK_EQ (ls.sectionIndex(), 2);
    // manual next mid-bar: new section starts on the current bar (grid kept), then the clock carries on
    run (t0 + 9.0); Action nx; nx.type = Action::NextSection; ls.handle (nx, t); CHECK_EQ (ls.sectionIndex(), 3); CHECK (std::abs (ls.beatInSection() - 2.0) < 0.05); // 1 s = 2 beats into the bar
    run (t0 + 9.0 + 2.9); CHECK_EQ (ls.sectionIndex(), 3); run (t0 + 9.0 + 3.05); CHECK_EQ (ls.sectionIndex(), 4); // 8 beats from that bar start
    // section with x2 repeat counts each pass
    int s4 = ls.sectionIndex(); int rep = ls.chart().sections[(size_t) s4].repeat; CHECK_EQ (rep, 2); run (t + 4.0); CHECK_EQ (ls.sectionIndex(), 4); CHECK_EQ (ls.pass(), 2); run (t + 4.0); CHECK_EQ (ls.sectionIndex(), 5);
    // tempo change mid-section: beats accumulate at the new tempo
    ls.setBpm (60); double tb = t; run (tb + 7.7); CHECK_EQ (ls.sectionIndex(), 5); run (tb + 8.05); CHECK_EQ (ls.sectionIndex(), 6);
    // MIDI clock present: no auto-advance (Cubase cues sections)
    Action ck; ck.type = Action::ClockBpm; ck.bpm = 120; double tc = t; for (; t < tc + 10; t += 0.01) { ls.handle (ck, t); ls.tick (t); } CHECK_EQ (ls.sectionIndex(), 6);
    // sections without bars stay manual
    Library l2 = seedLibrary(); MockSink2 s2; LiveSession m (l2, s2); m.start (l2.setlists[0], 0, 0); m.setPlaying (true, 0); for (double u = 0; u < 60; u += 0.01) m.tick (u); CHECK_EQ (m.sectionIndex(), 0);
    // ChordPro bars attribute + directive, written back
    Chart b = parseChart ("{title: B}\n{start_of_part: Verse 1 bars=8}\n[A]la\n{start_of_chorus: Chorus}\n{bars: 4}\n[D]la\n");
    CHECK_EQ (b.sections[0].bars, 8); CHECK_EQ (b.sections[0].name, std::string ("Verse 1")); CHECK_EQ (b.sections[1].bars, 4);
    Chart bb = parseChart (chartToChordPro (b)); CHECK_EQ (bb.sections[0].bars, 8); CHECK_EQ (bb.sections[1].bars, 4); CHECK_EQ (chartFromJson (toJson (b)).sections[1].bars, 4);
    // play to the end stops
    for (int k = 0; k < 400 && ls.playing(); ++k) { ls.handle (nx, t); t += 0.01; } CHECK (ls.sectionIndex() == (int) ls.chart().sections.size() - 1);
}

static void testBackup() {
    namespace fs = std::filesystem; std::string dir = (fs::temp_directory_path() / "wr_backup_test").string(); fs::remove_all (dir);
    auto libJson = [] (int n) { Library l = seedLibrary(); l.songs[0].chart.title = "Rev " + std::to_string (n); return toJson (l).dump(); };
    { BackupStore b (dir, 10, 60); auto r = b.open(); CHECK (r.source == BackupStore::Source::None); CHECK (! r.crashed);
      long long t = 1000; for (int i = 0; i < 25; ++i) { CHECK (b.save (libJson (i), t)); t += 120; } // 25 distinct saves 2 min apart
      auto v = b.list(); CHECK_EQ ((int) v.size(), 10); CHECK (v[0].seq > v[9].seq); std::string s; CHECK (b.read (v[0], s)); CHECK (s.find ("Rev 24") != std::string::npos);
      // rapid saves within 60 s coalesce into the newest snapshot
      b.save (libJson (100), t); b.save (libJson (101), t + 5); b.save (libJson (102), t + 10); v = b.list(); CHECK_EQ ((int) v.size(), 10); b.read (v[0], s); CHECK (s.find ("Rev 102") != std::string::npos);
      b.read (v[1], s); CHECK (s.find ("Rev 24") != std::string::npos);
      size_t before = b.list().size(); b.save (libJson (102), t + 500); CHECK_EQ (b.list().size(), before); // unchanged content: no new snapshot
      // no closeClean -> simulated crash
    }
    { BackupStore b (dir); auto r = b.open(); CHECK (r.crashed); CHECK (r.source == BackupStore::Source::Main); CHECK (r.json.find ("Rev 102") != std::string::npos); b.closeClean(); }
    { BackupStore b (dir); auto r = b.open(); CHECK (! r.crashed); b.closeClean(); }
    // corrupt main file (crash mid-write on an old build) -> newest valid backup restored automatically
    { std::ofstream f (dir + "/library.json", std::ios::trunc); f << "{\"songs\": [ {\"id\": \"half"; }
    { BackupStore b (dir); auto r = b.open(); CHECK (r.source == BackupStore::Source::Backup); CHECK (r.mainWasBad); CHECK (r.json.find ("Rev 102") != std::string::npos);
      CHECK (fs::exists (dir + "/library.corrupt.json")); CHECK (BackupStore::validLibraryJson (readFile (dir + "/library.json")));
      CHECK (libraryFromJson (Json::parse (r.json)).songs[0].chart.title == "Rev 102");
      // manual restore of an older backup snapshots the current state first (undoable)
      auto v = b.list(); std::string out; auto older = v[3]; std::string olderTxt; b.read (older, olderTxt);
      CHECK (b.restore (older, out, 999999)); CHECK_EQ (out, olderTxt); CHECK_EQ (readFile (dir + "/library.json"), olderTxt);
      auto v2 = b.list(); std::string newest; b.read (v2[0], newest); CHECK (newest.find ("Rev 102") != std::string::npos); CHECK ((int) v2.size() <= 10);
      // a corrupt backup is skipped
      { std::ofstream f (v2[0].path, std::ios::trunc); f << "garbage"; } { std::ofstream f (dir + "/library.json", std::ios::trunc); f << ""; }
      b.closeClean(); }
    { BackupStore b (dir); auto r = b.open(); CHECK (r.source == BackupStore::Source::Backup); CHECK (BackupStore::validLibraryJson (r.json)); b.closeClean(); }
    CHECK (! BackupStore::validLibraryJson ("{}")); CHECK (! BackupStore::validLibraryJson ("[1,2]"));
    fs::remove_all (dir);
}

static void testExport() {
    Library lib = seedLibrary(); Song s2 = lib.songs[0]; s2.id = "two"; s2.chart.title = "Second Song (Ünïcode – test)"; s2.chart.bpm = 120; lib.songs.push_back (s2);
    Setlist set = lib.setlists[0]; set.entries.push_back ({ "two", "G", 0, "lead" });
    Player gtr { "Clayton", "Guitar", 2, {} }; Player keys { "Sarah", "Keys", 0, { { "he-who-was", { "Bb", -1 } } } };
    set.players = { gtr, keys };
    // part keys: band = setlist key; guitar = capo 2 shapes; keys = own key for song 1
    auto band0 = partKeyFor (lib, set, 0, nullptr); CHECK_EQ (band0.soundingKey, "A"); CHECK_EQ (band0.capo, 0);
    auto g0 = partKeyFor (lib, set, 0, &set.players[0]); CHECK_EQ (g0.soundingKey, "A"); CHECK_EQ (g0.capo, 2);
    auto k0 = partKeyFor (lib, set, 0, &set.players[1]); CHECK_EQ (k0.soundingKey, "Bb"); auto k1 = partKeyFor (lib, set, 1, &set.players[1]); CHECK_EQ (k1.soundingKey, "G");
    Chart gc = chartForPlayer (lib, set, 0, &set.players[0]); CHECK_EQ (gc.capo, 2); CHECK_EQ (gc.sections[0].items[0].line.chords[0].chord.substr (0, 1), std::string ("G"));
    // ChordPro: one file, songs split, round-trips per player key
    std::string cp = setlistToChordPro (lib, set, &set.players[1]);
    auto songs = parseChordProSongs (cp); CHECK_EQ ((int) songs.size(), 2); CHECK_EQ (songs[0].key, "Bb"); CHECK_EQ (songs[1].key, "G"); CHECK_EQ (songs[1].bpm, 120.0);
    CHECK_EQ (songs[0].sections.size(), lib.songs[0].chart.sections.size()); CHECK_EQ (songs[0].sections[0].items[0].line.chords[0].chord.substr (0, 2), std::string ("Bb"));
    auto gs = parseChordProSongs (setlistToChordPro (lib, set, &set.players[0])); CHECK_EQ (gs[0].capo, 2); CHECK_EQ (gs[0].key, "A"); CHECK_EQ (gs[0].sections[0].items[0].line.chords[0].chord.substr (0, 1), std::string ("A")); // sounding again after parse
    // PDF: valid structure, one page per short song, xref offsets right, text in the player's key
    std::string pdf = setlistToPdf (lib, set, &set.players[0]);
    CHECK (pdf.rfind ("%PDF-1.4", 0) == 0); CHECK (pdf.find ("%%EOF") != std::string::npos);
    auto sx = pdf.rfind ("startxref\n"); size_t xref = (size_t) std::stoll (pdf.substr (sx + 10)); CHECK (pdf.compare (xref, 4, "xref") == 0);
    bool offsetsOk = true; { std::istringstream is (pdf.substr (xref)); std::string l; std::getline (is, l); int a, n; is >> a >> n; std::getline (is, l); std::getline (is, l);
        for (int i = 1; i < n; ++i) { std::getline (is, l); size_t o = (size_t) std::stoll (l.substr (0, 10)); if (pdf.compare (o, std::to_string (i).size() + 6, std::to_string (i) + " 0 obj") != 0) offsetsOk = false; } }
    CHECK (offsetsOk);
    int pages = 0; for (size_t p = 0; (p = pdf.find ("/Type /Page ", p)) != std::string::npos; ++p) ++pages; CHECK (pages >= 2);
    CHECK (pdf.find ("Capo 2: play G shapes \\(sounds A\\)") != std::string::npos); CHECK (pdf.find ("He who was") != std::string::npos);
    CHECK (pdf.find ("Second Song (\xDCn\xEF" "code") == std::string::npos);              // parens escaped
    CHECK (pdf.find ("Second Song \\(\xDCn\xEF" "code \x96 test\\)") != std::string::npos); // WinAnsi: Ü ï en-dash
    auto files = exportSetlist (lib, set, { nullptr, &set.players[0], &set.players[1] }, ExportFormat::Pdf);
    CHECK_EQ ((int) files.size(), 3); CHECK_EQ (files[1].name, std::string ("2026-11-08 Sunday - Clayton.pdf")); CHECK_EQ (files[1].mime, std::string ("application/pdf"));
    auto cfiles = exportSetlist (lib, set, { &set.players[1] }, ExportFormat::ChordPro); CHECK_EQ (cfiles[0].name, std::string ("2026-11-08 Sunday - Sarah.cho"));
    CHECK_EQ (safeFileName ("a/b:c"), std::string ("a-b-c"));
    { std::ofstream f ("/tmp/wr_export_test.pdf", std::ios::binary); f << pdf; } // for manual pdftotext / viewer checks
    // players persist with the setlist
    Setlist back = setlistFromJson (toJson (set)); CHECK_EQ ((int) back.players.size(), 2); CHECK_EQ (back.players[1].songs["he-who-was"].key, std::string ("Bb")); CHECK_EQ (back.players[0].capo, 2);
}


struct CueSink : MockSink2 {
    std::vector<std::pair<std::string, int>> said; int due = -1; int countBeats = -1; bool countSpoken = false;
    void announceSection (const std::string& n, int seq) override { said.push_back ({ n, seq }); }
    bool cueDue (int seq) override { return due >= seq; }
    void countIn (int b, bool sp) override { countBeats = b; countSpoken = sp; }
};
static std::vector<float> loadVoice (const std::string& w, double& sr) { auto d = readFile (std::string (WR_SOURCE_DIR) + "/resources/voice/" + w + ".wav"); std::vector<float> m; decodeWav ((const uint8_t*) d.data(), d.size(), m, sr); return m; }

static void testCues() {
    // --- section name -> words
    auto W = [] (const char* n) { std::string o; for (auto& w : cueWords (n)) o += w + "|"; return o; };
    CHECK_EQ (W ("Verse 1"), std::string ("verse|one|")); CHECK_EQ (W ("Chorus"), std::string ("chorus|")); CHECK_EQ (W ("Pre-Chorus"), std::string ("pre-chorus|"));
    CHECK_EQ (W ("PreChorus 2"), std::string ("pre-chorus|two|")); CHECK_EQ (W ("Pre Chorus"), std::string ("pre-chorus|")); CHECK_EQ (W ("Final Chorus x2"), std::string ("final|chorus|"));
    CHECK_EQ (W ("Bridge 10"), std::string ("bridge|ten|")); CHECK_EQ (W ("Verse1"), std::string ("verse|one|")); CHECK_EQ (W ("Inst."), std::string ("instrumental|"));
    CHECK_EQ (W ("Tag (x3)"), std::string ("tag|")); CHECK_EQ (W ("Intro"), std::string ("intro|")); CHECK_EQ (W ("Outro"), std::string ("outro|")); CHECK_EQ (W ("Instrumental"), std::string ("instrumental|"));
    CHECK_EQ (W ("Spontaneous Worship"), std::string ("spontaneous|worship|")); CHECK_EQ (W ("Chorus 15"), std::string ("chorus|one|five|")); CHECK_EQ (W ("Glory Section"), std::string ("glory|section|"));
    CHECK_EQ (spokenName ("Verse 2"), std::string ("Verse two")); CHECK_EQ (spokenName ("Bridge"), std::string ("Bridge")); CHECK_EQ (numberWord (12), std::string ("twelve")); CHECK (numberWord (13).empty());
    // --- bundled voice samples decode; every word used by the seed chart and common names is covered
    VoiceBank vb; int loaded = 0;
    for (const char* w : { "verse", "chorus", "pre-chorus", "bridge", "tag", "intro", "outro", "instrumental", "interlude", "ending", "final", "again", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten", "eleven", "twelve" }) {
        double sr = 0; auto m = loadVoice (w, sr); if (! m.empty() && sr == 22050) { ++loaded; float pk = 0; for (float x : m) pk = std::max (pk, std::abs (x)); if (pk < 0.5f || pk > 1.0f || m.size() > (size_t) sr) std::printf ("  voice %s: peak %.2f len %zu\n", w, pk, m.size()); vb.addWord (w, m, sr); } }
    CHECK_EQ (loaded, 24);
    vb.prepare (48000); Chart seed = parseChart (readFile (std::string (WR_SOURCE_DIR) + "/resources/he-who-was.cho")); bool cov = true; for (auto& sec : seed.sections) cov = cov && vb.covers (sec.name); CHECK (cov);
    auto ph = vb.phrase ("Verse 2"); double sr2 = 0; auto v = loadVoice ("verse", sr2), tw = loadVoice ("two", sr2);
    CHECK (std::abs ((double) ph.size() - ((double) (v.size() + tw.size()) * 48000.0 / 22050.0 + 0.045 * 48000)) < 8); // resampled words + gap
    CHECK (! vb.covers ("Glory Section")); CHECK (vb.phrase ("Glory Section").empty()); vb.addPhrase ("Glory Section", std::vector<float> (2205, 0.1f), 22050); CHECK (vb.covers ("Glory Section")); CHECK (std::abs ((int) vb.phrase ("Glory Section").size() - 4800) <= 4); // TTS fallback
    CHECK (vb.number (3).len > 0 && vb.number (13).len == 0);
    { std::vector<uint8_t> bad (40, 0); std::vector<float> m; double r; CHECK (! decodeWav (bad.data(), bad.size(), m, r)); }

    // --- audio-thread scheduling (ClickGen): 120 BPM 4/4 at 48 kHz, 1 beat = 24000 samples
    const int fs = 48000, B = 24000; VoiceClip nums[13]; for (int i = 1; i <= 12; ++i) nums[i] = vb.number (i);
    auto voiceStarts = [&] (ClickGen& c, ClickGen::Params p, int samples, std::vector<int>& starts, int gapSamples = 6000) { // render voice only and find word onsets
        std::vector<float> buf ((size_t) samples, 0.0f); p.gain = 0; c.process (buf.data(), buf.data(), samples, p); bool in = false; int quiet = 0;
        for (int i = 0; i < samples; ++i) { bool loud = std::abs (buf[(size_t) i]) > 0.01f; if (loud && ! in) { starts.push_back (i); in = true; } if (loud) quiet = 0; else if (in && ++quiet > gapSamples) in = false; }
        if (std::getenv ("WR_DEBUG")) { for (int x : starts) std::printf (" %d", x); std::printf ("\n"); } };
    ClickGen::Params P; P.bpm = 120; P.beatsPerBar = 4; P.on = true; P.cueGain = 1.0f;
    // synthetic clips with crisp edges so onsets are exact: name 0.2 s, numbers 0.1 s
    std::vector<float> synName ((size_t) (0.2 * fs), 0.5f), synLong ((size_t) (0.9 * fs), 0.5f), synNum ((size_t) (0.1 * fs), 0.3f);
    VoiceClip sn[13]; for (int i = 1; i <= 12; ++i) sn[i] = { synNum.data(), (int) synNum.size() };
    auto cueRun = [&] (double pressAtBeats, int bpb, double bpm, std::vector<float>& name, int& dueOut, std::vector<int>& st, long long& barsAtPress) {
        ClickGen c; c.prepare (fs); c.setNumbers (sn); ClickGen::Params q = P; q.bpm = bpm; q.beatsPerBar = bpb; const double beat = fs * 60.0 / bpm;
        std::vector<float> pre ((size_t) (beat * pressAtBeats), 0.0f); c.process (pre.data(), pre.data(), (int) pre.size(), q); barsAtPress = c.bar();
        c.announce ({ name.data(), (int) name.size() }, 5, q); dueOut = (int) c.dueBar(); CHECK (! c.speaking()); // never speaks in the current bar
        const double nextBar = (std::ceil (pressAtBeats / bpb + 1e-9) * bpb - pressAtBeats) * beat;
        std::vector<int> tmp; voiceStarts (c, q, (int) (nextBar + beat * (bpb + 0.5)), tmp, 300); // through the change bar line
        for (int x : tmp) { int v = (int) std::lround ((x - nextBar) / beat * 100); if (v >= -5) st.push_back (v); } // (ignores the tail of the click that was ringing at the press) // in 1/100 beat from the cue bar's 1
        CHECK_EQ (c.bar(), barsAtPress + 2); };
    for (double press : { 1.2, 3.5, 3.95, 0.05 }) { // anywhere in bar 1 (4/4, 120): cue bar = bar 2, change at bar 3
        int due = 0; long long b0 = 0; std::vector<int> st; cueRun (4.0 + press, 4, 120, synName, due, st, b0);
        CHECK_EQ ((long long) due, b0 + 2); CHECK_EQ ((int) st.size(), 4); // name on 1, counts on 2 3 4
        if (st.size() == 4) { CHECK (std::abs (st[0]) <= 1); CHECK (std::abs (st[1] - 100) <= 1); CHECK (std::abs (st[2] - 200) <= 1); CHECK (std::abs (st[3] - 300) <= 1); } }
    { int due = 0; long long b0 = 0; std::vector<int> st; cueRun (3 + 1.5, 3, 90, synName, due, st, b0); // 3/4: "Chorus, 2, 3"
      CHECK_EQ ((long long) due, b0 + 2); CHECK_EQ ((int) st.size(), 3); if (st.size() == 3) { CHECK (std::abs (st[1] - 100) <= 1); CHECK (std::abs (st[2] - 200) <= 1); } }
    { // long name at a fast tempo: the name fades out (8 ms) and "2" still starts exactly on beat 2
      ClickGen c; c.prepare (fs); c.setNumbers (sn); ClickGen::Params q = P; q.bpm = 160; q.gain = 0; const int bt = 18000; std::vector<float> pre ((size_t) bt * 5, 0.0f); c.process (pre.data(), pre.data(), (int) pre.size(), q);
      c.announce ({ synLong.data(), (int) synLong.size() }, 6, q); std::vector<float> o ((size_t) bt * 5, 0.0f); c.process (o.data(), o.data(), (int) o.size(), q); // cue bar starts at 3 beats in
      const size_t b2 = (size_t) bt * 4; CHECK (std::abs (o[b2 - 10] - 0.5f) < 0.01f); CHECK (std::abs (o[b2 + 1000] - 0.3f) < 0.01f); CHECK (std::abs (o[b2 + 200] - (0.3f + 0.5f * (1 - 201.0f / 384))) < 0.02f); }
    { int due = 0; long long b0 = 0; std::vector<int> st; cueRun (8 + 1.0, 6, 120, synName, due, st, b0); CHECK_EQ ((int) st.size(), 6); } // 6/8: name + 2..6
    { // real voice: "Verse two" then 2 3 4, one bar later the change
      ClickGen c; c.prepare (fs); c.setNumbers (nums); std::vector<float> pre ((size_t) (B * 5.3), 0.0f); c.process (pre.data(), pre.data(), (int) pre.size(), P);
      std::vector<float> name = vb.phrase ("Verse 2"); c.announce ({ name.data(), (int) name.size() }, 7, P); CHECK_EQ (c.dueBar(), 4); CHECK_EQ (c.cueSeq(), 7); // pressed in bar 2 -> cue bar 3 -> change at bar 4
      std::vector<float> o ((size_t) (B * 6.8), 0.0f); ClickGen::Params q = P; q.gain = 0; c.process (o.data(), o.data(), (int) o.size(), q);
      auto energy = [&] (double fromBeat) { size_t a2 = (size_t) ((fromBeat - 5.3 + 0.0) * B); if (fromBeat < 5.3) a2 = 0; float e = 0; for (size_t i = a2; i < a2 + 4000 && i < o.size(); ++i) e = std::max (e, std::abs (o[i])); return e; };
      CHECK (energy (5.35) < 1e-4f); CHECK (energy (8.0) > 0.05f); CHECK (energy (9.0) > 0.05f); CHECK (energy (10.0) > 0.05f); CHECK (energy (11.0) > 0.05f); CHECK_EQ (c.bar(), 4); }
    { // cue volume / mute rules
      ClickGen c; c.prepare (fs); ClickGen::Params m = P; m.muted = true; std::vector<float> pre (100, 0.0f); c.process (pre.data(), pre.data(), 100, m);
      std::vector<float> nm (4800, 0.5f); c.announce ({ nm.data(), 4800 }, 1, m); std::vector<float> o ((size_t) B * 5, 0.0f); c.process (o.data(), o.data(), (int) o.size(), m);
      float pk = 0; for (float x : o) pk = std::max (pk, std::abs (x)); CHECK (pk < 0.01f); // muted click mutes cues by default
      ClickGen c2; c2.prepare (fs); m.cuesWhenMuted = true; c2.process (pre.data(), pre.data(), 100, m); c2.announce ({ nm.data(), 4800 }, 1, m); std::fill (o.begin(), o.end(), 0.0f); c2.process (o.data(), o.data(), (int) o.size(), m);
      pk = 0; for (float x : o) pk = std::max (pk, std::abs (x)); CHECK (pk > 0.3f); } // option: cues still on
    { // count-in: 8 clicks at 120 = 4 s, ending on a bar line; audible even when muted; spoken numbers per beat
      for (int beats : { 8, 4, 6 }) for (int bpb : { 4, 3 }) {
        ClickGen c; c.prepare (fs); c.setNumbers (nums); ClickGen::Params m = P; m.muted = true; m.beatsPerBar = bpb; m.cueGain = 0;
        c.countIn (beats, true, bpb); CHECK (c.countingIn());
        std::vector<float> o ((size_t) (B * beats + B / 2), 0.0f); long long bar0 = c.bar(); c.process (o.data(), o.data(), (int) o.size(), m);
        int clicks = 0; float firstPk = 0, secondPk = 0; for (int k = 0; k < beats; ++k) { float pk = 0; for (int i = k * B; i < k * B + 600; ++i) pk = std::max (pk, std::abs (o[(size_t) i])); if (pk > 0.05f) ++clicks; if (k == 0) firstPk = pk; if (k == 1) secondPk = pk; }
        CHECK_EQ (clicks, beats); CHECK (! c.countingIn()); CHECK_EQ (c.beatInBar(), 0); // song starts on a downbeat right after
        { int o = (bpb - beats % bpb) % bpb; CHECK_EQ (c.bar() - bar0, (long long) ((o + beats) / bpb + (o == 0 ? 1 : 0))); } (void) firstPk; (void) secondPk;
      }
      ClickGen c; c.prepare (fs); c.setNumbers (nums); ClickGen::Params m = P; m.on = false; c.countIn (8, false, 4); std::vector<float> o ((size_t) B * 9, 0.0f); c.process (o.data(), o.data(), (int) o.size(), m);
      float pk1 = 0, pk2 = 0, after = 0; for (int i = 0; i < 600; ++i) { pk1 = std::max (pk1, std::abs (o[(size_t) i])); pk2 = std::max (pk2, std::abs (o[(size_t) (B + i)])); after = std::max (after, std::abs (o[(size_t) (8 * B + i)])); }
      CHECK (pk1 > pk2 * 1.4f); CHECK (after < 1e-4f); // downbeat accent; click switch off -> silent after the count-in
      ClickGen cOn; cOn.prepare (fs); cOn.setNumbers (nums); ClickGen::Params mon = P; mon.on = true; mon.muted = false; cOn.countIn (8, false, 4);
      std::vector<float> oOn ((size_t) B * 10, 0.0f); cOn.process (oOn.data(), oOn.data(), (int) oOn.size(), mon);
      float songClick = 0; for (int i = 0; i < 600; ++i) songClick = std::max (songClick, std::abs (oOn[(size_t) (8 * B + i)]));
      CHECK (songClick > 0.05f); // click left on: the downbeat after the count-in is still audible
      float pk5 = 0; for (int i = 0; i < 600; ++i) pk5 = std::max (pk5, std::abs (o[(size_t) (4 * B + i)])); CHECK (pk5 > pk2 * 1.4f); // bar 2's 1 accented too
    }

    // --- live session: queued to the bar line while the grid runs, immediate otherwise
    Library lib = seedLibrary(); CueSink sink; LiveSession ls (lib, sink); ls.start (lib.setlists[0], 0, 0);
    Action nx; nx.type = Action::NextSection; ls.handle (nx, 1); CHECK_EQ (ls.sectionIndex(), 1); CHECK (sink.said.empty()); // click off, not playing: immediate (v0.3 behaviour)
    ls.setClickOn (true); CHECK (ls.quantizing());
    ls.handle (nx, 2); CHECK_EQ (ls.sectionIndex(), 1); CHECK (ls.sectionPending()); CHECK_EQ (ls.pendingSection(), 2); CHECK_EQ ((int) sink.said.size(), 1); CHECK_EQ (sink.said[0].first, std::string ("Pre-Chorus"));
    ls.tick (2.1); CHECK_EQ (ls.sectionIndex(), 1); // bar line not reached
    ls.handle (nx, 2.2); CHECK_EQ (ls.pendingSection(), 3); CHECK_EQ (sink.said.back().first, std::string ("Chorus")); // second press moves the target on, re-announced
    sink.due = sink.said.back().second; ls.tick (2.5); CHECK_EQ (ls.sectionIndex(), 3); CHECK (! ls.sectionPending()); CHECK_EQ (ls.currentSceneName(), std::string ("Chorus"));
    Action gs; gs.type = Action::GotoSection; gs.value = 0; ls.handle (gs, 3); CHECK_EQ (ls.sectionIndex(), 3); CHECK_EQ (sink.said.back().first, std::string ("Verse 1")); // Cubase / screen too
    sink.due = sink.said.back().second; ls.tick (3.1); CHECK_EQ (ls.sectionIndex(), 0);
    Action pv; pv.type = Action::PrevSection; ls.handle (pv, 4); CHECK (! ls.sectionPending()); // nothing before the first section
    Action jc; jc.type = Action::JumpChorus; ls.handle (jc, 5); CHECK_EQ (ls.pendingSection(), 3); CHECK_EQ (sink.said.back().first, std::string ("Chorus"));
    lib.settings.voiceCues = false; ls.handle (nx, 5.1); CHECK (sink.said.back().first.empty()); // cues off: still quantised, nothing spoken
    lib.settings.voiceCues = true;
    // repeat pass announces "again"
    { int rep = -1; for (int i = 0; i < (int) ls.chart().sections.size(); ++i) if (ls.chart().sections[(size_t) i].repeat > 1) { rep = i; break; }
      if (rep >= 0) { sink.due = 1000; ls.tick (6); ls.gotoSection (rep, 6.1); sink.due = sink.said.back().second; ls.tick (6.2); CHECK_EQ (ls.sectionIndex(), rep); ls.handle (nx, 6.3); CHECK_EQ (sink.said.back().first, ls.chart().sections[(size_t) rep].name + " again"); } }
    lib.settings.quantizeSections = false; ls.handle (nx, 7); CHECK (! ls.sectionPending());
    // count-in: global 8, per-song override, bars codes, spoken option; auto-advance waits for it
    CHECK_EQ (countInBeats (8, 4), 8); CHECK_EQ (countInBeats (101, 3), 3); CHECK_EQ (countInBeats (102, 4), 8); CHECK_EQ (countInBeats (0, 4), 0); CHECK_EQ (countInBeats (-1, 4), 0);
    Library l2 = seedLibrary(); auto& c2 = l2.songs[0].chart; c2.bpm = 120; c2.time = "4/4"; for (auto& s2 : c2.sections) s2.bars = 1;
    CueSink k2; LiveSession m (l2, k2); m.start (l2.setlists[0], 0, 0); m.setPlaying (true, 0); CHECK_EQ (k2.countBeats, 8); CHECK (! k2.countSpoken);
    CHECK (m.clickOn()); CHECK (k2.clickOn); // Play turns the metronome on; it is not only the count-in
    for (double u = 0; u < 3.9; u += 0.01) m.tick (u); CHECK_EQ (m.sectionIndex(), 0); CHECK (m.clickOn());
    for (double u = 3.9; u < 6.05; u += 0.01) m.tick (u); CHECK_EQ (m.sectionIndex(), 1); CHECK (m.clickOn()); // still clicking after the count-in
    m.setPlaying (false, 6.5); CHECK (! m.clickOn()); CHECK (! k2.clickOn);
    l2.songs[0].countIn = 102; l2.settings.countInSpoken = true; m.setPlaying (true, 7); CHECK (m.clickOn()); CHECK_EQ (k2.countBeats, 8); CHECK (k2.countSpoken);
    l2.songs[0].countIn = 0; k2.countBeats = -1; m.setPlaying (true, 8); CHECK_EQ (k2.countBeats, -1); // off: no count-in
    l2.songs[0].chart.time = "3/4"; l2.songs[0].countIn = 101; m.refreshChart(); m.setPlaying (true, 9); CHECK_EQ (k2.countBeats, 3);
    // settings persist; defaults
    { Library x = seedLibrary(); CHECK_EQ (x.settings.clickPreset, 0); CHECK_EQ (x.settings.downbeatDb, 4.4f); x.settings.clickPreset = 3; x.settings.downbeatDb = 9; auto y = libraryFromJson (toJson (x)); CHECK_EQ (y.settings.clickPreset, 3); CHECK_EQ (y.settings.downbeatDb, 9.0f);
      CHECK_EQ (std::string (ClickGen::presetName (2)), std::string ("Cowbell (hi / lo)")); CHECK_EQ ((int) ClickGen::kNumPresets, 4); }
    { Library d; CHECK_EQ (d.settings.clickOut, 2); CHECK (d.settings.voiceCues); CHECK_EQ (d.settings.countIn, 8); CHECK (! d.settings.cuesWhenClickMuted); }
    { Library x = seedLibrary(); x.settings.cueDb = -12; x.settings.cuesWhenClickMuted = true; x.settings.countIn = 101; x.songs[0].countIn = 4; auto y = libraryFromJson (toJson (x));
      CHECK_EQ (y.settings.cueDb, -12.0f); CHECK (y.settings.cuesWhenClickMuted); CHECK_EQ (y.settings.countIn, 101); CHECK_EQ (y.songs[0].countIn, 4); }
    { Library x = seedLibrary(); x.settings.favoritePedals = { "overdrive" }; x.settings.recentPedals = { "delay", "chorus" };
      auto y = libraryFromJson (toJson (x)); CHECK_EQ (y.settings.favoritePedals.size(), (size_t) 1); CHECK_EQ (y.settings.favoritePedals[0], std::string ("overdrive"));
      CHECK_EQ (y.settings.recentPedals.size(), (size_t) 2); CHECK_EQ (y.settings.recentPedals[1], std::string ("chorus"));
      CHECK (toJson (seedLibrary()).dump().find ("favPedals") == std::string::npos); }
    { // footswitch map is global (the library), not per song; factory PC/CC still dispatch the saved action
      Library lib = seedLibrary();
      CHECK_EQ (lib.controllers[0].sw[0].action, std::string ("tap")); CHECK_EQ (lib.controllers[0].comboE, std::string ("tunerToggle"));
      lib.controllers[0].sw[0].action = "boost"; lib.controllers[0].sw[0].longAction.clear(); lib.controllers[0].sw[3].action = "clickMute"; lib.controllers[0].comboE = "stop";
      lib.controllers[1].sw[1].action = "jumpChorus";
      auto back = libraryFromJson (toJson (lib));
      CHECK_EQ (back.controllers[0].sw[0].action, std::string ("boost")); CHECK_EQ (back.controllers[0].sw[3].action, std::string ("clickMute"));
      CHECK_EQ (back.controllers[0].comboE, std::string ("stop")); CHECK_EQ (back.controllers[1].sw[1].action, std::string ("jumpChorus"));
      CHECK_EQ (back.controllers[0].sw[0].press.number, 81 - 1); // CC 80 binding is untouched
      resetFootswitchMap (back.controllers[0]); resetFootswitchMap (back.controllers[1]);
      CHECK_EQ (back.controllers[0].sw[0].action, std::string ("tap")); CHECK_EQ (back.controllers[0].sw[1].action, std::string ("boost"));
      CHECK_EQ (back.controllers[0].comboE, std::string ("tunerToggle")); CHECK_EQ (back.controllers[0].sw[0].longAction, std::string ("tunerToggle"));
      CHECK_EQ (back.controllers[1].sw[0].action, std::string ("playStart")); CHECK_EQ (back.controllers[1].sw[0].longAction, std::string ("stop"));
      CHECK_EQ (back.controllers[1].sw[1].action, std::string ("nextSection")); CHECK_EQ (back.controllers[1].comboF, std::string ("stop"));
      CHECK_EQ (back.controllers[0].sw[0].press.number, 80);
      MidiRouter mapped; auto g = defaultGuitarUnit(); g.sw[0].action = "boost"; g.sw[0].longAction.clear(); g.sw[0].longMs = 0; g.comboE = "stop";
      auto sec = defaultSectionUnit(); sec.sw[1].action = "jumpChorus";
      mapped.setProfiles ({ g, sec });
      mapped.handle (0xB0, 80, 127, 0); Action out[4]; int n = mapped.pop (out, 4); CHECK (n == 1 && out[0].type == Action::Boost);
      mapped.handle (0xC0, 0, 0, 1000, 0); mapped.flushFactory (1100); n = mapped.pop (out, 4); CHECK (n == 1 && out[0].type == Action::Boost);
      mapped.handle (0xC0, 1, 0, 2000, 1); mapped.flushFactory (2100); n = mapped.pop (out, 4); CHECK (n == 1 && out[0].type == Action::JumpChorus);
      mapped.handle (0xC0, 0, 0, 3000, 0); mapped.handle (0xC0, 1, 0, 3020, 0); mapped.flushFactory (3100); n = mapped.pop (out, 4); CHECK (n == 1 && out[0].type == Action::TransportStop); }
}

int main() { setvbuf (stdout, nullptr, _IONBF, 0);
    testChords(); testTabs(); testParser(); testJson(); testMidi(); testLive(); testTempo(); testSwitchesClickTuner(); testCapo(); testAutoAdvance(); testBackup(); testExport(); testCues();
    std::printf ("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
