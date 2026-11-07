// worship-core render tests: click presets, voice cues, count-in, click outputs, Link lock - on the ClickEngine alone
// (no instrument DSP). Writes WAVs of each priority pedal on a synthetic guitar part
// and checks: finite/non-silent output, click-free scene changes, spillover tails, 4-cable routing, tuner, CPU.
#include "audio/ClickEngine.h"
#include "audio/DspUtil.h"
#include "model/Song.h"
#include "audio/Voice.h"
#include <cstdio>
#include <cmath>
#include <chrono>
#include <string>
#include <functional>
#include <sys/stat.h>
using namespace wr; using dsp::clampf;

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

struct Render { std::vector<float> o[4]; };
static Render run (ClickEngine& ce, size_t len, int nOut) {
    Render r; for (int c = 0; c < nOut; ++c) r.o[c].assign (len, 0.0f);
    for (size_t s = 0; s < len; s += BLK) { int n = (int) std::min ((size_t) BLK, len - s); float* outs[4] = { nullptr, nullptr, nullptr, nullptr };
        for (int c = 0; c < nOut; ++c) outs[c] = r.o[c].data() + s; ce.process (outs, nOut, n); }
    return r;
}
static float rms (const std::vector<float>& v, size_t a, size_t b) { double s = 0; b = std::min (b, v.size()); for (size_t i = a; i < b; ++i) s += (double) v[i] * v[i]; return b > a ? (float) std::sqrt (s / (double) (b - a)) : 0; }

int main (int argc, char** argv) {
    std::string dir = argc > 1 ? argv[1] : "renders"; mkdir (dir.c_str(), 0755);
    // ---- click outputs: 0 = outs 1/2, 1 = outs 3/4, 2 = out 2 only; nothing leaks onto the other channels
    for (int co = 0; co < 3; ++co) {
        ClickEngine ce; ce.prepare (SR, BLK); ce.post (cmdBpm (120)); ce.post (cmdClick (Command::ClickOut, (float) co)); ce.post (cmdClick (Command::ClickOn, 1)); ce.post (cmdClick (Command::ClickDb, -6));
        auto r = run (ce, SR * 2, 4); float e[4]; for (int c = 0; c < 4; ++c) e[c] = rms (r.o[c], 0, SR * 2);
        const bool want[3][4] = { { 1, 1, 0, 0 }, { 0, 0, 1, 1 }, { 0, 1, 0, 0 } }; bool ok = true; for (int c = 0; c < 4; ++c) ok = ok && ((e[c] > 0.005f) == want[co][c]);
        char msg[160]; std::snprintf (msg, sizeof msg, "click out %d: rms %.3f %.3f %.3f %.3f", co, e[0], e[1], e[2], e[3]); CHECK (ok, msg);
        CHECK (ce.clickBar.load() >= 1 && ce.bpm.load() == 120.0, "  bar grid runs at the posted tempo");
    }
    { ClickEngine ce; ce.prepare (SR, BLK); ce.post (cmdClick (Command::ClickOut, 1)); ce.post (cmdClick (Command::ClickOn, 1)); auto r = run (ce, SR, 2); // outs 3/4 on a 2-out device
      CHECK (rms (r.o[0], 0, SR) == 0 && rms (r.o[1], 0, SR) == 0 && ce.clickBar.load() >= 1, "click pair missing on this device: silent, bar grid keeps running"); }
    { ClickEngine ce; ce.prepare (SR, BLK); ce.post (cmdBpm (120)); ce.post (cmdClick (Command::ClickOn, 1)); run (ce, BLK * 10, 2);
      ce.setLinkBeat (2.5); run (ce, BLK, 2); CHECK (ce.linkReseats.load() == 1, "Link beat re-seats the grid once on join");
      ce.setLinkBeat (2.5 + (BLK * 1.0 / SR) * 2.0); run (ce, BLK, 2); CHECK (ce.linkReseats.load() == 1, "  then follows without re-seating"); }

    // ---- 4e. spoken section cue + count-in on the click output (bundled voice samples)
    {
        VoiceBank vb; for (const char* w : { "verse", "two", "three", "four", "one", "chorus" }) { std::string fn = std::string (WR_SOURCE_DIR) + "/resources/voice/" + w + ".wav";
            FILE* f = std::fopen (fn.c_str(), "rb"); if (! f) continue; std::vector<uint8_t> d; int ch; while ((ch = std::fgetc (f)) != EOF) d.push_back ((uint8_t) ch); std::fclose (f);
            std::vector<float> m; double sr; if (decodeWav (d.data(), d.size(), m, sr)) vb.addWord (w, m, sr); }
        vb.prepare (SR); CHECK (vb.size() == 6, "voice samples load from resources/voice");
        VoiceClip nums[13]; for (int i = 1; i <= 12; ++i) nums[i] = vb.number (i);
        ClickEngine rig; rig.prepare (SR, BLK); rig.setNumbers (nums);
        rig.post (cmdBpm (100)); rig.post (cmdClick (Command::ClickDb, -12)); rig.post (cmdClick (Command::CueDb, -3)); rig.post (cmdClick (Command::ClickOn, 1));
        std::vector<float> lead ((size_t) (SR * 0.6 * 4 + SR * 0.6 * 1.3), 0.0f); auto a = run (rig, lead.size(), 4); // into beat 2 of bar 2
        int slot = rig.stageCue (vb.phrase ("Verse 2")); rig.post (cmdAnnounce (slot, 42));
        std::vector<float> rest ((size_t) (SR * 0.6 * 7.2), 0.0f); /* 2.7 beats to the cue bar, the cue bar, a bit */ auto b = run (rig, rest.size(), 4);
        CHECK (rig.cueAck.load() == 42 && rig.cueDueBar.load() == 4, "announce acknowledged; next bar is the cue bar, change at the bar line after it");
        CHECK (rig.clickBar.load() >= 4, "grid reached the change bar line");
        float early = rms (b.o[1], 0, (size_t) (SR * 0.6 * 2.6)); CHECK (early < 0.02f, "nothing spoken in the rest of the current bar (only the click)");
        float voice = rms (b.o[1], (size_t) (SR * 0.6 * 2.7), (size_t) (SR * 0.6 * 3.7)), amp = std::max (rms (b.o[0], 0, rest.size()), rms (b.o[2], 0, rest.size()));
        char msg[160]; std::snprintf (msg, sizeof msg, "cue voice on the click output (out 2 rms %.3f), other outputs silent (%.6f)", voice, amp); CHECK (voice > 0.02f && amp < 1e-6f, msg);
        std::vector<float> all = a.o[1]; all.insert (all.end(), b.o[1].begin(), b.o[1].end());
        // count-in with the click muted: 8 audible clicks + spoken numbers, then silence (click muted)
        rig.post (cmdClick (Command::ClickMute, 1)); rig.post (cmdCountIn (8, true));
        std::vector<float> ci ((size_t) (SR * 0.6 * 9), 0.0f); auto c = run (rig, ci.size(), 4);
        float during = 0; for (size_t i = 0; i < (size_t) (SR * 0.6 * 8); ++i) during = std::max (during, std::abs (c.o[1][i])); float after = rms (c.o[1], (size_t) (SR * 0.6 * 8.2), ci.size());
        std::snprintf (msg, sizeof msg, "count-in audible while click muted (peak %.3f), silent after (%.5f)", during, after); CHECK (during > 0.1f && after < 1e-4f, msg);
        all.insert (all.end(), c.o[1].begin(), c.o[1].end()); writeWav (dir + "/_voice_cue_and_count_in.wav", all, all);
    }

    // ---- 4f. click presets: beat 1 is a different sound (pitch / spectrum), not only louder; count-in uses the same downbeat
    {
        auto spectrum = [] (const std::vector<float>& x, size_t a, size_t len) { // magnitude 100 Hz..6 kHz in 25 Hz bins, Hann window
            std::vector<double> m; for (double f = 100; f <= 6000; f += 25) { double re = 0, im = 0; for (size_t i = 0; i < len; ++i) { double w = 0.5 - 0.5 * std::cos (2 * M_PI * (double) i / (double) len), v = x[a + i] * w, ph = 2 * M_PI * f * (double) i / SR; re += v * std::cos (ph); im -= v * std::sin (ph); } m.push_back (std::sqrt (re * re + im * im)); } return m; };
        auto peakHz = [] (const std::vector<double>& m) { size_t k = 0; for (size_t i = 1; i < m.size(); ++i) if (m[i] > m[k]) k = i; return 100.0 + 25.0 * (double) k; };
        auto centroid = [] (const std::vector<double>& m) { double a = 0, b = 0; for (size_t i = 0; i < m.size(); ++i) { a += m[i] * (100.0 + 25.0 * (double) i); b += m[i]; } return b > 0 ? a / b : 0; };
        auto corr = [] (const std::vector<double>& a, const std::vector<double>& b) { double ab = 0, aa = 0, bb = 0; for (size_t i = 0; i < a.size(); ++i) { ab += a[i] * b[i]; aa += a[i] * a[i]; bb += b[i] * b[i]; } return ab / std::sqrt (aa * bb + 1e-30); };
        std::vector<float> demo; const size_t beatN = (size_t) (SR * 0.5), win = 1440; // 120 BPM, 30 ms analysis window
        for (int pr = 0; pr < ClickGen::kNumPresets; ++pr) {
            ClickGen c; c.prepare (SR); ClickGen::Params p; p.bpm = 120; p.beatsPerBar = 4; p.on = true; p.gain = 0.5f; p.preset = pr; p.cuesOn = false;
            std::vector<float> o (beatN * 8, 0.0f); c.process (o.data(), o.data(), (int) o.size(), p); demo.insert (demo.end(), o.begin(), o.end());
            auto d = spectrum (o, 0, win), u = spectrum (o, beatN, win), d2 = spectrum (o, beatN * 4, win), u3 = spectrum (o, beatN * 2, win);
            double pd = peakHz (d), pu = peakHz (u), cd = centroid (d), cu = centroid (u), r = corr (d, u);
            bool pitchDiff = std::abs (pd - pu) / std::min (pd, pu) > 0.15, timbreDiff = r < 0.8;
            char msg[200]; std::snprintf (msg, sizeof msg, "%s: downbeat peak %.0f Hz / centroid %.0f vs beat 2 %.0f / %.0f, spectral corr %.2f", ClickGen::presetName (pr), pd, cd, pu, cu, r);
            CHECK (pitchDiff && timbreDiff, msg);
            CHECK (corr (d, d2) > 0.98 && corr (u, u3) > 0.98, "  same sound on every downbeat / every other beat");
            // level-normalised: still different (so it's not only louder)
            ClickGen e; e.prepare (SR); p.downGain = 1.0f; std::vector<float> q (beatN * 2, 0.0f); e.process (q.data(), q.data(), (int) q.size(), p);
            float pkD = 0, pkU = 0; for (size_t i = 0; i < win; ++i) { pkD = std::max (pkD, std::abs (q[i])); pkU = std::max (pkU, std::abs (q[beatN + i])); }
            CHECK (corr (spectrum (q, 0, win), spectrum (q, beatN, win)) < 0.8, "  downbeat differs at equal level setting (0 dB)"); (void) pkD; (void) pkU;
            // count-in: muted click, same downbeat sound
            ClickGen ci; ci.prepare (SR); ClickGen::Params pm = p; pm.downGain = 1.66f; pm.muted = true; ci.countIn (8, false, 4); std::vector<float> w (beatN * 8, 0.0f); ci.process (w.data(), w.data(), (int) w.size(), pm);
            CHECK (corr (spectrum (w, 0, win), d) > 0.98 && corr (spectrum (w, beatN, win), u) > 0.98, "  count-in uses the same downbeat / beat sounds");
        }
        { ClickGen c; c.prepare (SR); ClickGen::Params p; p.on = true; p.gain = 0.5f; p.downGain = std::pow (10.0f, 9.0f / 20.0f); std::vector<float> o (beatN * 2, 0.0f); c.process (o.data(), o.data(), (int) o.size(), p);
          float a = 0, b = 0; for (size_t i = 0; i < win; ++i) { a = std::max (a, std::abs (o[i])); b = std::max (b, std::abs (o[beatN + i])); }
          char msg[120]; std::snprintf (msg, sizeof msg, "downbeat level setting +9 dB -> %.1f dB louder", 20 * std::log10 (a / b)); CHECK (std::abs (20 * std::log10 (a / b) - 9) < 1.0, msg); }
        writeWav (dir + "/_click_presets.wav", demo, demo);
    }

    std::printf ("%s\n", fails ? "RENDER TESTS FAILED" : "all render tests passed");
    return fails ? 1 : 0;
}
