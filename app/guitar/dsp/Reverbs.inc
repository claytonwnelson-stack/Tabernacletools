// Worship reverb set. Hall / Room / Chamber / Church / Modulated / Bloom reuse the existing 8-line FDN
// (wk::Reverb). Plate is a short Schroeder tank (parallel combs + allpasses). Reverse and Gated are
// tiny delay/envelope effects. Spring, Shimmer and Cloud already exist and are not duplicated.

inline float killDenorm (float x) { return std::abs (x) < 1.0e-20f ? 0.0f : x; }

inline float apProcess (DelayLine& d, float len, float g, float x) {
    float z = d.read (std::max (1.0f, len));
    float y = killDenorm (-g * x + z);
    d.push (x + g * z);
    return y;
}

// Fixed-size FDN character. Tone 0 = dark, 1 = bright. Size is the room, not a knob.
class SpaceReverb : public ReverbBase {
public:
    float sizeK = 0.7f;
    SpaceReverb (const char* ty, const char* name, float size, float dMin, float dMax, float dDef, float pre, float mix, float tone) : sizeK (size) {
        type = ty; displayName = name; category = PedalCategory::Ambient; hasTails = true;
        addParam ("mix", "Mix", 0, 1, mix); addParam ("decay", "Decay", dMin, dMax, dDef, "s");
        addParam ("predelay", "Pre-Delay", 0, 150, pre, "ms"); addParam ("tone", "Tone", 0, 1, tone);
    }
    void render (float* L, float* R, int n, const ProcessContext&) override {
        rp.reverbOn = true; rp.revMix = p (0); rp.revDecay = p (1); rp.revSize = sizeK; rp.revPredelay = p (2); rp.revDamp = 1.0f - p (3); rp.revShimmer = 0; runWet (L, R, n);
    }
};

class HallReverb : public SpaceReverb { public: HallReverb() : SpaceReverb ("hall", "Hall", 0.82f, 0.6f, 8.0f, 2.6f, 22, 0.28f, 0.48f) {} };
class RoomReverb : public SpaceReverb { public: RoomReverb() : SpaceReverb ("room", "Room", 0.28f, 0.3f, 2.5f, 0.85f, 8, 0.22f, 0.42f) {} };
class ChamberReverb : public SpaceReverb { public: ChamberReverb() : SpaceReverb ("chamber", "Chamber", 0.48f, 0.4f, 4.0f, 1.5f, 14, 0.25f, 0.50f) {} };
class ChurchReverb : public SpaceReverb { public: ChurchReverb() : SpaceReverb ("church", "Church", 1.0f, 1.5f, 12.0f, 5.5f, 40, 0.32f, 0.40f) {} };

// Plate: Schroeder (four damped combs in parallel, two allpasses). Feedback stays below 1, so the tail dies.
class PlateReverb : public Pedal {
public:
    DelayLine pre, comb[4], ap[2]; OnePole damp, tone;
    PlateReverb() { type = "plate"; displayName = "Plate"; category = PedalCategory::Ambient; hasTails = true;
        addParam ("mix", "Mix", 0, 1, 0.26f); addParam ("decay", "Decay", 0.4f, 6.0f, 1.8f, "s"); addParam ("predelay", "Pre-Delay", 0, 80, 10, "ms"); addParam ("tone", "Tone", 0, 1, 0.62f); }
    void onPrepare() override {
        pre.init ((int) (fs_ * 0.1f));
        for (auto& c : comb) c.init ((int) (fs_ * 0.08f));
        for (auto& a : ap) a.init ((int) (fs_ * 0.02f));
    }
    void onParamsChanged() override { float f = 2500.0f * std::pow (4.0f, p (3)); damp.set (fs_, f); tone.set (fs_, f); }
    void onReset() override { pre.clear(); for (auto& c : comb) c.clear(); for (auto& a : ap) a.clear(); damp.z = tone.z = 0; }
    void render (float* L, float* R, int n, const ProcessContext&) override {
        const float mix = p (0), decay = std::max (0.25f, p (1)), preS = std::max (1.0f, p (2) * 0.001f * fs_);
        const float fb = std::min (0.85f, std::exp (-6.9f * 0.037f / decay));
        const float ms[4] = { 0.0297f, 0.0371f, 0.0411f, 0.0437f };
        for (int s = 0; s < n; ++s) {
            float x = tone.lp (0.5f * (L[s] + R[s]));
            pre.push (x); x = pre.read (preS);
            float yL = 0, yR = 0;
            for (int i = 0; i < 4; ++i) {
                float len = ms[i] * fs_;
                float d = comb[i].read (len);
                float v = x + damp.lp (d) * fb;
                comb[i].push (std::abs (v) < 1.0e-12f ? 0.0f : v);
                yL += d; yR += comb[i].read (len * 0.83f);
            }
            yL = apProcess (ap[0], 0.0051f * fs_, 0.7f, yL * 0.25f);
            yR = apProcess (ap[1], 0.0067f * fs_, 0.7f, yR * 0.25f);
            L[s] = killDenorm (yL) * mix; R[s] = killDenorm (yR) * mix;
        }
    }
};

// Reverse: the previous window played backwards, two crossfaded heads so the join doesn't click.
class ReverseVerb : public Pedal {
public:
    DelayLine buf; OnePole tone; float ph = 0;
    ReverseVerb() { type = "reverse"; displayName = "Reverse"; category = PedalCategory::Ambient; hasTails = true;
        addParam ("mix", "Mix", 0, 1, 0.32f); addParam ("time", "Time", 200, 1400, 600, "ms"); addParam ("tone", "Tone", 0, 1, 0.45f); }
    void onPrepare() override { buf.init ((int) (fs_ * 1.6f)); }
    void onParamsChanged() override { tone.set (fs_, 900.0f * std::pow (8.0f, p (2))); }
    void onReset() override { buf.clear(); tone.z = 0; ph = 0; }
    void render (float* L, float* R, int n, const ProcessContext&) override {
        const float win = clampf (p (1) * 0.001f, 0.12f, 1.45f) * fs_, mix = p (0), step = 1.0f / win;
        for (int s = 0; s < n; ++s) {
            ph += step; if (ph >= 1) ph -= 1;
            float p2 = ph + 0.5f; if (p2 >= 1) p2 -= 1;
            float y = buf.read (ph * win) * std::sin (kPi * ph) + buf.read (p2 * win) * std::sin (kPi * p2);
            buf.push (0.5f * (L[s] + R[s]));
            y = killDenorm (tone.lp (y));
            L[s] = y * mix; R[s] = y * mix * 0.95f;
        }
    }
};

// Gated: four short combs, opened by the note and slammed shut after Hold.
class GatedVerb : public Pedal {
public:
    DelayLine c[4]; OnePole tone; float env = 0; int holdLeft = 0;
    GatedVerb() { type = "gated"; displayName = "Gated"; category = PedalCategory::Ambient; hasTails = true;
        addParam ("mix", "Mix", 0, 1, 0.35f); addParam ("hold", "Hold", 0.08f, 0.6f, 0.22f, "s"); addParam ("tone", "Tone", 0, 1, 0.58f); }
    void onPrepare() override { for (auto& d : c) d.init ((int) (fs_ * 0.08f)); }
    void onParamsChanged() override { tone.set (fs_, 1200.0f * std::pow (6.0f, p (2))); }
    void onReset() override { for (auto& d : c) d.clear(); tone.z = 0; env = 0; holdLeft = 0; }
    void render (float* L, float* R, int n, const ProcessContext&) override {
        const float mix = p (0), atk = 1.0f / (0.004f * fs_), rel = std::exp (-6.9f / (0.025f * fs_));
        const float lens[4] = { 0.013f * fs_, 0.017f * fs_, 0.023f * fs_, 0.029f * fs_ };
        const int holdN = (int) (p (1) * fs_);
        for (int s = 0; s < n; ++s) {
            float x = 0.5f * (L[s] + R[s]);
            if (std::abs (x) > 0.015f) holdLeft = holdN;
            if (holdLeft > 0) { env = std::min (1.0f, env + atk); --holdLeft; } else env *= rel;
            env = killDenorm (env);
            float y = 0;
            for (int i = 0; i < 4; ++i) { float d = c[i].read (lens[i]); c[i].push (softClip (x + d * 0.45f) * env); y += d; }
            y = tone.lp (y * 0.28f) * env;
            L[s] = y * mix; R[s] = y * mix;
        }
    }
};

// Modulated hall: the FDN plus a slow chorus on the wet tank only.
class ModReverb : public ReverbBase {
public:
    DelayLine cL, cR; float ph = 0;
    ModReverb() { type = "modverb"; displayName = "Modulated"; category = PedalCategory::Ambient; hasTails = true;
        addParam ("mix", "Mix", 0, 1, 0.30f); addParam ("decay", "Decay", 0.6f, 8.0f, 2.2f, "s"); addParam ("depth", "Depth", 0, 1, 0.4f); addParam ("tone", "Tone", 0, 1, 0.5f); }
    void onPrepare() override { ReverbBase::onPrepare(); cL.init ((int) (fs_ * 0.05f)); cR.init ((int) (fs_ * 0.05f)); }
    void onReset() override { ReverbBase::onReset(); cL.clear(); cR.clear(); ph = 0; }
    void render (float* L, float* R, int n, const ProcessContext&) override {
        rp.reverbOn = true; rp.revMix = p (0); rp.revDecay = p (1); rp.revSize = 0.72f; rp.revPredelay = 18; rp.revDamp = 1.0f - p (3); rp.revShimmer = 0;
        runWet (L, R, n);
        const float depth = p (2);
        for (int s = 0; s < n; ++s) {
            ph += 0.35f / fs_; if (ph >= 1) ph -= 1;
            float d = 8.0f + depth * 18.0f * (1.0f + std::sin (k2Pi * ph));
            float wl = cL.read (d), wr = cR.read (d + 3.0f);
            cL.push (L[s]); cR.push (R[s]);
            float m = 0.15f + 0.55f * depth;
            L[s] = killDenorm (L[s] * (1.0f - m) + wl * m); R[s] = killDenorm (R[s] * (1.0f - m) + wr * m);
        }
    }
};

// Bloom: swell the reverb input (picked notes open slowly). No octave shifter, so it stays cheaper than Cloud.
class BloomVerb : public ReverbBase {
public:
    float env = 0;
    BloomVerb() { type = "bloom"; displayName = "Bloom"; category = PedalCategory::Ambient; hasTails = true;
        addParam ("mix", "Mix", 0, 1, 0.40f); addParam ("decay", "Decay", 0.8f, 8.0f, 3.2f, "s"); addParam ("swell", "Swell", 0, 1, 0.65f); addParam ("tone", "Tone", 0, 1, 0.46f); }
    void onReset() override { ReverbBase::onReset(); env = 0; }
    void render (float* L, float* R, int n, const ProcessContext&) override {
        const float swell = p (2), rise = 1.0f / (fs_ * (0.03f + 0.9f * swell)), fall = std::exp (-1.0f / (0.35f * fs_));
        for (int s = 0; s < n; ++s) {
            float e = std::abs (L[s]) + std::abs (R[s]);
            if (e > 0.02f) env = std::min (1.0f, env + rise); else env *= fall;
            float g = (1.0f - swell) + swell * env; L[s] *= g; R[s] *= g;
        }
        rp.reverbOn = true; rp.revMix = p (0); rp.revDecay = p (1); rp.revSize = 0.78f; rp.revPredelay = 24; rp.revDamp = 1.0f - p (3); rp.revShimmer = 0;
        runWet (L, R, n);
    }
};
