// Tabernacle Tools v0.9 - circuit-modelled pedals (included into Pedals.cpp inside the anonymous namespace).
// Each model follows the topology of a publicly published schematic (clipping type/position, filter corners,
// gain range). LEGAL: display names are DESCRIPTIVE ONLY - never a manufacturer product name or trademark in the UI.
// The "circuit" comment is for developers; see docs/SOUND_LIBRARY.md "Naming rules".

// Germanium/silicon 2-transistor fuzz with pre-boost (circuit: 3-transistor "MkII" germanium bender).
class GermaniumFuzz : public DriveBase {
public:
    GermaniumFuzz() { type = "gefuzz"; displayName = "Germanium Bender Fuzz"; category = PedalCategory::Drive;
        addParam ("attack", "Attack", 0, 1, 0.7f); addParam ("level", "Level", -24, 6, -8, "dB"); }
    void onPrepare() override { aaIn.setLP (fs_, 8000, 0.707f); post1.setLP (fs_, 4500, 0.707f); post2.setHP (fs_, 90, 0.707f); }
    void onParamsChanged() override { pre.setHP (fs_, 200, 0.7f); preGain = 3.0f + 200.0f * p (0) * p (0); tone.setPeak (fs_, 1200, 0.8f, 3.0f); outGain = dbg (p (1)) * 0.5f; cleanMix = 0; }
    float clip (float x) override { float y = asymClip (x, 0.35f); return std::clamp (y * 1.4f, -0.9f, 0.8f); }
};

// Four-stage silicon fuzz with passive mid-scooped tone (circuit: "triangle/ram's head" 4-transistor fuzz/sustainer).
class TriStageFuzz : public DriveBase {
public:
    Biquad stage2; OnePole lpTone; Biquad hpTone;
    TriStageFuzz() { type = "tristage"; displayName = "Tri-Stage Sustain Fuzz"; category = PedalCategory::Drive;
        addParam ("sustain", "Sustain", 0, 1, 0.6f); addParam ("tone", "Tone", 0, 1, 0.5f); addParam ("volume", "Volume", -24, 6, -10, "dB"); }
    void onPrepare() override { aaIn.setLP (fs_, 9000, 0.707f); post1.setLP (fs_, 6000, 0.707f); post2.setHP (fs_, 60, 0.707f); onClipRate(); }
    void onClipRate() override { const float r = clipFs(); stage2.setHP (r, 100, 0.7f); lpTone.set (r, 700); hpTone.setHP (r, 1100, 0.6f); }
    void onReset() override { DriveBase::onReset(); stage2.reset(); lpTone.z = 0; hpTone.reset(); }
    void onParamsChanged() override { pre.setHP (fs_, 80, 0.7f); preGain = 4.0f + 80.0f * p (0); tone.setPeak (fs_, 1000, 0.5f, -2.0f); outGain = dbg (p (2)) * 0.6f; cleanMix = 0; }
    float clip (float x) override { // two cascaded clipping stages, then tone blend (LP vs HP = mid scoop)
        float y = softClip (x); y = softClip (stage2.process (y) * 8.0f);
        float t = p (1); return (1 - t) * lpTone.lp (y) * 1.6f + t * hpTone.process (y);
    }
};

// Germanium/silicon single-transistor treble booster (circuit: 1-transistor treble booster, 5 nF input cap).
class TrebleBooster : public DriveBase {
public:
    TrebleBooster() { type = "treblebst"; displayName = "Treble Booster"; category = PedalCategory::Drive;
        addParam ("boost", "Boost", 0, 1, 0.6f); addChoice ("range", "Range", { "Treble", "Mid", "Full" }, 0); }
    void onPrepare() override { aaIn.setLP (fs_, 12000, 0.707f); post1.setLP (fs_, 9000, 0.707f); post2.setHP (fs_, 40, 0.707f); }
    void onParamsChanged() override { const float hp[] = { 1800, 700, 120 }; pre.setHP (fs_, hp[std::clamp (pi (1), 0, 2)], 0.6f); preGain = 1.5f + 10.0f * p (0); tone.setShelf (fs_, 3000, 2, true); outGain = 0.8f; cleanMix = 0.5f; }
    float clip (float x) override { return asymClip (x, 0.2f) * 0.6f; }
};

// Op-amp overdrive with asymmetric diode clipping and a "sweet" mid hump (circuit: low-gain blues overdrive, op-amp + discrete).
class BluesDrive : public DriveBase {
public:
    BluesDrive() { type = "bluesdrive"; displayName = "Blues Drive"; category = PedalCategory::Drive;
        addParam ("gain", "Gain", 0, 1, 0.45f); addParam ("tone", "Tone", 0, 1, 0.5f); addParam ("level", "Level", -18, 12, 0, "dB"); }
    void onPrepare() override { aaIn.setLP (fs_, 10000, 0.707f); post1.setLP (fs_, 7500, 0.707f); post2.setHP (fs_, 50, 0.707f); }
    void onParamsChanged() override { pre.setPeak (fs_, 600, 0.6f, 3); preGain = 1.0f + 40.0f * p (0) * p (0); tone.setShelf (fs_, 1800, -9.0f + 18.0f * p (1), true); outGain = dbg (p (2)) * 0.6f; cleanMix = 0.15f; }
    float clip (float x) override { return asymClip (x, 0.15f) * 0.75f; }
};

// Transparent "amp-in-a-box" - flat response, two series soft clippers (circuit: dual op-amp "breaker" style drive).
class AmpInBox : public DriveBase {
public:
    AmpInBox() { type = "ampinbox"; displayName = "Transparent Amp-in-a-Box"; category = PedalCategory::Drive;
        addParam ("gain", "Gain", 0, 1, 0.35f); addParam ("tone", "Tone", 0, 1, 0.5f); addParam ("volume", "Volume", -18, 12, 0, "dB"); }
    void onPrepare() override { aaIn.setLP (fs_, 11000, 0.707f); post1.setLP (fs_, 9000, 0.707f); post2.setHP (fs_, 35, 0.707f); }
    void onParamsChanged() override { pre.setHP (fs_, 40, 0.7f); preGain = 1.0f + 25.0f * p (0); tone.setShelf (fs_, 2500, -6.0f + 12.0f * p (1), true); outGain = dbg (p (2)) * 0.65f; cleanMix = 0.0f; }
    float clip (float x) override { return softClip (softClip (x) * 1.5f) * 0.7f; }
};

// Octave-up fuzz: full-wave rectifier (circuit: transformer-coupled rectifier octave fuzz).
class OctaveFuzz : public DriveBase {
public:
    OctaveFuzz() { type = "octfuzz"; displayName = "Octave-Up Fuzz"; category = PedalCategory::Drive;
        addParam ("fuzz", "Fuzz", 0, 1, 0.6f); addParam ("volume", "Volume", -24, 6, -8, "dB"); }
    void onPrepare() override { aaIn.setLP (fs_, 6000, 0.707f); post1.setLP (fs_, 5000, 0.707f); post2.setHP (fs_, 120, 0.707f); }
    void onParamsChanged() override { pre.setHP (fs_, 150, 0.7f); preGain = 2.0f + 30.0f * p (0); tone.setPeak (fs_, 1500, 0.7f, 4); outGain = dbg (p (1)) * 0.6f; cleanMix = 0; }
    float clip (float x) override { return softClip (std::abs (softClip (x)) * 2.0f - 0.6f); }
};

// OTA compressor (circuit: CA3080 OTA "squeeze" compressor). Fixed fast attack, long release, characteristic pump.
class OtaComp : public Pedal {
public:
    Env env; float gr = 1;
    OtaComp() { type = "otacomp"; displayName = "OTA Squeeze Comp"; category = PedalCategory::Dynamics;
        addParam ("sensitivity", "Sensitivity", 0, 1, 0.6f); addParam ("output", "Output", -12, 12, 0, "dB"); }
    void onPrepare() override { env.set (fs_, 2, 300); }
    void onReset() override { env.v = 0; gr = 1; }
    void render (float* L, float* R, int n, const ProcessContext&) override {
        const float thr = 0.3f * std::pow (0.05f, p (0)), out = dbg (p (1));
        for (int s = 0; s < n; ++s) { float e = env.run (0.5f * (L[s] + R[s])); float g = e > thr ? std::pow (thr / e, 0.85f) : 1.0f; g *= 1.0f + 2.5f * p (0);
            L[s] = softClip (L[s] * g) * out; R[s] = softClip (R[s] * g) * out; }
    }
};

// Noise gate with hysteresis (standard design).
class NoiseGate : public Pedal {
public:
    Env env; float g = 0;
    NoiseGate() { type = "gate"; displayName = "Noise Gate"; category = PedalCategory::Dynamics;
        addParam ("threshold", "Threshold", -80, -20, -60, "dB"); addParam ("release", "Release", 10, 500, 120, "ms"); }
    void onPrepare() override { env.set (fs_, 1, 30); }
    void onReset() override { env.v = 0; g = 0; }
    void render (float* L, float* R, int n, const ProcessContext&) override {
        const float open = dbg (p (0)), close = open * 0.5f, rel = std::exp (-1.0f / (0.001f * p (1) * fs_)), atk = std::exp (-1.0f / (0.0005f * fs_));
        bool isOpen = g > 0.5f;
        for (int s = 0; s < n; ++s) { float e = env.run (0.5f * (std::abs (L[s]) + std::abs (R[s])));
            if (e > open) isOpen = true; else if (e < close) isOpen = false;
            g = isOpen ? 1.0f + (g - 1.0f) * atk : g * rel; L[s] *= g; R[s] *= g; }
    }
};

// BBD flanger (circuit: bucket-brigade flanger with regen, ~0.5-10 ms sweep).
class Flanger : public Pedal {
public:
    DelayLine l, r; float ph = 0, fbl = 0, fbr = 0;
    Flanger() { type = "flanger"; displayName = "Jet Flanger"; category = PedalCategory::Modulation;
        addParam ("rate", "Rate", 0.05f, 5, 0.3f, "Hz"); addParam ("depth", "Depth", 0, 1, 0.7f); addParam ("manual", "Manual", 0, 1, 0.3f); addParam ("regen", "Regen", -0.9f, 0.9f, 0.5f); addParam ("mix", "Mix", 0, 1, 0.5f); }
    void onPrepare() override { l.init ((int) (fs_ * 0.02f)); r.init ((int) (fs_ * 0.02f)); }
    void onReset() override { l.clear(); r.clear(); fbl = fbr = 0; }
    void render (float* L, float* R, int n, const ProcessContext&) override {
        const float base = (0.0005f + 0.004f * p (2)) * fs_, sw = 0.006f * fs_ * p (1), fb = p (3), m = p (4);
        for (int s = 0; s < n; ++s) { ph += p (0) / fs_; if (ph >= 1) ph -= 1;
            float tri = 2.0f * std::abs (2.0f * ph - 1.0f) - 1.0f, tr2 = -tri;
            float dl = base + sw * 0.5f * (1 + tri), dr = base + sw * 0.5f * (1 + tr2);
            l.push (softClip (L[s] + fbl * fb)); r.push (softClip (R[s] + fbr * fb));
            fbl = l.read (dl); fbr = r.read (dr);
            L[s] = L[s] * (1 - 0.5f * m) + fbl * m; R[s] = R[s] * (1 - 0.5f * m) + fbr * m; }
    }
};

// Pitch vibrato (circuit: BBD vibrato, 100% wet).
class Vibrato : public Pedal {
public:
    DelayLine l, r; float ph = 0;
    Vibrato() { type = "vibrato"; displayName = "Pitch Vibrato"; category = PedalCategory::Modulation;
        addParam ("rate", "Rate", 0.5f, 10, 5, "Hz"); addParam ("depth", "Depth", 0, 1, 0.4f); }
    void onPrepare() override { l.init ((int) (fs_ * 0.02f)); r.init ((int) (fs_ * 0.02f)); }
    void onReset() override { l.clear(); r.clear(); }
    void render (float* L, float* R, int n, const ProcessContext&) override {
        const float c = 0.005f * fs_, d = 0.004f * fs_ * p (1);
        for (int s = 0; s < n; ++s) { ph += p (0) / fs_; if (ph >= 1) ph -= 1; float m = c + d * std::sin (k2Pi * ph);
            l.push (L[s]); r.push (R[s]); L[s] = l.read (m); R[s] = r.read (m); }
    }
};

// Wah: resonant bandpass swept by an expression pedal (circuit: inductor wah, ~350 Hz-2.2 kHz, Q~4) or auto/envelope.
class Wah : public Pedal {
public:
    Biquad bl, br; Env env; float ph = 0, lastF = -1;
    Wah() { type = "wah"; displayName = "Classic Wah"; category = PedalCategory::Modulation;
        addParam ("position", "Position", 0, 1, 0.5f); addChoice ("mode", "Mode", { "Pedal", "Auto (LFO)", "Envelope" }, 0); addParam ("rate", "Rate", 0.1f, 8, 2, "Hz");
        addParam ("sens", "Sens", 0, 1, 0.5f); addParam ("q", "Q", 1, 10, 4); addParam ("mix", "Mix", 0, 1, 1); }
    void onPrepare() override { env.set (fs_, 5, 120); }
    void onReset() override { bl.reset(); br.reset(); env.v = 0; }
    static void bp (Biquad& b, float fs, float f, float q) { float w = k2Pi * f / fs, c = std::cos (w), al = std::sin (w) / (2 * q); b.norm (1 + al, al * q, 0, -al * q, -2 * c, 1 - al); }
    void render (float* L, float* R, int n, const ProcessContext&) override {
        for (int s = 0; s < n; ++s) {
            float pos = p (0);
            if (pi (1) == 1) { ph += p (2) / fs_; if (ph >= 1) ph -= 1; pos = 0.5f + 0.5f * std::sin (k2Pi * ph); }
            else if (pi (1) == 2) pos = clampf (env.run (0.5f * (L[s] + R[s])) * (2 + 30 * p (3)), 0, 1);
            if ((s & 15) == 0) { float f = 350.0f * std::pow (6.3f, pos); if (std::abs (f - lastF) > 1) { bp (bl, fs_, f, p (4)); bp (br, fs_, f, p (4)); lastF = f; } }
            float m = p (5); L[s] = L[s] * (1 - m) + bl.process (L[s]) * m * 1.5f; R[s] = R[s] * (1 - m) + br.process (R[s]) * m * 1.5f;
        }
    }
};

// Envelope filter (circuit: state-variable "auto-wah" envelope follower, LP/BP, up/down sweep).
class EnvFilter : public Pedal {
public:
    Env env; float lp[2] {}, bpz[2] {};
    EnvFilter() { type = "envfilter"; displayName = "Envelope Filter"; category = PedalCategory::Modulation;
        addParam ("sens", "Sens", 0, 1, 0.5f); addParam ("peak", "Peak", 0.5f, 10, 4); addChoice ("mode", "Mode", { "Low-pass", "Band-pass" }, 1); addChoice ("dir", "Sweep", { "Up", "Down" }, 0); }
    void onPrepare() override { env.set (fs_, 4, 150); }
    void onReset() override { env.v = 0; lp[0] = lp[1] = bpz[0] = bpz[1] = 0; }
    void render (float* L, float* R, int n, const ProcessContext&) override {
        const float q = 1.0f / p (1);
        for (int s = 0; s < n; ++s) {
            float e = clampf (env.run (0.5f * (L[s] + R[s])) * (2 + 40 * p (0)), 0, 1); if (pi (3) == 1) e = 1 - e;
            float f = 2.0f * std::sin (kPi * std::min (0.2f, 200.0f * std::pow (20.0f, e) / fs_));
            float* io[2] = { &L[s], &R[s] };
            for (int c = 0; c < 2; ++c) { float x = *io[c]; float hp = x - lp[c] - q * bpz[c]; bpz[c] += f * hp; lp[c] += f * bpz[c];
                bpz[c] = clampf (bpz[c], -4, 4); lp[c] = clampf (lp[c], -4, 4); *io[c] = pi (2) == 0 ? lp[c] : bpz[c]; }
        }
    }
};

// Analog BBD delay: dark, companded, repeats saturate (circuit: MN3005-style analog delay, 20-600 ms).
class AnalogDelay : public Pedal {
public:
    DelayLine l; Smooth t; OnePole lp1, lp2; float ph = 0;
    AnalogDelay() { type = "analogdly"; displayName = "Bucket-Brigade Delay"; category = PedalCategory::Time; hasTails = true;
        addChoice ("sync", "Sync", { "Free", "Tempo" }, 1); addChoice ("div", "Division", kDivs, 1); addParam ("time", "Time", 20, 600, 350, "ms");
        addParam ("feedback", "Repeats", 0, 1.05f, 0.4f); addParam ("mix", "Mix", 0, 1, 0.35f); addParam ("mod", "Mod", 0, 1, 0.2f); }
    void onPrepare() override { l.init ((int) (fs_ * 2.1f)); t.set (fs_, 200); t.v = t.t = 0.35f * fs_; lp1.set (fs_, 2800); lp2.set (fs_, 3200); }
    void onReset() override { l.clear(); lp1.z = lp2.z = 0; }
    void render (float* L, float* R, int n, const ProcessContext& ctx) override {
        float sec = pi (0) == 1 ? (float) (60.0 / std::max (30.0, ctx.bpm)) * divBeats (pi (1)) : getParam (2) * 0.001f; t.t = clampf (sec, 0.02f, 2.0f) * fs_;
        const float fb = p (3), m = p (4), md = p (5) * 0.002f * fs_;
        for (int s = 0; s < n; ++s) { ph += 0.7f / fs_; if (ph >= 1) ph -= 1;
            float w = l.read (t.next() + md * (1 + std::sin (k2Pi * ph)));
            l.push (softClip (lp2.lp (lp1.lp (0.5f * (L[s] + R[s]) + w * fb))));
            L[s] = w * m; R[s] = w * m; }
    }
};

// Tape echo: wow & flutter, head saturation, tone loss per repeat, 3 playback heads (circuit/mechanism: multi-head tape echo).
class TapeEcho : public Pedal {
public:
    DelayLine l; Smooth t; OnePole lp; Biquad hp; float wow = 0, flut = 0; wk::Rng rng; float drift = 0;
    TapeEcho() { type = "tapeecho"; displayName = "Space Tape Echo"; category = PedalCategory::Time; hasTails = true;
        addChoice ("sync", "Sync", { "Free", "Tempo" }, 1); addChoice ("div", "Division", kDivs, 1); addParam ("time", "Time", 60, 1000, 380, "ms");
        addParam ("intensity", "Intensity", 0, 1.05f, 0.45f); addParam ("mix", "Echo Vol", 0, 1, 0.35f); addParam ("wear", "Tape Wear", 0, 1, 0.3f);
        addChoice ("heads", "Heads", { "1", "1+2", "1+2+3" }, 0); }
    void onPrepare() override { l.init ((int) (fs_ * 3.2f)); t.set (fs_, 400); t.v = t.t = 0.38f * fs_; hp.setHP (fs_, 150, 0.7f); }
    void onParamsChanged() override { lp.set (fs_, 5000.0f - 3000.0f * p (5)); }
    void onReset() override { l.clear(); lp.z = 0; hp.reset(); }
    void render (float* L, float* R, int n, const ProcessContext& ctx) override {
        float sec = pi (0) == 1 ? (float) (60.0 / std::max (30.0, ctx.bpm)) * divBeats (pi (1)) : getParam (2) * 0.001f; t.t = clampf (sec, 0.04f, 1.0f) * fs_;
        const float fb = p (3), m = p (4), wear = p (5); const int heads = pi (6) + 1;
        for (int s = 0; s < n; ++s) {
            wow += 0.6f / fs_; flut += 7.0f / fs_; if (wow >= 1) wow -= 1; if (flut >= 1) flut -= 1; drift = drift * 0.9995f + 0.0005f * rng.next();
            float mod = (0.0015f * std::sin (k2Pi * wow) + 0.0003f * std::sin (k2Pi * flut) + 0.002f * drift) * wear * fs_;
            float d = t.next() + mod, w = 0;
            for (int h = 1; h <= heads; ++h) w += l.read (d * (float) h / (float) heads * (heads == 1 ? 1.0f : 1.0f)) / (float) heads;
            l.push (std::tanh (1.3f * hp.process (lp.lp (0.5f * (L[s] + R[s]) + w * fb))));
            L[s] = w * m; R[s] = w * m; }
    }
};

// Dotted-eighth "worship" delay: tempo-locked 1/8. + optional 1/4 tap, ducking, filtered repeats (modern worship staple).
class WorshipDelay : public Pedal {
public:
    DelayLine a, b; OnePole lp; Biquad hp; Env env;
    WorshipDelay() { type = "dotted8"; displayName = "Dotted-Eighth Delay"; category = PedalCategory::Time; hasTails = true;
        addParam ("feedback", "Feedback", 0, 0.9f, 0.4f); addParam ("mix", "Mix", 0, 1, 0.35f); addParam ("quarter", "1/4 Tap", 0, 1, 0.5f); addParam ("duck", "Ducking", 0, 1, 0.4f); addParam ("tone", "Tone", 0, 1, 0.5f); }
    void onPrepare() override { a.init ((int) (fs_ * 2.1f)); b.init ((int) (fs_ * 2.1f)); hp.setHP (fs_, 200, 0.7f); env.set (fs_, 5, 250); }
    void onParamsChanged() override { lp.set (fs_, 2000.0f * std::pow (4.0f, p (4))); }
    void onReset() override { a.clear(); b.clear(); lp.z = 0; hp.reset(); env.v = 0; }
    void render (float* L, float* R, int n, const ProcessContext& ctx) override {
        const float beat = (float) (60.0 / std::max (30.0, ctx.bpm)) * fs_, d8 = std::min (beat * 0.75f, 2.0f * fs_), dq = std::min (beat, 2.0f * fs_);
        for (int s = 0; s < n; ++s) { float x = 0.5f * (L[s] + R[s]); float duck = 1.0f - p (3) * clampf (env.run (x) * 4, 0, 1);
            float w1 = a.read (d8), w2 = b.read (dq); a.push (x + hp.process (lp.lp (w1)) * p (0)); b.push (x + w2 * p (0) * 0.6f);
            float m = p (1) * duck; L[s] = (w1 + w2 * p (2) * 0.5f) * m; R[s] = (w1 * 0.7f + w2 * p (2)) * m; }
    }
};

// Ambient "cloud" reverb: long modulated FDN + shimmer + built-in swell (modern worship staple).
class CloudVerb : public ReverbBase {
public:
    Env env; float sw = 0;
    CloudVerb() { type = "cloud"; displayName = "Cloud Ambient Reverb"; category = PedalCategory::Ambient; hasTails = true;
        addParam ("mix", "Mix", 0, 1, 0.5f); addParam ("decay", "Decay", 4, 30, 14, "s"); addParam ("shimmer", "Shimmer", 0, 1, 0.3f); addParam ("swell", "Swell", 0, 1, 0.5f); }
    void onPrepare() override { ReverbBase::onPrepare(); env.set (fs_, 1, 80); }
    void render (float* L, float* R, int n, const ProcessContext&) override {
        // auto-swell the reverb INPUT so picked notes bloom (attack noise removed)
        const float rise = 1.0f / (fs_ * (0.05f + 0.8f * p (3)));
        for (int s = 0; s < n; ++s) { float e = env.run (0.5f * (L[s] + R[s])); if (e > 0.02f && sw < 1) sw = std::min (1.0f, sw + rise); else if (e < 0.005f) sw = 0; float g = (1 - p (3)) + p (3) * sw; L[s] *= g; R[s] *= g; }
        rp.reverbOn = true; rp.revMix = p (0); rp.revDecay = p (1); rp.revSize = 1.0f; rp.revPredelay = 60; rp.revDamp = 0.55f; rp.revShimmer = p (2); runWet (L, R, n);
    }
};

// Spring reverb (mechanism: 2-3 spring tank - dispersive allpass chain + short feedback "drip").
class SpringVerb : public Pedal {
public:
    DelayLine dl; float ap[6] {}; DelayLine apd[6]; OnePole lp;
    SpringVerb() { type = "spring"; displayName = "Spring Tank"; category = PedalCategory::Ambient; hasTails = true;
        addParam ("dwell", "Dwell", 0, 1, 0.5f); addParam ("mix", "Mix", 0, 1, 0.3f); addParam ("tone", "Tone", 0, 1, 0.5f); }
    void onPrepare() override { dl.init ((int) (fs_ * 0.1f)); for (auto& d : apd) d.init (64); }
    void onParamsChanged() override { lp.set (fs_, 2000 + 4000 * p (2)); }
    void onReset() override { dl.clear(); for (auto& d : apd) d.clear(); lp.z = 0; }
    void render (float* L, float* R, int n, const ProcessContext&) override {
        const float fb = 0.6f + 0.3f * p (0), D = 0.037f * fs_;
        for (int s = 0; s < n; ++s) { float x = 0.5f * (L[s] + R[s]) * (0.5f + p (0)); float y = dl.read (D) * fb + x;
            for (int i = 0; i < 6; ++i) { float d = apd[i].read (8 + 5 * i); float v = y + 0.6f * d; apd[i].push (v); y = d - 0.6f * v; }
            dl.push (softClip (lp.lp (y))); L[s] = y * p (1); R[s] = -y * p (1) * 0.9f; }
    }
};
