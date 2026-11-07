// Core click engine: click track + presets + count-in + spoken section cues + bar grid + Ableton Link phase lock.
// Instrument-agnostic: the Guitar app mixes it after its rig; the Core Test Harness (and later Bass/Keys/Chart apps)
// run it on its own. Threading: message thread post()/stageCue(); audio thread drain() + process*().
#pragma once
#include "Command.h"
#include "Click.h"
#include <limits>
#include <vector>

namespace wr {

class ClickEngine {
public:
    void prepare (double sampleRate, int maxBlock);
    static bool owns (Command::Type t);                          // click / cue / count-in / meter command types
    bool post (const Command& c);                                // message thread
    int stageCue (std::vector<float> phrase) { int s = nextSlot_; nextSlot_ = (nextSlot_ + 1) % 8; cueSlots_[(size_t) s] = std::move (phrase); return s; }
    void setNumbers (const VoiceClip* n13) { click_.setNumbers (n13); }   // while audio is stopped
    // ---- audio thread
    void setLinkBeat (double beatAtBlockStart) { linkBeat_ = beatAtBlockStart; }
    void drain();                                                // apply posted commands
    // mix into the click outputs: clickOut 0 = outs 1/2, 1 = outs 3/4, 2 = out 2 (mono). Samples [off, off+n).
    void processRange (float* const* outs, int numOuts, int off, int n);
    void process (float* const* outs, int numOuts, int n) { drain(); for (int o = 0; o < n; o += kChunk) processRange (outs, numOuts, o, std::min (kChunk, n - o)); }
    int clickOut() const { return clickOut_; }
    bool active() const { return clickOn_ || cuesOn_; }
    // ---- state for the UI / LiveSession (any thread)
    std::atomic<double> bpm { 72.0 };
    std::atomic<int> clickBeat { 0 };             // beat in bar (0 = downbeat)
    std::atomic<long long> clickBar { 0 };        // bar counter of the grid (monotonic)
    std::atomic<int> cueAck { -1 };               // last announce seq the audio thread scheduled
    std::atomic<long long> cueDueBar { 0 };       // bar at which that section change is due
    std::atomic<bool> countingIn { false };
    std::atomic<int> linkReseats { 0 };
private:
    static constexpr int kChunk = 512;
    void handle (const Command& c);
    ClickGen::Params params() const;
    SpscQueue<Command, 1024> queue_;
    double fs_ = 48000; double linkBeat_ = std::numeric_limits<double>::quiet_NaN();
    ClickGen click_; bool clickOn_ = false, clickMuted_ = false, clickAccent_ = true; float clickGain_ = 0.25f; int clickOut_ = 2, clickBpb_ = 4;
    bool cuesOn_ = true, cuesWhenMuted_ = false; float cueGain_ = 0.5f; int clickPreset_ = 0; float downGain_ = 1.66f;
    std::array<std::vector<float>, 8> cueSlots_; int nextSlot_ = 0; std::vector<float> scratch_;
};

} // namespace wr
