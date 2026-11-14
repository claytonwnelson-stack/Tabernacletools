// MIDI Clock send (24 ppqn) + helpers. Pure C++17, realtime-safe (no allocation in process()).
// Receive lives in MidiRouter (0xF8 / Start / Stop / Continue / Song Position Pointer).
#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <array>

namespace wr {

enum class MidiClockMode : int { Off = 0, Send = 1, Receive = 2 };
inline const char* midiClockModeName (MidiClockMode m) {
    switch (m) { case MidiClockMode::Send: return "Send"; case MidiClockMode::Receive: return "Receive"; default: return "Off"; }
}
inline MidiClockMode midiClockModeFromInt (int v) { return v == 1 ? MidiClockMode::Send : v == 2 ? MidiClockMode::Receive : MidiClockMode::Off; }

// one short MIDI message to emit at a sample offset inside the current audio block
struct MidiOutMsg { int sample = 0; uint8_t status = 0, d1 = 0, d2 = 0; int size = 1; };

// Sample-accurate MIDI Clock generator. Driven from the audio thread with the live session's tempo and transport.
class MidiClockOut {
public:
    void setSampleRate (double sr) { sr_ = std::max (1.0, sr); }
    void setBpm (double bpm) {
        bpm = std::clamp (bpm, 20.0, 400.0);
        if (std::abs (bpm - bpm_) < 1e-9) return;
        bpm_ = bpm; samplesPerTick_ = sr_ * 60.0 / (bpm_ * 24.0); // 24 ppqn
    }
    double bpm() const { return bpm_; }
    bool running() const { return running_; }

    // absolute song position in quarter-note beats (0 = song start / first downbeat)
    double beats() const { return beats_; }
    void seekBeats (double beats) { // Song Position Pointer + Continue (if running)
        beats_ = std::max (0.0, beats); tickPhase_ = 0;
        const int spp = std::clamp ((int) std::llround (beats_ * 4.0), 0, 16383); // MIDI beats = 16th notes
        queue (0xF2, (uint8_t) (spp & 0x7F), (uint8_t) ((spp >> 7) & 0x7F), 3);
        if (running_) queue (0xFB, 0, 0, 1); // Continue after a locate
    }

    void setRunning (bool on) {
        if (on == running_) return;
        if (on) { tickPhase_ = 0; queue (beats_ > 1e-9 ? 0xFB : 0xFA, 0, 0, 1); } // Continue if mid-song, else Start
        else queue (0xFC, 0, 0, 1);
        running_ = on;
    }

    // force Start from beat 0 (PLAY / count-in begins)
    void startFromTop() { beats_ = 0; tickPhase_ = 0; running_ = true; queue (0xFA, 0, 0, 1); }
    void stop() { if (! running_) return; running_ = false; queue (0xFC, 0, 0, 1); }

    // emit queued transport/SPP first, then Clock ticks while running. Returns messages written.
    int process (int numSamples, MidiOutMsg* out, int maxOut) {
        int n = 0;
        auto emit = [&] (const MidiOutMsg& m) { if (n < maxOut) out[n++] = m; };
        for (int i = 0; i < nQ_ && n < maxOut; ++i) { q_[(size_t) i].sample = 0; emit (q_[(size_t) i]); }
        nQ_ = 0;
        if (! running_ || samplesPerTick_ <= 0 || maxOut <= 0) return n;
        double acc = tickPhase_; // samples since last tick (0 .. samplesPerTick_)
        for (int i = 0; i < numSamples && n < maxOut; ++i) {
            acc += 1.0;
            if (acc + 1e-12 >= samplesPerTick_) {
                acc -= samplesPerTick_;
                MidiOutMsg m; m.sample = i; m.status = 0xF8; m.size = 1; emit (m);
                beats_ += 1.0 / 24.0;
            }
        }
        tickPhase_ = acc;
        return n;
    }

    void reset() { running_ = false; beats_ = 0; tickPhase_ = 0; nQ_ = 0; }

private:
    void queue (uint8_t st, uint8_t d1, uint8_t d2, int sz) {
        if (nQ_ >= (int) q_.size()) return;
        q_[(size_t) nQ_++] = { 0, st, d1, d2, sz };
    }
    double sr_ = 48000, bpm_ = 120, samplesPerTick_ = 48000.0 * 60.0 / (120.0 * 24.0);
    double beats_ = 0, tickPhase_ = 0; bool running_ = false;
    std::array<MidiOutMsg, 8> q_ {}; int nQ_ = 0;
};

// Song Position Pointer -> quarter-note beats
inline double sppToBeats (int spp) { return std::max (0, spp) / 4.0; }
inline int beatsToSpp (double beats) { return std::clamp ((int) std::llround (std::max (0.0, beats) * 4.0), 0, 16383); }

} // namespace wr
