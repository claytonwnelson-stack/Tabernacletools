// YIN monophonic pitch tracker, used by the tuner and the Key9/Synth9-style pedals.
// Runs on a 2x-decimated signal; analysis every `hop` decimated samples. Allocation-free after prepare().
#pragma once
#include "DspUtil.h"
#include <vector>
#include <cmath>
#include <string>

namespace wr {

class PitchTracker {
public:
    void prepare (double fs, float minHz = 70.0f, float maxHz = 1300.0f) {
        dec_ = fs > 64000 ? 4 : 2; fsd_ = (float) fs / (float) dec_;
        tauMax_ = (int) (fsd_ / minHz) + 2; tauMin_ = std::max (2, (int) (fsd_ / maxHz));
        win_ = tauMax_ * 2; hop_ = 192;
        ring_.assign ((size_t) (win_ + tauMax_) * 2, 0.0f); ringN_ = (int) ring_.size(); w_ = 0;
        frame_.assign ((size_t) (win_ + tauMax_), 0.0f); d_.assign ((size_t) tauMax_ + 1, 0.0f);
        aa1_.setLP ((float) fs, fsd_ * 0.4f, 0.707f); aa2_ = aa1_; hp_.setHP ((float) fs, 50.0f, 0.707f);
        env_ = 0; envC_ = std::exp (-1.0f / (0.05f * (float) fs)); hz_ = 0; conf_ = 0; cnt_ = 0; since_ = 0;
    }
    // feed raw input; returns true when a new estimate is available
    bool push (const float* x, int n) {
        bool upd = false;
        for (int i = 0; i < n; ++i) {
            float v = hp_.process (x[i]); float a = std::abs (v); env_ = a > env_ ? a : env_ * envC_;
            v = aa2_.process (aa1_.process (v));
            if (++cnt_ >= dec_) { cnt_ = 0; ring_[(size_t) w_] = v; w_ = (w_ + 1) % ringN_; if (++since_ >= hop_) { since_ = 0; analyse(); upd = true; } }
        }
        return upd;
    }
    float frequency() const { return hz_; }      // 0 when no pitch
    float confidence() const { return conf_; }   // 0..1
    float level() const { return env_; }          // peak envelope of input
    float midiNote() const { return hz_ > 0 ? 69.0f + 12.0f * std::log2 (hz_ / 440.0f) : -1.0f; }

private:
    void analyse() {
        const int N = win_ + tauMax_;
        for (int i = 0; i < N; ++i) { int r = (w_ - N + i) % ringN_; if (r < 0) r += ringN_; frame_[(size_t) i] = ring_[(size_t) r]; }
        if (env_ < 0.003f) { hz_ = 0; conf_ = 0; return; }
        d_[0] = 1; float run = 0;
        for (int tau = 1; tau <= tauMax_; ++tau) {
            float s = 0; const float* a = frame_.data(); const float* b = a + tau;
            for (int j = 0; j < win_; ++j) { float df = a[j] - b[j]; s += df * df; }
            run += s; d_[(size_t) tau] = run > 0 ? s * (float) tau / run : 1.0f;
        }
        int best = -1;
        for (int tau = tauMin_; tau < tauMax_; ++tau)
            if (d_[(size_t) tau] < 0.15f) { while (tau + 1 < tauMax_ && d_[(size_t) tau + 1] < d_[(size_t) tau]) ++tau; best = tau; break; }
        if (best < 0) { // fall back to global min
            float m = 1e9f; for (int tau = tauMin_; tau < tauMax_; ++tau) if (d_[(size_t) tau] < m) { m = d_[(size_t) tau]; best = tau; }
            if (m > 0.35f) { conf_ = 0; hz_ = 0; return; }
        }
        float y0 = d_[(size_t) best - 1], y1 = d_[(size_t) best], y2 = d_[(size_t) best + 1], den = y0 - 2 * y1 + y2;
        float t = (float) best + (std::abs (den) > 1e-9f ? 0.5f * (y0 - y2) / den : 0.0f);
        hz_ = fsd_ / t; conf_ = dsp::clampf (1.0f - y1, 0, 1);
    }
    std::vector<float> ring_, frame_, d_; int ringN_ = 0, w_ = 0, win_ = 0, hop_ = 192, tauMin_ = 2, tauMax_ = 2, dec_ = 2, cnt_ = 0, since_ = 0;
    float fsd_ = 24000, env_ = 0, envC_ = 0.99f, hz_ = 0, conf_ = 0; dsp::Biquad aa1_, aa2_, hp_;
};

// Tuner read-out helper.
struct TunerReading { bool valid = false; std::string note; int octave = 0; float cents = 0, hz = 0; };
inline TunerReading tunerReading (float hz, float conf, float a4 = 440.0f) {
    static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    TunerReading r; if (hz <= 0 || conf < 0.6f) return r;
    float m = 69.0f + 12.0f * std::log2 (hz / a4); int n = (int) std::lround (m);
    r.valid = true; r.hz = hz; r.cents = (m - (float) n) * 100.0f; r.note = names[((n % 12) + 12) % 12]; r.octave = n / 12 - 1; return r;
}

} // namespace wr
