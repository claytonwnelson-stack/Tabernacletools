#include "ClickEngine.h"
#include <cmath>
#include <algorithm>

namespace wr {

void ClickEngine::prepare (double sr, int maxBlock) { fs_ = sr; click_.prepare (sr); scratch_.assign ((size_t) std::max (kChunk, maxBlock), 0.0f); }
bool ClickEngine::owns (Command::Type t) {
    switch (t) { case Command::ClickOn: case Command::ClickMute: case Command::ClickDb: case Command::ClickOut: case Command::ClickMeter: case Command::ClickAccent:
        case Command::ClickReset: case Command::CueAnnounce: case Command::CueDb: case Command::CuesOn: case Command::CuesWhenMuted: case Command::CountIn:
        case Command::ClickPreset: case Command::DownbeatDb: return true; default: return false; }
}
bool ClickEngine::post (const Command& c) { return queue_.push (c); }
void ClickEngine::drain() { Command c; while (queue_.pop (c)) handle (c); }
ClickGen::Params ClickEngine::params() const {
    ClickGen::Params p; p.bpm = bpm.load(); p.beatsPerBar = clickBpb_; p.on = clickOn_; p.muted = clickMuted_; p.accent = clickAccent_; p.gain = clickGain_;
    p.cuesOn = cuesOn_; p.cuesWhenMuted = cuesWhenMuted_; p.cueGain = cueGain_; p.preset = clickPreset_; p.downGain = downGain_; return p;
}
void ClickEngine::handle (const Command& c) {
    switch (c.type) {
        case Command::SetBpm: bpm.store (clampf (c.value, 30, 300)); break;
        case Command::ClickOn: { bool on = c.value > 0.5f; if (on && ! clickOn_) click_.reset(); clickOn_ = on; break; }
        case Command::ClickMute: clickMuted_ = c.value > 0.5f; break;
        case Command::ClickDb: clickGain_ = std::pow (10.0f, clampf (c.value, -60, 6) / 20.0f); break;
        case Command::ClickOut: clickOut_ = std::clamp ((int) c.value, 0, 2); break;
        case Command::ClickMeter: clickBpb_ = std::clamp ((int) c.value, 1, 16); break;
        case Command::ClickAccent: clickAccent_ = c.value > 0.5f; break;
        case Command::ClickReset: click_.reset(); break;
        case Command::CueAnnounce: { if (c.slot < 0 || c.slot >= 8) break; auto& v = cueSlots_[(size_t) c.slot];
            click_.announce ({ v.data(), (int) v.size() }, (int) c.generation, params()); cueDueBar.store (click_.dueBar()); cueAck.store ((int) c.generation); break; }
        case Command::CueDb: cueGain_ = std::pow (10.0f, clampf (c.value, -60, 6) / 20.0f); break;
        case Command::CuesOn: cuesOn_ = c.value > 0.5f; break;
        case Command::CuesWhenMuted: cuesWhenMuted_ = c.value > 0.5f; break;
        case Command::ClickPreset: clickPreset_ = std::clamp ((int) c.value, 0, 3); break;
        case Command::DownbeatDb: downGain_ = std::pow (10.0f, clampf (c.value, -12, 12) / 20.0f); break;
        case Command::CountIn: click_.countIn ((int) c.value, c.param == 1, clickBpb_); break;
        default: break;
    }
}
void ClickEngine::processRange (float* const* outs, int numOuts, int off, int n) {
    const int co = clickOut_; float* cl = nullptr; float* cr = nullptr;
    if (co == 0) { cl = numOuts > 0 ? outs[0] : nullptr; cr = numOuts > 1 ? outs[1] : nullptr; }
    else if (co == 1) { cl = numOuts > 2 ? outs[2] : nullptr; cr = numOuts > 3 ? outs[3] : nullptr; }
    else { cl = numOuts > 1 ? outs[1] : nullptr; }
    float* L = cl ? cl + off : (cr ? cr + off : nullptr); float* R = cr ? cr + off : L;
    auto cp = params();
    if (linkBeat_ == linkBeat_) { if (click_.lockTo (linkBeat_, clickBpb_)) linkReseats.fetch_add (1); linkBeat_ = std::numeric_limits<double>::quiet_NaN(); }
    if (L) { click_.process (L, R, n, cp); for (int s = 0; s < n; ++s) { L[s] = clampf (L[s], -1, 1); R[s] = clampf (R[s], -1, 1); } }
    else { // no click output on this device: keep the bar grid (cue timing, count-in) running silently
        for (int o = 0; o < n; o += (int) scratch_.size()) { const int k = std::min ((int) scratch_.size(), n - o); std::fill (scratch_.begin(), scratch_.begin() + k, 0.0f); cp.gain = 0; cp.cueGain = 0; click_.process (scratch_.data(), scratch_.data(), k, cp); } }
    clickBeat.store (click_.beatInBar()); clickBar.store (click_.bar()); countingIn.store (click_.countingIn());
}

} // namespace wr
