// Bearing Live - pedal base class (pure C++17, no JUCE). Every pedal is a Pedal subclass with named params.
#pragma once
#include "../third_party/wk/Engine.h"
#include "audio/DspUtil.h"
#include <string>
#include <vector>
#include <memory>

namespace wr {
using wk::Biquad; using wk::DelayLine; using wk::OnePole; using wk::Smooth; using dsp::clampf; using dsp::kPi; using dsp::k2Pi;

struct ParamSpec {
    std::string id, name, unit;
    float min = 0, max = 1, def = 0.5f;
    std::vector<std::string> choices; // non-empty => stepped choice (value = index)
    bool isChoice() const { return ! choices.empty(); }
};

// Shared per-block info the rig hands to every pedal.
struct ProcessContext {
    double sampleRate = 48000, bpm = 120;
    float pitchHz = 0, pitchConf = 0, inputLevel = 0; // from the rig's pitch tracker (raw guitar input)
    bool trackerRunning = false;
};

enum class PedalCategory { Dynamics, Drive, Pitch, Synth, Modulation, Time, Ambient, AmpCab, Utility };

class Pedal {
public:
    virtual ~Pedal() = default;
    // --- identity
    std::string type, displayName; PedalCategory category = PedalCategory::Utility;
    bool implemented = true;   // false = v0.1 stub (passes audio through, shows TODO in UI)
    bool hasTails = false;     // tails pedals keep ringing after being switched off (spillover)
    bool wantsPitch = false;   // needs the rig's pitch tracker

    // --- params
    const std::vector<ParamSpec>& specs() const { return specs_; }
    int numParams() const { return (int) specs_.size(); }
    int paramIndex (const std::string& id) const { for (int i = 0; i < numParams(); ++i) if (specs_[(size_t) i].id == id) return i; return -1; }
    void setParam (int i, float v) { if (i >= 0 && i < numParams()) { auto& s = specs_[(size_t) i]; target_[(size_t) i] = clampf (v, s.min, s.max); if (! prepared_) cur_[(size_t) i] = target_[(size_t) i]; } }
    bool setParam (const std::string& id, float v) { int i = paramIndex (id); setParam (i, v); return i >= 0; }
    float getParam (int i) const { return (i >= 0 && i < numParams()) ? target_[(size_t) i] : 0.0f; }
    float getParam (const std::string& id) const { return getParam (paramIndex (id)); }
    float normalised (int i) const { auto& s = specs_[(size_t) i]; return (getParam (i) - s.min) / (s.max - s.min); }

    // --- enable (click-free 8 ms ramp)
    void setEnabled (bool e) { enabled_ = e; }
    bool isEnabled() const { return enabled_; }
    bool isSilent() const { return ! enabled_ && gain_ == 0.0f && (! hasTails || idleBlocks_ > idleLimit_); }

    void prepare (double fs, int maxBlock) {
        fs_ = (float) fs; maxBlock_ = maxBlock; dryL_.assign ((size_t) maxBlock, 0); dryR_.assign ((size_t) maxBlock, 0);
        idleLimit_ = (int) (fs * 2.0 / std::max (1, maxBlock)) + 1;
        prepared_ = true; cur_ = target_; onPrepareBase(); onPrepare(); onParamsChanged(); reset();
    }
    // v0.12 anti-aliasing: oversampling factor (1/2/4/8) for pedals with nonlinear stages. Audio-thread safe (no allocation).
    void setOversampling (int factor) { factor = factor >= 8 ? 8 : factor >= 4 ? 4 : factor >= 2 ? 2 : 1; if (factor == osFactor_) return; osFactor_ = factor; onOversampling(); }
    int oversampling() const { return osFactor_; }
    virtual bool oversamples() const { return false; }      // true for amp / drive pedals
    virtual double latencySamples() const { return 0; }     // added delay at the base rate (phase delay at 1 kHz)
    void reset() { onReset(); gain_ = enabled_ ? 1.0f : 0.0f; idleBlocks_ = idleLimit_ + 1; }

    // In-place stereo processing. n <= maxBlock.
    void process (float* L, float* R, int n, const ProcessContext& ctx) {
        if (n <= 0) return;
        smoothParams (n);
        const float g0 = gain_, step = (float) n / (0.008f * fs_);
        const float g1 = enabled_ ? std::min (1.0f, g0 + step) : std::max (0.0f, g0 - step);
        gain_ = g1;
        if (! implemented) return;
        if (! hasTails) {
            if (g0 == 0.0f && g1 == 0.0f) return; // fully bypassed
            std::copy (L, L + n, dryL_.data()); std::copy (R, R + n, dryR_.data());
            render (L, R, n, ctx);
            for (int s = 0; s < n; ++s) { float g = g0 + (g1 - g0) * (float) s / (float) n; L[s] = dryL_[(size_t) s] + (L[s] - dryL_[(size_t) s]) * g; R[s] = dryR_[(size_t) s] + (R[s] - dryR_[(size_t) s]) * g; }
        } else {
            // spillover: the input is gated by the enable ramp, the wet signal always plays out.
            if (g0 == 0.0f && g1 == 0.0f && idleBlocks_ > idleLimit_) return;
            for (int s = 0; s < n; ++s) { float g = g0 + (g1 - g0) * (float) s / (float) n; dryL_[(size_t) s] = L[s]; dryR_[(size_t) s] = R[s]; L[s] *= g; R[s] *= g; }
            render (L, R, n, ctx); // tails pedals replace L/R with WET ONLY
            float e = 0;
            for (int s = 0; s < n; ++s) { e = std::max (e, std::abs (L[s]) + std::abs (R[s])); L[s] += dryL_[(size_t) s]; R[s] += dryR_[(size_t) s]; }
            if (g1 == 0.0f && e < 1.0e-5f) ++idleBlocks_; else idleBlocks_ = 0;
        }
    }

protected:
    void addParam (std::string id, std::string name, float mn, float mx, float def, std::string unit = {}) {
        specs_.push_back ({ std::move (id), std::move (name), std::move (unit), mn, mx, def, {} }); target_.push_back (def); cur_.push_back (def);
    }
    void addChoice (std::string id, std::string name, std::vector<std::string> ch, int def) {
        float mx = (float) ch.size() - 1; specs_.push_back ({ std::move (id), std::move (name), {}, 0.0f, mx, (float) def, std::move (ch) }); target_.push_back ((float) def); cur_.push_back ((float) def);
    }
    float p (int i) const { return cur_[(size_t) i]; }               // smoothed value
    int pi (int i) const { return (int) std::lround (target_[(size_t) i]); } // choices: no smoothing
    virtual void onPrepareBase() {}     // base classes (allocation-safe point, before onPrepare)
    virtual void onOversampling() {}    // osFactor_ changed (audio thread: no allocation)
    virtual void onPrepare() {}
    virtual void onReset() {}
    virtual void onParamsChanged() {}   // recompute filters etc. (called at most once per block)
    virtual void render (float* L, float* R, int n, const ProcessContext& ctx) = 0;

    float fs_ = 48000; int maxBlock_ = 512; int osFactor_ = 4;
private:
    void smoothParams (int n) {
        bool changed = false; const float k = 1.0f - std::exp (-(float) n / (0.025f * fs_));
        for (size_t i = 0; i < cur_.size(); ++i) {
            float d = target_[i] - cur_[i];
            if (d != 0.0f) { changed = true; if (specs_[i].isChoice() || std::abs (d) < 1e-5f * (specs_[i].max - specs_[i].min + 1e-9f)) cur_[i] = target_[i]; else cur_[i] += d * k; }
        }
        if (changed) onParamsChanged();
    }
    std::vector<ParamSpec> specs_; std::vector<float> target_, cur_;
    std::vector<float> dryL_, dryR_;
    bool enabled_ = false, prepared_ = false; float gain_ = 0.0f; int idleBlocks_ = 1 << 20, idleLimit_ = 100;
};

struct PedalInfo { std::string type, name; PedalCategory category; bool implemented; bool priority; };
const std::vector<PedalInfo>& pedalCatalog();          // every pedal type, in library order
// v0.9: load a cabinet impulse response into an "amp" or "cab" pedal (false if wrong type / bad IR)
bool loadCabImpulse (Pedal& p, const float* data, int len, double sampleRate);
std::unique_ptr<Pedal> createPedal (const std::string& type); // nullptr for unknown type
const char* categoryName (PedalCategory c);

} // namespace wr
