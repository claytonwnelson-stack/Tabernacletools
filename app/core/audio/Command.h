// Message-thread -> audio-thread command record + lock-free queue (shared by the click engine and instrument engines).
// The Type list is one enum for all engines; each engine ignores types it does not own.
#pragma once
#include <atomic>
#include <array>
#include <cstdint>

namespace wr {

struct Command {
    enum Type : uint8_t { SetEnabled, SetParam, SetBpm, SetTrim, SetRouting, SetTuner, SetTunerMute, Panic,
                          ClickOn, ClickMute, ClickDb, ClickOut, ClickMeter, ClickAccent, ClickReset, TailSec,
                          CueAnnounce, CueDb, CuesOn, CuesWhenMuted, CountIn, ClickPreset, DownbeatDb, SetOversampling } type = Panic;
    uint8_t lane = 0; int16_t slot = -1, param = -1; uint32_t generation = 0; float value = 0;
};

template <class T, int N> class SpscQueue { // single producer / single consumer, lock-free
public:
    bool push (const T& v) { int w = w_.load (std::memory_order_relaxed), nw = (w + 1) % N; if (nw == r_.load (std::memory_order_acquire)) return false; buf_[(size_t) w] = v; w_.store (nw, std::memory_order_release); return true; }
    bool pop (T& v) { int r = r_.load (std::memory_order_relaxed); if (r == w_.load (std::memory_order_acquire)) return false; v = buf_[(size_t) r]; r_.store ((r + 1) % N, std::memory_order_release); return true; }
private:
    std::array<T, N> buf_ {}; std::atomic<int> w_ { 0 }, r_ { 0 };
};

inline Command cmdBpm (double b) { Command c; c.type = Command::SetBpm; c.value = (float) b; return c; }
inline Command cmdClick (Command::Type t, float v) { Command c; c.type = t; c.value = v; return c; }
inline Command cmdAnnounce (int slot, int seq) { Command c; c.type = Command::CueAnnounce; c.slot = (int16_t) slot; c.generation = (uint32_t) seq; return c; }
inline Command cmdCountIn (int beats, bool spoken) { Command c; c.type = Command::CountIn; c.value = (float) beats; c.param = spoken ? 1 : 0; return c; }

} // namespace wr
