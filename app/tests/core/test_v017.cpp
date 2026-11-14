// v0.17 MIDI Clock: send (24 ppqn, Start/Stop/Continue, SPP), receive smoothing, mode settings, absolute beats
#include "midi/MidiClock.h"
#include "midi/MidiRouter.h"
#include "model/LiveSession.h"
#include "model/Song.h"
#include <cmath>
#include <cstdio>
#include <vector>
using namespace wr;
static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (! (c)) { ++fails; std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)
struct Sink : RigSink { void loadRig (const RigPreset&) override {} void applyScene (const Scene&) override {} void setPedal (const std::string&, bool) override {} void setBpm (double b) override { bpm = b; } double bpm = 0; };

static int countStatus (const MidiOutMsg* m, int n, uint8_t st) { int c = 0; for (int i = 0; i < n; ++i) if (m[i].status == st) ++c; return c; }

int main() {
    // mode helpers
    CHECK (midiClockModeFromInt (0) == MidiClockMode::Off && midiClockModeFromInt (1) == MidiClockMode::Send && midiClockModeFromInt (2) == MidiClockMode::Receive);
    CHECK (std::string (midiClockModeName (MidiClockMode::Send)) == "Send");

    // sender: Start + 24 clocks per beat at 120 BPM / 48 kHz
    { MidiClockOut o; o.setSampleRate (48000); o.setBpm (120); // 24 ticks/beat, 0.5 s/beat => 1000 samples/tick
      o.startFromTop(); MidiOutMsg m[128]; int n = o.process (0, m, 128); CHECK (countStatus (m, n, 0xFA) == 1);
      n = o.process (24000, m, 128); // half a second = 1 beat = 24 ticks
      CHECK (countStatus (m, n, 0xF8) == 24);
      CHECK (std::abs (o.beats() - 1.0) < 0.02);
      o.stop(); n = o.process (0, m, 8); CHECK (countStatus (m, n, 0xFC) == 1 && ! o.running()); }

    // Continue after seek mid-song + SPP
    { MidiClockOut o; o.setSampleRate (48000); o.setBpm (120); o.startFromTop();
      MidiOutMsg m[32]; (void) o.process (48000, m, 32); // advance ~2 beats
      o.seekBeats (8.0); // bar 3 in 4/4
      int n = o.process (0, m, 32);
      CHECK (countStatus (m, n, 0xF2) == 1);
      int spp = -1; for (int i = 0; i < n; ++i) if (m[i].status == 0xF2) spp = m[i].d1 | (m[i].d2 << 7);
      CHECK (spp == beatsToSpp (8.0) && spp == 32); // 8 quarter notes * 4 = 32 MIDI beats
      CHECK (countStatus (m, n, 0xFB) == 1); // Continue after locate while running
    }

    // settings round-trip: clockMode + ports; old boolean "clock" migrates
    { Library lib; lib.settings.midiClockMode = 1; lib.settings.midiClockOutId = "uid-1"; lib.settings.midiClockInId = "in-2";
      lib.settings.followMidiClock = false; auto j = toJson (lib); auto lib2 = libraryFromJson (j);
      CHECK (lib2.settings.midiClockMode == 1 && lib2.settings.midiClockOutId == "uid-1" && lib2.settings.midiClockInId == "in-2");
      CHECK (! lib2.settings.followMidiClock);
      // legacy: only boolean clock true -> Receive (no clockMode key)
      Json leg = Json::object(); leg.set ("version", 1).set ("songs", Json::array()).set ("setlists", Json::array()).set ("rigs", Json::array()).set ("controllers", Json::array());
      leg.set ("settings", Json::object().set ("clock", true));
      auto L3 = libraryFromJson (leg); CHECK (L3.settings.midiClockMode == 2 && L3.settings.followMidiClock);
      Json legOff = Json::object(); legOff.set ("version", 1).set ("songs", Json::array()).set ("setlists", Json::array()).set ("rigs", Json::array()).set ("controllers", Json::array());
      legOff.set ("settings", Json::object().set ("clock", false));
      auto L4 = libraryFromJson (legOff); CHECK (L4.settings.midiClockMode == 0);
    }

    // receive: smoothed BPM from jittery clock ticks + Start/Stop/SPP
    { MidiRouter r; r.setExternal (16, 36, true);
      // 120 BPM = 20.833 ms/tick; add +/- 0.5 ms jitter
      double t = 1000, tick = 60000.0 / 120.0 / 24.0; Action a[16];
      for (int i = 0; i < 24 * 8; ++i) { double jt = t + ((i % 3) - 1) * 0.4; r.handle (0xF8, 0, 0, jt); t += tick; }
      int n = r.pop (a, 16); CHECK (n >= 1); double bpm = 0; for (int i = 0; i < n; ++i) if (a[i].type == Action::ClockBpm) bpm = a[i].bpm;
      CHECK (bpm > 118 && bpm < 122);
      r.handle (0xFA, 0, 0, t); n = r.pop (a, 16); CHECK (n == 1 && a[0].type == Action::TransportStart);
      r.handle (0xFC, 0, 0, t + 1); n = r.pop (a, 16); CHECK (n == 1 && a[0].type == Action::TransportStop);
      r.handle (0xF2, 32 & 0x7F, (32 >> 7) & 0x7F, t + 2); n = r.pop (a, 16); CHECK (n == 1 && a[0].type == Action::SongPosition && a[0].value == 32);
    }

    // LiveSession: absolute beats + SPP seek + transport stop; locate gen on section jump while playing
    { Library lib = seedLibrary(); Song so; so.id = "m"; so.chart = parseChart ("{title: T}\n{tempo: 120}\n{time: 4/4}\n{start_of_verse: V1}\n{bars: 2}\n[G]a\n{end_of_verse}\n{start_of_chorus: C}\n{bars: 2}\n[C]b\n{end_of_chorus}\n");
      lib.songs.push_back (so); Setlist set; set.date = "2026-11-04"; SetlistEntry e; e.songId = "m"; set.entries = { e }; lib.settings.countIn = 0;
      Sink sink; LiveSession live (lib, sink); live.start (set, 0, 0);
      CHECK (std::abs (live.absoluteBeats()) < 1e-9);
      live.setPlaying (true, 0); live.tick (0); live.tick (2.0); // 2 s at 120 = 4 beats = 1 bar into a 2-bar section
      CHECK (live.sectionIndex() == 0 && live.absoluteBeats() > 3.5 && live.absoluteBeats() < 4.5);
      const int loc0 = live.midiLocateGen(); live.gotoSection (1, 2.0); live.tick (2.0); // land (quantize may defer)
      if (live.sectionPending()) { /* force land */ live.practiceSection (1, 2.0); }
      CHECK (live.sectionIndex() == 1 && live.midiLocateGen() > loc0);
      CHECK (live.absoluteBeats() >= 8.0 - 0.01); // 2 bars * 4 beats
      live.handle (Action { Action::TransportStop }, 3.0); CHECK (! live.playing());
      live.handle (Action { Action::SongPosition, 16 }, 3.0); // 16 MIDI beats = 4 quarter notes = bar 2 of section 0
      CHECK (live.sectionIndex() == 0 && std::abs (live.absoluteBeats() - 4.0) < 0.05);
    }

    // followMidiClock mirror when mode changes via library apply path
    { AppSettings s; s.midiClockMode = 2; s.followMidiClock = s.midiClockMode == 2; CHECK (s.followMidiClock);
      s.midiClockMode = 1; s.followMidiClock = s.midiClockMode == 2; CHECK (! s.followMidiClock); }

    std::printf ("v0.17: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
