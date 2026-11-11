// Bearing Live - pedal implementations. Pure C++17. Each class = one pedal; register it in pedalCatalog()/createPedal().
#include "Oversampler.h"
#include "Pedal.h"
#include <map>
#include <functional>

namespace wr {
namespace {

inline float dbg (float db) { return std::pow (10.0f, db / 20.0f); }
inline float softClip (float x) { return std::tanh (x); }
inline float asymClip (float x, float bias) { return std::tanh (x + bias) - std::tanh (bias); }
inline float divBeats (int d) { static const float b[] = { 1.0f, 0.75f, 2.0f / 3.0f, 0.5f, 1.0f / 3.0f, 0.25f, 1.5f, 2.0f }; return b[std::clamp (d, 0, 7)]; }
const std::vector<std::string> kDivs = { "1/4", "1/8.", "1/4T", "1/8", "1/8T", "1/16", "1/4.", "1/2" };

// Simple linear-interp granular pitch shifter (two crossfaded taps). ratio 2 = octave up, 0.5 = octave down.
struct GrainShifter {
    DelayLine buf; float ph = 0, W = 2048, ratio = 1;
    void prepare (float fs, float winMs) { buf.init ((int) (fs * 0.25f)); W = fs * winMs * 0.001f; ph = 0; }
    void clear() { buf.clear(); }
    inline float process (float x) {
        buf.push (x);
        ph += (1.0f - ratio) / W; while (ph >= 1) ph -= 1; while (ph < 0) ph += 1;
        float p2 = ph + 0.5f; if (p2 >= 1) p2 -= 1;
        return buf.read (ph * W + 1) * std::sin (kPi * ph) + buf.read (p2 * W + 1) * std::sin (kPi * p2);
    }
};

struct Env { float a = 0, r = 0, v = 0; void set (float fs, float atkMs, float relMs) { a = std::exp (-1.0f / (0.001f * atkMs * fs)); r = std::exp (-1.0f / (0.001f * relMs * fs)); }
             inline float run (float x) { x = std::abs (x); v = x > v ? x + (v - x) * a : x + (v - x) * r; return v; } };

// ============================================================ DYNAMICS
class Compressor : public Pedal {
public:
    Compressor() { type = "comp"; displayName = "Comp"; category = PedalCategory::Dynamics;
        addParam ("sustain", "Sustain", 0, 1, 0.5f); addParam ("attack", "Attack", 1, 50, 10, "ms"); addParam ("level", "Level", -12, 12, 0, "dB"); addParam ("blend", "Blend", 0, 1, 0.8f); }
    enum { kSus, kAtk, kLvl, kBlend };
    Env env; float gr = 1;
    void onParamsChanged() override { env.set (fs_, p (kAtk), 180); }
    void onReset() override { env.v = 0; gr = 1; }
    void render (float* L, float* R, int n, const ProcessContext&) override {
        const float thr = dbg (-10.0f - 30.0f * p (kSus)), ratio = 2.0f + 10.0f * p (kSus), mk = dbg (p (kLvl) + 18.0f * p (kSus) * 0.6f), bl = p (kBlend);
        for (int s = 0; s < n; ++s) {
            float e = env.run (0.5f * (L[s] + R[s]));
            float g = e > thr ? std::pow (e / thr, 1.0f / ratio - 1.0f) : 1.0f;
            gr = g; float k = (g * bl + (1 - bl)) * mk; L[s] *= k; R[s] *= k;
        }
    }
};

class VolumeSwell : public Pedal {
public:
    VolumeSwell() { type = "swell"; displayName = "Volume Swell"; category = PedalCategory::Dynamics;
        addParam ("attack", "Rise", 50, 3000, 600, "ms"); addParam ("sens", "Sens", 0, 1, 0.5f); }
    Env fast; float g = 0, lastE = 0; int hold = 0;
    void onParamsChanged() override { fast.set (fs_, 1, 30); }
    void onReset() override { g = 0; lastE = 0; hold = 0; }
    void render (float* L, float* R, int n, const ProcessContext&) override {
        const float thr = 0.002f + 0.05f * (1.0f - p (1)), rise = 1.0f / (0.001f * p (0) * fs_);
        for (int s = 0; s < n; ++s) {
            float e = fast.run (0.5f * (L[s] + R[s]));
            if (e > thr && e > lastE * 1.8f + thr * 0.5f && hold == 0) { g = 0; hold = (int) (0.05f * fs_); } // new pick attack: restart swell
            if (hold > 0) --hold;
            lastE = lastE * 0.999f + e * 0.001f;
            g = std::min (1.0f, g + rise); float k = g * g; L[s] *= k; R[s] *= k;
        }
    }
};

// ============================================================ DRIVES
class Boost : public Pedal {
public:
    Boost() { type = "boost"; displayName = "Boost"; category = PedalCategory::Drive;
        addParam ("gain", "Gain", 0, 15, 6, "dB"); addParam ("bass", "Bass", -6, 6, 0, "dB"); addParam ("treble", "Treble", -6, 6, 1, "dB"); }
    Biquad bl, br, tl, tr;
    void onParamsChanged() override { bl.setShelf (fs_, 150, p (1), false); br.b0 = bl.b0; br.b1 = bl.b1; br.b2 = bl.b2; br.a1 = bl.a1; br.a2 = bl.a2;
        tl.setShelf (fs_, 3000, p (2), true); tr.b0 = tl.b0; tr.b1 = tl.b1; tr.b2 = tl.b2; tr.a1 = tl.a1; tr.a2 = tl.a2; }
    void onReset() override { bl.reset(); br.reset(); tl.reset(); tr.reset(); }
    void render (float* L, float* R, int n, const ProcessContext&) override {
        const float g = dbg (p (0));
        for (int s = 0; s < n; ++s) { L[s] = tl.process (bl.process (L[s])) * g; R[s] = tr.process (br.process (R[s])) * g; }
    }
};

// Generic mono-summed drive with pre/post filters. Subclasses define the clipper and voicing.
// v0.12: the clipper runs oversampled (Off/2x/4x/8x). The clean blend goes through the same up/down filters so the
// clean + clipped mix stays phase-aligned (no comb filtering). Filters INSIDE clip() must be designed at clipFs().
class DriveBase : public Pedal {
public:
    Biquad pre, post1, post2, aaIn, tone; float bias = 0; Oversampler os;
    void onReset() override { pre.reset(); post1.reset(); post2.reset(); aaIn.reset(); tone.reset(); os.reset(); }
    void onPrepareBase() override { os.prepare (maxBlock_, 2); os.setFactor (osFactor_); }
    void onOversampling() override { os.setFactor (osFactor_); onClipRate(); }
    bool oversamples() const override { return true; }
    double latencySamples() const override { return Oversampler::latencySamples (osFactor_); }
    float clipFs() const { return fs_ * (float) os.factor(); }
    virtual void onClipRate() {}          // re-design filters used inside clip() at clipFs()
    virtual float clip (float x) = 0;
    virtual float drivePath (float x) { return clip (x); }
    float preGain = 1, outGain = 1, cleanMix = 0;
    void render (float* L, float* R, int n, const ProcessContext&) override {
        for (int s = 0; s < n; ++s) { const float x = 0.5f * (L[s] + R[s]); L[s] = pre.process (aaIn.process (x)) * preGain; R[s] = x; }
        float* u = os.up (0, L, n); os.up (1, R, n); const int m = n * os.factor();
        for (int i = 0; i < m; ++i) u[i] = drivePath (u[i]);
        os.down (0, L, n); os.down (1, R, n);
        for (int s = 0; s < n; ++s) { float y = tone.process (post2.process (post1.process (L[s]))); y = (y + R[s] * cleanMix) * outGain; L[s] = y; R[s] = y; }
    }
};

class Overdrive : public DriveBase { // Tube Screamer-ish: mid hump, soft clip, tone LP
public:
    Overdrive() { type = "overdrive"; displayName = "Overdrive"; category = PedalCategory::Drive;
        addParam ("drive", "Drive", 0, 1, 0.4f); addParam ("tone", "Tone", 0, 1, 0.5f); addParam ("level", "Level", -18, 12, 0, "dB"); }
    void onPrepare() override { aaIn.setLP (fs_, 9000, 0.707f); post1.setLP (fs_, 7000, 0.707f); post2.setHP (fs_, 60, 0.707f); }
    void onParamsChanged() override {
        pre.setHP (fs_, 720, 0.6f); preGain = 1.0f + 60.0f * p (0) * p (0);
        tone.setLP (fs_, 900.0f * std::pow (8.0f, p (1)), 0.707f); outGain = dbg (p (2)) * 0.6f; cleanMix = 0.6f;
    }
    float clip (float x) override { return softClip (x) * 0.7f; }
};

class Klon : public DriveBase { // clean + clipped blend, treble control
public:
    Klon() { type = "klon"; displayName = "Transparent Boost-Drive"; category = PedalCategory::Drive;
        addParam ("gain", "Gain", 0, 1, 0.3f); addParam ("treble", "Treble", 0, 1, 0.5f); addParam ("output", "Output", -18, 12, 0, "dB"); }
    void onPrepare() override { aaIn.setLP (fs_, 10000, 0.707f); post1.setLP (fs_, 8000, 0.707f); post2.setHP (fs_, 40, 0.707f); }
    void onParamsChanged() override {
        pre.setPeak (fs_, 1000, 0.7f, 6.0f * p (0)); preGain = 1.0f + 30.0f * p (0);
        tone.setShelf (fs_, 2000, -8.0f + 16.0f * p (1), true); outGain = dbg (p (2)) * 0.7f; cleanMix = 1.0f - 0.6f * p (0);
    }
    float clip (float x) override { return std::clamp (x, -1.0f, 1.0f) * 0.4f + softClip (x) * 0.3f; }
};

class Distortion : public DriveBase { // RAT-ish: big gain, hard clip, filter
public:
    Distortion() { type = "distortion"; displayName = "Distortion"; category = PedalCategory::Drive;
        addParam ("dist", "Distortion", 0, 1, 0.5f); addParam ("filter", "Filter", 0, 1, 0.4f); addParam ("volume", "Volume", -24, 6, -6, "dB"); }
    void onPrepare() override { aaIn.setLP (fs_, 8000, 0.707f); post1.setLP (fs_, 6500, 0.707f); post2.setHP (fs_, 80, 0.707f); }
    void onParamsChanged() override { pre.setHP (fs_, 300, 0.707f); preGain = 2.0f + 400.0f * p (0) * p (0); tone.setLP (fs_, 7000.0f * std::pow (0.07f, p (1)), 0.707f); outGain = dbg (p (2)) * 0.5f; cleanMix = 0; }
    float clip (float x) override { return std::clamp (x, -0.7f, 0.7f) + 0.1f * softClip (x); }
};

class Fuzz : public DriveBase { // Fuzz Face-ish: asymmetric bias, gated sputter at low "bias"
public:
    Fuzz() { type = "fuzz"; displayName = "Fuzz"; category = PedalCategory::Drive;
        addParam ("fuzz", "Fuzz", 0, 1, 0.7f); addParam ("tone", "Tone", 0, 1, 0.5f); addParam ("volume", "Volume", -24, 6, -8, "dB"); addParam ("bias", "Bias", 0, 1, 0.6f); }
    void onPrepare() override { aaIn.setLP (fs_, 7000, 0.707f); post1.setLP (fs_, 6000, 0.707f); post2.setHP (fs_, 70, 0.707f); }
    void onParamsChanged() override { pre.setHP (fs_, 120, 0.707f); preGain = 5.0f + 300.0f * p (0); tone.setLP (fs_, 1200.0f * std::pow (6.0f, p (1)), 0.6f); outGain = dbg (p (2)) * 0.6f; cleanMix = 0; bias = 0.15f + 0.5f * (1.0f - p (3)); }
    float clip (float x) override { float y = asymClip (x, bias); return std::clamp (y * 1.4f, -0.9f, 0.6f); }
};

// ============================================================ PITCH
class OctaveDown : public Pedal { // analog-style flip-flop octave divider (Octaver / OC-2)
public:
    OctaveDown() { type = "octdown"; displayName = "Octave Down"; category = PedalCategory::Pitch;
        addParam ("sub1", "Oct -1", 0, 1, 0.7f); addParam ("sub2", "Oct -2", 0, 1, 0.0f); addParam ("dry", "Dry", 0, 1, 0.8f); addParam ("tone", "Tone", 0, 1, 0.4f); }
    Biquad trk1, trk2, outLp, outLp2; Env env; float ff1 = 1, ff2 = 1, last = 0; bool armed = true;
    void onPrepare() override { trk1.setLP (fs_, 700, 0.707f); trk2.setLP (fs_, 700, 0.707f); env.set (fs_, 2, 60); }
    void onParamsChanged() override { float f = 200.0f * std::pow (10.0f, p (3)); outLp.setLP (fs_, f, 0.707f); outLp2.setLP (fs_, f, 0.707f); }
    void onReset() override { trk1.reset(); trk2.reset(); outLp.reset(); outLp2.reset(); env.v = 0; ff1 = ff2 = 1; last = 0; }
    void render (float* L, float* R, int n, const ProcessContext&) override {
        for (int s = 0; s < n; ++s) {
            float x = 0.5f * (L[s] + R[s]); float e = env.run (x); float t = trk2.process (trk1.process (x));
            float hyst = 0.08f * e;
            if (armed && t > hyst) { armed = false; ff1 = -ff1; if (ff1 > 0) ff2 = -ff2; }
            else if (! armed && t < -hyst) armed = true;
            float sub = outLp2.process (outLp.process (ff1 * e * p (0) + ff2 * e * p (1))) * 1.6f;
            float y = x * p (2) + sub; L[s] = y; R[s] = y;
        }
    }
};

class PolyOctave : public Pedal { // POG-style: granular -1 / +1 / +2 octaves
public:
    PolyOctave() { type = "polyoct"; displayName = "Poly Octave"; category = PedalCategory::Pitch;
        addParam ("sub", "Oct -1", 0, 1, 0.5f); addParam ("up1", "Oct +1", 0, 1, 0.5f); addParam ("up2", "Oct +2", 0, 1, 0.0f); addParam ("dry", "Dry", 0, 1, 0.8f); }
    GrainShifter dn, u1, u2;
    void onPrepare() override { dn.prepare (fs_, 60); dn.ratio = 0.5f; u1.prepare (fs_, 35); u1.ratio = 2.0f; u2.prepare (fs_, 25); u2.ratio = 4.0f; }
    void onReset() override { dn.clear(); u1.clear(); u2.clear(); }
    void render (float* L, float* R, int n, const ProcessContext&) override {
        for (int s = 0; s < n; ++s) { float x = 0.5f * (L[s] + R[s]); float y = x * p (3) + dn.process (x) * p (0) + u1.process (x) * p (1) + u2.process (x) * p (2) * 0.7f; L[s] = y; R[s] = y; }
    }
};

class Harmonizer : public Pedal { // fixed interval harmony or Whammy sweep (position 0..1 -> 0..interval)
public:
    Harmonizer() { type = "harmonizer"; displayName = "Harmonizer / Pitch Bend"; category = PedalCategory::Pitch;
        addParam ("interval", "Interval", -24, 24, 12, "st"); addParam ("position", "Position", 0, 1, 1); addParam ("mix", "Mix", 0, 1, 0.5f); addChoice ("mode", "Mode", { "Harmony", "Whammy" }, 0); }
    GrainShifter sh;
    void onPrepare() override { sh.prepare (fs_, 40); }
    void onReset() override { sh.clear(); }
    void render (float* L, float* R, int n, const ProcessContext&) override {
        float semis = std::round (p (0)) * (pi (3) == 1 ? p (1) : 1.0f); sh.ratio = std::pow (2.0f, semis / 12.0f);
        bool whammy = pi (3) == 1; float m = p (2);
        for (int s = 0; s < n; ++s) { float x = 0.5f * (L[s] + R[s]); float w = sh.process (x); float y = whammy ? w * m + x * (1 - m) : x + w * m; L[s] = y; R[s] = y; }
    }
};

// Pitch tracking -> worship-keys engine (monophonic in v0.1).
class TrackedKeys : public Pedal {
public:
    wk::Engine eng; wk::Params kp; int curNote = -1, cand = -1, candCount = 0; float envSm = 0; std::vector<float> bl, br;
    void onPrepare() override { eng.prepare (fs_); bl.assign ((size_t) maxBlock_, 0); br.assign ((size_t) maxBlock_, 0); applyVoice(); }
    void onReset() override { eng.allNotesOff(); curNote = -1; cand = -1; }
    virtual void applyVoice() = 0;
    virtual float dryLevel() = 0; virtual float wetLevel() = 0; virtual int octaveShift() { return 0; }
    void render (float* L, float* R, int n, const ProcessContext& ctx) override {
        float lvl = ctx.inputLevel, gate = 0.01f;
        if (ctx.trackerRunning) {
            int want = -1;
            if (lvl > gate && ctx.pitchConf > 0.8f && ctx.pitchHz > 0) want = (int) std::lround (69.0f + 12.0f * std::log2 (ctx.pitchHz / 440.0f)) + 12 * octaveShift();
            if (lvl < gate * 0.6f) want = -2; // release
            if (want == -2) { if (curNote >= 0) { eng.noteOff (curNote); curNote = -1; } cand = -1; }
            else if (want >= 0 && want != curNote) {
                if (want == cand) ++candCount; else { cand = want; candCount = 1; }
                if (candCount >= 2) { if (curNote >= 0) eng.noteOff (curNote); curNote = std::clamp (want, 0, 127); eng.noteOn (curNote, clampf (0.3f + lvl * 3.0f, 0.2f, 1.0f)); cand = -1; }
            }
        }
        eng.render (bl.data(), br.data(), n);
        const float d = dryLevel(), w = wetLevel();
        for (int s = 0; s < n; ++s) { L[s] = L[s] * d + bl[(size_t) s] * w; R[s] = R[s] * d + br[(size_t) s] * w; }
    }
};

class Key9 : public TrackedKeys {
public:
    Key9() { type = "key9"; displayName = "Guitar-to-Piano"; category = PedalCategory::Synth; wantsPitch = true;
        addChoice ("voice", "Voice", { "Grand", "E.Piano", "Organ", "Hymn Organ" }, 1); addParam ("keys", "Keys", 0, 1, 0.8f); addParam ("dry", "Dry", 0, 1, 0.3f); addParam ("tone", "Tone", 0, 1, 0.5f); }
    int lastVoice = -1;
    void applyVoice() override {
        kp = wk::Params(); kp.delayOn = false; kp.reverbOn = false; kp.master = -3; kp.organOn = kp.padOn = kp.pianoOn = false;
        int v = pi (0); lastVoice = v;
        if (v <= 1) { kp.pianoOn = true; kp.pianoType = v; kp.pianoTone = p (3); }
        else { kp.organOn = true; kp.organType = v == 3 ? 1 : 0; kp.rotaryOn = v == 2; }
        eng.setParams (kp);
    }
    void onParamsChanged() override { if (pi (0) != lastVoice || std::abs (kp.pianoTone - p (3)) > 0.02f) applyVoice(); }
    float dryLevel() override { return p (2); } float wetLevel() override { return p (1); }
};

class Synth9 : public TrackedKeys {
public:
    Synth9() { type = "synth9"; displayName = "Guitar Synth"; category = PedalCategory::Synth; wantsPitch = true;
        addParam ("cutoff", "Cutoff", 300, 8000, 2500, "Hz"); addParam ("attack", "Attack", 0.005f, 3, 0.3f, "s"); addParam ("synth", "Synth", 0, 1, 0.8f); addParam ("dry", "Dry", 0, 1, 0.2f); addChoice ("octave", "Octave", { "-1", "0", "+1" }, 1); }
    float lastC = -1, lastA = -1;
    void applyVoice() override {
        kp = wk::Params(); kp.delayOn = false; kp.reverbOn = false; kp.master = -3; kp.organOn = kp.pianoOn = false; kp.padOn = true;
        kp.padCutoff = p (0); kp.padAttack = p (1); kp.padRelease = 1.2f; kp.padShimmer = 0.1f; lastC = p (0); lastA = p (1); eng.setParams (kp);
    }
    void onParamsChanged() override { if (std::abs (lastC - p (0)) > 20 || std::abs (lastA - p (1)) > 0.01f) applyVoice(); }
    float dryLevel() override { return p (3); } float wetLevel() override { return p (2); }
    int octaveShift() override { return pi (4) - 1; }
};

// ============================================================ MODULATION
class Chorus : public Pedal {
public:
    Chorus() { type = "chorus"; displayName = "Chorus"; category = PedalCategory::Modulation;
        addParam ("rate", "Rate", 0.05f, 5, 0.6f, "Hz"); addParam ("depth", "Depth", 0, 1, 0.5f); addParam ("mix", "Mix", 0, 1, 0.5f); }
    DelayLine dl, dr; float ph = 0;
    void onPrepare() override { dl.init ((int) (fs_ * 0.05f)); dr.init ((int) (fs_ * 0.05f)); }
    void onReset() override { dl.clear(); dr.clear(); }
    void render (float* L, float* R, int n, const ProcessContext&) override {
        const float inc = p (0) / fs_, base = 0.007f * fs_, dep = 0.004f * fs_ * p (1), m = p (2);
        for (int s = 0; s < n; ++s) {
            ph += inc; if (ph >= 1) ph -= 1;
            dl.push (L[s]); dr.push (R[s]);
            float a = wk::sineTab() (ph), b = wk::sineTab() (std::fmod (ph + 0.25f, 1.0f));
            float wl = dl.read (base + dep * (0.5f + 0.5f * a)), wr = dr.read (base + dep * (0.5f + 0.5f * b));
            L[s] = L[s] * (1 - 0.5f * m) + wl * m; R[s] = R[s] * (1 - 0.5f * m) + wr * m;
        }
    }
};

struct AllPass1 { float a = 0, z = 0; inline float process (float x) { float y = -a * x + z; z = x + a * y; return y; } };

class Phaser : public Pedal {
public:
    Phaser() { type = "phaser"; displayName = "Phaser"; category = PedalCategory::Modulation;
        addParam ("rate", "Rate", 0.05f, 5, 0.4f, "Hz"); addParam ("depth", "Depth", 0, 1, 0.7f); addParam ("feedback", "Feedback", 0, 0.9f, 0.4f); addParam ("mix", "Mix", 0, 1, 0.5f); }
    AllPass1 apL[6], apR[6]; float ph = 0, fbL = 0, fbR = 0;
    void onReset() override { for (auto& a : apL) a.z = 0; for (auto& a : apR) a.z = 0; fbL = fbR = 0; }
    void render (float* L, float* R, int n, const ProcessContext&) override {
        const float inc = p (0) / fs_, dep = p (1), fb = p (2), m = p (3);
        for (int s = 0; s < n; ++s) {
            ph += inc; if (ph >= 1) ph -= 1;
            float lfo = 0.5f + 0.5f * wk::sineTab() (ph), lfo2 = 0.5f + 0.5f * wk::sineTab() (std::fmod (ph + 0.2f, 1.0f));
            auto coef = [&] (float l) { float f = 200.0f * std::pow (20.0f, l * dep); float t = std::tan (kPi * f / fs_); return (1 - t) / (1 + t); };
            float cl = coef (lfo), cr = coef (lfo2); float xl = L[s] + fbL * fb, xr = R[s] + fbR * fb;
            for (int i = 0; i < 6; ++i) { apL[i].a = cl; apR[i].a = cr; xl = apL[i].process (xl); xr = apR[i].process (xr); }
            fbL = xl; fbR = xr; L[s] = L[s] * (1 - 0.5f * m) + xl * m; R[s] = R[s] * (1 - 0.5f * m) + xr * m;
        }
    }
};

class Univibe : public Pedal { // 4 staggered allpass stages driven by a skewed "lamp" LFO
public:
    Univibe() { type = "univibe"; displayName = "Photocell Vibe"; category = PedalCategory::Modulation;
        addParam ("speed", "Speed", 0.3f, 10, 2, "Hz"); addParam ("intensity", "Intensity", 0, 1, 0.7f); addChoice ("mode", "Mode", { "Chorus", "Vibrato" }, 0); }
    AllPass1 ap[4]; float ph = 0, lamp = 0;
    void onReset() override { for (auto& a : ap) a.z = 0; lamp = 0; }
    void render (float* L, float* R, int n, const ProcessContext&) override {
        const float inc = p (0) / fs_, I = p (1); const bool vib = pi (2) == 1; const float lampC = 1.0f - std::exp (-1.0f / (0.004f * fs_));
        static const float stageHz[4] = { 80.0f, 300.0f, 1100.0f, 3000.0f };
        for (int s = 0; s < n; ++s) {
            ph += inc; if (ph >= 1) ph -= 1;
            float t = 0.5f + 0.5f * wk::sineTab() (ph); t = t * t; lamp += (t - lamp) * lampC;
            float x = 0.5f * (L[s] + R[s]), y = x;
            for (int i = 0; i < 4; ++i) { float f = stageHz[i] * (0.4f + 2.0f * lamp * I); float tt = std::tan (kPi * std::min (f, fs_ * 0.4f) / fs_); ap[i].a = (1 - tt) / (1 + tt); y = ap[i].process (y); }
            float o = vib ? y : 0.5f * (x + y); L[s] = o; R[s] = o;
        }
    }
};

class Tremolo : public Pedal {
public:
    Tremolo() { type = "tremolo"; displayName = "Tremolo"; category = PedalCategory::Modulation;
        addParam ("rate", "Rate", 0.5f, 12, 4, "Hz"); addParam ("depth", "Depth", 0, 1, 0.5f); addParam ("shape", "Shape", 0, 1, 0.2f);
        addChoice ("sync", "Sync", { "Free", "1/4", "1/8", "1/8T", "1/16" }, 0); addChoice ("stereo", "Stereo", { "Mono", "Pan" }, 0); }
    float ph = 0;
    void onReset() override { ph = 0; }
    void render (float* L, float* R, int n, const ProcessContext& ctx) override {
        static const float beats[] = { 0, 1.0f, 0.5f, 1.0f / 3.0f, 0.25f };
        float hz = pi (3) == 0 ? p (0) : (float) (ctx.bpm / 60.0) / beats[pi (3)];
        const float inc = hz / fs_, dep = p (1), sh = p (2); const bool pan = pi (4) == 1;
        for (int s = 0; s < n; ++s) {
            ph += inc; if (ph >= 1) ph -= 1;
            auto wave = [&] (float q) { float sn = wk::sineTab() (q); float sq = std::tanh (sn * (1 + 12 * sh)) / std::tanh (1 + 12 * sh); return 0.5f + 0.5f * (sh > 0.01f ? sq : sn); };
            float a = 1.0f - dep * wave (ph), b = pan ? 1.0f - dep * wave (std::fmod (ph + 0.5f, 1.0f)) : a;
            L[s] *= a; R[s] *= b;
        }
    }
};

class Rotary : public Pedal { // horn (AM + doppler) and drum, slow/fast with inertia
public:
    Rotary() { type = "rotary"; displayName = "Rotary"; category = PedalCategory::Modulation;
        addChoice ("speed", "Speed", { "Slow", "Fast" }, 0); addParam ("drive", "Drive", 0, 1, 0.2f); addParam ("mix", "Mix", 0, 1, 1.0f); }
    DelayLine hl, hr; Biquad xlp, xhp; float hph = 0, dph = 0, hs = 0.8f, ds = 0.7f;
    void onPrepare() override { hl.init ((int) (fs_ * 0.02f)); hr.init ((int) (fs_ * 0.02f)); xlp.setLP (fs_, 800, 0.707f); xhp.setHP (fs_, 800, 0.707f); }
    void onReset() override { hl.clear(); hr.clear(); xlp.reset(); xhp.reset(); }
    void render (float* L, float* R, int n, const ProcessContext&) override {
        const bool fast = pi (0) == 1; const float ht = fast ? 6.8f : 0.8f, dt = fast ? 5.9f : 0.7f, acc = 1.0f / (fs_ * 0.9f), dr = 1 + 4 * p (1), m = p (2);
        for (int s = 0; s < n; ++s) {
            hs += (ht - hs) * acc * 1.6f; ds += (dt - ds) * acc * 0.5f;
            hph += hs / fs_; if (hph >= 1) hph -= 1; dph += ds / fs_; if (dph >= 1) dph -= 1;
            float x = std::tanh (0.5f * (L[s] + R[s]) * dr) / dr * (1 + 0.3f * p (1));
            float lo = xlp.process (x), hi = xhp.process (x);
            float sh = wk::sineTab() (hph), ch = wk::sineTab() (std::fmod (hph + 0.25f, 1.0f)), sd = wk::sineTab() (dph);
            hl.push (hi); hr.push (hi);
            float hL = hl.read (fs_ * 0.0012f * (1 + sh)) * (0.75f + 0.25f * ch), hR = hr.read (fs_ * 0.0012f * (1 - sh)) * (0.75f - 0.25f * ch);
            float dL = lo * (0.85f + 0.15f * sd), dR = lo * (0.85f - 0.15f * sd);
            L[s] = L[s] * (1 - m) + (hL + dL) * m; R[s] = R[s] * (1 - m) + (hR + dR) * m;
        }
    }
};

// ============================================================ TIME / AMBIENT (tails pedals: render() returns WET ONLY)
class Delay : public Pedal {
public:
    Delay() { type = "delay"; displayName = "Delay"; category = PedalCategory::Time; hasTails = true;
        addChoice ("sync", "Sync", { "Free", "Tempo" }, 1); addChoice ("div", "Division", kDivs, 1); addParam ("time", "Time", 40, 2000, 450, "ms");
        addParam ("feedback", "Feedback", 0, 0.95f, 0.35f); addParam ("mix", "Mix", 0, 1, 0.3f); addParam ("tone", "Tone", 0, 1, 0.6f); addParam ("mod", "Mod", 0, 1, 0.2f);
        addChoice ("pingpong", "Ping-Pong", { "Off", "On" }, 1); }
    enum { kSync, kDiv, kTime, kFb, kMix, kTone, kMod, kPP };
    DelayLine l, r; Smooth time; OnePole lpL, lpR; Biquad hpL, hpR; float ph = 0;
    void onPrepare() override { l.init ((int) (fs_ * 2.6f)); r.init ((int) (fs_ * 2.6f)); time.set (fs_, 150); time.v = time.t = 0.45f * fs_; hpL.setHP (fs_, 120, 0.7f); hpR.setHP (fs_, 120, 0.7f); }
    void onParamsChanged() override { float f = 1500.0f * std::pow (8.0f, p (kTone)); lpL.set (fs_, f); lpR.set (fs_, f); }
    void onReset() override { l.clear(); r.clear(); lpL.z = lpR.z = 0; hpL.reset(); hpR.reset(); }
    float delaySeconds (double bpm) const { return pi (kSync) == 1 ? (float) (60.0 / std::max (30.0, bpm)) * divBeats (pi (kDiv)) : getParam (kTime) * 0.001f; }
    void render (float* L, float* R, int n, const ProcessContext& ctx) override {
        time.t = clampf (delaySeconds (ctx.bpm), 0.02f, 2.5f) * fs_;
        const float fb = p (kFb), m = p (kMix), md = p (kMod) * 0.0015f * fs_; const bool pp = pi (kPP) == 1;
        for (int s = 0; s < n; ++s) {
            ph += 0.5f / fs_; if (ph >= 1) ph -= 1;
            float d = time.next() + md * (1 + wk::sineTab() (ph));
            float wl = l.read (d), wr = r.read (d);
            float fl = hpL.process (lpL.lp (wl)) * fb, fr = hpR.process (lpR.lp (wr)) * fb;
            if (pp) { l.push (0.5f * (L[s] + R[s]) + fr); r.push (fl); } else { l.push (L[s] + fl); r.push (R[s] + fr); }
            L[s] = wl * m; R[s] = wr * m;
        }
    }
};

class ReverbBase : public Pedal {
public:
    wk::Reverb rv; wk::Params rp; std::vector<float> tl, tr;
    void onPrepare() override { rv.prepare (fs_); tl.assign ((size_t) maxBlock_, 0); tr.assign ((size_t) maxBlock_, 0); }
    void onReset() override { rv.prepare (fs_); }
    void runWet (float* L, float* R, int n) {
        std::copy (L, L + n, tl.data()); std::copy (R, R + n, tr.data());
        rv.process (L, R, n, rp);
        for (int s = 0; s < n; ++s) { L[s] -= tl[(size_t) s]; R[s] -= tr[(size_t) s]; }
    }
};

class Reverb : public ReverbBase {
public:
    Reverb() { type = "reverb"; displayName = "Reverb"; category = PedalCategory::Ambient; hasTails = true;
        addParam ("mix", "Mix", 0, 1, 0.3f); addParam ("decay", "Decay", 0.5f, 15, 3.5f, "s"); addParam ("size", "Size", 0, 1, 0.7f); addParam ("predelay", "Pre-Delay", 0, 150, 20, "ms"); addParam ("damp", "Damp", 0, 1, 0.5f); }
    void render (float* L, float* R, int n, const ProcessContext&) override {
        rp.reverbOn = true; rp.revMix = p (0); rp.revDecay = p (1); rp.revSize = p (2); rp.revPredelay = p (3); rp.revDamp = p (4); rp.revShimmer = 0; runWet (L, R, n);
    }
};

class Shimmer : public ReverbBase {
public:
    Shimmer() { type = "shimmer"; displayName = "Ambient Shimmer"; category = PedalCategory::Ambient; hasTails = true;
        addParam ("mix", "Mix", 0, 1, 0.4f); addParam ("decay", "Decay", 2, 20, 8, "s"); addParam ("shimmer", "Shimmer", 0, 1, 0.6f); addParam ("damp", "Tone", 0, 1, 0.4f); }
    void render (float* L, float* R, int n, const ProcessContext&) override {
        rp.reverbOn = true; rp.revMix = p (0); rp.revDecay = p (1); rp.revSize = 0.95f; rp.revPredelay = 40; rp.revDamp = p (3); rp.revShimmer = p (2); runWet (L, R, n);
    }
};

// Freeze: on switch-on captures ~0.6 s and sustains it as a granular pad; on switch-off it fades over `release`.
class Freeze : public Pedal {
public:
    Freeze() { type = "freeze"; displayName = "Freeze"; category = PedalCategory::Ambient; hasTails = true;
        addParam ("level", "Level", 0, 1, 0.7f); addParam ("release", "Release", 0.1f, 8, 2.5f, "s"); addParam ("tone", "Tone", 0, 1, 0.6f); }
    std::vector<float> capL, capR; int capLen = 0, capPos = 0; bool capturing = false, holding = false, wasOn = false;
    float gph[3] {}, gst[3] {}, lvl = 0; wk::Rng rng; OnePole lpL, lpR; int grainLen = 1;
    void onPrepare() override { capLen = (int) (fs_ * 0.6f); capL.assign ((size_t) capLen, 0); capR.assign ((size_t) capLen, 0); grainLen = (int) (fs_ * 0.25f); }
    void onParamsChanged() override { float f = 1500.0f * std::pow (8.0f, p (2)); lpL.set (fs_, f); lpR.set (fs_, f); }
    void onReset() override { capturing = holding = wasOn = false; lvl = 0; capPos = 0; }
    void render (float* L, float* R, int n, const ProcessContext&) override {
        const bool on = isEnabled();
        if (on && ! wasOn) { capturing = true; capPos = 0; holding = false; for (int g = 0; g < 3; ++g) { gph[g] = (float) g / 3.0f; gst[g] = 0; } }
        wasOn = on;
        const float rel = std::exp (-6.9f / (p (1) * fs_)), atk = 1.0f / (0.15f * fs_);
        for (int s = 0; s < n; ++s) {
            if (capturing) { // the input here is the enable-gated dry signal; capture it
                capL[(size_t) capPos] = L[s]; capR[(size_t) capPos] = R[s];
                if (++capPos >= capLen) { capturing = false; holding = true; }
            }
            float ol = 0, orr = 0;
            if (holding) {
                for (int g = 0; g < 3; ++g) {
                    gph[g] += 1.0f / (float) grainLen;
                    if (gph[g] >= 1) { gph[g] -= 1; gst[g] = (0.5f + 0.5f * rng.next()) * (float) (capLen - grainLen - 1); }
                    float w = std::sin (kPi * gph[g]); w *= w; int idx = (int) gst[g] + (int) (gph[g] * (float) grainLen);
                    idx = std::clamp (idx, 0, capLen - 1); ol += capL[(size_t) idx] * w; orr += capR[(size_t) idx] * w;
                }
                ol *= 0.67f; orr *= 0.67f;
                if (on) lvl = std::min (1.0f, lvl + atk); else lvl *= rel;
                if (! on && lvl < 1e-4f) { holding = false; lvl = 0; }
            }
            float g = lvl * p (0); L[s] = lpL.lp (ol) * g; R[s] = lpR.lp (orr) * g;
        }
    }
};

class Looper : public Pedal { // TODO(v0.2): record / overdub / play / undo, tempo-quantised, footswitch-driven.
public:
    Looper() { type = "looper"; displayName = "Looper"; category = PedalCategory::Time; implemented = false;
        addParam ("level", "Loop Level", 0, 1, 0.8f); }
    void render (float*, float*, int, const ProcessContext&) override {}
};

// ============================================================ UTILITY
class EQ : public Pedal {
public:
    EQ() { type = "eq"; displayName = "EQ"; category = PedalCategory::Utility;
        addParam ("lowcut", "Low Cut", 20, 400, 80, "Hz"); addParam ("bass", "Bass", -12, 12, 0, "dB"); addParam ("midfreq", "Mid Freq", 200, 4000, 800, "Hz");
        addParam ("mid", "Mid", -12, 12, 0, "dB"); addParam ("treble", "Treble", -12, 12, 0, "dB"); addParam ("highcut", "High Cut", 2000, 18000, 12000, "Hz"); addParam ("level", "Level", -12, 12, 0, "dB"); }
    Biquad f[2][5];
    void onParamsChanged() override {
        for (int c = 0; c < 2; ++c) { f[c][0].setHP (fs_, p (0), 0.707f); f[c][1].setShelf (fs_, 120, p (1), false); f[c][2].setPeak (fs_, p (2), 0.8f, p (3)); f[c][3].setShelf (fs_, 4000, p (4), true); f[c][4].setLP (fs_, p (5), 0.707f); }
    }
    void onReset() override { for (auto& c : f) for (auto& b : c) b.reset(); }
    void render (float* L, float* R, int n, const ProcessContext&) override {
        const float g = dbg (p (6));
        for (int s = 0; s < n; ++s) { float l = L[s], r = R[s]; for (int i = 0; i < 5; ++i) { l = f[0][i].process (l); r = f[1][i].process (r); } L[s] = l * g; R[s] = r * g; }
    }
};

#include "PedalsV09.inc"
#include "AmpV09.inc"
#include "Reverbs.inc"

using Maker = std::function<std::unique_ptr<Pedal>()>;
struct Entry { PedalInfo info; Maker make; };
template <class T> Entry E (bool priority) { T t; return { { t.type, t.displayName, t.category, t.implemented, priority }, [] { return std::unique_ptr<Pedal> (new T()); } }; }
const std::vector<Entry>& registry() {
    static const std::vector<Entry> r = {
        E<Compressor> (true), E<Boost> (true), E<Overdrive> (true), E<Klon> (false), E<Distortion> (false), E<Fuzz> (true),
        E<OctaveDown> (true), E<PolyOctave> (false), E<Harmonizer> (false), E<Key9> (false), E<Synth9> (false),
        E<Chorus> (true), E<Univibe> (false), E<Rotary> (false), E<Tremolo> (true), E<Phaser> (false),
        E<Delay> (true), E<Reverb> (true), E<Shimmer> (true), E<Freeze> (true), E<Looper> (false), E<EQ> (true), E<VolumeSwell> (false),
        // v0.9 circuit-modelled pedals + amp/cab
        E<GermaniumFuzz> (false), E<TriStageFuzz> (false), E<TrebleBooster> (false), E<BluesDrive> (false), E<AmpInBox> (false), E<OctaveFuzz> (false),
        E<OtaComp> (false), E<NoiseGate> (true), E<Flanger> (false), E<Vibrato> (false), E<Wah> (false), E<EnvFilter> (false),
        E<AnalogDelay> (false), E<TapeEcho> (false), E<WorshipDelay> (true), E<CloudVerb> (true), E<SpringVerb> (false),
        E<HallReverb> (true), E<RoomReverb> (true), E<PlateReverb> (true), E<ChamberReverb> (true), E<ChurchReverb> (true),
        E<ReverseVerb> (true), E<GatedVerb> (true), E<ModReverb> (true), E<BloomVerb> (true),
        E<AmpModel> (true), E<CabSim> (false) };
    return r;
}
} // namespace

const std::vector<PedalInfo>& pedalCatalog() { static std::vector<PedalInfo> v = [] { std::vector<PedalInfo> o; for (auto& e : registry()) o.push_back (e.info); return o; }(); return v; }
std::unique_ptr<Pedal> createPedal (const std::string& type) { for (auto& e : registry()) if (e.info.type == type) return e.make(); return nullptr; }
bool loadCabImpulse (Pedal& p, const float* d, int len, double sr) {
    if (auto* a = dynamic_cast<AmpModel*> (&p)) return a->loadImpulse (d, len, sr);
    if (auto* c = dynamic_cast<CabSim*> (&p)) return c->loadImpulse (d, len, sr);
    return false;
}
const char* categoryName (PedalCategory c) {
    switch (c) { case PedalCategory::Dynamics: return "Dynamics"; case PedalCategory::Drive: return "Drive"; case PedalCategory::Pitch: return "Pitch"; case PedalCategory::Synth: return "Synth";
                 case PedalCategory::Modulation: return "Modulation"; case PedalCategory::Time: return "Time"; case PedalCategory::Ambient: return "Ambient"; case PedalCategory::AmpCab: return "Amp & Cab"; default: return "Utility"; }
}
} // namespace wr
