// v0.12 tests: amp/drive anti-aliasing (oversampling), latency reporting, CPU guard, CPU cost per factor.
#include "dsp/Oversampler.h"
#include "dsp/RigBuilder.h"
#include "dsp/Rig.h"
#include "model/Song.h"
#include <cstdio>
#include <cmath>
#include <complex>
#include <chrono>
#include <vector>
using namespace wr;
static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (! (c)) { ++fails; std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)
static const double PI = 3.14159265358979323846;

static void fft (std::vector<std::complex<double>>& a) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) { size_t bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap (a[i], a[j]); }
    for (size_t len = 2; len <= n; len <<= 1) { const std::complex<double> w = std::polar (1.0, -2 * PI / (double) len);
        for (size_t i = 0; i < n; i += len) { std::complex<double> u (1); for (size_t k = 0; k < len / 2; ++k) { auto x = a[i + k], y = a[i + k + len / 2] * u; a[i + k] = x + y; a[i + k + len / 2] = x - y; u *= w; } } }
}
// Harmonic vs inharmonic (aliased) energy of a pedal's response to a pure sine on an exact FFT bin.
struct AliasResult { double inharmDb, harmDb; };
static AliasResult aliasing (Pedal& p, int factor, int bin = 1500, float amp = 0.5f) {
    const int N = 16384, fs = 48000; p.setOversampling (factor); p.prepare (fs, 256); p.setEnabled (true); p.reset();
    ProcessContext ctx; std::vector<float> L (256), R (256), out; const double f0 = (double) bin * fs / N; long t = 0;
    for (int b = 0; b < (N * 2) / 256 + 40; ++b) { for (int i = 0; i < 256; ++i, ++t) L[(size_t) i] = R[(size_t) i] = amp * (float) std::sin (2 * PI * f0 * (double) t / fs);
        p.process (L.data(), R.data(), 256, ctx); if (b >= 40) out.insert (out.end(), L.begin(), L.end()); }
    std::vector<std::complex<double>> X ((size_t) N); for (int i = 0; i < N; ++i) X[(size_t) i] = out[(size_t) i] * (0.5 - 0.5 * std::cos (2 * PI * i / N));   // Hann
    fft (X); double harm = 0, inh = 0;
    for (int k = 4; k < N / 2; ++k) { const double e = std::norm (X[(size_t) k]); int nearest = (int) std::lround ((double) k / bin) * bin; if (nearest > 0 && std::abs (k - nearest) <= 3) harm += e; else inh += e; }
    return { 10 * std::log10 (inh + 1e-30), 10 * std::log10 (harm + 1e-30) };
}

static void testOversamplerFilters() {
    for (int f : { 2, 4, 8 }) {
        const double lat = Oversampler::latencySamples (f);
        std::printf ("  %dx: latency %.2f samples (%.3f ms @ 48 kHz)\n", f, lat, lat / 48.0);
        CHECK (lat > 0.5 && lat < 10);   // polyphase IIR: a few samples, not a long FIR
        for (double hz : { 100.0, 1000.0, 10000.0, 18000.0 }) {   // passband flat (up + down only)
            Oversampler o; o.prepare (512); o.setFactor (f); std::vector<float> in (512), out (512); double ei = 0, eo = 0;
            for (int b = 0; b < 40; ++b) { for (int i = 0; i < 512; ++i) in[(size_t) i] = (float) std::sin (2 * PI * hz * (b * 512 + i) / 48000); o.up (0, in.data(), 512); o.down (0, out.data(), 512);
                if (b > 10) for (int i = 0; i < 512; ++i) { ei += in[(size_t) i] * in[(size_t) i]; eo += out[(size_t) i] * out[(size_t) i]; } }
            CHECK (std::abs (10 * std::log10 (eo / ei)) < 0.1);
        }
        // image rejection: a 15 kHz tone upsampled must leave almost nothing above 24 kHz in the oversampled buffer
        Oversampler o; o.prepare (1024); o.setFactor (f); std::vector<float> in (1024); std::vector<float> up;
        for (int b = 0; b < 8; ++b) { for (int i = 0; i < 1024; ++i) in[(size_t) i] = (float) std::sin (2 * PI * 15000 * (b * 1024 + i) / 48000); float* u = o.up (0, in.data(), 1024); if (b >= 4) up.insert (up.end(), u, u + 1024 * f); }
        const size_t M = 4096 * (size_t) f; std::vector<std::complex<double>> X (M); for (size_t i = 0; i < M; ++i) X[i] = up[i] * (0.5 - 0.5 * std::cos (2 * PI * (double) i / (double) M));
        fft (X); double pass = 0, stop = 0; const double binHz = 48000.0 * f / (double) M;
        for (size_t k = 1; k < M / 2; ++k) { const double hz = (double) k * binHz; (hz < 24000 ? pass : stop) += std::norm (X[k]); }
        const double rej = 10 * std::log10 (pass / (stop + 1e-30)); std::printf ("      image rejection @15 kHz: %.1f dB\n", rej); CHECK (rej > 60);
    }
}

static void testAliasReduction() {
    struct Case { const char* type; std::vector<std::pair<const char*, float>> params; } cases[] = {
        { "amp", { { "model", 5 }, { "gain", 1.0f }, { "output", 1 } } },          // Modern High Gain, preamp only (no cab hiding it)
        { "amp", { { "model", 3 }, { "gain", 0.9f }, { "output", 0 }, { "cab", 1 } } }, // British Crunch through the full amp + cab
        { "distortion", { { "dist", 1.0f }, { "filter", 0.0f } } },
        { "fuzz", { { "fuzz", 1.0f }, { "tone", 1.0f } } },
    };
    for (auto& c : cases) {
        double prev = 1e9, off = 0, at4 = 0, at8 = 0;
        for (int f : { 1, 2, 4, 8 }) {
            auto p = createPedal (c.type); for (auto& kv : c.params) p->setParam (kv.first, kv.second);
            auto r = aliasing (*p, f); const double rel = r.inharmDb - r.harmDb;
            std::printf ("  %-10s %s %dx: aliasing %.1f dB below the harmonics\n", c.type, c.params[0].first, f, -rel);
            if (f == 1) off = rel; if (f == 4) at4 = rel; if (f == 8) at8 = rel;
            CHECK (rel < prev + 1.0); prev = rel;   // never meaningfully worse with more oversampling
        }
        CHECK (at4 < off - 10.0);   // 4x removes >= 10 dB of aliasing for a 4.4 kHz sine at full gain (hard clippers are the worst case)
        CHECK (at8 < off - 17.0);
    }
}

static void testCleanBlendAligned() {   // Klon-style clean blend must not comb-filter when oversampled
    auto p = createPedal ("klon"); p->setParam ("gain", 0.0f); p->setParam ("output", 0.0f);
    for (int f : { 1, 4 }) {
        p->setOversampling (f); p->prepare (48000, 256); p->setEnabled (true); p->reset();
        ProcessContext ctx; std::vector<float> L (256), R (256); double lo = 0, hi = 0; long t = 0;
        for (int b = 0; b < 200; ++b) { for (int i = 0; i < 256; ++i, ++t) { float x = 0.01f * (float) (std::sin (2 * PI * 300 * t / 48000.0) + std::sin (2 * PI * 7000 * t / 48000.0)); L[(size_t) i] = R[(size_t) i] = x; }
            p->process (L.data(), R.data(), 256, ctx); if (b > 50) for (float v : L) lo += v * v; }
        (void) hi; static double ref = 0; if (f == 1) ref = lo; else { const double d = 10 * std::log10 (lo / ref); std::printf ("  clean-blend level change at 4x: %.2f dB\n", d); CHECK (std::abs (d) < 0.5); }
    }
}

static void testGovernor() {
    OversamplingGovernor g; g.setUser (8, true); const double blk = 128.0 / 48000;
    int steps = 0; for (int i = 0; i < (int) (0.9 / blk); ++i) steps += g.update (0.9, blk); CHECK (steps == 0 && g.current == 8);   // < 1 s of overload: no change
    for (int i = 0; i < (int) (0.2 / blk); ++i) g.update (0.9, blk); CHECK (g.current == 4); CHECK (g.reduced());
    for (int i = 0; i < (int) (3.5 / blk); ++i) g.update (0.95, blk); CHECK (g.current == 1);   // keeps stepping down to Off
    for (int i = 0; i < (int) (10 / blk); ++i) g.update (0.5, blk); CHECK (g.current == 1);     // middle zone: hold
    for (int i = 0; i < (int) (31 / blk); ++i) g.update (0.1, blk); CHECK (g.current == 2);     // 30 s calm: one step back up
    for (int i = 0; i < (int) (65 / blk); ++i) g.update (0.1, blk); CHECK (g.current == 8 && ! g.reduced());
    g.setUser (4, false); for (int i = 0; i < (int) (5 / blk); ++i) g.update (0.99, blk); CHECK (g.current == 4);   // auto off: never touches it
}

static RigPreset ampRig() {
    RigPreset r; r.name = "t"; PedalDef a; a.uid = "__amp__"; a.type = "amp"; a.enabled = true; a.params = { { "model", 4 }, { "gain", 0.8f } };
    PedalDef d; d.uid = "od"; d.type = "overdrive"; d.enabled = true; r.lanes[0] = { d, a }; return r;
}
static void testRigIntegration() {
    RigEngine rig; rig.prepare (48000, 128); rig.post (cmdRouting (Routing::TwoCable));
    rig.post (cmdOversampling (4, true)); rig.setChain (buildChain (ampRig()));
    std::vector<float> in (128), o[4]; for (auto& v : o) v.assign (128, 0); float* outs[4] = { o[0].data(), o[1].data(), o[2].data(), o[3].data() }; const float* ins[2] = { in.data(), nullptr };
    auto run = [&] (double seconds) { for (int b = 0; b < (int) (seconds * 48000 / 128); ++b) { for (int i = 0; i < 128; ++i) in[(size_t) i] = 0.2f * (float) std::sin (2 * PI * 196 * (b * 128 + i) / 48000.0); rig.process (ins, 2, outs, 2, 128); } };
    rig.setLoadOverride (0.1); run (0.2);
    CHECK (rig.oversamplingNow.load() == 4); const double lat = rig.latencySamples(); std::printf ("  rig latency (overdrive + amp at 4x): %.2f samples\n", lat);
    CHECK (std::abs (lat - 2 * Oversampler::latencySamples (4)) < 1e-9);
    rig.setLoadOverride (0.95); run (1.2); CHECK (rig.oversamplingNow.load() == 2); CHECK (rig.oversamplingReduced.load());
    bool finite = true; for (float v : o[0]) finite = finite && std::isfinite (v); CHECK (finite);
    rig.setLoadOverride (0.1); run (31); CHECK (rig.oversamplingNow.load() == 4); CHECK (! rig.oversamplingReduced.load());
    rig.post (cmdOversampling (1, false)); run (0.05); CHECK (rig.oversamplingNow.load() == 1); CHECK (rig.latencySamples() == 0);
    rig.post (cmdOversampling (8, false)); run (0.05); CHECK (rig.oversamplingNow.load() == 8);
    rig.setChain (buildChain (ampRig())); run (0.05);   // a new chain picks up the current factor
    if (auto* c = rig.currentChain()) for (auto& lane : c->lanes) for (auto& s : lane) if (s.pedal->oversamples()) CHECK (s.pedal->oversampling() == 8);
}

static void testSettingsJson() {
    Library l; CHECK (l.settings.ampOversampling == 4); CHECK (l.settings.autoOversampling);
    l.settings.ampOversampling = 8; l.settings.autoOversampling = false;
    auto back = libraryFromJson (toJson (l)); CHECK (back.settings.ampOversampling == 8); CHECK (! back.settings.autoOversampling);
    auto def = libraryFromJson (Json::object()); CHECK (def.settings.ampOversampling == 4);
}

static void testCpu() {
    for (int f : { 1, 2, 4, 8 }) {
        auto amp = createPedal ("amp"); amp->setParam ("model", 5); amp->setParam ("gain", 0.8f);
        auto od = createPedal ("overdrive");
        for (auto* p : { amp.get(), od.get() }) { p->setOversampling (f); p->prepare (48000, 128); p->setEnabled (true); }
        ProcessContext ctx; std::vector<float> L (128), R (128); const int blocks = 10 * 48000 / 128;
        auto t0 = std::chrono::steady_clock::now();
        for (int b = 0; b < blocks; ++b) { for (int i = 0; i < 128; ++i) L[(size_t) i] = R[(size_t) i] = 0.3f * (float) std::sin (0.05 * (b * 128 + i)); od->process (L.data(), R.data(), 128, ctx); amp->process (L.data(), R.data(), 128, ctx); }
        const double el = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("  CPU overdrive + amp + cab at %dx: %.2f%% of one x86 core (10 s audio in %.3f s)\n", f, el * 10.0, el);
        CHECK (el < (f == 8 ? 3.0 : 1.5));   // generous bound for CI machines; iPad numbers are TODO on device
    }
}

int main() {
    testOversamplerFilters(); testAliasReduction(); testCleanBlendAligned(); testGovernor(); testRigIntegration(); testSettingsJson(); testCpu();
    std::printf ("v0.12: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
