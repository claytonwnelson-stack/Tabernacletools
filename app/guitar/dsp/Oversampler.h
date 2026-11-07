// Tabernacle Tools v0.12 - anti-aliasing oversampler for the nonlinear amp / drive stages (pure C++17, no JUCE).
// Same structure as juce::dsp::Oversampling's "polyphase IIR" mode: cascaded 2x stages, each a half-band filter built from
// two parallel chains of first-order allpasses (Valenzuela/de Soras design, minimum-phase-ish, low latency, cheap).
// Why not juce::dsp::Oversampling itself: the guitar DSP is JUCE-free so it builds + is tested on Linux/CI and inside the
// AUv3 the same way; the algorithm and quality are equivalent (see tests/guitar/test_v012.cpp for measured numbers).
// Real-time safe: prepare() allocates for 8x once; setFactor() on the audio thread only resets state.
#pragma once
#include <vector>
#include <array>
#include <cmath>
#include <algorithm>

namespace wr {
namespace hb {
// Half-band allpass coefficients for `n` coefficients and a transition bandwidth `tbw` (fraction of the high rate, 0..0.5).
inline std::vector<double> designHalfband (int n, double tbw) {
    const double pi = 3.14159265358979323846;
    double k = std::tan ((1.0 - 2.0 * tbw) * pi / 4.0); k *= k;
    const double kks = std::pow (1.0 - k * k, 0.25), e = 0.5 * (1.0 - kks) / (1.0 + kks), e2 = e * e, e4 = e2 * e2;
    const double q = e * (1.0 + e4 * (2.0 + e4 * (15.0 + 150.0 * e4)));
    const int order = n * 2 + 1; std::vector<double> c ((size_t) n);
    for (int idx = 0; idx < n; ++idx) {
        const int cc = idx + 1; double num = 0, den = 0, t; int i = 0, j = 1;
        do { t = std::pow (q, (double) (i * (i + 1))) * std::sin ((i * 2 + 1) * cc * pi / order) * j; num += t; j = -j; ++i; } while (std::abs (t) > 1e-100 && i < 100);
        i = 1; j = -1; do { t = std::pow (q, (double) (i * i)) * std::cos (i * 2 * cc * pi / order) * j; den += t; j = -j; ++i; } while (std::abs (t) > 1e-100 && i < 100);
        num *= std::pow (q, 0.25); den += 0.5;
        const double ww = num / den, wwsq = ww * ww, x = std::sqrt ((1.0 - wwsq * k) * (1.0 - wwsq / k)) / (1.0 + wwsq);
        c[(size_t) idx] = (1.0 - x) / (1.0 + x);
    }
    return c;
}
struct AllpassChain {   // coefs[0,2,4..] -> path A, coefs[1,3,5..] -> path B
    std::vector<float> a, b; std::vector<float> ax, ay, bx, by;
    void set (const std::vector<double>& c) { a.clear(); b.clear(); for (size_t i = 0; i < c.size(); ++i) (i % 2 ? b : a).push_back ((float) c[i]); ax.assign (a.size(), 0); ay = ax; bx.assign (b.size(), 0); by = bx; }
    void reset() { std::fill (ax.begin(), ax.end(), 0.0f); std::fill (ay.begin(), ay.end(), 0.0f); std::fill (bx.begin(), bx.end(), 0.0f); std::fill (by.begin(), by.end(), 0.0f); }
    static inline float run (float x, const std::vector<float>& c, std::vector<float>& mx, std::vector<float>& my) {
        for (size_t i = 0; i < c.size(); ++i) { const float y = (x - my[i]) * c[i] + mx[i]; mx[i] = x; my[i] = y; x = y; } return x; }
    inline void up (float x, float& o0, float& o1) { o0 = run (x, a, ax, ay); o1 = run (x, b, bx, by); }
    inline float down (float x0, float x1) { return 0.5f * (run (x1, a, ax, ay) + run (x0, b, bx, by)); }
};
} // namespace hb

// Multichannel (up to 2) oversampler: factor 1 (off), 2, 4 or 8.
class Oversampler {
public:
    static constexpr int kMaxFactor = 8, kMaxCh = 2;
    void prepare (int maxBlock, int channels = 1) {
        ch_ = std::clamp (channels, 1, kMaxCh); maxBlock_ = std::max (1, maxBlock);
        // stage 0 (base -> 2x) needs the steep filter; later stages only have to reject images above the original band
        static const std::array<std::vector<double>, 3> coefs = { hb::designHalfband (10, 0.035), hb::designHalfband (4, 0.22), hb::designHalfband (3, 0.36) };
        for (int c = 0; c < kMaxCh; ++c) for (int s = 0; s < 3; ++s) { upF_[c][s].set (coefs[(size_t) s]); dnF_[c][s].set (coefs[(size_t) s]); }
        for (auto& b : buf_) b.assign ((size_t) maxBlock_ * kMaxFactor, 0.0f);
        for (auto& b : tmp_) b.assign ((size_t) maxBlock_ * kMaxFactor, 0.0f);
        reset();
    }
    void setFactor (int f) { f = f >= 8 ? 8 : f >= 4 ? 4 : f >= 2 ? 2 : 1; if (f != factor_) { factor_ = f; reset(); } }
    int factor() const { return factor_; }
    int stages() const { return factor_ == 8 ? 3 : factor_ == 4 ? 2 : factor_ == 2 ? 1 : 0; }
    void reset() { for (auto& c : upF_) for (auto& s : c) s.reset(); for (auto& c : dnF_) for (auto& s : c) s.reset(); }
    // Upsample n base-rate samples of channel c; returns the n*factor() buffer to process in place.
    float* up (int c, const float* in, int n) {
        float* out = buf_[(size_t) c].data(); if (factor_ == 1) { std::copy (in, in + n, out); return out; }
        const float* src = in; int len = n; const int S = stages();
        for (int st = 0; st < S; ++st) {
            float* dst = ((S - 1 - st) % 2 == 0) ? out : tmp_[(size_t) c].data();   // alternate so the last stage lands in `out`
            for (int i = 0; i < len; ++i) upF_[c][st].up (src[i], dst[2 * i], dst[2 * i + 1]);
            src = dst; len *= 2;
        }
        return out;
    }
    // Downsample channel c's buffer (as returned by up) to n base-rate samples.
    void down (int c, float* out, int n) {
        float* src = buf_[(size_t) c].data(); if (factor_ == 1) { std::copy (src, src + n, out); return; }
        int len = n * factor_;
        for (int s = stages() - 1; s >= 0; --s) { const int half = len / 2; float* dst = s == 0 ? out : src;   // in-place is safe: dst[i] written after src[2i],src[2i+1] read
            for (int i = 0; i < half; ++i) dst[i] = dnF_[c][s].down (src[2 * i], src[2 * i + 1]); len = half; }
    }
    // Phase delay at 1 kHz / 48 kHz in base-rate samples for a factor (measured once on a scratch instance; message thread).
    static double latencySamples (int factor);
private:
    int factor_ = 1, ch_ = 1, maxBlock_ = 512;
    hb::AllpassChain upF_[kMaxCh][3], dnF_[kMaxCh][3];
    std::array<std::vector<float>, kMaxCh> buf_, tmp_;
};

inline double measureOversamplerLatency (int factor) {
    Oversampler o; o.prepare (256, 1); o.setFactor (factor);
    const double pi = 3.14159265358979323846, w = 2 * pi * 1000.0 / 48000.0; double sc = 0, cc = 0, si = 0, ci = 0; std::vector<float> in (256), out (256);
    for (int blk = 0; blk < 64; ++blk) {
        for (int i = 0; i < 256; ++i) in[(size_t) i] = (float) std::sin (w * (blk * 256 + i));
        o.up (0, in.data(), 256); o.down (0, out.data(), 256);
        if (blk >= 16) for (int i = 0; i < 256; ++i) { const double t = w * (blk * 256 + i); sc += out[(size_t) i] * std::sin (t); cc += out[(size_t) i] * std::cos (t); si += in[(size_t) i] * std::sin (t); ci += in[(size_t) i] * std::cos (t); }
    }
    double ph = std::atan2 (cc, sc) - std::atan2 (ci, si); while (ph > 0) ph -= 2 * pi; while (ph < -2 * pi) ph += 2 * pi;
    return -ph / w;
}
inline double Oversampler::latencySamples (int factor) {
    static const std::array<double, 4> table = { 0.0, measureOversamplerLatency (2), measureOversamplerLatency (4), measureOversamplerLatency (8) };
    return factor >= 8 ? table[3] : factor >= 4 ? table[2] : factor >= 2 ? table[1] : 0.0;
}
} // namespace wr
