// v0.7 practice track player (core: every app). Plays a decoded song recording on the main outs with
//  * speed 0.5..1.5 without pitch change (WSOLA time-stretch: overlap-add of Hann frames, each frame placed where it best
//    matches the natural continuation of the previous one - no FFT, cheap enough for an iPad next to the rig)
//  * an A-B loop (loop a section) and seek
//  * position() for chart following (section markers -> LiveSession::practiceSection)
// Threading: setTrack / control setters on the message thread; process() on the audio thread. A loaded track is
// immutable and shared (std::shared_ptr swapped under a tiny spin flag the audio thread only try-locks).
#pragma once
#include <atomic>
#include <memory>
#include <vector>
#include <cmath>
#include <algorithm>

namespace wr {

struct PracticeAudio { std::vector<float> left, right; double sampleRate = 44100; double seconds() const { return sampleRate > 0 ? (double) left.size() / sampleRate : 0; } };

class PracticePlayer {
public:
    void prepare (double outputRate) { fs_ = outputRate > 0 ? outputRate : 48000; fifoL_.reserve (16384); fifoR_.reserve (16384); olaL_.assign (kFrame, 0.0f); olaR_.assign (kFrame, 0.0f); reset_.store (true); } // audio stopped
    void setTrack (std::shared_ptr<const PracticeAudio> t);   // message thread; stops playback, position 0
    bool hasTrack() const { return hasTrack_.load(); }
    double length() const { return length_.load(); }
    void play (bool on) { playing_.store (on && hasTrack()); }
    bool playing() const { return playing_.load(); }
    void setSpeed (double s) { speed_.store (std::clamp (s, 0.5, 1.5)); }
    double speed() const { return speed_.load(); }
    void setGain (float g) { gain_.store (g); }
    void seek (double sec) { seekTo_.store (std::max (0.0, sec)); }
    void setLoop (double a, double b) { loopA_.store (a); loopB_.store (b > a + 0.05 ? b : -1.0); } // b <= a: loop off
    void clearLoop() { loopB_.store (-1.0); }
    bool looping() const { return loopB_.load() > 0; }
    double position() const { return pos_.load(); }        // seconds into the recording (what you hear, +-1 frame)
    // audio thread: ADDS into l / r (r may be null = mono)
    void process (float* l, float* r, int n);
    static constexpr int kFrame = 2048, kHop = 512, kSearch = 384;   // at the track's rate
private:
    void renderFrame (const PracticeAudio& t);               // one WSOLA hop -> kHop samples into fifo_
    double readTrackPos() const { return anaPos_; }
    std::shared_ptr<const PracticeAudio> pending_, cur_; std::atomic<bool> lock_ { false }, swap_ { false }, hasTrack_ { false }, reset_ { true };
    std::atomic<bool> playing_ { false }; std::atomic<double> speed_ { 1.0 }, seekTo_ { -1.0 }, loopA_ { 0.0 }, loopB_ { -1.0 }, pos_ { 0.0 }, length_ { 0.0 };
    std::atomic<float> gain_ { 1.0f };
    double fs_ = 48000;
    // WSOLA state (track rate)
    double anaPos_ = 0; long long prevStart_ = -1; std::vector<float> olaL_, olaR_, win_;
    std::vector<float> fifoL_, fifoR_; size_t fifoRead_ = 0; double frac_ = 0; // output resampler (track rate -> fs)
};

} // namespace wr
