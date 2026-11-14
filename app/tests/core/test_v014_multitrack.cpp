// v0.14 tests: multitrack backing tracks - tempo map, sequencing, arrangement re-sequencing, crossfaded jumps / vamps,
// mute persistence, routing, disk streaming (WAV + cue markers), sync messages, live glue, locks.
#include "model/Chart.h"
#include "model/Song.h"
#include "model/Multitrack.h"
#include "audio/MultitrackPlayer.h"
#include "audio/MultitrackLive.h"
#include "sync/Sync.h"
#include <cstdio>
#include <cmath>
using namespace wr; using namespace wr::mt;
static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (! (c)) { ++fails; std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)
static const char* kSong = "{title: Test}\n{key: G}\n{tempo: 120}\n{time: 4/4}\n{start_of_verse: Verse 1}\n[G]one\n{end_of_verse}\n{start_of_chorus: Chorus}\n[C]two\n{end_of_chorus}\n{start_of_bridge: Bridge}\n[D]three\n{end_of_bridge}\n";
constexpr double FS = 1000;   // tiny rate keeps buffers small; 120 BPM -> 500 samples a beat, 2000 a bar

struct Sink : RigSink {
    MultitrackLive* ml = nullptr; double bpm = 120; std::vector<std::string> said;
    void loadRig (const RigPreset&) override {} void applyScene (const Scene&) override {} void setPedal (const std::string&, bool) override {}
    void setBpm (double b) override { bpm = b; } void announceSection (const std::string& n, int) override { said.push_back (n); }
    bool cueDue (int) override { return false; }
    void countIn (int beats, bool) override { if (ml) ml->onCountIn (beats, bpm); }
};
// stem whose sample value encodes its source index (ramp), so we can see exactly what was read
static std::unique_ptr<StemSource> ramp (int64_t n, float scale = 1e-5f) { auto m = std::make_unique<MemorySource>(); m->rate = FS;
    for (int64_t i = 0; i < n; ++i) { m->L.push_back ((float) i * scale); m->R.push_back ((float) i * scale); } return m; }
static MultitrackSet set3 () { MultitrackSet m; m.sampleRate = FS; m.tempo = TempoMap::constant (120); m.beatsPerBar = 4; m.lengthSec = 24;
    m.sectionBeats = { 0, 16, 32 }; Stem s; s.id = "a"; s.name = "Keys Pad"; m.stems.push_back (s); return m; }   // 3 sections x 4 bars = 8 s each

int main() { std::setvbuf (stdout, nullptr, _IONBF, 0);
    std::printf ("-- tempo map\n");
    { TempoMap t; t.offsetSec = 1; t.pts = { { 0, 120 }, { 8, 60 } };
      CHECK (std::abs (t.secAt (0) - 1) < 1e-9); CHECK (std::abs (t.secAt (8) - 5) < 1e-9); CHECK (std::abs (t.secAt (10) - 7) < 1e-9);
      CHECK (std::abs (t.beatAt (7) - 10) < 1e-9); CHECK (std::abs (t.beatAt (3) - 4) < 1e-9); CHECK (t.bpmAt (9) == 60); }
    std::printf ("-- sequencing as written + arranged (delete / repeat sections)\n");
    Chart w = parseChart (kSong); CHECK (w.sections.size() == 3);
    auto m = set3();
    { auto seq = buildSequence (m, w, w); CHECK (seq.size() == 3);
      CHECK (seq[0].srcStart == 0 && seq[0].srcEnd == 8000 && seq[1].srcStart == 8000 && seq[2].srcEnd == 24000 && seq[2].outStart == 16000); }
    Chart arr = w; arr.arrangement = { "V1", "C", "C", "B" };
    Chart a2 = arrangedChart (w, arr.arrangement);
    CHECK (a2.sections.size() == 4);
    { auto seq = buildSequence (m, w, a2); CHECK (seq.size() == 4);
      CHECK (seq[2].srcStart == 8000 && seq[2].outStart == 16000 && seq[3].srcStart == 16000 && seq[3].outStart == 24000); }
    std::printf ("-- sequencer: repeat = crossfaded splice back to the chorus start, sample-accurate\n");
    { Sequencer q; q.setSequence (buildSequence (m, w, a2), m.tempo, 4, FS); q.play(); Span sp[64];
      int64_t heard = 0; int splices = 0;
      for (int b = 0; b < 200; ++b) { int n = q.render (100, sp, 64); for (int i = 0; i < n; ++i) { if (sp[i].g0 > sp[i].g1) ++splices; heard += 0; } (void) heard; if (q.current() == 2) { n = q.render (100, sp, 64); for (int i = 0; i < n; ++i) if (sp[i].g0 > sp[i].g1) ++splices; break; } }
      CHECK (q.current() == 2); CHECK (q.srcPos() >= 8000 && q.srcPos() <= 8200); CHECK (splices >= 1);
      // a delete (V1 B) while playing in the chorus keeps position by arranged index and still splices
      Chart a3 = arrangedChart (w, std::vector<std::string> { "V1", "B" }); q.setSequence (buildSequence (m, w, a3), m.tempo, 4, FS); CHECK (q.current() == 1); CHECK (q.srcPos() >= 16000); }
    std::printf ("-- quantised jump on the next bar + vamp\n");
    { Sequencer q; q.setSequence (buildSequence (m, w, w), m.tempo, 4, FS); q.play(); Span sp[64];
      for (int i = 0; i < 25; ++i) q.render (100, sp, 64);                      // 2500 samples = bar 2, beat 2
      CHECK (std::abs (q.barInSection() - 2) < 1e-9);
      q.requestJump (2, Quant::NextBar); CHECK (q.pendingJump() == 2);
      for (int i = 0; i < 14; ++i) q.render (100, sp, 64); CHECK (q.current() == 0);   // 3900: not yet
      q.render (100, sp, 64); CHECK (q.current() == 2); CHECK (q.srcPos() == 16000); // landed exactly at 4000 (bar line)
      q.setLoop (true); for (int i = 0; i < 85; ++i) q.render (100, sp, 64);       // past the bridge end: vamped
      CHECK (q.current() == 2 && q.srcPos() < 24000 && q.srcPos() >= 16000);
      q.setLoop (false); for (int i = 0; i < 85; ++i) q.render (100, sp, 64); CHECK (q.ended() && ! q.playing()); }
    { Sequencer q; q.setSequence (buildSequence (m, w, w), m.tempo, 4, FS); q.play(); Span sp[64];
      for (int i = 0; i < 5; ++i) q.render (100, sp, 64); q.requestJump (1, Quant::SectionEnd);
      for (int i = 0; i < 70; ++i) q.render (100, sp, 64); CHECK (q.current() == 0);
      for (int i = 0; i < 6; ++i) q.render (100, sp, 64); CHECK (q.current() == 1); }
    std::printf ("-- preroll (count-in) then stems\n");
    { Sequencer q; q.setSequence (buildSequence (m, w, w), m.tempo, 4, FS); q.play (4000); Span sp[64];
      int n = q.render (100, sp, 64); CHECK (n == 0 && q.srcPos() == 0); for (int i = 0; i < 40; ++i) q.render (100, sp, 64); CHECK (q.srcPos() == 100); }
    std::printf ("-- mix: solo / mute / pan / routing\n");
    { MultitrackSet x = set3(); Stem b; b.name = "Drums"; x.stems.push_back (b); Stem c; c.name = "Click Guide"; c.bus = Bus::Cue; x.stems.push_back (c);
      CHECK (stemGain (x, 1) == 1.0f); x.stems[1].solo = true; CHECK (stemGain (x, 0) == 0 && stemGain (x, 1) == 1.0f); x.stems[1].solo = false;
      CHECK (muteStemsMatching (x, "keys") == 1 && x.stems[0].mute && stemGain (x, 0) == 0);
      float l, r; panGains (0, l, r); CHECK (std::abs (l - 1) < 1e-5 && std::abs (r - 1) < 1e-5); panGains (-1, l, r); CHECK (r < 1e-5);
      CHECK (guessStemColor ("BGV") == "pink" && guessStemColor ("Bass") == "purple" && stemNameFromFile ("/x/03 - Keys_Pad.wav") == "Keys Pad");
      // mute persistence: saved with the song, independent per song
      Song s; s.id = "s1"; s.chart = w; s.multitrack = x; Song back = songFromJson (toJson (s));
      CHECK (back.multitrack.stems.size() == 3 && back.multitrack.stems[0].mute && ! back.multitrack.stems[1].mute && back.multitrack.stems[2].bus == Bus::Cue);
      CHECK (mutedStemNames (back.multitrack) == std::vector<std::string> { "Keys Pad" });
      CHECK (back.multitrack.sectionBeats.size() == 3 && back.multitrack.sectionBeats[1] == 16);
      Library L; L.songs = { s }; Song o; o.id = "s2"; o.multitrack = set3(); L.songs.push_back (o);
      Library L2 = libraryFromJson (toJson (L)); CHECK (L2.song ("s1")->multitrack.stems[0].mute && ! L2.song ("s2")->multitrack.stems[0].mute); }
    std::printf ("-- player: streamed reads match the source; routing to the cue bus only; splice crossfade sums to ~unity\n");
    { MultitrackPlayer p; p.prepare (FS); MultitrackSet x = set3(); Stem c; c.name = "Guide"; c.bus = Bus::Cue; x.stems.push_back (c);
      std::vector<std::unique_ptr<StemSource>> src; src.push_back (ramp (24000)); src.push_back (ramp (24000));
      CHECK (p.load (x, std::move (src), buildSequence (x, w, w), true));
      p.play(); std::vector<float> L (100), R (100), Q (100);
      p.process (L.data(), R.data(), Q.data(), 100); CHECK (std::abs (L[50] - 50e-5f) < 1e-6 && std::abs (Q[50] - 50e-5f) < 1e-6);
      x.stems[0].mute = true; p.setMix (x); for (int i = 0; i < 3; ++i) { std::fill (L.begin(), L.end(), 0.f); p.process (L.data(), R.data(), Q.data(), 100); }
      CHECK (L[99] == 0.0f); CHECK (p.underruns() == 0);
      p.locate (2); std::fill (Q.begin(), Q.end(), 0.f); p.process (nullptr, nullptr, Q.data(), 100);
      CHECK (p.current() == 2); CHECK (Q[99] > 0.15f && Q[99] < 0.17f);   // after the 10-sample crossfade: reading ~16000+
      Transport t = p.transport(); CHECK (t.loaded && t.playing && t.section == 2); p.unload(); CHECK (! p.loaded()); }
    std::printf ("-- WAV streaming from disk with cue markers (threaded reader)\n");
    { std::vector<float> l (30000), r (30000); for (int i = 0; i < 30000; ++i) { l[(size_t) i] = (float) i / 30000.0f; r[(size_t) i] = -l[(size_t) i]; }
      const std::string f = "/tmp/wr_v014_stem.wav"; CHECK (writeWavFloat (f, l, r, FS, { { 0.0, "Verse 1" }, { 8.0, "chorus" }, { 16.0, "Bridge" } }));
      std::string err; auto ws = WavFileSource::open (f, &err); CHECK (ws && err.empty()); if (! ws) { std::printf ("wav: %s\n", err.c_str()); return 1; } CHECK (ws->frames() == 30000 && ws->sampleRate() == FS);
      CHECK (ws->markers().size() == 3 && ws->markers()[1].label == "chorus");
      MultitrackSet x = set3(); x.sectionBeats.clear(); CHECK (applyFileMarkers (x, w, ws->markers()) == 3); CHECK (x.sectionBeats[1] == 16 && x.sectionBeats[2] == 32);
      float a[4], b[4]; ws->read (29998, 4, a, b); CHECK (std::abs (a[1] - 29999 / 30000.0f) < 1e-6 && b[1] < 0 && a[2] == 0);
      MultitrackPlayer p; p.prepare (FS); std::vector<std::unique_ptr<StemSource>> src; src.push_back (std::move (ws));
      CHECK (p.load (x, std::move (src), buildSequence (x, w, w), false)); p.play();
      std::vector<float> L (256), R (256); p.process (L.data(), R.data(), nullptr, 256); CHECK (std::abs (L[100] - 100 / 30000.0f) < 1e-6);
      p.unload(); std::remove (f.c_str()); }
    CHECK (! WavFileSource::open ("/nonexistent.wav"));
    std::printf ("-- live glue: count-in preroll, stems drive the chart, footswitch jumps + vamp, lock\n");
    { Library lib = seedLibrary(); Song s; s.id = "mt"; s.chart = w; s.chart.bpm = 120; s.multitrack = set3(); lib.songs.push_back (s);
      Setlist set; set.name = "Sun"; set.date = "2026-11-08"; SetlistEntry e; e.songId = "mt"; set.entries = { e }; lib.setlists = { set };
      Sink sink; LiveSession live (lib, sink); live.start (set, 0, 0);
      MultitrackPlayer p; p.prepare (FS); MultitrackLive ml (p); sink.ml = &ml;
      Opener open = [] (const std::string&) { return ramp (24000); };
      CHECK (ml.loadSong (lib.song ("mt"), live, open, false, true) == "Backing tracks need a subscription" && ! ml.active());
      CHECK (ml.loadSong (lib.song ("mt"), live, open, true, true).empty() && ml.active());
      Action play; play.type = Action::Play; CHECK (! ml.handle (play, live, 0, true)); live.handle (play, 0);   // count-in 8 beats = 4 s
      ml.tick (live, 0); std::vector<float> L (1000);
      for (int i = 0; i < 4; ++i) p.process (L.data(), nullptr, nullptr, 1000); CHECK (p.playing() && p.transport().beat < 0.01);
      for (int i = 0; i < 9; ++i) p.process (L.data(), nullptr, nullptr, 1000); ml.tick (live, 1); CHECK (p.current() == 1 && live.sectionIndex() == 1);
      Action nx; nx.type = Action::TracksNext; CHECK (ml.handle (nx, live, 1, true)); CHECK (live.sectionPending() && ! sink.said.empty());
      for (int i = 0; i < 3; ++i) p.process (L.data(), nullptr, nullptr, 1000); ml.tick (live, 2); CHECK (p.current() == 2 && live.sectionIndex() == 2);
      Action lp; lp.type = Action::TracksLoop; ml.handle (lp, live, 2, true); for (int i = 0; i < 2; ++i) p.process (L.data(), nullptr, nullptr, 1000); CHECK (p.loopSection() == 2);
      for (int i = 0; i < 12; ++i) p.process (L.data(), nullptr, nullptr, 1000); ml.tick (live, 3); CHECK (p.playing() && p.current() == 2);
      Action st; st.type = Action::TracksStop; CHECK (ml.handle (st, live, 3, true)); p.process (L.data(), nullptr, nullptr, 100); CHECK (! p.playing() && ! live.playing());
      CHECK (ml.handle (nx, live, 4, false)); // locked: consumed, nothing plays
      // arrangement edit re-sequences without unloading
      live.setArrangement ({ "C", "B" }, true); ml.arrangementChanged (lib.song ("mt"), live); p.process (L.data(), nullptr, nullptr, 10); CHECK (ml.active()); }
    std::printf ("-- sync: leader's transport goes out as mt; followers keep it (no stems in the snapshot)\n");
    { Library lib = seedLibrary(); Song s; s.id = "mt"; s.chart = w; s.multitrack = set3(); lib.songs.push_back (s);
      Setlist set; set.name = "Sun"; set.date = "2026-11-08"; SetlistEntry e; e.songId = "mt"; set.entries = { e }; lib.setlists = { set };
      Sink sink; LiveSession live (lib, sink); live.start (set, 0, 0); sync::FakeClock clk; sync::Leader ld ("S1");
      ld.update (live, clk, 0); auto snap = ld.snapshotLine (live, clk, 0);
      CHECK (snap.find ("Keys Pad") == std::string::npos);                    // stems stay on the leader
      Transport t; t.loaded = t.playing = true; t.section = 1; t.loop = 1; ld.setTransport (t); auto ev = ld.update (live, clk, 1);
      bool sawMt = false; for (auto& l : ev) sawMt |= l.find ("\"t\":\"mt\"") != std::string::npos; CHECK (sawMt);
      CHECK (ld.update (live, clk, 2).empty());                               // no change, no event
      Library flib = seedLibrary(); Sink fs; LiveSession flive (flib, fs); std::vector<std::string> sent;
      sync::Follower fo ([&] (const std::string& l) { sent.push_back (l); }); fo.onConnected();
      fo.onLine (ld.snapshotLine (live, clk, 2), flive, flib, clk, 2); CHECK (fo.transport.playing && fo.transport.loop == 1);
      CHECK (flib.song ("mt") && flib.song ("mt")->multitrack.empty());
      t.loop = -1; ld.setTransport (t); for (auto& l : ld.update (live, clk, 3)) fo.onLine (l, flive, flib, clk, 3); CHECK (fo.transport.loop == -1 && fo.transport.section == 1);
      sync::Msg mm; CHECK (sync::decode (sync::encode ("mt", 5, toJson (t)), mm) && transportFromJson (mm.body).section == 1); }
    std::printf ("-- health warnings\n");
    { Load l; CHECK (loadWarning (l).empty()); l.underruns = 2; CHECK (loadWarning (l).find ("storage") != std::string::npos); l = {}; l.cpu = 0.9; CHECK (! loadWarning (l).empty());
      l = {}; l.rateMismatch = true; CHECK (loadWarning (l).find ("sample rate") != std::string::npos); }
    std::printf ("v0.14 multitrack: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
