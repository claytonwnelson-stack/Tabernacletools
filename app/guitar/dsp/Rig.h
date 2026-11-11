// Bearing Live - the two-lane rig engine (BEFORE AMP / AFTER AMP), routing, trims, tuner, scenes.
// Threading: the UI/message thread builds a Chain and calls setChain(); it posts Commands with post().
// The audio thread calls process(). No locks or allocation on the audio thread.
#pragma once
#include "Pedal.h"
#include "audio/PitchTracker.h"
#include "audio/ClickEngine.h"
#include <atomic>
#include <array>
#include <limits>
#include <chrono>

namespace wr {

// v0.12 CPU guard for amp/drive oversampling. Fed the rig's DSP load (time spent / block duration) every block.
// Steps the factor DOWN (8 -> 4 -> 2 -> Off) after `downAfterSec` above `high`; steps back UP towards the user's choice
// after `upAfterSec` below `low`. Pure arithmetic, audio-thread safe.
struct OversamplingGovernor {
    int user = 4, current = 4; bool autoOn = true;
    float high = 0.70f, low = 0.35f, downAfterSec = 1.0f, upAfterSec = 30.0f;
    double hi = 0, lo = 0;
    void setUser (int f, bool aut) { user = f; autoOn = aut; current = f; hi = lo = 0; }
    bool reduced() const { return current < user; }
    // returns true when `current` changed
    bool update (double load, double blockSec) {
        if (! autoOn) { if (current != user) { current = user; return true; } return false; }
        if (load > high) { hi += blockSec; lo = 0; } else if (load < low) { lo += blockSec; hi = std::max (0.0, hi - blockSec); } else { hi = std::max (0.0, hi - blockSec); lo = 0; }
        if (hi >= downAfterSec && current > 1) { current /= 2; hi = 0; return true; }
        if (lo >= upAfterSec && current < user) { current *= 2; lo = 0; return true; }
        return false;
    }
};

enum class Lane { Pre = 0, Post = 1 };
enum class Routing { FourCable = 0, TwoCable = 1 };

struct PedalSlot { std::string uid; std::unique_ptr<Pedal> pedal; };
struct Chain {
    std::vector<PedalSlot> lanes[2]; // [0] = before amp, [1] = after amp
    uint32_t generation = 0;
    PedalSlot* find (const std::string& uid, Lane* laneOut = nullptr, int* idxOut = nullptr);
};

class RigEngine {
public:
    RigEngine();
    ~RigEngine();
    void prepare (double sampleRate, int maxBlock);
    double sampleRate() const { return fs_; }
    int maxBlock() const { return maxBlock_; }

    // ---- message thread
    void setChain (std::unique_ptr<Chain> c);      // prepares the chain and hands it to the audio thread
    void collectGarbage();                          // call periodically from the message thread
    bool post (const Command& c) { if (c.type == Command::SetBpm) click.post (c); return ClickEngine::owns (c.type) ? click.post (c) : queue_.push (c); }
    const Chain* currentChain() const { return active_; } // read-only peek for UI (layout only; params are set via post())
    uint32_t generation() const { return gen_; }

    // ---- audio thread. ins/outs are channel pointers. Unused/missing channels may be null.
    // FourCable: in[0] guitar -> PRE -> out[0] (to amp input); in[1] amp loop send -> POST -> out[2],out[3] (loop return L/R)
    // TwoCable:  in[0] guitar -> PRE -> POST -> out[0],out[1]
    void process (const float* const* ins, int numIns, float* const* outs, int numOuts, int n);

    // ---- read-only meters (any thread)
    std::atomic<float> tunerHz { 0 }, tunerConf { 0 }, inLevel { 0 }, outPeakPre { 0 }, outPeakPost { 0 };
    std::atomic<double> bpm { 72.0 };
    std::atomic<int> routingNow { 0 };
    std::atomic<bool> fellBackToTwoCable { false };
    std::atomic<bool> clickConflict { false };    // click pair is also carrying rig audio (e.g. outs 3/4 = loop return in four-cable)
    std::atomic<bool> tailRinging { false };      // an outgoing rig is ringing out
    // v0.12 anti-aliasing: factor in use (may be auto-reduced below the setting), rig DSP load 0..1+
    std::atomic<int> oversamplingNow { 4 }; std::atomic<bool> oversamplingReduced { false }; std::atomic<float> dspLoad { 0 };
    double latencySamples() const;                // message thread: amp/drive oversampling delay of the current chain (base-rate samples)
    void setLoadOverride (double load) { loadOverride_ = load; }   // tests: pretend the DSP took this fraction of the block (< 0 = measure)
    // the shared core click / voice cue / count-in engine, mixed onto its own outputs after the limiter
    ClickEngine click;

private:
    void handle (const Command& c);
    void runLane (std::vector<PedalSlot>& lane, float* L, float* R, int n);
    double fs_ = 48000; int maxBlock_ = 512;
    Chain* active_ = nullptr; std::atomic<Chain*> pending_ { nullptr }, retired_ { nullptr };
    uint32_t gen_ = 0;
    SpscQueue<Command, 2048> queue_;
    Routing routing_ = Routing::FourCable; float trim_[2] { 1, 1 }; float trimSm_[2] { 1, 1 };
    bool tunerOn_ = true, tunerMute_ = false; float muteGain_ = 1;
    PitchTracker tracker_; ProcessContext ctx_;
    std::vector<float> aL_, aR_, bL_, bR_;
    OversamplingGovernor gov_; int osApplied_ = 4; double loadOverride_ = -1;
    void applyOversampling (Chain* c, int f) { if (c) for (auto& lane : c->lanes) for (auto& s : lane) s.pedal->setOversampling (f); }
    float limEnv_[2] { 0, 0 };
    Chain* tail_ = nullptr; float tailGain_ = 0, tailSec_ = 3; std::vector<float> tL_, tR_, uL_, uR_;
};

// Command helpers (message thread)
Command cmdEnable (const Chain& c, const std::string& uid, bool on);
Command cmdParam (const Chain& c, const std::string& uid, const std::string& param, float v, bool* ok = nullptr);
inline Command cmdTrim (Lane l, float db) { Command c; c.type = Command::SetTrim; c.lane = (uint8_t) l; c.value = db; return c; }
inline Command cmdRouting (Routing r) { Command c; c.type = Command::SetRouting; c.value = (float) (int) r; return c; }
inline Command cmdOversampling (int factor, bool autoReduce) { Command c; c.type = Command::SetOversampling; c.value = (float) factor; c.param = autoReduce ? 1 : 0; return c; }
inline Command cmdTuner (bool on, bool mute) { Command c; c.type = mute ? Command::SetTunerMute : Command::SetTuner; c.value = on ? 1.0f : 0.0f; return c; }

} // namespace wr
