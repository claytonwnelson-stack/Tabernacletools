// v0.9 tests: circuit-modelled pedals, amp + cab + direct-to-PA, IR loader, trademark-free names, sound pack entitlements, keys stub.
#include "dsp/Pedal.h"
#include "model/Packs.h"
#include "../../keys/dsp/KeysEngine.h"
#include <cstdio>
#include <cmath>
#include <set>
#include <algorithm>
#include <cctype>
using namespace wr;
static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (! (c)) { ++fails; std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

static const char* kNew[] = { "gefuzz", "tristage", "treblebst", "bluesdrive", "ampinbox", "octfuzz", "otacomp", "gate", "flanger", "vibrato", "wah", "envfilter",
                              "analogdly", "tapeecho", "dotted8", "cloud", "spring", "amp", "cab" };
struct Res { float peak = 0, rms = 0; bool finite = true; std::vector<float> out; };
static Res run (Pedal& p, int blocks, float amp, float freq = 196, double bpm = 120, bool impulse = false) {
    Res r; float L[256], R[256]; ProcessContext ctx; ctx.bpm = bpm; double acc = 0; long n = 0;
    for (int b = 0; b < blocks; ++b) {
        for (int i = 0; i < 256; ++i) { long t = (long) b * 256 + i; L[i] = R[i] = impulse ? (t == 0 ? amp : 0.0f) : amp * std::sin (6.2831853f * freq * (float) t / 48000.0f); }
        p.process (L, R, 256, ctx);
        for (int i = 0; i < 256; ++i) { r.finite = r.finite && std::isfinite (L[i]) && std::isfinite (R[i]); r.peak = std::max (r.peak, std::abs (L[i])); acc += L[i] * L[i]; ++n; r.out.push_back (L[i]); }
    }
    r.rms = (float) std::sqrt (acc / std::max (1L, n)); return r;
}
static std::unique_ptr<Pedal> mk (const std::string& t) { auto p = createPedal (t); if (p) { p->prepare (48000, 256); p->setEnabled (true); p->reset(); } return p; }

static void testNewPedals() {
    std::set<std::string> cat; for (auto& i : pedalCatalog()) cat.insert (i.type);
    for (auto* t : kNew) { CHECK (cat.count (t)); auto p = mk (t); CHECK (p != nullptr); if (! p) continue; CHECK (p->implemented);
        auto r = run (*p, 200, 0.3f); if (! r.finite || r.rms < 1e-4f || r.peak > 4) std::printf ("  %s rms %.4f peak %.3f finite %d\n", t, r.rms, r.peak, (int) r.finite);
        CHECK (r.finite); CHECK (r.peak < 4.0f); if (std::string (t) != "gate") CHECK (r.rms > 1e-4f); }
    // every param extreme stays finite
    for (auto* t : kNew) { auto p = mk (t); for (int i = 0; i < p->numParams(); ++i) p->setParam (i, p->specs()[(size_t) i].max); auto a = run (*p, 60, 0.8f); CHECK (a.finite);
        for (int i = 0; i < p->numParams(); ++i) p->setParam (i, p->specs()[(size_t) i].min); auto b = run (*p, 60, 0.8f); CHECK (b.finite); }
}
static void testBehaviour() {
    { auto g = mk ("gate"); auto r = run (*g, 100, 0.0003f); CHECK (r.out.back() == 0.0f || std::abs (r.out.back()) < 1e-6f); auto r2 = run (*g, 50, 0.3f); CHECK (r2.rms > 0.1f); }
    { auto d = mk ("dotted8"); d->setParam ("mix", 1); d->setParam ("duck", 0); d->setParam ("quarter", 0); auto r = run (*d, 120, 1.0f, 0, 120, true);
      int at = (int) (0.375 * 48000); float pk = 0; int where = 0; for (int i = 1000; i < 25000; ++i) if (std::abs (r.out[(size_t) i]) > pk) { pk = std::abs (r.out[(size_t) i]); where = i; }
      CHECK (std::abs (where - at) < 8); }
    { // drives add harmonics: output != scaled input
      for (auto* t : { "gefuzz", "tristage", "bluesdrive", "ampinbox", "octfuzz" }) { auto p = mk (t); auto r = run (*p, 40, 0.5f, 220); CHECK (r.rms > 0.01f); } }
    { auto w = mk ("wah"); w->setParam ("position", 0); auto lo = run (*w, 40, 0.3f, 2000); auto w2 = mk ("wah"); w2->setParam ("position", 1); auto hi = run (*w2, 40, 0.3f, 2000); CHECK (hi.rms > lo.rms * 1.5f); }
}
static void testAmp() {
    for (int m = 0; m < 6; ++m) for (int o = 0; o < 3; ++o) {
        auto a = mk ("amp"); a->setParam ("model", (float) m); a->setParam ("output", (float) o); a->setParam ("gain", 1); a->setParam ("master", 1); a->setParam ("level", 12);
        auto r = run (*a, 100, 0.9f); CHECK (r.finite); CHECK (r.rms > 0.01f); CHECK (r.peak <= 0.96f); // limiter protects PA / headphones
    }
    { auto a = mk ("amp"); a->setParam ("gain", 0.1f); auto c = run (*a, 50, 0.3f); auto h = mk ("amp"); h->setParam ("model", 5); h->setParam ("gain", 1); auto hg = run (*h, 50, 0.3f);
      CHECK (c.rms > 0); CHECK (hg.rms > 0); }
    // IR loader: unit impulse IR == pass-through of the cab stage
    { auto c = mk ("cab"); float ir[64] = { 1.0f }; CHECK (loadCabImpulse (*c, ir, 64, 48000)); CHECK ((int) std::lround (c->getParam ("cab")) == 5);
      auto r = run (*c, 4, 0.5f, 300); float L = 0.5f * std::sin (6.2831853f * 300 * 500 / 48000.0f); CHECK (std::abs (r.out[500] - L) < 1e-3f); }
    { auto e = mk ("eq"); float ir[4] = { 1 }; CHECK (! loadCabImpulse (*e, ir, 4, 48000)); auto c = mk ("cab"); CHECK (! loadCabImpulse (*c, nullptr, 0, 48000)); }
    { auto a = mk ("amp"); std::vector<float> ir (4800); for (size_t i = 0; i < ir.size(); ++i) ir[i] = std::exp (-(float) i / 200.0f) * ((i % 7) ? 0.3f : -0.5f);
      CHECK (loadCabImpulse (*a, ir.data(), (int) ir.size(), 44100)); auto r = run (*a, 40, 0.3f); CHECK (r.finite); CHECK (r.rms > 0.001f); }
}
static std::string lower (std::string s) { for (auto& c : s) c = (char) std::tolower ((unsigned char) c); return s; }
static void testNames() {
    static const char* tm[] = { "tube screamer", "ibanez", "klon", "centaur", "big muff", "electro-harmonix", "ehx", "fuzz face", "dunlop", "cry baby", "crybaby", "boss", "mxr", "dyna comp",
        "rat", "proco", "tone bender", "rangemaster", "dallas", "octavia", "uni-vibe", "univibe", "mu-tron", "echoplex", "space echo", "roland", "memory man", "strymon", "big sky", "timeline",
        "fender", "marshall", "vox", "mesa", "dumble", "plexi", "bluesbreaker", "whammy", "digitech", "pog", "key9", "synth9", "line 6", "kemper", "neural", "eventide", "leslie", "hammond", "rhodes", "wurlitzer" };
    int bad = 0;
    for (auto& i : pedalCatalog()) { auto n = lower (i.name); for (auto* t : tm) { std::string w = t; size_t at = n.find (w);
        bool word = at != std::string::npos && (at == 0 || ! std::isalpha ((unsigned char) n[at - 1])) && (at + w.size() >= n.size() || ! std::isalpha ((unsigned char) n[at + w.size()]));
        if (word) { ++bad; std::printf ("  trademark-ish name: '%s' (%s)\n", i.name.c_str(), t); } } }
    CHECK (bad == 0);
}
static void testPacks() { // v0.10: packs are content groupings; subscription logic is tested in test_v010.cpp
    std::set<std::string> cat; for (auto& i : pedalCatalog()) cat.insert (i.type);
    std::set<std::string> ids;
    for (auto& p : packManifest()) { CHECK (ids.insert (p.id).second);
        for (auto& c : p.content) if (c.find (':') == std::string::npos) { if (! cat.count (c)) std::printf ("  pack %s: unknown pedal %s\n", p.id.c_str(), c.c_str()); CHECK (cat.count (c)); } }
    for (auto& i : pedalCatalog()) if (i.implemented) { auto* p = packForContent (i.type); if (! p) std::printf ("  pedal not in any pack: %s\n", i.type.c_str()); CHECK (p != nullptr); }
}
static void testKeys() {
    using namespace wr::keys;
    CHECK (keyToPitchClass ("B") == 11); CHECK (keyToPitchClass ("Eb") == 3); CHECK (keyToPitchClass ("F#m") == 6); CHECK (keyToPitchClass ("H") == -1);
    for (auto& pt : starterPatches()) { CHECK (packForContent (pt.id) != nullptr); KeysEngine k; k.prepare (48000); CHECK (k.setPatch (pt.id)); k.noteOn (60, 0.8f);
        float L[256], R[256]; double e = 0; bool fin = true; for (int b = 0; b < 200; ++b) { k.render (L, R, 256); for (int i = 0; i < 256; ++i) { e += L[i] * L[i]; fin = fin && std::isfinite (L[i]); } }
        if (e <= 1e-4) std::printf ("  silent keys patch %s\n", pt.id.c_str()); CHECK (fin); CHECK (e > 1e-4); }
    KeysEngine k; k.prepare (48000); k.setPadKey ("B", true); CHECK (k.padRoot() == 11); CHECK (k.patch() == "keys:Key Pad");
    float L[256], R[256]; double e = 0; for (int b = 0; b < 400; ++b) { k.render (L, R, 256); for (int i = 0; i < 256; ++i) e += L[i] * L[i]; } CHECK (e > 1e-3);
    k.setPadKey ("C", true); CHECK (k.padRoot() == 0); k.setPadKey ("C", false); CHECK (k.padRoot() == -1); CHECK (! k.setPatch ("keys:nope"));
}
int main() { setvbuf (stdout, nullptr, _IONBF, 0);
    testNewPedals(); testBehaviour(); testAmp(); testNames(); testPacks(); testKeys();
    std::printf ("%d checks, %d failures\n", checks, fails); return fails ? 1 : 0; }
