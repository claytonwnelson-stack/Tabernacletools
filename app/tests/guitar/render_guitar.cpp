// Guitar app offline render + DSP tests (the rig, plus the core click mixed after it). Writes WAVs of each priority pedal on a synthetic guitar part
// and checks: finite/non-silent output, click-free scene changes, spillover tails, 4-cable routing, tuner, CPU.
#include "dsp/RigBuilder.h"
#include "model/Song.h"
#include "audio/Voice.h"
#include <cstdio>
#include <cmath>
#include <chrono>
#include <string>
#include <functional>
#include <sys/stat.h>
using namespace wr;

static int fails = 0;
#define CHECK(c, msg) do { if (! (c)) { ++fails; std::printf ("FAIL: %s  (%s)\n", msg, #c); } else std::printf ("ok   %s\n", msg); } while (0)
static const int SR = 48000, BLK = 128;

static void writeWav (const std::string& fn, const std::vector<float>& L, const std::vector<float>& R) {
    FILE* f = std::fopen (fn.c_str(), "wb"); if (! f) return; int n = (int) L.size(); int data = n * 4;
    auto w32 = [&] (uint32_t x) { std::fwrite (&x, 4, 1, f); }; auto w16 = [&] (uint16_t x) { std::fwrite (&x, 2, 1, f); };
    std::fwrite ("RIFF", 1, 4, f); w32 (36 + data); std::fwrite ("WAVEfmt ", 1, 8, f); w32 (16); w16 (1); w16 (2); w32 (SR); w32 (SR * 4); w16 (4); w16 (16);
    std::fwrite ("data", 1, 4, f); w32 (data);
    for (int i = 0; i < n; ++i) { w16 ((uint16_t) (int16_t) (clampf (L[i], -1, 1) * 32767)); w16 ((uint16_t) (int16_t) (clampf (R[i], -1, 1) * 32767)); }
    std::fclose (f);
}

// Karplus-Strong guitar: the orange riff (G string 6 4 6 7, B 5, G 4) over an A / Asus2 strum, guitar level ~0.3 peak
static std::vector<float> guitar (double secs) {
    std::vector<float> out ((size_t) (secs * SR), 0.0f);
    struct Pluck { double t; float midi; float amp; };
    std::vector<Pluck> pl;
    const float strumA[] = { 45, 52, 57, 61, 64 }, strumSus[] = { 45, 52, 57, 59, 64 };
    for (int bar = 0; bar < 2; ++bar) { double t0 = bar * 3.2; for (int k = 0; k < 5; ++k) pl.push_back ({ t0 + k * 0.012, (bar ? strumSus : strumA)[k], 0.5f }); }
    const float riff[] = { 61, 59, 61, 62, 64, 59 }; for (int i = 0; i < 6; ++i) pl.push_back ({ 6.6 + i * 0.42, riff[i], 0.8f });
    wk::Rng rng;
    for (auto& p : pl) {
        float hz = wk::mtof (p.midi); int N = (int) (SR / hz); std::vector<float> d ((size_t) N); for (auto& x : d) x = rng.next() * p.amp;
        size_t s0 = (size_t) (p.t * SR); float last = 0;
        for (size_t i = 0; s0 + i < out.size() && i < (size_t) (SR * 3.0); ++i) { size_t k = i % (size_t) N; float y = 0.996f * 0.5f * (d[k] + last); last = d[k]; d[k] = y; out[s0 + i] += y * 0.35f; }
    }
    return out;
}

struct Render { std::vector<float> o[4]; };
static Render run (RigEngine& rig, const std::vector<float>& in0, const std::vector<float>* in1, int nOut, std::function<void (int block)> perBlock = {}) {
    Render r; for (int c = 0; c < nOut; ++c) r.o[c].assign (in0.size(), 0.0f);
    std::vector<float> zero (BLK, 0.0f);
    for (size_t s = 0, b = 0; s < in0.size(); s += BLK, ++b) {
        int n = (int) std::min ((size_t) BLK, in0.size() - s); if (perBlock) perBlock ((int) b);
        const float* ins[2] = { in0.data() + s, in1 ? in1->data() + s : zero.data() }; float* outs[4] = { nullptr, nullptr, nullptr, nullptr };
        for (int c = 0; c < nOut; ++c) outs[c] = r.o[c].data() + s;
        rig.process (ins, 2, outs, nOut, n);
    }
    return r;
}
static float rms (const std::vector<float>& v, size_t a, size_t b) { double s = 0; b = std::min (b, v.size()); for (size_t i = a; i < b; ++i) s += (double) v[i] * v[i]; return b > a ? (float) std::sqrt (s / (double) (b - a)) : 0; }
static float maxJump (const std::vector<float>& v, size_t a, size_t b) { float m = 0; for (size_t i = std::max<size_t> (a, 1); i < std::min (b, v.size()); ++i) m = std::max (m, std::abs (v[i] - v[i - 1])); return m; }

int main (int argc, char** argv) {
    std::string dir = argc > 1 ? argv[1] : "renders"; mkdir (dir.c_str(), 0755);
    auto g = guitar (10.0);

    // ---- 1. each priority pedal alone in TwoCable mode -> WAV
    const char* lanePre[] = { "comp", "boost", "overdrive", "fuzz", "octdown" };
    for (auto& info : pedalCatalog()) {
        RigEngine rig; rig.prepare (SR, BLK); RigPreset rp; rp.name = info.type; bool pre = false; for (auto* t : lanePre) if (info.type == t) pre = true;
        rp.lanes[pre ? 0 : 1].push_back ({ "p", info.type, true, {} });
        if (info.type == "freeze") rp.lanes[1].back().enabled = false;
        rig.setChain (buildChain (rp)); rig.post (cmdRouting (Routing::TwoCable)); rig.post (cmdTuner (info.type == "key9" || info.type == "synth9", false));
        auto r = run (rig, g, nullptr, 2, [&] (int b) { if (info.type == "freeze") { if (b == (int) (7.0 * SR / BLK)) rig.post (cmdEnable (*rig.currentChain(), "p", true)); } });
        bool fin = true; for (float x : r.o[0]) fin = fin && std::isfinite (x);
        float lev = rms (r.o[0], 0, r.o[0].size()); float dry = rms (g, 0, g.size());
        writeWav (dir + "/" + info.type + ".wav", r.o[0], r.o[1]);
        char msg[160]; std::snprintf (msg, sizeof msg, "%-11s finite, audible (rms %.3f vs dry %.3f)%s", info.type.c_str(), lev, dry, info.implemented ? "" : " [stub=passthrough]");
        CHECK (fin && lev > 0.2f * dry && lev < 8.0f * dry + 0.05f, msg);
    }
    writeWav (dir + "/_dry_guitar.wav", g, g);

    // ---- 2. full seed rig, four-cable routing; out1 = before-amp mono, outs3/4 = after-amp stereo (loop return)
    {
        RigEngine rig; rig.prepare (SR, BLK); rig.setChain (buildChain (defaultRig())); rig.post (cmdRouting (Routing::FourCable)); rig.post (cmdTrim (Lane::Pre, -10));
        // pretend the amp sim returns a scaled copy of out1 on its loop send (with one block of latency)
        std::vector<float> loop (g.size(), 0.0f);
        Render r; for (int c = 0; c < 4; ++c) r.o[c].assign (g.size(), 0.0f);
        for (size_t s = 0; s < g.size(); s += BLK) {
            int n = (int) std::min ((size_t) BLK, g.size() - s);
            if (s >= (size_t) BLK) for (int i = 0; i < n; ++i) loop[s + (size_t) i] = r.o[0][s - BLK + (size_t) i] * 2.0f;
            const float* ins[2] = { g.data() + s, loop.data() + s }; float* outs[4] = { r.o[0].data() + s, r.o[1].data() + s, r.o[2].data() + s, r.o[3].data() + s };
            rig.process (ins, 2, outs, 4, n);
        }
        CHECK (rms (r.o[0], 0, g.size()) > 0.01f, "4-cable: out1 (to amp input) carries the BEFORE AMP lane");
        CHECK (rms (r.o[1], 0, g.size()) == 0.0f, "4-cable: out2 silent");
        float l = rms (r.o[2], 0, g.size()), rr = rms (r.o[3], 0, g.size()), diff = 0; for (size_t i = 0; i < g.size(); ++i) diff += std::abs (r.o[2][i] - r.o[3][i]);
        CHECK (l > 0.01f && rr > 0.01f && diff > 1.0f, "4-cable: outs 3/4 (loop return) are stereo AFTER AMP lane");
        CHECK (rig.routingNow.load() == 0 && ! rig.fellBackToTwoCable.load(), "4-cable: active");
        writeWav (dir + "/_fourcable_out1_preamp.wav", r.o[0], r.o[0]); writeWav (dir + "/_fourcable_out34_return.wav", r.o[2], r.o[3]);
        // only 2 outputs available -> automatic 2-cable fallback
        auto r2 = run (rig, g, nullptr, 2); CHECK (rig.fellBackToTwoCable.load() && rms (r2.o[0], 0, g.size()) > 0.01f, "4-cable requested with 2 outs -> 2-cable fallback");
    }

    // ---- 3. click-free switching: 220 Hz sine through Boost (+15 dB) toggled every 0.25 s and a scene change
    {
        std::vector<float> s ((size_t) SR * 2); for (size_t i = 0; i < s.size(); ++i) s[i] = 0.2f * std::sin (k2Pi * 220.0f * (float) i / SR);
        RigEngine rig; rig.prepare (SR, BLK); RigPreset rp; rp.lanes[0].push_back ({ "boost", "boost", false, { { "gain", 15.0f } } }); rp.lanes[1].push_back ({ "od", "overdrive", false, {} });
        rig.setChain (buildChain (rp)); rig.post (cmdRouting (Routing::TwoCable)); rig.post (cmdTuner (false, false));
        bool on = false; Scene sc { "x", { { "od", { true, { { "drive", 0.8f } } } } } };
        auto r = run (rig, s, nullptr, 2, [&] (int b) { if (b > 2 && b % 94 == 0) { on = ! on; rig.post (cmdEnable (*rig.currentChain(), "boost", on)); } if (b == 700) for (auto& c : sceneCommands (*rig.currentChain(), sc)) rig.post (c); });
        float slope = 0.2f * 5.62f * k2Pi * 220.0f / SR; // max natural sample step of the boosted sine
        float j = maxJump (r.o[0], (size_t) BLK * 4, s.size());
        char msg[120]; std::snprintf (msg, sizeof msg, "no clicks on pedal toggles / scene change (max step %.4f <= %.4f)", j, slope * 1.5f); CHECK (j <= slope * 1.5f, msg);
    }

    // ---- 4. spillover: delay + reverb tails keep ringing after the scene turns them off; dry passes untouched
    {
        RigEngine rig; rig.prepare (SR, BLK); RigPreset rp; rp.lanes[1].push_back ({ "delay", "delay", true, { { "mix", 0.5f }, { "feedback", 0.5f } } }); rp.lanes[1].push_back ({ "verb", "reverb", true, { { "mix", 0.5f } } });
        rig.setChain (buildChain (rp)); rig.post (cmdRouting (Routing::TwoCable)); rig.post (cmdTuner (false, false));
        std::vector<float> burst ((size_t) SR * 4, 0.0f); for (size_t i = 0; i < (size_t) SR / 2; ++i) burst[i] = g[(size_t) (6.6 * SR) + i];
        auto r = run (rig, burst, nullptr, 2, [&] (int b) { if (b == (int) (0.6 * SR / BLK)) { rig.post (cmdEnable (*rig.currentChain(), "delay", false)); rig.post (cmdEnable (*rig.currentChain(), "verb", false)); } });
        float tail = rms (r.o[0], (size_t) (0.8 * SR), (size_t) (1.8 * SR));
        char msg[120]; std::snprintf (msg, sizeof msg, "spillover: tails ring after switch-off (tail rms %.4f)", tail); CHECK (tail > 0.003f, msg);
        writeWav (dir + "/_spillover.wav", r.o[0], r.o[1]);
    }

    // ---- 4b. tempo drives the synced delay: 1/4 echo lands at 60/BPM s (song tempo, nudge, MIDI clock all set the rig BPM)
    for (double bpm : { 72.0, 120.0, 90.0 }) {
        RigEngine rig; rig.prepare (SR, BLK); RigPreset rp; rp.lanes[1].push_back ({ "delay", "delay", true, { { "div", 0.0f }, { "mix", 1.0f }, { "feedback", 0.0f } } });
        rig.setChain (buildChain (rp)); rig.post (cmdRouting (Routing::TwoCable)); rig.post (cmdTuner (false, false)); rig.post (cmdBpm (bpm));
        std::vector<float> imp ((size_t) SR * 2, 0.0f); for (int i = 0; i < 64; ++i) imp[(size_t) (SR / 10 + i)] = 0.5f * std::sin (k2Pi * i / 64.0f);
        auto r = run (rig, imp, nullptr, 2); size_t from = (size_t) (SR / 10 + SR * 0.15), best = from; float pk = 0;
        for (size_t i = from; i < r.o[0].size(); ++i) if (std::abs (r.o[0][i]) > pk) { pk = std::abs (r.o[0][i]); best = i; }
        double echo = (double) ((long) best - SR / 10) / SR, want = 60.0 / bpm;
        char msg[120]; std::snprintf (msg, sizeof msg, "delay 1/4 synced at %.0f BPM: echo %.3f s (want %.3f)", bpm, echo, want); CHECK (std::abs (echo - want) < 0.03, msg);
    }

    // ---- 4c. click track: tempo, accent on beat 1, routing to its own pair, mute, tuner mute leaves it alone
    {
        auto onsets = [] (const std::vector<float>& x) { std::vector<std::pair<size_t, float>> o; size_t last = 0; bool armed = true;
            for (size_t i = 0; i < x.size(); ++i) { float a = std::abs (x[i]); if (armed && a > 0.01f) { size_t e = std::min (x.size(), i + 480); float pk = 0; for (size_t k = i; k < e; ++k) pk = std::max (pk, std::abs (x[k])); o.push_back ({ i, pk }); armed = false; last = i; } if (! armed && i - last > 2400) armed = true; } return o; };
        RigEngine rig; rig.prepare (SR, BLK); rig.setChain (std::make_unique<Chain>()); rig.post (cmdRouting (Routing::FourCable)); rig.post (cmdTuner (false, false));
        rig.post (cmdBpm (120)); rig.post (cmdClick (Command::ClickMeter, 3)); rig.post (cmdClick (Command::ClickDb, -6)); rig.post (cmdClick (Command::ClickOut, 2)); rig.post (cmdClick (Command::ClickOn, 1));
        std::vector<float> sil ((size_t) SR * 3, 0.0f); auto r = run (rig, sil, nullptr, 4);
        auto on = onsets (r.o[1]); bool spacing = on.size() >= 6; for (size_t i = 1; i < on.size(); ++i) spacing = spacing && std::abs ((double) (on[i].first - on[i - 1].first) - SR * 0.5) < 2.0;
        char msg[160]; std::snprintf (msg, sizeof msg, "click at 120 BPM: %d clicks 0.5 s apart on out 2", (int) on.size()); CHECK (spacing, msg);
        bool accent = on.size() >= 6 && on[0].second > on[1].second * 1.4f && on[3].second > on[2].second * 1.4f && std::abs (on[1].second - on[2].second) < 0.02f;
        std::snprintf (msg, sizeof msg, "click accent on beat 1 of 3/4 (%.2f vs %.2f)", on.empty() ? 0.f : on[0].second, on.size() > 1 ? on[1].second : 0.f); CHECK (accent, msg);
        CHECK (rms (r.o[0], 0, sil.size()) < 1e-6f && rms (r.o[2], 0, sil.size()) < 1e-6f && rms (r.o[3], 0, sil.size()) < 1e-6f, "click only on its own output (out 2 in four-cable), amp outs untouched");
        CHECK (! rig.clickConflict.load(), "out 2 click in four-cable: no conflict flag");
        rig.post (cmdClick (Command::ClickOut, 1)); run (rig, std::vector<float> (BLK * 4, 0.0f), nullptr, 4); CHECK (rig.clickConflict.load(), "outs 3/4 click in four-cable: conflict flagged (shares the loop return)");
        // two-cable rig on 1/2, click on 3/4 (in-ears): separate
        rig.post (cmdRouting (Routing::TwoCable)); std::vector<float> g2 (g.begin(), g.begin() + SR * 2); auto r2 = run (rig, g2, nullptr, 4);
        CHECK (onsets (r2.o[2]).size() >= 3 && ! rig.clickConflict.load(), "two-cable: click on outs 3/4, no conflict");
        float rigL = rms (r2.o[0], 0, g2.size()); CHECK (rigL > 0.003f, "two-cable: guitar still on outs 1/2");
        // tuner mute silences the rig but not the click
        rig.post (cmdTuner (true, true)); auto r3 = run (rig, g2, nullptr, 4);
        CHECK (rms (r3.o[0], (size_t) SR / 10, g2.size()) < 1e-4f && onsets (r3.o[2]).size() >= 3, "tuner open: rig muted, click keeps going");
        rig.post (cmdTuner (false, false));
        // mute: smooth (no click-free violation) and silent
        rig.post (cmdClick (Command::ClickMute, 1)); auto r4 = run (rig, sil, nullptr, 4); CHECK (rms (r4.o[2], (size_t) SR / 10, sil.size()) < 1e-5f, "click mute silences it");
        rig.post (cmdClick (Command::ClickMute, 0)); auto r5 = run (rig, sil, nullptr, 4); CHECK (onsets (r5.o[2]).size() >= 5, "click unmute brings it back");
        // MIDI-clock tempo (any rig BPM) is followed
        rig.post (cmdBpm (90)); auto r6 = run (rig, sil, nullptr, 4); auto o6 = onsets (r6.o[2]); bool sp90 = o6.size() >= 4; for (size_t i = 2; i < o6.size(); ++i) sp90 = sp90 && std::abs ((double) (o6[i].first - o6[i - 1].first) - SR * 60.0 / 90.0) < 2.0;
        CHECK (sp90, "click follows a tempo change (90 BPM)");
        writeWav (dir + "/_click.wav", r.o[1], r.o[1]);
    }

    // ---- 4d. rig change keeps the old rig's tails (fade), instead of cutting them
    for (float tailSec : { 3.0f, 0.0f }) {
        RigEngine rig; rig.prepare (SR, BLK); RigPreset rp; rp.lanes[1].push_back ({ "delay", "delay", true, { { "mix", 0.5f }, { "feedback", 0.5f } } }); rp.lanes[1].push_back ({ "verb", "reverb", true, { { "mix", 0.5f }, { "decay", 4.0f } } });
        rig.setChain (buildChain (rp)); rig.post (cmdRouting (Routing::TwoCable)); rig.post (cmdTuner (false, false)); rig.post (cmdClick (Command::TailSec, tailSec));
        std::vector<float> burst ((size_t) SR * 6, 0.0f); for (size_t i = 0; i < (size_t) SR / 2; ++i) burst[i] = g[(size_t) (6.6 * SR) + i];
        RigPreset dry; dry.name = "Dry"; dry.lanes[0].push_back ({ "eq", "eq", true, {} });
        auto r = run (rig, burst, nullptr, 2, [&] (int b) { if (b == (int) (0.6 * SR / BLK)) rig.setChain (buildChain (dry)); if (b % 20 == 0) rig.collectGarbage(); });
        float tail = rms (r.o[0], (size_t) (0.8 * SR), (size_t) (1.8 * SR)), late = rms (r.o[0], (size_t) (4.0 * SR), (size_t) (6.0 * SR));
        float j = maxJump (r.o[0], (size_t) (0.55 * SR), (size_t) (0.75 * SR)), before = maxJump (r.o[0], (size_t) (0.4 * SR), (size_t) (0.58 * SR));
        char msg[160];
        if (tailSec > 0) {
            std::snprintf (msg, sizeof msg, "rig change: old rig's tails ring out (rms %.4f after the switch)", tail); CHECK (tail > 0.003f, msg);
            std::snprintf (msg, sizeof msg, "rig change: tails faded out after %.0f s (rms %.6f)", tailSec, late); CHECK (late < 1e-4f, msg);
            std::snprintf (msg, sizeof msg, "rig change: no click at the swap (max step %.4f vs %.4f before)", j, before); CHECK (j <= before * 1.5f + 0.01f, msg);
            writeWav (dir + "/_rig_change_tails.wav", r.o[0], r.o[1]);
        } else { std::snprintf (msg, sizeof msg, "rig change with tails off: cut (rms %.5f)", tail); CHECK (tail < 0.001f, msg); }
    }

    // ---- 4e. spoken section cue + count-in on the click output (bundled voice samples)
    {
        VoiceBank vb; for (const char* w : { "verse", "two", "three", "four", "one", "chorus" }) { std::string fn = std::string (WR_SOURCE_DIR) + "/resources/voice/" + w + ".wav";
            FILE* f = std::fopen (fn.c_str(), "rb"); if (! f) continue; std::vector<uint8_t> d; int ch; while ((ch = std::fgetc (f)) != EOF) d.push_back ((uint8_t) ch); std::fclose (f);
            std::vector<float> m; double sr; if (decodeWav (d.data(), d.size(), m, sr)) vb.addWord (w, m, sr); }
        vb.prepare (SR); CHECK (vb.size() == 6, "voice samples load from resources/voice");
        VoiceClip nums[13]; for (int i = 1; i <= 12; ++i) nums[i] = vb.number (i);
        RigEngine rig; rig.prepare (SR, BLK); rig.click.setNumbers (nums); rig.setChain (std::make_unique<Chain>()); rig.post (cmdRouting (Routing::FourCable)); rig.post (cmdTuner (false, false));
        rig.post (cmdBpm (100)); rig.post (cmdClick (Command::ClickDb, -12)); rig.post (cmdClick (Command::CueDb, -3)); rig.post (cmdClick (Command::ClickOn, 1));
        std::vector<float> lead ((size_t) (SR * 0.6 * 4 + SR * 0.6 * 1.3), 0.0f); auto a = run (rig, lead, nullptr, 4); // into beat 2 of bar 2
        int slot = rig.click.stageCue (vb.phrase ("Verse 2")); rig.post (cmdAnnounce (slot, 42));
        std::vector<float> rest ((size_t) (SR * 0.6 * 7.2), 0.0f); /* 2.7 beats to the cue bar, the cue bar, a bit */ auto b = run (rig, rest, nullptr, 4);
        CHECK (rig.click.cueAck.load() == 42 && rig.click.cueDueBar.load() == 4, "announce acknowledged; next bar is the cue bar, change at the bar line after it");
        CHECK (rig.click.clickBar.load() >= 4, "grid reached the change bar line");
        float early = rms (b.o[1], 0, (size_t) (SR * 0.6 * 2.6)); CHECK (early < 0.02f, "nothing spoken in the rest of the current bar (only the click)");
        float voice = rms (b.o[1], (size_t) (SR * 0.6 * 2.7), (size_t) (SR * 0.6 * 3.7)), amp = std::max (rms (b.o[0], 0, rest.size()), rms (b.o[2], 0, rest.size()));
        char msg[160]; std::snprintf (msg, sizeof msg, "cue voice on the click output (out 2 rms %.3f), amp outputs silent (%.6f)", voice, amp); CHECK (voice > 0.02f && amp < 1e-6f, msg);
        std::vector<float> all = a.o[1]; all.insert (all.end(), b.o[1].begin(), b.o[1].end());
        // count-in with the click muted: 8 audible clicks + spoken numbers, then silence (click muted)
        rig.post (cmdClick (Command::ClickMute, 1)); rig.post (cmdCountIn (8, true));
        std::vector<float> ci ((size_t) (SR * 0.6 * 9), 0.0f); auto c = run (rig, ci, nullptr, 4);
        float during = 0; for (size_t i = 0; i < (size_t) (SR * 0.6 * 8); ++i) during = std::max (during, std::abs (c.o[1][i])); float after = rms (c.o[1], (size_t) (SR * 0.6 * 8.2), ci.size());
        std::snprintf (msg, sizeof msg, "count-in audible while click muted (peak %.3f), silent after (%.5f)", during, after); CHECK (during > 0.1f && after < 1e-4f, msg);
        all.insert (all.end(), c.o[1].begin(), c.o[1].end()); writeWav (dir + "/_voice_cue_and_count_in.wav", all, all);
    }

    // ---- 5. tuner
    {
        RigEngine rig; rig.prepare (SR, BLK); rig.setChain (std::make_unique<Chain>()); rig.post (cmdTuner (true, true));
        for (float hz : { 82.41f, 110.0f, 146.83f, 196.0f, 246.94f, 329.63f, 112.0f }) {
            std::vector<float> s ((size_t) SR / 2); for (size_t i = 0; i < s.size(); ++i) s[i] = 0.2f * std::sin (k2Pi * hz * (float) i / SR) + 0.08f * std::sin (2 * k2Pi * hz * (float) i / SR);
            auto r = run (rig, s, nullptr, 2); auto t = tunerReading (rig.tunerHz.load(), rig.tunerConf.load());
            float expectCents = 1200.0f * std::log2 (hz / (440.0f * std::pow (2.0f, std::round (12.0f * std::log2 (hz / 440.0f)) / 12.0f)));
            char msg[120]; std::snprintf (msg, sizeof msg, "tuner %.2f Hz -> %s%d %+.1f c (expect %+.1f)", hz, t.note.c_str(), t.octave, t.cents, expectCents);
            CHECK (t.valid && std::abs (t.cents - expectCents) < 3.0f, msg);
            CHECK (rms (r.o[0], (size_t) SR / 20, s.size()) < 1e-4f, "tuner mute silences outputs");
        }
    }

    // ---- 5b. Key9 / Synth9: pitch tracking drives the worship-keys engines (dry muted -> anything heard is the synth)
    for (const char* ty : { "key9", "synth9" }) {
        RigEngine rig; rig.prepare (SR, BLK); RigPreset rp; rp.lanes[0].push_back ({ "k", ty, true, { { "dry", 0.0f } } });
        rig.setChain (buildChain (rp)); rig.post (cmdRouting (Routing::TwoCable)); rig.post (cmdTuner (false, false));
        auto r = run (rig, g, nullptr, 2); float lev = rms (r.o[0], (size_t) (6.6 * SR), (size_t) (9.0 * SR));
        writeWav (dir + "/_" + std::string (ty) + "_wet_only.wav", r.o[0], r.o[1]);
        char msg[120]; std::snprintf (msg, sizeof msg, "%s tracks the riff and plays the keys engine (wet rms %.4f)", ty, lev); CHECK (lev > 0.003f, msg);
    }

    // ---- 6. CPU: full seed rig + shimmer + freeze on, 128-sample blocks
    {
        RigEngine rig; rig.prepare (SR, BLK); auto rp = defaultRig(); for (auto& l : rp.lanes) for (auto& p : l) if (p.type != "fuzz" && p.type != "freeze") p.enabled = true;
        rig.setChain (buildChain (rp)); rig.post (cmdRouting (Routing::FourCable));
        auto t0 = std::chrono::steady_clock::now(); run (rig, g, &g, 4); double el = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        char msg[120]; std::snprintf (msg, sizeof msg, "CPU: 10 s of audio with 11 pedals on in %.3f s (%.1f%% of realtime, 1 core x86)", el, el * 10.0); CHECK (el < 2.0, msg);
    }
    std::printf ("%s\n", fails ? "RENDER TESTS FAILED" : "all render tests passed");
    return fails ? 1 : 0;
}
