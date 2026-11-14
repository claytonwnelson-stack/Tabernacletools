// v0.14 multitrack backing-track player: disk streaming + mixer for up to 16 stereo stems.
//  * Stems are streamed from WAV files in the app container (import converts mp3 / m4a / aiff / other-rate files to
//    32-bit float WAV at the session rate once, so playback never decodes). Nothing is loaded whole into RAM.
//  * A background reader thread keeps a small chunk cache per stem filled ahead of the playhead AND ahead of the next
//    splice target (next arranged section if it is not contiguous, the vamp start, a queued jump), so splices never wait
//    on the disk. Missing data plays as silence and counts an underrun (-> "storage can't keep up" warning).
//  * Mixer: per-stem volume / pan / mute / solo (smoothed), routing to Main L/R or the Out 2 click/cue bus (mono, in-ears).
//  * Transport: wr::mt::Sequencer (sample-accurate splices with 10 ms crossfades, quantised jumps, vamps).
// Threading: load() / control setters on the message thread; process() on the audio thread (no locks, no allocation,
// no file I/O). The reader thread is the only one touching files.
#pragma once
#include "../model/Multitrack.h"
#include "Command.h"
#include <array>
#include <cmath>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace wr::mt {

// a stem's audio. read() fills n frames from `start` (zeros past the end / before 0); called on the reader thread only.
struct StemSource {
    virtual ~StemSource() = default;
    virtual int64_t frames() const = 0;
    virtual double sampleRate() const = 0;
    virtual void read (int64_t start, int n, float* l, float* r) = 0;
};
struct MemorySource : StemSource {           // tests / tiny stems
    std::vector<float> L, R; double rate = 48000;
    int64_t frames() const override { return (int64_t) L.size(); }
    double sampleRate() const override { return rate; }
    void read (int64_t s, int n, float* l, float* r) override;
};
// streaming WAV reader: PCM 16 / 24 / 32-bit int or 32-bit float, mono or stereo (mono -> both sides)
class WavFileSource : public StemSource {
public:
    static std::unique_ptr<WavFileSource> open (const std::string& path, std::string* error = nullptr);
    ~WavFileSource() override { if (f_) std::fclose (f_); }
    int64_t frames() const override { return frames_; }
    double sampleRate() const override { return rate_; }
    void read (int64_t start, int n, float* l, float* r) override;
    const std::vector<FileMarker>& markers() const { return markers_; }   // 'cue ' points + 'labl' names, seconds
private:
    std::FILE* f_ = nullptr; int64_t dataOff_ = 0, frames_ = 0; int ch_ = 0, bits_ = 0; bool float_ = false; double rate_ = 0;
    std::vector<FileMarker> markers_; std::vector<uint8_t> raw_;
};
// writes a 32-bit float stereo WAV (+ optional cue markers). Used by the import path's tests and the offline renders.
// v0.16 streaming import: write a 32-bit float stereo WAV block by block (no whole-file buffer); markers at close
class WavFloatWriter {
public:
    bool open (const std::string& path, double rate);
    bool write (const float* l, const float* r, int n);
    bool close (const std::vector<FileMarker>& markers = {});   // patches the sizes, appends cue / labl chunks
    ~WavFloatWriter() { if (f_) close(); }
    int64_t frames() const { return frames_; }
private:
    std::FILE* f_ = nullptr; double rate_ = 48000; int64_t frames_ = 0; std::vector<float> buf_;
};
bool writeWavFloat (const std::string& path, const std::vector<float>& l, const std::vector<float>& r, double rate, const std::vector<FileMarker>& markers = {});

class MultitrackPlayer {
public:
    static constexpr int kChunk = 16384, kSlots = 12, kAhead = 4, kTargetAhead = 2, kMaxBlock = 1024, kMaxSpans = 64;
    MultitrackPlayer() = default;
    ~MultitrackPlayer() { unload(); }
    void prepare (double outputRate) { fs_ = outputRate > 0 ? outputRate : 48000; }   // audio stopped
    // message thread. `sync` = no reader thread (tests): process() fills the cache inline.
    bool load (const MultitrackSet& set, std::vector<std::unique_ptr<StemSource>> sources, std::vector<Segment> seq, bool sync = false);
    void unload();
    void setSequence (std::vector<Segment> seq);           // arrangement edit while loaded (keeps playing; crossfaded)
    bool loaded() const { return loaded_.load(); }
    int stems() const { return nStems_; }
    bool rateMismatch() const { return mismatch_; }
    // mixer (message thread; smoothed on the audio thread)
    void setMix (const MultitrackSet& set);                // volume / pan / mute / solo / bus of every stem
    void setMasterDb (float db) { master_.store (std::pow (10.0f, db / 20.0f)); }
    // transport (message thread -> audio thread)
    void play (double prerollSec = 0) { post ({ C::Play, 0, (float) prerollSec }); }
    void stop() { post ({ C::Stop }); }
    void locate (int arrIdx) { post ({ C::Locate, arrIdx }); }
    void jump (int arrIdx, Quant q) { post ({ C::Jump, arrIdx, (float) (int) q }); }
    void cancelJump() { post ({ C::CancelJump }); }
    void loop (bool on) { post ({ C::Loop, on ? 1 : 0 }); }
    // state published by the audio thread
    bool playing() const { return playing_.load(); }
    int current() const { return cur_.load(); }
    int loopSection() const { return loopSec_.load(); }
    int pendingJump() const { return pending_.load(); }
    double beat() const { return beat_.load(); }
    double bar() const { return bar_.load(); }
    bool ended() const { return ended_.load(); }
    int sectionChanges() const { return changes_.load(); }
    int underruns() const { return underruns_.load(); }
    void clearUnderruns() { underruns_.store (0); }
    Transport transport() const { Transport t; t.loaded = loaded(); t.playing = playing(); t.section = current(); t.loop = loopSection(); t.pending = pendingJump(); t.beat = beat(); return t; }
    // audio thread: ADDS into main l / r and the mono cue bus (any may be null: that bus is dropped)
    void process (float* mainL, float* mainR, float* cue, int n);
    void fillForTest() { fillOnce(); }                    // tests: one reader pass
private:
    struct C { enum T : uint8_t { None, Play, Stop, Locate, Jump, CancelJump, Loop } t = None; int i = 0; float v = 0; };
    void post (C c) { cmds_.push (c); wake(); }
    void wake() { cv_.notify_one(); }
    void processBlock (float* mainL, float* mainR, float* cue, int n);
    struct Slot { std::atomic<int64_t> chunk { -1 }; std::vector<float> l, r; };
    struct StemData { std::unique_ptr<StemSource> src; std::array<Slot, kSlots> slots; std::atomic<float> gain { 0 }, pan { 0 }; std::atomic<int> bus { 0 }; float curGain = 0, curPan = 0; };
    bool readCached (StemData& s, int64_t src, int len, float* l, float* r);   // audio thread
    void fillOnce();                                                            // reader thread
    void ensure (StemData& s, int64_t chunk, const int64_t* want, int nWant);
    void readerLoop();
    TempoMap tempo_; int bpb_ = 4; double rate_ = 48000;
    double fs_ = 48000; bool sync_ = false, mismatch_ = false;
    std::vector<std::unique_ptr<StemData>> st_; int nStems_ = 0;
    Sequencer seq_; std::vector<Segment> pendingSeq_; std::atomic<bool> seqPending_ { false }, busy_ { false }, loaded_ { false };
    SpscQueue<C, 128> cmds_;
    std::atomic<float> master_ { 1.0f };
    std::atomic<bool> playing_ { false }, ended_ { false }; std::atomic<int> cur_ { -1 }, loopSec_ { -1 }, pending_ { -1 }, changes_ { 0 }, underruns_ { 0 };
    std::atomic<double> beat_ { 0 }, bar_ { 0 }; std::atomic<int64_t> pos_ { 0 }, target_ { -1 };
    std::thread reader_; std::mutex m_; std::condition_variable cv_; std::atomic<bool> quit_ { false };
    std::array<Span, kMaxSpans> spans_ {}; std::vector<float> tl_, tr_;
};

} // namespace wr::mt
