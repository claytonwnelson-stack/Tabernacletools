#include "MidiRouter.h"
#include <cmath>

namespace wr {
static const char* kNames[] = { "", "prevSection", "nextSection", "jumpChorus", "nextSong", "prevSong", "tap", "boost", "switchC", "switchD", "tunerToggle", "toggleView",
                                "gotoSection", "clockBpm", "start", "stop", "learned", "clickMute", "clickToggle", "play", "bankNext", "bankPrev", "bankChanged",
                                "tracksPlay", "tracksNext", "tracksLoop", "tracksStop", "songPosition", "playStart" };
Action::Type actionFromName (const std::string& n) { for (int i = 1; i < (int) (sizeof kNames / sizeof *kNames); ++i) if (n == kNames[i]) return (Action::Type) i; return Action::None; }
const char* actionName (Action::Type t) { return kNames[(int) t]; }

void MidiRouter::fire (int u, int sw, bool longP) {
    auto& p = units_[(size_t) u]; Action a; a.unit = u; a.sw = sw; a.type = actionFromName (act (u, sw, longP));
    if ((a.type == Action::BankNext || a.type == Action::BankPrev) && ! p.banks.empty()) { // handled here: realtime, no allocation
        const int n = (int) p.banks.size(); p.bank = ((p.bank + (a.type == Action::BankNext ? 1 : -1)) % n + n) % n;
        a.type = Action::BankChanged; a.value = p.bank; emit (a); return; }
    if (a.type != Action::None && a.type != Action::BankNext && a.type != Action::BankPrev) emit (a);
}

void MidiRouter::fireNamed (int u, const std::string& name) {
    auto& p = units_[(size_t) u]; Action a; a.unit = u; a.sw = -1; a.type = actionFromName (name);
    if ((a.type == Action::BankNext || a.type == Action::BankPrev) && ! p.banks.empty()) {
        const int n = (int) p.banks.size(); p.bank = ((p.bank + (a.type == Action::BankNext ? 1 : -1)) % n + n) % n;
        a.type = Action::BankChanged; a.value = p.bank; emit (a); return; }
    if (a.type != Action::None && a.type != Action::BankNext && a.type != Action::BankPrev) emit (a);
}

void MidiRouter::noteFactory (int u, int pc, double t) {
    if (u < 0 || u >= nUnits_) return;
    auto& slot = pend_[(size_t) u];
    const int sw = ((pc % 4) + 4) % 4;
    if (slot.on && t >= slot.t && t - slot.t <= 50.0) {
        const int a = ((slot.pc % 4) + 4) % 4;
        slot.on = false;
        const bool e = (a == 0 && sw == 1) || (a == 1 && sw == 0);
        const bool f = (a == 2 && sw == 3) || (a == 3 && sw == 2);
        if (e || f) { fireNamed (u, e ? units_[(size_t) u].comboE : units_[(size_t) u].comboF); return; }
        fire (u, a, false);
    } else if (slot.on) fire (u, ((slot.pc % 4) + 4) % 4, false);
    slot.on = true; slot.pc = pc; slot.t = t;
}

void MidiRouter::flushFactory (double t) {
    for (int u = 0; u < nUnits_; ++u) {
        auto& slot = pend_[(size_t) u];
        if (slot.on && t - slot.t >= 50.0) { const int sw = ((slot.pc % 4) + 4) % 4; slot.on = false; fire (u, sw, false); }
    }
}

bool MidiRouter::unitMessage (int u, uint8_t st, uint8_t d1, uint8_t d2, double t, bool ignoreChannel) {
    auto& p = units_[(size_t) u];
    const int type = st & 0xF0;
    const bool noteOn = type == 0x90 && d2 > 0;
    for (int s = 0; s < 4; ++s) {
        auto& sw = p.sw[s];
        if (sw.longPress.matches (st, d1, p.channel, ignoreChannel)) { if (type == 0xC0 || noteOn || (type == 0xB0 && d2 >= 64)) fire (u, s, true); return true; }
        if (! sw.press.matches (st, d1, p.channel, ignoreChannel)) continue;
        const bool hasRelease = sw.press.kind == MidiBinding::CC || sw.press.kind == MidiBinding::Note;
        const bool down = type == 0xC0 || noteOn || (type == 0xB0 && d2 >= 64);
        auto& h = held_[(size_t) u][(size_t) s];
        const std::string& pa = act (u, s, false), & la = act (u, s, true);
        const bool needsHold = hasRelease && ! la.empty() && sw.longPress.kind == MidiBinding::None && la != pa;
        if (! needsHold) { if (down) fire (u, s, false); return true; }
        const bool instant = pa == "tap";
        if (down) { h.down = true; h.fired = false; h.t = t; if (instant) fire (u, s, false); }
        else if (h.down) { h.down = false; if (! h.fired && ! instant) fire (u, s, false); }
        return true;
    }
    // Factory Chocolate program change (display 000-003 is PC 0-3; later groups wrap with % 4).
    // Only on a known USB port — a merged buffer cannot tell two identical pedals apart.
    if (ignoreChannel && type == 0xC0) { noteFactory (u, d1, t); return true; }
    return false;
}

void MidiRouter::handle (uint8_t st, uint8_t d1, uint8_t d2, double t, int fromUnit) {
    // ---- system realtime: MIDI clock (24 ppqn)
    if (st == 0xF8) {
        // a gap > 250 ms (slower than 10 BPM) means the clock stopped and restarted: start measuring afresh
        if (clockLast_ >= 0 && t - clockLast_ > 250.0) { clockT0_ = -1; clockBeatMs_ = 0; }
        clockLast_ = t;
        if (clockT0_ < 0) { clockT0_ = t; clockCount_ = 0; return; }
        if (++clockCount_ >= 24) { double beat = (t - clockT0_) / (clockCount_ / 24.0); clockT0_ = t; clockCount_ = 0;
            if (beat > 0) {
                // v0.17: EMA smooth the beat period so jittery USB/BT/network clocks settle without jumping the click
                clockBeatMs_ = clockBeatMs_ <= 0 ? beat : clockBeatMs_ * 0.8 + beat * 0.2;
                double b = 60000.0 / clockBeatMs_; b = std::round (b * 10.0) / 10.0;
                if (std::abs (b - clockBpm_) >= 0.1) clockBpm_ = b;
                // emitted once per beat (not only on change) so the session knows the clock is still present
                if (followClock_) { Action a; a.type = Action::ClockBpm; a.bpm = clampBpm (clockBpm_); emit (a); } } }
        return;
    }
    if (st == 0xFA || st == 0xFB) { clockT0_ = -1; clockBeatMs_ = 0; Action a; a.type = Action::TransportStart; emit (a); return; }
    if (st == 0xFC) { Action a; a.type = Action::TransportStop; emit (a); return; }
    if (st == 0xF2) { // Song Position Pointer: d1 = LSB, d2 = MSB of 14-bit MIDI beats (16th notes)
        if (followClock_) { Action a; a.type = Action::SongPosition; a.value = (int) d1 | ((int) d2 << 7); emit (a); }
        return; }
    if (st >= 0xF0) return;
    const int type = st & 0xF0, ch = (st & 0x0F) + 1;
    const bool noteOn = type == 0x90 && d2 > 0;

    // ---- MIDI learn
    if (learnU_ >= 0 && learnU_ < nUnits_ && (type == 0xB0 || type == 0xC0 || noteOn) && ! (type == 0xB0 && d2 < 64)) {
        auto& p = units_[(size_t) learnU_]; auto& b = learnLong_ ? p.sw[learnSw_].longPress : p.sw[learnSw_].press;
        b.kind = type == 0xB0 ? MidiBinding::CC : type == 0xC0 ? MidiBinding::PC : MidiBinding::Note; b.number = d1; b.channel = 0; p.channel = ch;
        Action a; a.type = Action::Learned; a.unit = learnU_; a.sw = learnSw_; a.value = learnLong_ ? 1 : 0; emit (a); learnU_ = -1; return;
    }

    // ---- footswitch units (a known USB port never falls through onto the other pedal)
    if (fromUnit >= 0 && fromUnit < nUnits_) { unitMessage (fromUnit, st, d1, d2, t, true); return; }
    for (int u = 0; u < nUnits_; ++u) if (unitMessage (u, st, d1, d2, t, false)) return;

    // ---- external (Cubase): PC n / note (noteBase + n) => go to section n; CC 102/103 = next/prev section
    if (ch == extCh_) {
        Action a;
        if (type == 0xC0) { a.type = Action::GotoSection; a.value = d1; emit (a); }
        else if (noteOn && d1 >= noteBase_) { a.type = Action::GotoSection; a.value = d1 - noteBase_; emit (a); }
        else if (type == 0xB0 && d2 >= 64 && (d1 == 102 || d1 == 103)) { a.type = d1 == 102 ? Action::NextSection : Action::PrevSection; emit (a); }
    }
}

void MidiRouter::tick (double t) {
    for (int u = 0; u < nUnits_; ++u) for (int s = 0; s < 4; ++s) {
        auto& h = held_[(size_t) u][(size_t) s];
        if (h.down && ! h.fired && t - h.t >= (units_[(size_t) u].sw[s].longMs > 0 ? units_[(size_t) u].sw[s].longMs : units_[(size_t) u].longPressMs)) { h.fired = true; fire (u, s, true); }
    }
}
} // namespace wr
