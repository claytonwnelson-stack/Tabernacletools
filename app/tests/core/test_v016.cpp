// v0.16 tests: voice cue manifest completeness, hymn fixes, CCLI per-song totals, tempo map editing, auto-advance deferral
#include "audio/VoiceManifest.h"
#include "audio/Voice.h"
#include "model/Hymns.h"
#include "model/Ccli.h"
#include "model/Multitrack.h"
#include "model/Chart.h"
#include "model/LiveSession.h"
#include "audio/MultitrackPlayer.h"
#include "audio/MultitrackLive.h"
#include <cstdio>
#include <fstream>
#include <iterator>
using namespace wr;
static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (! (c)) { ++fails; std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)
struct Sink : RigSink { void loadRig (const RigPreset&) override {} void applyScene (const Scene&) override {} void setPedal (const std::string&, bool) override {} void setBpm (double) override {} bool cueDue (int) override { return true; } };
static const char* kSong = "{title: T}\n{key: G}\n{tempo: 120}\n{time: 4/4}\n{start_of_verse: Verse 1}\n{x_bars: 2}\n[G]one\n{end_of_verse}\n{start_of_chorus: Chorus}\n[C]two\n{end_of_chorus}\n";
int main() {
    // voice manifest: every cue word has a bundled WAV that decodes
    for (int i = 0; i < voice::kNumCueWords; ++i) { std::ifstream f (std::string (WR_SOURCE_DIR) + "/resources/voice/" + voice::kCueWords[i] + ".wav", std::ios::binary);
        std::vector<char> d ((std::istreambuf_iterator<char> (f)), {}); std::vector<float> m; double sr = 0;
        CHECK (! d.empty() && decodeWav ((const uint8_t*) d.data(), d.size(), m, sr) && ! m.empty()); }
    CHECK (voice::kNumCueWords >= 45);
    // hymn fixes
    for (auto& h : hymnLibrary()) { if (h.id == "christ-the-lord-is-risen") CHECK (h.chordPro.find ("boasting") == std::string::npos && h.chordPro.find ("O [C]grave") != std::string::npos);
        if (h.id == "o-for-a-thousand-tongues") CHECK (h.chordPro.find ("(1839) (1839)") == std::string::npos); }
    // tempo map editing
    { mt::TempoMap t = mt::TempoMap::constant (120); mt::setTempoAt (t, 32.4, 60); CHECK (t.pts.size() == 2 && t.pts[1].beat == 32);
      mt::setTempoAt (t, 32, 90); CHECK (t.pts.size() == 2 && t.pts[1].bpm == 90);
      CHECK (mt::tempoMapText (t, 4) == "Bar 1: 120 BPM - Bar 9: 90 BPM");
      CHECK (mt::removeTempoPoint (t, 1) && ! mt::removeTempoPoint (t, 0));
      mt::MultitrackSet m; m.tempo = mt::TempoMap::constant (120); m.sectionBeats = { 0, 16, 48 };
      mt::editTempoKeepingMarkers (m, [] (mt::TempoMap& tm) { mt::setTempoAt (tm, 32, 60); });
      CHECK (std::abs (m.sectionBeats[1] - 16) < 1e-9); CHECK (std::abs (m.tempo.secAt (m.sectionBeats[2]) - 24.0) < 1e-9); CHECK (std::abs (m.sectionBeats[2] - 40) < 1e-9); }
    // streaming WAV writer (block by block) round-trips audio + markers
    { const std::string f = "/tmp/wr_v016.wav"; mt::WavFloatWriter w; CHECK (w.open (f, 1000));
      std::vector<float> l (300), r (300); for (int b = 0; b < 10; ++b) { for (int i = 0; i < 300; ++i) { l[(size_t) i] = (float) (b * 300 + i) / 3000.0f; r[(size_t) i] = -l[(size_t) i]; } CHECK (w.write (l.data(), r.data(), 300)); }
      CHECK (w.frames() == 3000 && w.close ({ { 1.0, "Verse 1" }, { 2.0, "Chorus" } }));
      std::string err; auto src = mt::WavFileSource::open (f, &err); CHECK (src && src->frames() == 3000 && src->markers().size() == 2 && src->markers()[1].label == "Chorus");
      if (src) { float a[2], b[2]; src->read (2999, 2, a, b); CHECK (std::abs (a[0] - 2999 / 3000.0f) < 1e-6 && b[0] < 0 && a[1] == 0); }
      std::remove (f.c_str()); }
    // arrangement re-sequence while playing: handed to the audio thread, no dropped block
    { Chart w = parseChart ("{title: T}\n{start_of_verse: Verse 1}\nx\n{end_of_verse}\n{start_of_chorus: Chorus}\ny\n{end_of_chorus}\n");
      mt::MultitrackSet m; m.sampleRate = 1000; m.tempo = mt::TempoMap::constant (120); m.lengthSec = 16; m.sectionBeats = { 0, 16 }; m.stems.push_back ({});
      auto ms = std::make_unique<mt::MemorySource>(); ms->rate = 1000; ms->L.assign (16000, 0.5f); ms->R = ms->L;
      std::vector<std::unique_ptr<mt::StemSource>> src; src.push_back (std::move (ms));
      mt::MultitrackPlayer p; p.prepare (1000); CHECK (p.load (m, std::move (src), mt::buildSequence (m, w, w), true)); p.play();
      std::vector<float> L (100); p.process (L.data(), nullptr, nullptr, 100);
      p.setSequence (mt::buildSequence (m, w, arrangedChart (w, { "V1", "C", "C" }))); std::fill (L.begin(), L.end(), 0.f);
      p.process (L.data(), nullptr, nullptr, 100); CHECK (L[50] > 0.49f);               // audio kept flowing through the swap
      int segs = 0; (void) segs; p.unload(); }
    // stems drive the chart: no beat-based auto-advance and no self-landing of cues while tracks play
    { Library lib = seedLibrary(); Song so; so.id = "x"; so.chart = parseChart (kSong); so.chart.sections[0].bars = 2; lib.songs.push_back (so);
      Setlist set; set.name = "S"; set.date = "2026-11-08"; SetlistEntry e; e.songId = "x"; set.entries = { e }; lib.settings.countIn = 0;
      Sink sink; LiveSession live (lib, sink); live.start (set, 0, 0); live.setPlaying (true, 0);
      live.setTracksDrive (true); live.tick (0.1); live.tick (10.0); CHECK (live.sectionIndex() == 0);   // 2 bars at 120 = 4 s: would have advanced
      live.followCue (1, 1, false); live.tick (10.1); CHECK (live.sectionPending() && live.sectionIndex() == 0);
      live.practiceSection (1, 10.2); CHECK (live.sectionIndex() == 1 && ! live.sectionPending());
      live.setTracksDrive (false); live.gotoSection (0, 11); live.tick (11.0); live.tick (16.0); CHECK (live.sectionIndex() == 1); }
    // CCLI per-song totals (CCLI's Search & Report flow: song #, title, authors, times used, digital)
    { Library lib; Song a; a.id = "a"; a.chart.title = "Song A"; a.ccli.number = "123"; a.ccli.authors = "X"; lib.songs.push_back (a); Song b; b.id = "b"; b.chart.title = "Song B"; lib.songs.push_back (b);
      ccli::logUsage (lib, "a", "2026-11-01", "S", true, true); ccli::logUsage (lib, "a", "2026-11-08", "S", false, true); ccli::logUsage (lib, "b", "2026-11-08", "S", true, false);
      const auto t = ccli::exportTotalsCsv (lib, {});
      CHECK (t.rfind ("CCLI Song Number,Title,Authors,Times Used,Digital (Lyrics Projected),Charts Displayed,First Used,Last Used\r\n", 0) == 0);
      CHECK (t.find ("123,Song A,X,2,1,2,2026-11-01,2026-11-08\r\n") != std::string::npos && t.find (",Song B,,1,1,0,2026-11-08,2026-11-08") != std::string::npos);
      CHECK (t.find ("Song A") < t.find ("Song B"));
      CHECK (ccli::exportSummary (lib, {}).find ("Search & Report") != std::string::npos); }
    std::printf ("v0.16: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
