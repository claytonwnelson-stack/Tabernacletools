// MIDI routing: two M-Vave Chocolate Plus units (guitar ch1, sections ch2), long-press detection, MIDI learn,
// and external MIDI (e.g. Cubase over a CoreMIDI network session): MIDI Clock tempo, Program Change / notes as section cues.
// Pure C++17, realtime-safe (no allocation in handle()/tick()).
#pragma once
#include "../model/Song.h"
#include <array>

namespace wr {

struct Action {
    enum Type : uint8_t { None, PrevSection, NextSection, JumpChorus, NextSong, PrevSong, Tap, Boost, SwitchC, SwitchD, TunerToggle, ToggleView,
                          GotoSection, ClockBpm, TransportStart, TransportStop, Learned, ClickMute, ClickToggle, Play,
                          BankNext, BankPrev, BankChanged /* v0.8: value = new bank, unit = unit */,
                          TracksPlay, TracksNext, TracksLoop, TracksStop /* v0.14 backing tracks */,
                          SongPosition /* v0.17: MIDI SPP -> value = MIDI beats (16ths) */,
                          PlayStart /* short press: start if stopped; does not stop */ } type = None;
    int value = 0; double bpm = 0; int unit = -1, sw = -1;
};
Action::Type actionFromName (const std::string& n);
const char* actionName (Action::Type t);

class MidiRouter {
public:
    static constexpr int kMaxUnits = 4, kMaxOut = 16;
    void setProfiles (const std::vector<ControllerProfile>& p) { nUnits_ = std::min ((int) p.size(), kMaxUnits); for (int i = 0; i < nUnits_; ++i) units_[(size_t) i] = p[(size_t) i]; for (auto& u : held_) for (auto& h : u) h = {}; for (auto& pnd : pend_) pnd = {}; }
    void setExternal (int channel, int noteBase, bool followClock) { extCh_ = channel; noteBase_ = noteBase; followClock_ = followClock; }
    const ControllerProfile& profile (int i) const { return units_[(size_t) i]; }
    int numProfiles() const { return nUnits_; }
    int bank (int u) const { return u >= 0 && u < nUnits_ ? units_[(size_t) u].bank : 0; }

    // MIDI learn: the next CC / PC / note-on is bound to unit `u`, switch `sw` (long = the separate long-press message)
    void beginLearn (int u, int sw, bool longPress) { learnU_ = u; learnSw_ = sw; learnLong_ = longPress; }
    void cancelLearn() { learnU_ = -1; }
    bool learning() const { return learnU_ >= 0; }

    // feed one short MIDI message. timeMs: monotonic milliseconds.
    // fromUnit >= 0 means the message came from that USB port (factory Chocolate program changes are
    // only recognised on a known port, so two identical pedals stay apart). -1 = merged host buffer.
    void handle (uint8_t status, uint8_t d1, uint8_t d2, double timeMs, int fromUnit = -1);
    void tick (double timeMs);          // call >= every 10 ms so long presses fire while the switch is still held
    void flushFactory (double timeMs);  // release a lone program-change after the A+B / C+D window (~50 ms)
    // drain generated actions
    int pop (Action* out, int max) { int n = std::min (max, nOut_); for (int i = 0; i < n; ++i) out[i] = out_[(size_t) i]; for (int i = n; i < nOut_; ++i) out_[(size_t) (i - n)] = out_[(size_t) i]; nOut_ -= n; return n; }
    double clockBpm() const { return clockBpm_; }

private:
    void emit (Action a) { if (nOut_ < kMaxOut) out_[(size_t) nOut_++] = a; }
    void fire (int u, int sw, bool longP);
    void fireNamed (int u, const std::string& name);
    void noteFactory (int u, int pc, double t);
    bool unitMessage (int u, uint8_t st, uint8_t d1, uint8_t d2, double t, bool ignoreChannel);
    const std::string& act (int u, int sw, bool longP) const { const auto& p = units_[(size_t) u];   // v0.8: the active bank's action
        if (! p.banks.empty()) { const auto& b = p.banks[(size_t) std::clamp (p.bank, 0, (int) p.banks.size() - 1)]; return longP ? b.longAction[sw] : b.action[sw]; }
        return longP ? p.sw[sw].longAction : p.sw[sw].action; }
    struct Held { bool down = false, fired = false; double t = 0; };
    std::array<ControllerProfile, kMaxUnits> units_ {}; int nUnits_ = 0;
    std::array<std::array<Held, 4>, kMaxUnits> held_ {};
    struct Pend { bool on = false; int pc = 0; double t = 0; };
    std::array<Pend, kMaxUnits> pend_ {};
    std::array<Action, kMaxOut> out_ {}; int nOut_ = 0;
    int learnU_ = -1, learnSw_ = 0; bool learnLong_ = false;
    int extCh_ = 16, noteBase_ = 36; bool followClock_ = true;
    int clockCount_ = 0; double clockT0_ = -1, clockBpm_ = 0, clockLast_ = -1, clockBeatMs_ = 0; // beatMs EMA for smooth BPM
};

class TapTempo {
public:
    // returns new bpm or 0 if not enough taps yet
    void reset() { n_ = 0; }
    double tap (double timeSec) {
        if (n_ > 0 && timeSec - t_[(size_t) ((n_ - 1) % 4)] > 2.0) n_ = 0;
        t_[(size_t) (n_ % 4)] = timeSec; ++n_;
        int k = std::min (n_, 4); if (k < 2) return 0;
        double first = t_[(size_t) ((n_ - k) % 4)], last = timeSec; double bpm = 60.0 * (k - 1) / (last - first);
        return clampBpm (bpm);
    }
private: std::array<double, 4> t_ {}; int n_ = 0;
};

} // namespace wr
