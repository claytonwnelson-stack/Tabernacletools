// Click track + bar grid + spoken section cues + count-in. Sample-accurate, realtime safe (no allocation).
// The bar grid always runs (so section changes can be quantised even with the click sound off);
// the click sound plays when on and not muted, or during a count-in (unless count-in is off).
#pragma once
#include "DspUtil.h"
#include "Voice.h"
#include <cmath>

namespace wr {
using dsp::clampf; using dsp::kPi; using dsp::k2Pi;
class ClickGen {
public:
    // click sounds: every preset has a distinct downbeat (different pitch and timbre, not just louder)
    enum Preset { BeepHiLo = 0, WoodblockHiLo, CowbellHiLo, RimSidestick, kNumPresets };
    static const char* presetName (int p) { static const char* n[] = { "Beep (hi / lo)", "Woodblock (hi / lo)", "Cowbell (hi / lo)", "Rimshot / sidestick" }; return n[std::max (0, std::min (3, p))]; }
    struct Params { int preset = BeepHiLo; float downGain = 1.66f; /* downbeat level relative to beats 2..n */ double bpm = 120; int beatsPerBar = 4; bool on = false, muted = false, accent = true; float gain = 0.25f;
                    bool cuesOn = true, cuesWhenMuted = false; float cueGain = 0.5f; };
    void prepare (double fs) { fs_ = fs; fStep_ = (float) (1.0 / (0.008 * fs)); }
    void reset() { phase_ = 1.0; beat_ = -1; } // next sample is a downbeat
    void setNumbers (const VoiceClip* n13) { for (int i = 0; i < 13; ++i) nums_[i] = n13 ? n13[i] : VoiceClip(); } // [1..12]

    // Announce a section: the NEXT bar is the cue bar - the name is spoken on its beat 1, then the remaining beats are
    // counted ("Verse two, 2, 3, 4" in 4/4, "Chorus, 2, 3" in 3/4) - and the change lands on the bar line after it.
    // Always exactly one full cue bar of the song's meter (the 8-click count-in is only for the start of a song).
    void announce (VoiceClip name, int seq, const Params&) {
        cueName_ = name; cueSeq_ = seq; pendingStart_ = true;
        announceBar_ = bar_ + 1; dueBar_ = announceBar_ + 1; lastDue_ = dueBar_;
    }
    // Count-in: restart the grid so `beats` clicks end exactly on a bar line, then the song starts. Audible even if muted.
    void countIn (int beats, bool spoken, int beatsPerBar) {
        const int bpb = clampBpb (beatsPerBar); reset(); countLeft_ = std::max (0, beats); countSpoken_ = spoken;
        if (countLeft_ > 0) beat_ = (bpb - (countLeft_ % bpb)) % bpb - 1; // so the last count-in click is the bar's last beat
    }

    // Ableton Link: keep the grid phase-locked to the shared timeline. `beat` = Link beat at the first sample of the next
    // block (quantum = beats per bar, so Link bar lines are our bar lines). Tiny drift is trimmed inside the beat;
    // a big jump (joining a session) re-seats the grid. Returns true when it re-seated.
    bool lockTo (double beat, int beatsPerBar) {
        const int bpb = clampBpb (beatsPerBar); const double q = (double) bpb;
        double target = std::fmod (beat, q); if (target < 0) target += q;
        double ours = std::fmod ((double) beat_ + phase_ + q, q);
        double d = target - ours; if (d > q * 0.5) d -= q; if (d < -q * 0.5) d += q;
        if (std::abs (d) < 1e-3) return false;
        const int tb = (int) std::floor (target + 1e-9) % bpb;
        if (std::abs (d) < 0.1) { if (tb == (int) std::floor (ours + 1e-9) % bpb && beat_ >= 0) phase_ = target - std::floor (target); return false; }
        const double f = target - std::floor (target);
        if (f < 0.02) { beat_ = (tb - 1 + bpb) % bpb; phase_ = 1.0; } else { beat_ = tb; phase_ = f; }
        return true;
    }
    double position() const { return std::fmod ((double) beat_ + phase_ + 16.0, 16.0); } // beat + fraction (tests)

    void process (float* L, float* R, int n, const Params& p) {
        const int bpb = clampBpb (p.beatsPerBar);
        const float clickT = ((p.on && ! p.muted) || countLeft_ > 0) ? p.gain : 0.0f;
        const float cueT = (p.cuesOn && (! p.muted || p.cuesWhenMuted || ! p.on)) || countLeft_ > 0 ? p.cueGain : 0.0f; // spoken count-in follows the count-in
        const float sm = 1.0f - std::exp (-1.0f / (0.004f * (float) fs_)), dec = std::exp (-1.0f / (0.012f * (float) fs_));
        const double inc = std::max (30.0, p.bpm) / 60.0 / fs_;
        for (int i = 0; i < n; ++i) {
            if (env_ < 1e-4f) g_ = clickT; else g_ += (clickT - g_) * sm;
            vg_ += (cueT - vg_) * sm;
            phase_ += inc;
            if (phase_ >= 1.0) { phase_ -= 1.0; beat_ = (beat_ + 1) % bpb; if (beat_ == 0) ++bar_; ++beatCount_; onBeat (p, bpb); }
            float y = 0;
            if (env_ > 1e-4f) { y = hitSample() * amp_ * g_; (void) dec; }
            if (vPos_ < vLen_) { y += vData_[vPos_++] * vg_; }
            if (fGain_ > 0 && fPos_ < fLen_) { y += fData_[fPos_++] * vg_ * fGain_; fGain_ -= fStep_; }
            L[i] += y; if (R != L) R[i] += y;
        }
    }
    // ---- state (audio thread writes; Rig publishes through atomics)
    int beatInBar() const { return beat_ < 0 ? 0 : beat_; }
    long long bar() const { return bar_; }
    long long beats() const { return beatCount_; }
    int cueSeq() const { return cueSeq_; }
    long long dueBar() const { return lastDue_; }
    bool countingIn() const { return countLeft_ > 0; }
    bool speaking() const { return vPos_ < vLen_; }
private:
    // ---- hit synthesis (per preset, downbeat vs other beats)
    void trigger (int preset, bool down) {
        preset_ = std::max (0, std::min (3, preset)); down_ = down; env_ = 1.0f; env2_ = 1.0f; osc_ = osc2_ = osc3_ = 0; t_ = 0; bp1_ = bp2_ = 0;
        auto tau = [&] (float s) { return std::exp (-1.0f / (s * (float) fs_)); };
        switch (preset_) {
            case BeepHiLo:      hz_ = down ? 1760.0f : 1175.0f; hz2_ = 0; dec_ = tau (0.012f); dec2_ = 0; break;                       // sine, A6 vs D6
            case WoodblockHiLo: hz_ = down ? 1250.0f : 820.0f; hz2_ = hz_ * 2.7f; dec_ = tau (down ? 0.022f : 0.018f); dec2_ = tau (0.004f); break; // hollow + knock, pitch drop
            case CowbellHiLo:   hz_ = down ? 800.0f : 540.0f; hz2_ = down ? 1190.0f : 800.0f; dec_ = tau (down ? 0.09f : 0.07f); dec2_ = tau (0.012f); break; // 808-style two squares
            default:            hz_ = down ? 1700.0f : 620.0f; hz2_ = down ? 460.0f : 2400.0f; dec_ = tau (down ? 0.035f : 0.012f); dec2_ = tau (down ? 0.018f : 0.003f); break; // rimshot ring+noise vs sidestick knock
        }
    }
    float noise() { rng_ = rng_ * 1664525u + 1013904223u; return (float) (int32_t) rng_ * (1.0f / 2147483648.0f); }
    static float sq (float ph) { return ph < kPi ? 1.0f : -1.0f; }
    float hitSample() {
        const float w = k2Pi / (float) fs_; float y = 0;
        auto adv = [&] (float& o, float hz) { o += w * hz; if (o > k2Pi) o -= k2Pi; };
        switch (preset_) {
            case BeepHiLo: y = std::sin (osc_) * env_; adv (osc_, hz_); break;
            case WoodblockHiLo: { float drop = 1.0f + 0.35f * env2_; y = 0.8f * std::sin (osc_) * env_ + 0.35f * std::sin (osc2_) * env2_ + 0.25f * noise() * env2_ * env2_; adv (osc_, hz_ * drop); adv (osc2_, hz2_); break; }
            case CowbellHiLo: { float x = 0.5f * (sq (osc_) + sq (osc2_)); bp1_ += 0.35f * (x - bp1_); bp2_ += 0.35f * (bp1_ - bp2_); float band = bp1_ - bp2_; // crude band-pass
                                y = (0.9f * band * 2.0f) * (0.6f * env_ + 0.4f * env2_); adv (osc_, hz_); adv (osc2_, hz2_); break; }
            default: { if (down_) { y = 0.5f * std::sin (osc_) * env_ + 0.45f * std::sin (osc2_) * env2_ + 0.5f * noise() * env2_; }
                       else { y = 0.7f * std::sin (osc_) * env_ + 0.3f * std::sin (osc2_) * env2_ + 0.25f * noise() * env2_ * env2_; }
                       adv (osc_, hz_); adv (osc2_, hz2_); break; }
        }
        env_ *= dec_; env2_ *= dec2_; if (env_ < 1e-4f && env2_ < 1e-4f) env_ = 0;
        return clampf (y, -1.0f, 1.0f);
    }
    int preset_ = 0; bool down_ = false; float env2_ = 0, hz2_ = 0, osc2_ = 0, osc3_ = 0, dec_ = 0.999f, dec2_ = 0, bp1_ = 0, bp2_ = 0, t_ = 0; uint32_t rng_ = 22222u;
    static int clampBpb (int b) { return std::max (1, std::min (16, b)); }
    void startVoice (VoiceClip c) {
        if (vPos_ < vLen_) { fData_ = vData_; fLen_ = vLen_; fPos_ = vPos_; fGain_ = 1.0f; } // let the old word fade (8 ms) instead of cutting
        vData_ = c.data; vLen_ = c.data ? c.len : 0; vPos_ = 0; }
    void onBeat (const Params& p, int bpb) {
        bool countBeat = false;
        if (countLeft_ > 0) { --countLeft_; countBeat = true; if (countSpoken_) { VoiceClip c = nums_[std::min (12, beat_ + 1)]; startVoice (c); } }
        if ((p.on || countBeat)) { const bool acc = (p.accent || countBeat) && beat_ == 0; trigger (p.preset, acc); amp_ = acc ? 0.6f * p.downGain : 0.6f; } // count-in uses the same downbeat sound
        if (cueSeq_ >= 0 && dueBar_ > 0) {
            if (pendingStart_ && beat_ == 0 && bar_ == announceBar_) { pendingStart_ = false; startVoice (cueName_); }
            else if (! pendingStart_ && bar_ == announceBar_ && beat_ >= 1) startVoice (nums_[std::min (12, beat_ + 1)]); // "2, 3, 4" on the beat (a long name is faded out)
            if (bar_ >= dueBar_) { dueBar_ = 0; }
        }
        (void) bpb;
    }
    double fs_ = 48000, phase_ = 1.0; int beat_ = -1; long long bar_ = 0, beatCount_ = 0;
    float env_ = 0, hz_ = 1000, amp_ = 1, osc_ = 0, g_ = 0, vg_ = 0;
    const float* vData_ = nullptr; int vLen_ = 0, vPos_ = 0;
    const float* fData_ = nullptr; int fLen_ = 0, fPos_ = 0; float fGain_ = 0, fStep_ = 1.0f / 384.0f;
    VoiceClip nums_[13] {}; VoiceClip cueName_ {}; int cueSeq_ = -1; long long announceBar_ = 0, dueBar_ = 0, lastDue_ = 0; bool pendingStart_ = false;
    int countLeft_ = 0; bool countSpoken_ = false;
};
} // namespace wr
