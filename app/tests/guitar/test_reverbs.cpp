// Reverb set: stable, silent input dies out, bypass leaves the dry signal, distinct looks.
#include "dsp/Pedal.h"
#include "dsp/PedalFaces.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <set>
#include <string>
using namespace wr;
static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (! (c)) { ++fails; std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

static std::unique_ptr<Pedal> mk (const char* t) {
    auto p = createPedal (t); if (! p) return nullptr; p->prepare (48000, 256); p->setEnabled (true); p->reset(); return p;
}
static bool block (Pedal& p, float* L, float* R, int n, float amp, int i0) {
    ProcessContext ctx; ctx.bpm = 72;
    for (int i = 0; i < n; ++i) { float x = (i0 + i == 0) ? amp : 0.0f; L[i] = R[i] = x; }
    p.process (L, R, n, ctx);
    for (int i = 0; i < n; ++i) if (! std::isfinite (L[i]) || ! std::isfinite (R[i])) return false;
    return true;
}

static void silenceAndBypass (const char* type) {
    auto p = mk (type); CHECK (p != nullptr); if (! p) return;
    if (p->paramIndex ("decay") >= 0) p->setParam ("decay", 0.4f);
    if (p->paramIndex ("dwell") >= 0) p->setParam ("dwell", 0.15f);
    if (p->paramIndex ("time") >= 0) p->setParam ("time", 250);
    if (p->paramIndex ("hold") >= 0) p->setParam ("hold", 0.1f);
    if (p->paramIndex ("mix") >= 0) p->setParam ("mix", 1.0f);
    float L[256], R[256]; bool finite = true; float peak = 0;
    for (int b = 0; b < 20; ++b) { finite = block (*p, L, R, 256, b == 0 ? 0.5f : 0.0f, b == 0 ? 0 : 1) && finite;
        for (int i = 0; i < 256; ++i) peak = std::max (peak, std::abs (L[i])); }
    CHECK (finite); CHECK (peak < 4.0f);
    float tail = 0;
    for (int b = 0; b < 800; ++b) { finite = block (*p, L, R, 256, 0, 1) && finite; if (b > 760) for (int i = 0; i < 256; ++i) tail = std::max (tail, std::abs (L[i]) + std::abs (R[i])); }
    if (! (finite && tail < 1.0e-3f)) std::printf ("  %s tail %.3e finite %d\n", type, tail, (int) finite);
    CHECK (finite); CHECK (tail < 1.0e-3f);
    for (int i = 0; i < p->numParams(); ++i) p->setParam (i, p->specs()[(size_t) i].max);
    for (int b = 0; b < 30; ++b) finite = block (*p, L, R, 256, 0.8f, 1) && finite;
    for (int i = 0; i < p->numParams(); ++i) p->setParam (i, p->specs()[(size_t) i].min);
    for (int b = 0; b < 30; ++b) finite = block (*p, L, R, 256, 0.8f, 1) && finite;
    CHECK (finite);
    p->setEnabled (false); p->reset();
    for (int i = 0; i < 256; ++i) { L[i] = 0.2f; R[i] = -0.1f; }
    float inL = L[0], inR = R[0];
    ProcessContext ctx; p->process (L, R, 256, ctx);
    CHECK (std::abs (L[10] - inL) < 1.0e-4f); CHECK (std::abs (R[10] - inR) < 1.0e-4f);
}

static void cpu () {
    const char* types[] = { "hall", "room", "plate", "chamber", "church", "spring", "shimmer", "reverse", "gated", "modverb", "bloom", "cloud", "reverb" };
    std::printf ("  reverb CPU at 48 kHz (1 s of audio, one instance):\n");
    for (auto* t : types) {
        auto p = mk (t); if (! p) { std::printf ("    %s MISSING\n", t); CHECK (false); continue; }
        float L[256], R[256]; ProcessContext ctx; ctx.bpm = 72;
        auto t0 = std::chrono::steady_clock::now();
        for (int b = 0; b < 48000 / 256; ++b) {
            for (int i = 0; i < 256; ++i) L[i] = R[i] = 0.2f * std::sin (0.01f * (b * 256 + i));
            p->process (L, R, 256, ctx);
        }
        double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("    %-10s %6.2f ms   about %.2f%% of one core\n", t, ms, ms / 10.0);
        CHECK (ms < 400.0); // even a debug build should be well under realtime
    }
}

int main() {
    const char* types[] = { "reverb", "hall", "room", "plate", "chamber", "church", "spring", "shimmer", "cloud", "reverse", "gated", "modverb", "bloom" };
    std::set<std::string> cat; for (auto& i : pedalCatalog()) cat.insert (i.type);
    std::set<uint32_t> colours;
    for (auto* t : types) {
        CHECK (cat.count (t));
        auto look = pedalLook (t); colours.insert (look.argb); CHECK (look.tag != nullptr && look.tag[0] != 0);
        if (auto* f = pedalFace (t)) { CHECK (f->mainParams.size() >= 2 && f->mainParams.size() <= 4); CHECK (createPedal (t)->paramIndex (f->mainParams[0]) >= 0); }
        silenceAndBypass (t);
    }
    CHECK (colours.size() >= 10);
    cpu();
    std::printf ("%d/%d reverb checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
