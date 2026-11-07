// Worship Keys - synthesis engine (pure C++17, no JUCE dependency, all sounds synthesized)
#pragma once
#include <cmath>
#include <vector>
#include <array>
#include <algorithm>
#include <cstdint>
#include <cstring>

namespace wk {

constexpr float kPi = 3.14159265358979f;
constexpr float k2Pi = 6.28318530717959f;
inline float mtof (float n) { return 440.0f * std::pow (2.0f, (n - 69.0f) / 12.0f); }
inline float dbToGain (float db) { return std::pow (10.0f, db / 20.0f); }
inline float clampf (float x, float a, float b) { return std::min (b, std::max (a, x)); }

struct Rng { uint32_t s = 0x1234567u; float next() { s = s * 1664525u + 1013904223u; return (float) (s >> 8) * (2.0f / 16777216.0f) - 1.0f; } };

struct SineTable {
    static constexpr int N = 4096;
    float t[N + 1];
    SineTable() { for (int i = 0; i <= N; ++i) t[i] = std::sin (k2Pi * (float) i / N); }
    inline float operator() (float ph) const { // ph in [0,1)
        float p = ph * N; int i = (int) p; float f = p - (float) i; i &= (N - 1);
        return t[i] + f * (t[i + 1] - t[i]);
    }
};
inline const SineTable& sineTab() { static SineTable s; return s; }

struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    inline float process (float x) { float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    void reset() { z1 = z2 = 0; }
    void norm (float a0, float B0, float B1, float B2, float A1, float A2) { b0 = B0 / a0; b1 = B1 / a0; b2 = B2 / a0; a1 = A1 / a0; a2 = A2 / a0; }
    void setLP (float fs, float f, float q) { float w = k2Pi * clampf (f, 10, fs * 0.45f) / fs, c = std::cos (w), al = std::sin (w) / (2 * q); norm (1 + al, (1 - c) / 2, 1 - c, (1 - c) / 2, -2 * c, 1 - al); }
    void setHP (float fs, float f, float q) { float w = k2Pi * clampf (f, 10, fs * 0.45f) / fs, c = std::cos (w), al = std::sin (w) / (2 * q); norm (1 + al, (1 + c) / 2, -(1 + c), (1 + c) / 2, -2 * c, 1 - al); }
    void setPeak (float fs, float f, float q, float db) { float A = std::pow (10.f, db / 40), w = k2Pi * f / fs, c = std::cos (w), al = std::sin (w) / (2 * q); norm (1 + al / A, 1 + al * A, -2 * c, 1 - al * A, -2 * c, 1 - al / A); }
    void setShelf (float fs, float f, float db, bool high) {
        float A = std::pow (10.f, db / 40), w = k2Pi * f / fs, c = std::cos (w), s = std::sin (w), al = s / 2 * std::sqrt (2.0f), sq = 2 * std::sqrt (A) * al;
        if (! high) norm ((A + 1) + (A - 1) * c + sq, A * ((A + 1) - (A - 1) * c + sq), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sq), -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sq);
        else        norm ((A + 1) - (A - 1) * c + sq, A * ((A + 1) + (A - 1) * c + sq), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sq), 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sq);
    }
};

struct OnePole { float a = 0, z = 0; void set (float fs, float f) { a = std::exp (-k2Pi * f / fs); } inline float lp (float x) { z = x + (z - x) * a; return z; } };
struct Smooth { float v = 0, t = 0, c = 0.001f; void set (float fs, float ms) { c = 1.0f - std::exp (-1.0f / (0.001f * ms * fs)); } inline float next() { v += (t - v) * c; return v; } };

struct DelayLine {
    std::vector<float> buf; int w = 0, mask = 0;
    void init (int maxSamples) { int n = 1; while (n < maxSamples + 4) n <<= 1; buf.assign ((size_t) n, 0.0f); mask = n - 1; w = 0; }
    void clear() { std::fill (buf.begin(), buf.end(), 0.0f); }
    inline void push (float x) { buf[(size_t) w] = x; w = (w + 1) & mask; }
    inline float read (float d) const { // d samples behind newest (>=0)
        float r = (float) w - 1.0f - d; while (r < 0) r += (float) (mask + 1);
        int i = (int) r; float f = r - (float) i;
        return buf[(size_t) (i & mask)] * (1 - f) + buf[(size_t) ((i + 1) & mask)] * f;
    }
};

// ------------------------------------------------------------------ parameters
struct Params {
    int transpose = 0; bool splitOn = false; int splitNote = 60;
    bool organOn = true, padOn = false, pianoOn = false;
    int organZone = 0, padZone = 0, pianoZone = 0;           // 0 whole, 1 lower, 2 upper
    float organLevel = 0.8f, padLevel = 0.7f, pianoLevel = 0.8f; // 0..1
    // organ
    int organType = 0;                       // 0 tonewheel, 1 pipe
    float drawbar[9] = { 8, 8, 8, 0, 0, 0, 0, 0, 0 };
    bool perc = false; int percHarm = 1; bool percFast = true; bool percSoft = false;
    float click = 0.4f; int vibMode = 6;     // 0 off, 1-3 V1-V3, 4-6 C1-C3
    bool rotaryOn = true; int rotaryFast = 0; float drive = 0.2f; bool organSustain = false;
    // pad
    float padDetune = 0.4f, padCutoff = 2500, padReso = 0.2f, padAttack = 1.5f, padRelease = 3.0f;
    float padShimmer = 0.2f, padMotion = 0.4f, padSub = 0.3f, padWidth = 0.8f;
    // piano
    int pianoType = 0; float pianoTone = 0.5f, pianoDecay = 1.0f, pianoVelSens = 0.7f, epTrem = 0.0f;
    // fx
    bool delayOn = true, delaySync = true, pingPong = true; int delayDiv = 3; float delayTimeMs = 400, delayFb = 0.35f, delayMix = 0.2f, delayHiCut = 4500;
    bool reverbOn = true; float revSize = 0.7f, revDecay = 4.0f, revPredelay = 20, revMix = 0.3f, revShimmer = 0.0f, revDamp = 0.5f;
    float lowCut = 40, eqLow = 0, eqMid = 0, eqHigh = 0, master = -6;
    double bpm = 120;
};

inline float delayDivBeats (int d) { static const float b[] = { 2.0f, 1.0f, 1.5f, 2.0f / 3.0f, 0.5f, 0.75f, 1.0f / 3.0f, 0.25f }; return b[std::clamp (d, 0, 7)]; }

// ------------------------------------------------------------------ envelope
struct ADSR {
    enum St { Idle, Att, Dec, Sus, Rel } st = Idle; float v = 0, aR = 0, dC = 0, s = 1, rC = 0;
    void set (float fs, float a, float d, float sus, float r) { aR = 1.0f / std::max (1.0f, a * fs); dC = std::exp (-1.0f / std::max (1.0f, d * fs)); s = sus; rC = std::exp (-1.0f / std::max (1.0f, r * fs * 0.3f)); }
    void on() { st = Att; } void off() { if (st != Idle) st = Rel; }
    inline float next() {
        switch (st) {
            case Att: v += aR; if (v >= 1) { v = 1; st = Dec; } break;
            case Dec: v = s + (v - s) * dC; if (std::abs (v - s) < 1e-4f) st = Sus; break;
            case Sus: v = s; break;
            case Rel: v *= rC; if (v < 1e-5f) { v = 0; st = Idle; } break;
            default: break;
        }
        return v;
    }
};

// ------------------------------------------------------------------ voice base
struct VoiceBase { bool active = false; int key = -1; int note = -1; bool held = false, sustained = false; uint32_t age = 0; float vel = 0; };

template <class V> V* allocVoice (std::vector<V>& vs, uint32_t& clock) {
    V* best = nullptr;
    for (auto& v : vs) if (! v.active) { best = &v; break; }
    if (! best) { // steal: prefer released voices, oldest first
        for (auto& v : vs) if (! v.held && (! best || v.age < best->age)) best = &v;
        if (! best) for (auto& v : vs) if (! best || v.age < best->age) best = &v;
    }
    best->age = ++clock; return best;
}

// ------------------------------------------------------------------ ORGAN
struct OrganVoice : VoiceBase {
    float ph[9] {}, inc[9] {}, env = 0, envTarget = 0, envC = 0, clickEnv = 0, percEnv = 0, chiff = 0; Biquad clickF;
};

class Organ {
public:
    static constexpr float tonewheelRatios[9] = { 0.5f, 1.5f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 8.0f };
    std::vector<OrganVoice> voices = std::vector<OrganVoice> (24);
    float fs = 48000; uint32_t clock = 0; Rng rng; float bars[9] {}; Smooth barS[9]; float percLevel = 0; bool percArmed = true;
    float pipeTab[SineTable::N + 1];
    // bus
    DelayLine vib; float vibPh = 0; Biquad xoverLo, xoverHi; DelayLine hornL, hornR, drumL, drumR;
    float hornPh = 0, drumPh = 0, hornRate = 0.8f, drumRate = 0.7f; Biquad toneLP;

    void prepare (float sr) {
        fs = sr; for (auto& s : barS) s.set (fs, 20);
        for (int i = 0; i <= SineTable::N; ++i) { float p = k2Pi * i / SineTable::N; pipeTab[i] = (std::sin (p) + 0.25f * std::sin (2 * p) + 0.12f * std::sin (3 * p) + 0.05f * std::sin (4 * p)) * 0.8f; }
        vib.init ((int) (fs * 0.01f)); hornL.init ((int) (fs * 0.01f)); hornR.init ((int) (fs * 0.01f)); drumL.init ((int) (fs * 0.01f)); drumR.init ((int) (fs * 0.01f));
        xoverLo.setLP (fs, 800, 0.707f); xoverHi.setHP (fs, 800, 0.707f); toneLP.setLP (fs, 7000, 0.6f);
    }
    bool anyHeld() const { for (auto& v : voices) if (v.active && v.held) return true; return false; }

    void noteOn (int key, int note, float vel, const Params& p) {
        bool legato = anyHeld();
        auto* v = allocVoice (voices, clock);
        v->active = true; v->key = key; v->note = note; v->held = true; v->sustained = false; v->vel = vel;
        float f0 = mtof ((float) note);
        static const float pipeRatios[9] = { 0.5f, 1.5f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 8.0f };
        for (int i = 0; i < 9; ++i) {
            float f = f0 * (p.organType == 0 ? tonewheelRatios[i] : pipeRatios[i]);
            while (f > 6500.0f) f *= 0.5f;           // tonewheel foldback
            if (p.organType == 0 && i == 0 && f < 32.7f) f *= 2.0f;
            v->inc[i] = f / fs; if (v->env <= 0.0f) v->ph[i] = rng.next() * 0.5f + 0.5f;
        }
        v->envTarget = 1; v->envC = 1.0f - std::exp (-1.0f / (fs * (p.organType == 0 ? 0.003f : 0.035f)));
        v->clickEnv = (p.organType == 0) ? 1.0f : 0.0f; v->chiff = p.organType == 1 ? 1.0f : 0.0f;
        v->clickF.setHP (fs, 1500 + rng.next() * 500 + 1500, 0.8f);
        if (p.perc && p.organType == 0 && ! legato) v->percEnv = 1.0f; else if (legato && p.perc) v->percEnv = 0.0f; else v->percEnv = 0.0f;
    }
    void release (OrganVoice& v, const Params& p) { v.held = false; v.envTarget = 0; v.envC = 1.0f - std::exp (-1.0f / (fs * (p.organType == 0 ? 0.006f : 0.07f))); }

    void render (float* L, float* R, int n, const Params& p, float level, bool fastSpeed) {
        for (int i = 0; i < 9; ++i) barS[i].t = p.drawbar[i] / 8.0f;
        const auto& st = sineTab();
        const float percDec = std::exp (-1.0f / (fs * (p.percFast ? 0.2f : 1.0f) * 0.4f));
        const float clickDec = std::exp (-1.0f / (fs * 0.004f));
        const float chiffDec = std::exp (-1.0f / (fs * 0.05f));
        const int percIdx = p.percHarm == 0 ? 3 : 4;
        const float percAmt = p.percSoft ? 0.5f : 1.0f;
        const bool pipe = p.organType == 1;
        // rotary speed ramp
        float hornT = (p.rotaryFast || fastSpeed) ? 6.7f : 0.8f, drumT = (p.rotaryFast || fastSpeed) ? 5.9f : 0.67f;
        float hornA = 1.0f - std::exp (-1.0f / (fs * 0.6f)), drumA = 1.0f - std::exp (-1.0f / (fs * 3.0f));
        float vibDepth = 0; bool chorus = false;
        if (p.vibMode >= 1 && p.vibMode <= 3) vibDepth = 0.00025f * p.vibMode; else if (p.vibMode >= 4) { vibDepth = 0.00025f * (p.vibMode - 3); chorus = true; }
        if (pipe) vibDepth = 0;
        float drv = 1.0f + p.drive * 7.0f, drvNorm = 1.0f / std::tanh (drv * 0.5f);
        for (int s = 0; s < n; ++s) {
            float b[9]; for (int i = 0; i < 9; ++i) b[i] = barS[i].next();
            float sum = 0;
            for (auto& v : voices) {
                if (! v.active) continue;
                v.env += (v.envTarget - v.env) * v.envC;
                if (v.envTarget == 0 && v.env < 1e-4f) { v.active = false; v.env = 0; continue; }
                float o = 0;
                for (int i = 0; i < 9; ++i) {
                    float w = pipe ? pipeTab[(int) (v.ph[i] * SineTable::N)] : st (v.ph[i]);
                    o += w * b[i];
                    v.ph[i] += v.inc[i]; if (v.ph[i] >= 1) v.ph[i] -= 1;
                }
                if (v.percEnv > 1e-4f) { o += st (v.ph[percIdx]) * v.percEnv * 2.0f * percAmt; v.percEnv *= percDec; }
                float out = o * v.env;
                if (v.clickEnv > 1e-4f) { out += v.clickF.process (rng.next()) * v.clickEnv * p.click * 0.6f; v.clickEnv *= clickDec; }
                if (v.chiff > 1e-4f) { out += rng.next() * v.chiff * 0.05f * v.env; v.chiff *= chiffDec; }
                sum += out;
            }
            sum *= 0.06f;
            // vibrato / chorus scanner
            vib.push (sum);
            if (vibDepth > 0) {
                vibPh += 6.9f / fs; if (vibPh >= 1) vibPh -= 1;
                float d = (0.0005f + vibDepth * (0.5f + 0.5f * st (vibPh))) * fs;
                float wet = vib.read (d);
                sum = chorus ? 0.5f * (sum + wet) : wet;
            }
            // drive
            if (! pipe) sum = std::tanh (sum * drv * 0.5f) * drvNorm / drv * 2.0f;
            sum = toneLP.process (sum);
            float l, r;
            if (p.rotaryOn && ! pipe) {
                hornRate += (hornT - hornRate) * hornA; drumRate += (drumT - drumRate) * drumA;
                hornPh += hornRate / fs; if (hornPh >= 1) hornPh -= 1;
                drumPh += drumRate / fs; if (drumPh >= 1) drumPh -= 1;
                float lo = xoverLo.process (sum), hi = xoverHi.process (sum);
                hornL.push (hi); hornR.push (hi); drumL.push (lo); drumR.push (lo);
                float hs = st (hornPh), hc = st (std::fmod (hornPh + 0.25f, 1.0f));
                float ds = st (drumPh), dc = st (std::fmod (drumPh + 0.25f, 1.0f));
                float hl = hornL.read ((0.0020f + 0.0006f * hs) * fs) * (1.0f + 0.4f * hs);
                float hr = hornR.read ((0.0020f + 0.0006f * hc) * fs) * (1.0f + 0.4f * hc);
                float dl = drumL.read ((0.0015f + 0.0003f * ds) * fs) * (1.0f + 0.2f * ds);
                float dr = drumR.read ((0.0015f + 0.0003f * dc) * fs) * (1.0f + 0.2f * dc);
                l = hl + dl; r = hr + dr;
            } else { l = r = sum; }
            L[s] += l * level; R[s] += r * level;
        }
    }
};

// ------------------------------------------------------------------ PADS
struct PadVoice : VoiceBase {
    static constexpr int NS = 7;
    float ph[NS] {}, inc[NS] {}, pan[NS] {}, subPh = 0, subInc = 0, shPh[2] {}, shInc[2] {};
    ADSR env; float fL[2] {}, fR[2] {}; float g = 0, k = 0; int cnt = 0; float lfoPh = 0;
};

inline float polyBlep (float t, float dt) {
    if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}

class Pads {
public:
    std::vector<PadVoice> voices = std::vector<PadVoice> (16);
    float fs = 48000; uint32_t clock = 0; Rng rng; float globalLfo = 0;
    void prepare (float sr) { fs = sr; }
    void noteOn (int key, int note, float vel, const Params& p) {
        auto* v = allocVoice (voices, clock);
        v->active = true; v->key = key; v->note = note; v->held = true; v->sustained = false; v->vel = vel;
        float f0 = mtof ((float) note);
        for (int i = 0; i < PadVoice::NS; ++i) {
            float spread = ((float) i / (PadVoice::NS - 1) * 2.0f - 1.0f);
            float cents = spread * p.padDetune * 35.0f + rng.next() * 1.5f;
            v->inc[i] = f0 * std::pow (2.0f, cents / 1200.0f) / fs;
            v->ph[i] = rng.next() * 0.5f + 0.5f;
            v->pan[i] = 0.5f + 0.5f * spread * p.padWidth;
        }
        v->subInc = f0 * 0.5f / fs; v->shInc[0] = f0 * 2.0f * 1.003f / fs; v->shInc[1] = f0 * 2.0f * 0.997f / fs;
        v->lfoPh = rng.next() * 0.5f + 0.5f;
        v->env.set (fs, p.padAttack, 2.0f, 0.85f, p.padRelease); v->env.on();
    }
    void release (PadVoice& v, const Params& p) { v.held = false; v.env.set (fs, p.padAttack, 2.0f, 0.85f, p.padRelease); v.env.off(); }
    void render (float* L, float* R, int n, const Params& p, float level, float modWheel) {
        const auto& st = sineTab();
        float res = 2.0f - 1.9f * clampf (p.padReso, 0, 1); // SVF damping (2 = no res)
        for (auto& v : voices) {
            if (! v.active) continue;
            for (int s = 0; s < n; ++s) {
                float e = v.env.next();
                if (v.env.st == ADSR::Idle) { v.active = false; break; }
                if ((v.cnt++ & 15) == 0) {
                    v.lfoPh += 16.0f * 0.11f / fs; if (v.lfoPh >= 1) v.lfoPh -= 1;
                    float mod = 1.0f + p.padMotion * 0.6f * st (v.lfoPh) + modWheel * 1.5f;
                    float fc = clampf (p.padCutoff * mod * (0.5f + 0.5f * e) * (0.7f + 0.3f * v.vel), 40.0f, fs * 0.42f);
                    v.g = std::tan (kPi * fc / fs); v.k = res;
                }
                float l = 0, r = 0;
                for (int i = 0; i < PadVoice::NS; ++i) {
                    float saw = 2.0f * v.ph[i] - 1.0f - polyBlep (v.ph[i], v.inc[i]);
                    v.ph[i] += v.inc[i]; if (v.ph[i] >= 1) v.ph[i] -= 1;
                    l += saw * (1.0f - v.pan[i]); r += saw * v.pan[i];
                }
                float sub = st (v.subPh) * p.padSub * 2.0f; v.subPh += v.subInc; if (v.subPh >= 1) v.subPh -= 1;
                float sh = 0;
                for (int j = 0; j < 2; ++j) { sh += (2.0f * v.shPh[j] - 1.0f - polyBlep (v.shPh[j], v.shInc[j])); v.shPh[j] += v.shInc[j]; if (v.shPh[j] >= 1) v.shPh[j] -= 1; }
                sh *= p.padShimmer * 0.9f;
                l += sub + sh * 0.6f; r += sub + sh * 0.6f;
                // TPT state variable lowpass (12dB) per channel
                auto svf = [&] (float x, float* z) { float h = (x - (v.k + v.g) * z[0] - z[1]) / (1 + v.g * (v.g + v.k)); float b = v.g * h + z[0]; z[0] = v.g * h + b; float lp = v.g * b + z[1]; z[1] = v.g * b + lp; return lp; };
                l = svf (l, v.fL); r = svf (r, v.fR);
                float a = e * 0.1f * level;
                L[s] += l * a; R[s] += r * a;
            }
        }
    }
};

// ------------------------------------------------------------------ PIANO (modal grand + FM electric piano)
struct PianoVoice : VoiceBase {
    static constexpr int NP = 24;
    int np = 0; double c[NP * 2] {}, sn[NP * 2] {}, cw[NP * 2] {}, sw[NP * 2] {}; float amp[NP * 2] {}, dec[NP * 2] {}, damp[NP * 2] {};
    float hammer = 0; Biquad hammerF; float pan = 0.5f; bool damped = false; float relGain = 1, relC = 1;
    // EP
    float cph = 0, cinc = 0, mph = 0, minc = 0, tph = 0, tinc = 0, idx = 0, idxDec = 0, epAmp = 0, epDec = 0, barkDec = 0, bark = 0;
};

class Piano {
public:
    std::vector<PianoVoice> voices = std::vector<PianoVoice> (32);
    float fs = 48000; uint32_t clock = 0; Rng rng; float tremPh = 0;
    void prepare (float sr) { fs = sr; }
    void noteOn (int key, int note, float vel, const Params& p) {
        auto* v = allocVoice (voices, clock);
        v->active = true; v->key = key; v->note = note; v->held = true; v->sustained = false; v->vel = vel; v->damped = false; v->relGain = 1; v->relC = 1;
        float f0 = mtof ((float) note);
        float velAmp = (1.0f - p.pianoVelSens) * 0.6f + p.pianoVelSens * std::pow (vel, 1.6f) + 0.05f;
        v->pan = 0.5f + clampf ((note - 64) / 60.0f, -0.4f, 0.4f);
        if (p.pianoType == 0) {
            float B = 0.00008f * std::pow (2.0f, (note - 21) / 18.0f);
            float T0 = 14.0f * std::pow (0.5f, (note - 21) / 22.0f) * p.pianoDecay;
            float bright = 1.9f - 1.1f * clampf (p.pianoTone * 0.6f + vel * 0.6f, 0, 1.2f);
            int np = 0;
            for (int k = 1; k <= PianoVoice::NP; ++k) {
                float fk = k * f0 * std::sqrt (1.0f + B * k * k);
                if (fk > fs * 0.45f || fk > 16000) break;
                float a = std::pow ((float) k, -bright) * std::abs (std::sin (kPi * k * 0.122f)) * velAmp;
                float T = T0 / (1.0f + (k - 1) * 0.35f + fk / 2500.0f);
                for (int sidx = 0; sidx < 2; ++sidx) {
                    int j = np * 2 + sidx;
                    float det = sidx == 0 ? 1.0f : 1.0f + 0.0007f + 0.0003f * rng.next();
                    double w = 2.0 * 3.14159265358979 * fk * det / fs;
                    v->cw[j] = std::cos (w); v->sw[j] = std::sin (w); v->c[j] = 0; v->sn[j] = 0;
                    float phase = rng.next() * 0.2f; v->c[j] = std::cos (phase); v->sn[j] = std::sin (phase);
                    v->amp[j] = a * 0.5f;
                    float TT = sidx == 0 ? T : T * 0.55f; // two-stage-ish decay via unequal strings
                    v->dec[j] = std::exp (-6.9f / (TT * fs));
                    v->damp[j] = std::exp (-6.9f / (0.12f * fs));
                }
                ++np;
            }
            v->np = np;
            v->hammer = velAmp * (0.4f + 0.6f * vel); v->hammerF.setLP (fs, 800 + 3000 * vel + 2000 * p.pianoTone, 0.7f);
        } else {
            v->np = 0; v->hammer = 0;
            v->cph = 0; v->cinc = f0 / fs; v->mph = 0; v->minc = f0 / fs; v->tph = 0; v->tinc = f0 * 14.0f / fs;
            v->idx = (0.4f + 2.2f * vel * (0.5f + p.pianoTone)) ; v->idxDec = std::exp (-1.0f / (fs * 0.35f));
            v->epAmp = velAmp; v->epDec = std::exp (-6.9f / (fs * 6.0f * std::pow (0.5f, (note - 60) / 24.0f) * p.pianoDecay));
            v->bark = vel * vel * (0.3f + p.pianoTone * 0.5f); v->barkDec = std::exp (-1.0f / (fs * 0.02f));
        }
    }
    void release (PianoVoice& v, const Params& p) {
        v.held = false;
        if (v.note >= 89 && p.pianoType == 0) return; // top strings have no dampers
        v.damped = true; v.relC = std::exp (-6.9f / (fs * (p.pianoType == 0 ? 0.25f : 0.18f)));
    }
    void render (float* L, float* R, int n, const Params& p, float level) {
        const auto& st = sineTab();
        float tremInc = 4.5f / fs;
        for (auto& v : voices) {
            if (! v.active) continue;
            float pl = std::sqrt (1.0f - v.pan), pr = std::sqrt (v.pan);
            float tph = tremPh;
            for (int s = 0; s < n; ++s) {
                float o = 0;
                if (p.pianoType == 0 || v.np > 0) {
                    for (int j = 0; j < v.np * 2; ++j) {
                        double c = v.c[j] * v.cw[j] - v.sn[j] * v.sw[j];
                        double sn = v.c[j] * v.sw[j] + v.sn[j] * v.cw[j];
                        v.c[j] = c; v.sn[j] = sn;
                        o += (float) sn * v.amp[j]; v.amp[j] *= v.dec[j];
                    }
                    if (v.hammer > 1e-4f) { o += v.hammerF.process (rng.next()) * v.hammer * 0.25f; v.hammer *= 0.9985f; }
                }
                if (v.np == 0) { // electric piano
                    float m = st (v.mph) * v.idx + st (v.tph) * v.bark * 0.3f;
                    float ph = v.cph + m * 0.15f; ph -= std::floor (ph);
                    float x = st (ph) * v.epAmp;
                    x = std::tanh (x * (1.0f + v.bark * 2.0f)) / (1.0f + v.bark);
                    o += x * 1.4f;
                    v.cph += v.cinc; if (v.cph >= 1) v.cph -= 1; v.mph += v.minc; if (v.mph >= 1) v.mph -= 1; v.tph += v.tinc; if (v.tph >= 1) v.tph -= 1;
                    v.idx = 0.25f + (v.idx - 0.25f) * v.idxDec; v.bark *= v.barkDec; v.epAmp *= v.epDec;
                }
                if (v.damped) v.relGain *= v.relC;
                o *= v.relGain * 0.4f * level;
                float tl = 1, tr = 1;
                if (p.pianoType == 1 && p.epTrem > 0) { float t = st (tph) * p.epTrem; tl = 1 - 0.5f * (1 + t) * p.epTrem; tr = 1 - 0.5f * (1 - t) * p.epTrem; tph += tremInc; if (tph >= 1) tph -= 1; }
                L[s] += o * pl * tl; R[s] += o * pr * tr;
            }
            float alive = v.relGain * (v.np > 0 ? v.amp[0] + (v.np > 1 ? v.amp[2] : 0) : v.epAmp);
            if (alive < 1e-5f && v.hammer < 1e-4f) v.active = false;
        }
        tremPh += tremInc * n; tremPh -= std::floor (tremPh);
    }
};

// ------------------------------------------------------------------ FX
class StereoDelay {
public:
    DelayLine l, r; float fs = 48000; Smooth time; OnePole lpL, lpR; Biquad hpL, hpR; Smooth mix;
    void prepare (float sr) { fs = sr; l.init ((int) (fs * 3)); r.init ((int) (fs * 3)); time.set (fs, 120); mix.set (fs, 30); hpL.setHP (fs, 150, 0.7f); hpR.setHP (fs, 150, 0.7f); time.v = time.t = 0.4f * fs; }
    void process (float* L, float* R, int n, const Params& p) {
        float t = p.delaySync ? (float) (60.0 / std::max (30.0, p.bpm)) * delayDivBeats (p.delayDiv) : p.delayTimeMs * 0.001f;
        time.t = clampf (t, 0.01f, 2.9f) * fs; mix.t = p.delayOn ? p.delayMix : 0.0f;
        lpL.set (fs, p.delayHiCut); lpR.set (fs, p.delayHiCut);
        float fb = clampf (p.delayFb, 0, 0.95f);
        for (int s = 0; s < n; ++s) {
            float d = time.next(), m = mix.next();
            float wl = l.read (d), wr = r.read (d);
            float inL = L[s], inR = R[s];
            float fl = hpL.process (lpL.lp (wl)) * fb, fr = hpR.process (lpR.lp (wr)) * fb;
            if (p.pingPong) { l.push (0.5f * (inL + inR) + fr); r.push (fl); }
            else { l.push (inL + fl); r.push (inR + fr); }
            L[s] = inL + wl * m; R[s] = inR + wr * m;
        }
    }
};

class PitchShifter { // octave-up granular shifter
public:
    DelayLine buf; float ph = 0, W = 2048;
    void prepare (float fs) { buf.init ((int) (fs * 0.2f)); W = fs * 0.05f; }
    inline float process (float x) {
        buf.push (x);
        ph += 1.0f / W; if (ph >= 1) ph -= 1;   // ratio 2: delay shrinks 1 sample per sample
        float p2 = ph + 0.5f; if (p2 >= 1) p2 -= 1;
        float d1 = W * (1.0f - ph), d2 = W * (1.0f - p2);
        float w1 = std::sin (kPi * ph), w2 = std::sin (kPi * p2);
        return buf.read (d1 + 1) * w1 + buf.read (d2 + 1) * w2;
    }
};

class Reverb { // 8-line FDN with diffusion, damping, modulation, shimmer
public:
    static constexpr int NL = 8;
    float fs = 48000; DelayLine lines[NL], pre, ap[4]; float apLen[4] {}; float baseMs[NL] = { 29.7f, 37.1f, 41.1f, 43.7f, 53.3f, 61.3f, 71.9f, 79.1f };
    OnePole damp[NL]; Smooth size, mix; float lfo = 0; PitchShifter shift; float shimFb = 0; Biquad shimHP, inHP; float gains[NL] {}; uint32_t cnt = 0;
    void prepare (float sr) {
        fs = sr; for (auto& d : lines) d.init ((int) (fs * 0.2f)); pre.init ((int) (fs * 0.25f));
        const float apMs[4] = { 4.77f, 3.59f, 12.7f, 9.3f }; for (int i = 0; i < 4; ++i) { ap[i].init ((int) (fs * 0.02f)); apLen[i] = apMs[i] * 0.001f * fs; }
        size.set (fs, 300); mix.set (fs, 30); shift.prepare (fs); shimHP.setHP (fs, 400, 0.7f); inHP.setHP (fs, 100, 0.7f); size.v = size.t = 1.0f;
    }
    void process (float* L, float* R, int n, const Params& p) {
        size.t = 0.4f + 1.1f * clampf (p.revSize, 0, 1); mix.t = p.reverbOn ? p.revMix : 0.0f;
        for (auto& d : damp) d.set (fs, 1500.0f + (1.0f - p.revDamp) * 12000.0f);
        float preD = clampf (p.revPredelay, 0, 200) * 0.001f * fs;
        float decay = clampf (p.revDecay, 0.3f, 20.0f);
        for (int s = 0; s < n; ++s) {
            float sz = size.next(), m = mix.next();
            float in = inHP.process (0.5f * (L[s] + R[s])) + shimFb;
            pre.push (in); float x = pre.read (preD);
            for (int i = 0; i < 4; ++i) { float d = ap[i].read (apLen[i]); float v = x + 0.6f * d; ap[i].push (v); x = d - 0.6f * v; }
            lfo += 0.3f / fs; if (lfo >= 1) lfo -= 1;
            float o[NL];
            if ((cnt++ & 31) == 0) for (int i = 0; i < NL; ++i) gains[i] = std::pow (10.0f, -3.0f * baseMs[i] * 0.001f * sz / decay);
            for (int i = 0; i < NL; ++i) {
                float len = baseMs[i] * 0.001f * fs * sz + (i < 2 ? sineTab() (std::fmod (lfo + i * 0.5f, 1.0f)) * 8.0f : 0.0f);
                float g = gains[i];
                o[i] = damp[i].lp (lines[i].read (len)) * g;
            }
            // Hadamard 8
            for (int h = 1; h < NL; h <<= 1) for (int i = 0; i < NL; i += h * 2) for (int j = i; j < i + h; ++j) { float a = o[j], b = o[j + h]; o[j] = a + b; o[j + h] = a - b; }
            const float nrm = 0.35355339f;
            for (int i = 0; i < NL; ++i) lines[i].push (o[i] * nrm + x * ((i & 1) ? -0.5f : 0.5f));
            float wl = (o[0] + o[2] + o[4] + o[6]) * nrm * 0.6f, wr = (o[1] + o[3] + o[5] + o[7]) * nrm * 0.6f;
            if (p.revShimmer > 0.001f && p.reverbOn) shimFb = shimHP.process (shift.process (0.5f * (wl + wr))) * p.revShimmer * 0.55f; else shimFb = 0;
            L[s] += wl * m; R[s] += wr * m;
        }
    }
};

// ------------------------------------------------------------------ ENGINE
class Engine {
public:
    Organ organ; Pads pads; Piano piano; StereoDelay delay; Reverb reverb;
    Biquad hpL[2], hpR[2], lsL, lsR, pkL, pkR, hsL, hsR;
    Params p; float fs = 48000; bool sustainDown = false; float modWheel = 0; float expression = 1.0f;
    Smooth lvOrgan, lvPad, lvPiano, master; float limEnv = 0; float lastEq[5] { -1, -1, -1, -1, -1 };
    bool keyHeld[128] {};

    void prepare (double sr) {
        fs = (float) sr; organ.prepare (fs); pads.prepare (fs); piano.prepare (fs); delay.prepare (fs); reverb.prepare (fs);
        for (auto* s : { &lvOrgan, &lvPad, &lvPiano, &master }) s->set (fs, 40);
        for (auto& l : lastEq) l = -1;
    }
    void setParams (const Params& np) {
        p = np;
        lvOrgan.t = p.organOn ? p.organLevel * p.organLevel : 0.0f;
        lvPad.t = p.padOn ? p.padLevel * p.padLevel : 0.0f;
        lvPiano.t = p.pianoOn ? p.pianoLevel * p.pianoLevel : 0.0f;
        master.t = dbToGain (p.master) * expression;
        float eq[5] = { p.lowCut, p.eqLow, p.eqMid, p.eqHigh, fs };
        if (std::memcmp (eq, lastEq, sizeof eq) != 0) {
            std::memcpy (lastEq, eq, sizeof eq);
            for (int i = 0; i < 2; ++i) { hpL[i].setHP (fs, p.lowCut, 0.707f); hpR[i].setHP (fs, p.lowCut, 0.707f); }
            lsL.setShelf (fs, 150, p.eqLow, false); lsR = lsL; lsR.reset();
            pkL.setPeak (fs, 1200, 0.7f, p.eqMid); pkR.setPeak (fs, 1200, 0.7f, p.eqMid);
            hsL.setShelf (fs, 7000, p.eqHigh, true); hsR.setShelf (fs, 7000, p.eqHigh, true);
        }
    }
    bool inZone (int zone, int key) const { if (! p.splitOn || zone == 0) return true; return zone == 1 ? key < p.splitNote : key >= p.splitNote; }

    void noteOn (int key, float vel) {
        if (key < 0 || key > 127) return;
        keyHeld[key] = true;
        int note = std::clamp (key + p.transpose, 0, 127);
        if (p.organOn && inZone (p.organZone, key)) organ.noteOn (key, note, vel, p);
        if (p.padOn && inZone (p.padZone, key)) pads.noteOn (key, note, vel, p);
        if (p.pianoOn && inZone (p.pianoZone, key)) piano.noteOn (key, note, vel, p);
    }
    template <class E, class V> void offFor (E& e, std::vector<V>& vs, int key, bool useSustain) {
        for (auto& v : vs) if (v.active && v.held && v.key == key) { if (useSustain && sustainDown) { v.held = false; v.sustained = true; } else e.release (v, p); }
    }
    void noteOff (int key) {
        if (key < 0 || key > 127) return;
        keyHeld[key] = false;
        offFor (organ, organ.voices, key, p.organSustain); offFor (pads, pads.voices, key, true); offFor (piano, piano.voices, key, true);
    }
    template <class E, class V> void releaseSustained (E& e, std::vector<V>& vs) { for (auto& v : vs) if (v.active && v.sustained) { v.sustained = false; e.release (v, p); } }
    void setSustain (bool down) {
        sustainDown = down;
        if (! down) { releaseSustained (organ, organ.voices); releaseSustained (pads, pads.voices); releaseSustained (piano, piano.voices); }
    }
    void allNotesOff() {
        for (int k = 0; k < 128; ++k) keyHeld[k] = false;
        sustainDown = false;
        for (auto& v : organ.voices) if (v.active) organ.release (v, p);
        for (auto& v : pads.voices) if (v.active) { v.sustained = false; pads.release (v, p); }
        for (auto& v : piano.voices) if (v.active) { v.sustained = false; piano.release (v, p); v.damped = true; v.relC = std::exp (-6.9f / (fs * 0.1f)); }
    }
    void controller (int cc, int val) {
        if (cc == 64) setSustain (val >= 64);
        else if (cc == 1) modWheel = val / 127.0f;
        else if (cc == 11) { expression = val / 127.0f; master.t = dbToGain (p.master) * expression; }
        else if (cc == 120 || cc == 123) allNotesOff();
    }
    void render (float* L, float* R, int n) {
        std::fill (L, L + n, 0.0f); std::fill (R, R + n, 0.0f);
        float lo = lvOrgan.v, lp = lvPad.v, lpi = lvPiano.v;
        for (int i = 0; i < n; ++i) { lvOrgan.next(); lvPad.next(); lvPiano.next(); }
        float lvO = 0.5f * (lo + lvOrgan.v), lvP = 0.5f * (lp + lvPad.v), lvPi = 0.5f * (lpi + lvPiano.v);
        organ.render (L, R, n, p, lvO, modWheel >= 0.5f);
        pads.render (L, R, n, p, lvP, 0.0f);
        piano.render (L, R, n, p, lvPi);
        for (int s = 0; s < n; ++s) {
            float l = L[s], r = R[s];
            for (int i = 0; i < 2; ++i) { l = hpL[i].process (l); r = hpR[i].process (r); }
            l = hsL.process (pkL.process (lsL.process (l))); r = hsR.process (pkR.process (lsR.process (r)));
            L[s] = l; R[s] = r;
        }
        delay.process (L, R, n, p);
        reverb.process (L, R, n, p);
        const float rel = std::exp (-1.0f / (fs * 0.15f));
        for (int s = 0; s < n; ++s) {
            float g = master.next(); float l = L[s] * g, r = R[s] * g;
            float pk = std::max (std::abs (l), std::abs (r));
            limEnv = pk > limEnv ? pk : limEnv * rel + pk * (1 - rel);
            float gr = limEnv > 0.9f ? 0.9f / limEnv : 1.0f;
            l *= gr; r *= gr;
            if (! std::isfinite (l)) l = 0; if (! std::isfinite (r)) r = 0;
            L[s] = clampf (l, -1, 1); R[s] = clampf (r, -1, 1);
        }
    }
};

} // namespace wk
