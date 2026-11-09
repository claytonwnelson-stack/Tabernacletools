// Tabernacle Tools v0.9 - amp + cab modelling (included into Pedals.cpp). Generic, descriptive names only.
// Topology per model: input HP -> 1..3 triode-like gain stages (biased tanh, inter-stage RC filters) -> passive-style
// tone stack (model-specific corner freqs) -> power amp (push-pull soft clip + sag) -> presence -> cab (synthetic or IR).
// Output modes: "Direct to PA" (full amp + cab + PA safety EQ/limiter, for players with NO amp),
// "Amp FX Return" (preamp only, no cab - into a real power amp/cab), "Headphones" (amp + cab + slight room).
struct AmpVoicing { const char* name; int stages; float preHP, stageHP, gainMax, bias, bassF, midF, trebF, midQ, sag, bright; };
const AmpVoicing kAmps[] = {
    { "Clean American",  1, 60, 30,   6, 0.05f, 100, 500, 3000, 0.5f, 0.10f, 3 },   // blackface-style 2x12 clean
    { "Tweed Breakup",   2, 80, 60,  25, 0.15f, 120, 700, 2500, 0.6f, 0.40f, 0 },   // tweed-style cathode-biased combo
    { "British Chime",   2, 90, 80,  20, 0.10f, 110, 900, 3500, 0.7f, 0.25f, 4 },   // top-boost class-A EL84 style
    { "British Crunch",  2, 100, 120, 45, 0.12f, 100, 650, 3200, 0.5f, 0.20f, 2 },  // 4-input plexi-style
    { "Boutique Lead",   3, 100, 150, 90, 0.10f, 90, 750, 3000, 0.8f, 0.15f, 1 },   // dumble-style smooth OD
    { "Modern High Gain",3, 120, 200, 220, 0.08f, 80, 500, 3500, 0.6f, 0.10f, 2 },  // rectified high-gain style
};
constexpr int kNumAmps = (int) (sizeof kAmps / sizeof kAmps[0]);
struct CabVoicing { const char* name; float hp, lowF, lowDb, midF, midDb, hiF, lp; };
const CabVoicing kCabs[] = {
    { "1x12 Open Back", 90, 120, 1, 2200, 3, 4500, 5500 },
    { "2x12 Open Back", 80, 110, 2, 2500, 4, 4000, 6000 },
    { "2x12 Blue Alnico", 85, 130, 1, 2000, 5, 3800, 5800 },
    { "4x12 Closed Back", 70, 100, 4, 2800, 3, 3500, 5000 },
    { "4x10 Tweed", 95, 150, 0, 1800, 4, 4200, 5200 },
};
constexpr int kNumCabs = (int) (sizeof kCabs / sizeof kCabs[0]);

// Cabinet section: synthetic voicing or user IR (mono FIR, truncated to kMaxIR taps; partitioned FFT = TODO).
struct CabSection {
    static constexpr int kMaxIR = 1024;
    Biquad f[5], mic[2]; std::vector<float> ir, hist; int pos = 0; bool useIR = false; float fs = 48000; int micType = 0; float micPos = 0.3f; std::vector<float> raw; double rawRate = 48000;
    void rebind (float sr) { fs = sr; if (! raw.empty()) { bool was = useIR; std::vector<float> r = raw; loadIR (r.data(), (int) r.size(), rawRate); useIR = was; } }
    // v0.10 mic + position: applied after the cab voice or IR. 0 Dynamic (presence bump), 1 Ribbon (dark, warm), 2 Condenser (airy).
    // position 0 = centre of the cone (bright) .. 1 = edge (dark). Simple filters, not a measured mic model.
    void setMic (int type, float position) { micType = std::clamp (type, 0, 2); micPos = clampf (position, 0, 1);
        const float lp = 11000.0f * std::pow (0.35f, micPos);
        if (micType == 0) { mic[0].setPeak (fs, 4200, 1.0f, 3.5f - 3.0f * micPos); mic[1].setLP (fs, lp, 0.707f); }
        else if (micType == 1) { mic[0].setShelf (fs, 200, 2.5f, false); mic[1].setLP (fs, lp * 0.55f, 0.6f); }
        else { mic[0].setShelf (fs, 6000, 3.0f - 2.0f * micPos, true); mic[1].setLP (fs, std::min (16000.0f, lp * 1.4f), 0.707f); } }
    void voice (float sr, int c) { fs = sr; auto& v = kCabs[std::clamp (c, 0, kNumCabs - 1)];
        f[0].setHP (fs, v.hp, 0.8f); f[1].setPeak (fs, v.lowF, 1.0f, v.lowDb); f[2].setPeak (fs, v.midF, 1.2f, v.midDb); f[3].setLP (fs, v.hiF, 0.9f); f[4].setLP (fs, v.lp, 0.7f); }
    bool loadIR (const float* data, int len, double irRate) {
        if (! data || len <= 0) { useIR = false; return false; }
        if (data != raw.data()) { raw.assign (data, data + len); rawRate = irRate; }
        // nearest-rate resample to fs, truncate, normalise energy
        int outLen = std::min (kMaxIR, (int) ((double) len * fs / irRate)); if (outLen < 8) { useIR = false; return false; }
        ir.assign ((size_t) outLen, 0); double e = 0;
        for (int i = 0; i < outLen; ++i) { double src = i * irRate / fs; int k = (int) src; float fr = (float) (src - k); float a = data[std::min (k, len - 1)], b = data[std::min (k + 1, len - 1)]; ir[(size_t) i] = a + (b - a) * fr; e += ir[(size_t) i] * ir[(size_t) i]; }
        float g = e > 0 ? (float) (1.0 / std::sqrt (e)) : 1; for (auto& x : ir) x *= g;
        for (int i = std::max (0, outLen - 32); i < outLen; ++i) ir[(size_t) i] *= (float) (outLen - i) / 32.0f; // fade tail
        hist.assign ((size_t) outLen * 2, 0); pos = 0; useIR = true; return true;
    }
    void reset() { for (auto& b : f) b.reset(); for (auto& b : mic) b.reset(); std::fill (hist.begin(), hist.end(), 0.0f); pos = 0; }
    inline float process (float x) {
        if (useIR) { const int N = (int) ir.size(); hist[(size_t) pos] = hist[(size_t) (pos + N)] = x; float y = 0; const float* h = &hist[(size_t) pos];
            for (int i = 0; i < N; ++i) y += ir[(size_t) i] * h[N - i]; pos = (pos + 1) % N; return y; } // IRs already contain their mic
        for (auto& b : f) x = b.process (x); return mic[1].process (mic[0].process (x));
    }
};

class AmpModel : public Pedal {
public:
    enum { kModel, kGain, kBass, kMid, kTreble, kPresence, kMaster, kOut, kCab, kLevel, kMic, kMicPos };
    Biquad inHP, stHP[3], stLP[3], bass, mid, treb, pres, paHP, paLP, room; float sagEnv = 0, lim = 1; CabSection cab; int lastCab = -1;
    Oversampler os;   // v0.12: gain stages + tone stack + power amp run at fs * factor
    void onPrepareBase() override { os.prepare (maxBlock_, 1); os.setFactor (osFactor_); }
    void onOversampling() override { os.setFactor (osFactor_); designNonlinear(); }
    bool oversamples() const override { return true; }
    double latencySamples() const override { return Oversampler::latencySamples (osFactor_); }
    float osFs() const { return fs_ * (float) os.factor(); }
    void designNonlinear() { auto& v = V(); const float r = osFs();
        for (int i = 0; i < 3; ++i) { stHP[i].setHP (r, v.stageHP * (1 + i * 0.5f), 0.7f); stLP[i].setLP (r, 9000.0f - 1500.0f * i, 0.7f); }
        bass.setShelf (r, v.bassF, -10 + 20 * p (kBass), false); mid.setPeak (r, v.midF, v.midQ, -12 + 20 * p (kMid)); treb.setShelf (r, v.trebF, -10 + 20 * p (kTreble) + v.bright, true); }
    AmpModel() { type = "amp"; displayName = "Amp + Cab"; category = PedalCategory::AmpCab;
        std::vector<std::string> m; for (auto& a : kAmps) m.push_back (a.name); addChoice ("model", "Amp", m, 0);
        addParam ("gain", "Gain", 0, 1, 0.4f); addParam ("bass", "Bass", 0, 1, 0.5f); addParam ("mid", "Mid", 0, 1, 0.5f); addParam ("treble", "Treble", 0, 1, 0.5f);
        addParam ("presence", "Presence", 0, 1, 0.5f); addParam ("master", "Master", 0, 1, 0.5f);
        addChoice ("output", "Output", { "Direct to PA", "Amp FX Return", "Headphones" }, 0);
        std::vector<std::string> c; for (auto& x : kCabs) c.push_back (x.name); c.push_back ("User IR"); addChoice ("cab", "Cab", c, 1);
        addParam ("level", "Level", -24, 12, 0, "dB");
        addChoice ("mic", "Mic", { "Dynamic", "Ribbon", "Condenser" }, 0); addParam ("micpos", "Mic Position", 0, 1, 0.3f); }
    const AmpVoicing& V() const { return kAmps[std::clamp (pi (kModel), 0, kNumAmps - 1)]; }
    bool loadImpulse (const float* d, int len, double sr) { bool ok = cab.loadIR (d, len, sr); if (ok) setParam ("cab", (float) kNumCabs); return ok; }
    bool cabActive() const { return pi (kOut) != 1; }
    void onPrepare() override { lastCab = -1; cab.rebind (fs_); paHP.setHP (fs_, 80, 0.7f); paLP.setLP (fs_, 9000, 0.7f); room.setPeak (fs_, 300, 0.7f, 1.5f); }
    void onReset() override { os.reset(); inHP.reset(); for (int i = 0; i < 3; ++i) { stHP[i].reset(); stLP[i].reset(); } bass.reset(); mid.reset(); treb.reset(); pres.reset(); paHP.reset(); paLP.reset(); room.reset(); cab.reset(); sagEnv = 0; lim = 1; }
    void onParamsChanged() override {
        auto& v = V(); inHP.setHP (fs_, v.preHP, 0.7f); designNonlinear();
        pres.setShelf (fs_, 4500, -6 + 12 * p (kPresence), true);
        int c = pi (kCab); if (c < kNumCabs) { cab.useIR = false; if (c != lastCab) cab.voice (fs_, c); } else { cab.fs = fs_; if (! cab.ir.empty()) cab.useIR = true; } lastCab = c;
        cab.setMic (pi (kMic), p (kMicPos));
    }
    void render (float* L, float* R, int n, const ProcessContext&) override {
        auto& v = V(); const float g = 1.0f + v.gainMax * p (kGain) * p (kGain), master = 0.2f + 1.8f * p (kMaster), lvl = dbg (p (kLevel)); const int out = pi (kOut);
        const float sagA = std::exp (-1.0f / (0.08f * osFs()));
        for (int s = 0; s < n; ++s) L[s] = inHP.process (0.5f * (L[s] + R[s]));
        float* u = os.up (0, L, n); const int m = n * os.factor(); const float sg2 = std::sqrt (g) * 2.0f;
        for (int k = 0; k < m; ++k) {   // ---- nonlinear section, oversampled
            float x = u[k];
            for (int i = 0; i < v.stages; ++i) { x = asymClip (stHP[i].process (x) * (i == 0 ? g : sg2), v.bias); x = stLP[i].process (x); }
            x = treb.process (mid.process (bass.process (x)));
            if (out != 1) { sagEnv = std::abs (x) + (sagEnv - std::abs (x)) * sagA; const float sag = 1.0f / (1.0f + v.sag * sagEnv * master); x = softClip (x * master * sag * 1.5f) * 0.7f; }
            u[k] = x;
        }
        os.down (0, L, n);
        for (int s = 0; s < n; ++s) {
            float x = L[s];
            if (out != 1) { // power amp + sag (FX-return mode leaves this to the real amp)
                x = pres.process (x);
                x = cab.process (x) * (cab.useIR ? 1.0f : 1.4f);
                if (out == 0) { x = paLP.process (paHP.process (x)); }       // PA safety EQ
                else x = room.process (x);
            } else x *= 0.5f * master;
            x *= lvl;
            // Safety gain after the cab. An instant ceiling here is a hard clip at the
            // base rate (48 kHz on the iPad), which the oversampler cannot filter, so a
            // loud amp sounded like a high buzz even when the interface was not clipping.
            // A few-millisecond attack is a gain change, not a new waveshaper.
            const float a = std::abs (x);
            const float ceilG = a > 0.89f ? 0.89f / a : 1.0f;
            const float att = 1.0f - std::exp (-1.0f / (0.004f * fs_));
            const float relC = 1.0f - std::exp (-1.0f / (0.080f * fs_));
            lim += (ceilG - lim) * (ceilG < lim ? att : relC);
            x *= lim;
            x = 0.95f * std::tanh (x / 0.95f); // bound the attack transient without a flat-top clip
            L[s] = x; R[s] = x;
        }
    }
};

// Standalone cab/IR loader (for players running a real amp head's line out / load box, or after drive pedals).
class CabSim : public Pedal {
public:
    CabSection cab; int lastCab = -1;
    CabSim() { type = "cab"; displayName = "Cab Sim / IR Loader"; category = PedalCategory::AmpCab;
        std::vector<std::string> c; for (auto& x : kCabs) c.push_back (x.name); c.push_back ("User IR"); addChoice ("cab", "Cab", c, 3); addParam ("level", "Level", -24, 12, 0, "dB");
        addChoice ("mic", "Mic", { "Dynamic", "Ribbon", "Condenser" }, 0); addParam ("micpos", "Mic Position", 0, 1, 0.3f); }
    bool loadImpulse (const float* d, int len, double sr) { bool ok = cab.loadIR (d, len, sr); if (ok) setParam ("cab", (float) kNumCabs); return ok; }
    void onParamsChanged() override { int c = pi (0); if (c < kNumCabs) { cab.useIR = false; if (c != lastCab) cab.voice (fs_, c); } else { cab.fs = fs_; if (! cab.ir.empty()) cab.useIR = true; } lastCab = c; cab.setMic (pi (2), p (3)); }
    void onPrepare() override { lastCab = -1; cab.rebind (fs_); }
    void onReset() override { cab.reset(); }
    void render (float* L, float* R, int n, const ProcessContext&) override { const float g = dbg (p (1)); for (int s = 0; s < n; ++s) { float y = cab.process (0.5f * (L[s] + R[s])) * g; L[s] = y; R[s] = y; } }
};
