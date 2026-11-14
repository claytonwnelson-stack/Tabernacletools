#include "MidiRouter.h"
#include <cctype>
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
        if (! (slot.on && t - slot.t >= 50.0)) continue;
        const int sw = ((slot.pc % 4) + 4) % 4; slot.on = false;
        auto& last = lastFactory_[(size_t) u];
        const std::string& lng = act (u, sw, true), & sh = act (u, sw, false);
        // Program change has no release, so a hold can never be timed. A second tap of the same
        // switch inside 0.4 s does the hold action instead (tuner, stop, click mute).
        const bool again = last.sw == sw && last.t >= 0 && t - last.t < 400.0 && ! lng.empty() && lng != sh;
        last.sw = sw; last.t = t;
        fire (u, sw, again);
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
    if (ignoreChannel && type == 0xC0) { sawPc_ = true; noteFactory (u, d1, t); return true; }
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

namespace {
std::string lowerCopy (std::string s) { for (auto& c : s) c = (char) std::tolower ((unsigned char) c); return s; }
bool looksLikePedal (const std::string& name) {
    const auto n = lowerCopy (name);
    return n.find ("chocolate") != std::string::npos || n.find ("m-vave") != std::string::npos || n.find ("mvave") != std::string::npos
        || n.find ("sinco") != std::string::npos || n.find ("foot") != std::string::npos;
}
bool looksVirtual (const std::string& name) {
    const auto n = lowerCopy (name);
    return n.find ("network") != std::string::npos || n.find ("session") != std::string::npos || n.find ("iac") != std::string::npos || n.find ("bluetooth") != std::string::npos;
}
struct Keyed { FootPortDesc port; std::string key; int occurrence = 0; bool pedal = false, virt = false, dup = false; };
}
FootAssignment assignFootPedals (const std::vector<FootPortDesc>& devices, const std::string& savedGuitar, const std::string& savedSection, const std::string& clockInId, bool onePedalSection) {
    std::vector<Keyed> all;
    for (auto& d : devices) {
        Keyed k; k.port = d; k.pedal = looksLikePedal (d.name); k.virt = ! k.pedal && looksVirtual (d.name);
        int occ = 0; bool dup = d.id.empty();
        for (auto& e : all) if (! d.id.empty() && e.port.id == d.id) { dup = true; e.dup = true; if (e.occurrence >= occ) occ = e.occurrence + 1; }
        k.occurrence = d.id.empty() ? 0 : occ; k.dup = dup || k.dup;
        if (d.id.empty()) k.key = "idx:" + std::to_string (d.index);
        else if (k.dup) k.key = d.id + "#" + std::to_string (k.occurrence);
        else k.key = d.id;
        all.push_back (std::move (k));
    }
    for (auto& k : all) if (k.dup && ! k.port.id.empty()) k.key = k.port.id + "#" + std::to_string (k.occurrence);
    auto match = [&] (const std::string& saved) {
        if (saved.empty()) return -1;
        for (int i = 0; i < (int) all.size(); ++i) if (all[(size_t) i].key == saved) return i;
        for (int i = 0; i < (int) all.size(); ++i) if (all[(size_t) i].occurrence == 0 && all[(size_t) i].port.id == saved) return i;
        return -1;
    };
    bool anyPedal = false; for (auto& k : all) if (k.pedal) anyPedal = true;
    std::vector<int> cand;
    for (int i = 0; i < (int) all.size(); ++i) {
        auto& k = all[(size_t) i];
        if (! clockInId.empty() && (k.port.id == clockInId || k.key == clockInId)) continue;
        if (anyPedal) { if (k.pedal) cand.push_back (i); }
        else if (! k.virt) cand.push_back (i);
    }
    const auto pool = cand;
    auto isCand = [&] (int i) { for (int c : pool) if (c == i) return true; return false; };
    int g = match (savedGuitar), s = match (savedSection);
    if (g >= 0 && ! isCand (g)) g = -1;
    if (s >= 0 && ! isCand (s)) s = -1;
    if (g >= 0 && g == s) s = -1;
    FootAssignment out; out.detected = (int) cand.size();
    auto take = [&] (int i) { for (auto it = cand.begin(); it != cand.end(); ++it) if (*it == i) { cand.erase (it); break; } };
    if (g >= 0) take (g);
    if (s >= 0) take (s);
    if (out.detected == 1) {
        const int only = cand.empty() ? (g >= 0 ? g : s) : cand[0];
        if (onePedalSection) s = only, g = -1; else g = only, s = -1;
        cand.clear();
    } else {
        if (g < 0 && ! cand.empty()) { g = cand[0]; cand.erase (cand.begin()); }
        if (s < 0 && ! cand.empty()) { s = cand[0]; cand.erase (cand.begin()); }
    }
    auto roleOf = [&] (int i) { FootRole r; if (i < 0 || i >= (int) all.size()) return r; auto& k = all[(size_t) i]; r.id = k.key; r.index = k.port.index; r.connected = true;
        r.name = k.port.name.empty() ? "USB pedal" : k.port.name;
        int seen = 0, nth = 0; for (auto& o : all) if (o.port.name == k.port.name) { ++seen; if (o.port.index == k.port.index) nth = seen; }
        if (seen > 1) r.name += " (" + std::to_string (nth) + ")";
        return r; };
    out.guitar = roleOf (g); out.section = roleOf (s);
    for (int i = 0; i < (int) all.size(); ++i) if (isCand (i) || i == g || i == s) {
        auto r = roleOf (i); if (i == g) r.id = out.guitar.id; if (i == s) r.id = out.section.id; out.pedals.push_back (r);
    }
    return out;
}

void MidiRouter::tick (double t) {
    for (int u = 0; u < nUnits_; ++u) for (int s = 0; s < 4; ++s) {
        auto& h = held_[(size_t) u][(size_t) s];
        if (h.down && ! h.fired && t - h.t >= (units_[(size_t) u].sw[s].longMs > 0 ? units_[(size_t) u].sw[s].longMs : units_[(size_t) u].longPressMs)) { h.fired = true; fire (u, s, true); }
    }
}
} // namespace wr
