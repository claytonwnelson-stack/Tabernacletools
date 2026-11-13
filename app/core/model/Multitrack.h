// v0.14 multitrack backing tracks (shared core: every app). Per-song stems (Prime-style): import, per-track mix, routing to
// the main outs or the Out 2 click/cue bus (in-ears only), remembered mutes, a tempo map, section markers on the track
// timeline, arrangement re-sequencing with short crossfades, quantised jumps and vamp loops.
// This header is the pure model + sequencer (no audio I/O, no threads): fully testable on Linux.
// Streaming + mixing live in audio/MultitrackPlayer.h.
#pragma once
#include "Json.h"
#include <string>
#include <vector>
#include <cstdint>

namespace wr { struct Chart; }

namespace wr::mt {

constexpr int kMaxStems = 16;                 // stereo stems per song (2017 iPad Pro disk + CPU budget)
constexpr double kCrossfadeSec = 0.010;       // splice crossfade for arrangement edits, jumps and loops

enum class Bus : uint8_t { Main = 0, Cue = 1 };   // Main = outs 1/2 (front of house); Cue = Out 2 click/cue bus (in-ears only)

struct Stem {
    std::string id, name, file, color;          // file: copy inside the app container (<data>/Multitracks/<song>/...)
    std::string sharedSession;                  // non-empty: leader audio, playable only while connected to that session
    float volumeDb = 0, pan = 0;                // pan -1..1
    bool mute = false, solo = false;
    Bus bus = Bus::Main;
};

struct TempoPoint { double beat = 0, bpm = 120; };
// Tempo map of the recording. offsetSec = where beat 0 (the first downbeat of the first section) is in the files.
struct TempoMap {
    double offsetSec = 0; std::vector<TempoPoint> pts;   // sorted by beat; empty = 120 BPM
    double bpmAt (double beat) const;
    double secAt (double beat) const;                    // file seconds of a beat
    double beatAt (double sec) const;                    // inverse
    static TempoMap constant (double bpm, double offsetSec = 0) { TempoMap t; t.offsetSec = offsetSec; t.pts.push_back ({ 0, bpm }); return t; }
};

// v0.16 tempo-map editing (Tracks > Tempo map...). Points snap to whole beats; there is always at least one point.
void setTempoAt (TempoMap&, double beat, double bpm);       // add, or change the point at that beat
bool removeTempoPoint (TempoMap&, size_t index);            // false for the only point
std::string tempoMapText (const TempoMap&, int beatsPerBar);// "Bar 1: 72 BPM - Bar 33: 76 BPM"

struct MultitrackSet {
    std::vector<Stem> stems;
    TempoMap tempo;
    int beatsPerBar = 4;
    std::vector<double> sectionBeats;           // per WRITTEN section: start beat on the track timeline (-1 = not marked)
    double lengthSec = 0;                       // longest stem
    double sampleRate = 48000;                  // stems are converted to this rate on import
    bool empty() const { return stems.empty(); }
};
// edit the tempo map while keeping the section markers where they are IN THE AUDIO (they were set by ear / from file markers)
template <class F> void editTempoKeepingMarkers (MultitrackSet& m, F&& edit) {
    std::vector<double> secs; for (double b : m.sectionBeats) secs.push_back (b >= 0 ? m.tempo.secAt (b) : -1.0);
    edit (m.tempo);
    for (size_t i = 0; i < secs.size(); ++i) if (secs[i] >= 0) m.sectionBeats[i] = m.tempo.beatAt (secs[i]);
}
Json toJson (const MultitrackSet&);
MultitrackSet multitrackFromJson (const Json&);

// any stem soloed -> only soloed stems sound. Mute always wins.
bool stemAudible (const MultitrackSet&, size_t i);
float stemGain (const MultitrackSet&, size_t i);       // linear incl. mute/solo
void panGains (float pan, float& l, float& r);          // constant power

// remembered mute states per song: just the stems' mute flags (saved with the song). Helpers for "mute what I play live".
std::vector<std::string> mutedStemNames (const MultitrackSet&);
int muteStemsMatching (MultitrackSet&, const std::string& word); // case-insensitive name match ("keys", "pad"); returns count
std::string guessStemColor (const std::string& name);  // drums red, bass purple, keys/pad blue, gtr orange, vox pink, click grey...
std::string stemNameFromFile (const std::string& fileName); // "03 - Keys Pad.wav" -> "Keys Pad"

// file markers (WAV 'cue ' + 'LIST/adtl/labl' or a marker text file) -> section starts. labels matched to section names /
// labels case-insensitively; unmatched markers are ignored. Returns how many sections got a marker.
struct FileMarker { double sec = 0; std::string label; };
int applyFileMarkers (MultitrackSet&, const Chart& written, const std::vector<FileMarker>&);
// sections without a marker: estimate from the previous marked section + its bars (needs Section::bars)
void fillMissingSectionBeats (MultitrackSet&, const Chart& written);

// ---------------------------------------------------------------- sequence (arranged order -> file sample ranges)
struct Segment { int arrIdx = 0, written = 0; int64_t srcStart = 0, srcEnd = 0; int64_t outStart = 0; double startBeat = 0, endBeat = 0; };
// arranged: the live (arranged) chart; Section::source = written index (-1 = as written: same index).
std::vector<Segment> buildSequence (const MultitrackSet&, const Chart& written, const Chart& arranged, std::string* warning = nullptr);

// ---------------------------------------------------------------- transport (single thread: the audio thread in the app)
enum class Quant : uint8_t { Now, NextBar, SectionEnd };
struct Span { int64_t src = 0; int len = 0; float g0 = 1, g1 = 1; int dst = 0; };   // read src..src+len, ramp g0->g1, add at dst in the block

class Sequencer {
public:
    void setSequence (std::vector<Segment> seq, const TempoMap& tempo, int beatsPerBar, double fs); // keeps position by arranged index
    void resequence (std::vector<Segment>&& seq);   // v0.16: same tempo/rate; swaps the vector in (no allocation: audio-thread safe)
    const std::vector<Segment>& sequence() const { return seq_; }
    void play (int64_t prerollSamples = 0);      // count-in: stems start after prerollSamples of silence
    void stop() { playing_ = false; fadeOut_ = 0; }
    bool playing() const { return playing_; }
    void locate (int arrIdx);                    // stopped or playing (playing = immediate crossfaded jump)
    void requestJump (int arrIdx, Quant q);      // live: lands on the next bar line / end of this section, crossfaded
    void cancelJump() { jumpTo_ = -1; }
    int pendingJump() const { return jumpTo_; }
    void setLoop (bool on) { loop_ = on ? cur_ : -1; }   // vamp the CURRENT section until released
    void setLoopSection (int arrIdx) { loop_ = arrIdx; }
    int loopSection() const { return loop_; }
    int current() const { return cur_; }           // arranged section index now sounding (-1 = nothing loaded)
    int64_t srcPos() const { return src_; }
    double beat() const;                           // track-timeline beat of the playhead
    double barInSection() const;                   // 1-based
    bool ended() const { return ended_; }
    int64_t preroll() const { return preroll_; }
    int64_t nextTargetSrc() const;                 // where the next splice will read from (stream read-ahead), -1 = none
    // one audio block of n samples -> spans to read (main voice + crossfade tail). Spans cover dst 0..n.
    int render (int n, Span* out, int maxSpans);
    int sectionChanges() const { return changes_; } // bumps every time current() changes
private:
    int64_t barLen (int64_t src) const;            // samples to the next bar line from src (within the current segment)
    void splice (int to, int64_t toSrc);
    std::vector<Segment> seq_; TempoMap tempo_; int bpb_ = 4; double fs_ = 48000;
    bool playing_ = false, ended_ = false; int cur_ = -1, loop_ = -1, jumpTo_ = -1; Quant jumpQ_ = Quant::NextBar; int64_t jumpAt_ = -1;
    int64_t src_ = 0, preroll_ = 0; int xf_ = 480;
    int64_t fadeSrc_ = 0; int fadeOut_ = 0, fadeIn_ = 0; int changes_ = 0;
};

// leader -> followers transport (rides the existing band sync; followers need no stems)
struct Transport { bool loaded = false, playing = false; int section = -1, loop = -1, pending = -1; double beat = 0;
                   bool operator== (const Transport& o) const { return loaded == o.loaded && playing == o.playing && section == o.section && loop == o.loop && pending == o.pending; } };
Json toJson (const Transport&);
Transport transportFromJson (const Json&);

// health: disk / CPU warnings for the existing battery / temperature banner ("" = fine)
struct Load { int underruns = 0; double cpu = 0; int stems = 0; bool rateMismatch = false; };
std::string loadWarning (const Load&);

} // namespace wr::mt
