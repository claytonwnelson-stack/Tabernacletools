#include <filesystem>
#include <cmath>
#include <cctype>
#include <cstring>
#include "dsp/AmpSection.h"
#include "audio/Voice.h"
#include <fstream>
#include <iterator>
#include <algorithm>

namespace wr {
static std::vector<std::string> choicesOf (const char* type, const char* id) {
    auto p = createPedal (type); int i = p ? p->paramIndex (id) : -1; return i >= 0 ? p->specs()[(size_t) i].choices : std::vector<std::string>();
}
const std::vector<std::string>& ampHeadNames() { static auto v = choicesOf ("amp", "model"); return v; }
const std::vector<std::string>& ampCabNames() { static auto v = choicesOf ("amp", "cab"); return v; }
const std::vector<std::string>& ampMicNames() { static auto v = choicesOf ("amp", "mic"); return v; }
int userIrCabIndex() { return (int) ampCabNames().size() - 1; }

PedalDef* ampSection (RigPreset& r) { for (auto& l : r.lanes) for (auto& d : l) if (d.uid == kAmpUid) return &d; return nullptr; }
const PedalDef* ampSection (const RigPreset& r) { return ampSection (const_cast<RigPreset&> (r)); }
PedalDef& ensureAmpSection (RigPreset& r, bool realAmp) {
    PedalDef d; bool had = false;
    for (auto& l : r.lanes) for (auto it = l.begin(); it != l.end(); ++it) if (it->uid == kAmpUid) { d = *it; l.erase (it); had = true; break; }
    if (! had) { d.uid = kAmpUid; d.type = "amp"; d.enabled = ! realAmp; d.params = { { "model", 0 }, { "cab", 1 }, { "output", 0 } }; }
    r.lanes[0].push_back (d); return r.lanes[0].back();
}
void setRealAmp (RigPreset& r, bool realAmp) { ensureAmpSection (r).enabled = ! realAmp; }
bool usingRealAmp (const RigPreset& r) { auto* a = ampSection (r); return ! a || ! a->enabled; }
bool setUserIr (RigPreset& r, const std::string& path) {
    auto probe = createPedal ("amp"); probe->prepare (48000, 64); if (! loadIrFile (*probe, path)) return false;
    auto& a = ensureAmpSection (r); a.file = path; a.params["cab"] = (float) userIrCabIndex(); return true;
}
std::string ampSummary (const RigPreset& r) {
    auto* a = ampSection (r); if (! a || ! a->enabled) return "Real amp (sim bypassed)";
    auto get = [&] (const char* k, float def) { auto it = a->params.find (k); return it == a->params.end() ? def : it->second; };
    auto pick = [] (const std::vector<std::string>& v, float x) { int i = std::clamp ((int) std::lround (x), 0, (int) v.size() - 1); return v.empty() ? std::string() : v[(size_t) i]; };
    static const char* outs[] = { "Direct to PA", "Amp FX Return", "Headphones" };
    int o = std::clamp ((int) std::lround (get ("output", 0)), 0, 2);
    std::string cab = o == 1 ? std::string ("no cab") : pick (ampCabNames(), get ("cab", 1));
    if (o != 1 && (int) std::lround (get ("cab", 1)) == userIrCabIndex() && ! a->file.empty()) { auto f = a->file.substr (a->file.find_last_of ("/\\") + 1); cab = "IR " + f; }
    return pick (ampHeadNames(), get ("model", 0)) + " -> " + cab + " - " + outs[o];
}
bool loadIrFile (Pedal& p, const std::string& path) {
    std::ifstream f (path, std::ios::binary); if (! f) return false;
    std::vector<uint8_t> bytes ((std::istreambuf_iterator<char> (f)), std::istreambuf_iterator<char>());
    if (bytes.size() > 8u * 1024 * 1024) return false; // IRs are small; refuse anything silly
    std::vector<float> mono; double sr = 0; if (! decodeWav (bytes.data(), bytes.size(), mono, sr)) return false;
    return loadCabImpulse (p, mono.data(), (int) mono.size(), sr);
}
IrCheck checkIr (const uint8_t* data, size_t size) {
    IrCheck c; if (! data || size < 44) { c.error = "Not a WAV file"; return c; }
    if (size > 8u * 1024 * 1024) { c.error = "File is larger than 8 MB - cab IRs are usually under 1 MB"; return c; }
    if (std::memcmp (data, "RIFF", 4) != 0 || std::memcmp (data + 8, "WAVE", 4) != 0) { c.error = "Not a WAV file (use .wav, not .aif/.mp3)"; return c; }
    std::vector<float> mono; double sr = 0; if (! decodeWav (data, size, mono, sr)) { c.error = "Unsupported WAV format (use 16/24-bit PCM or 32-bit float)"; return c; }
    if (mono.size() < 8 || sr < 8000 || sr > 384000) { c.error = "Too short or odd sample rate"; return c; }
    double e = 0; for (float x : mono) { if (! std::isfinite (x)) { c.error = "File contains invalid samples"; return c; } e += (double) x * x; }
    if (e < 1e-12) { c.error = "The IR is silent"; return c; }
    c.ok = true; c.sampleRate = sr; c.samples = (int) mono.size(); c.ms = 1000.0 * (double) mono.size() / sr;
    c.truncated = (double) mono.size() * 48000.0 / sr > 1024.0;   // CabSection::kMaxIR at 48 kHz (~21 ms) - the tail is faded
    return c;
}
std::string safeIrFileName (const std::string& name) {
    std::string b = name.substr (name.find_last_of ("/\\") == std::string::npos ? 0 : name.find_last_of ("/\\") + 1), o;
    for (char ch : b) o += (std::isalnum ((unsigned char) ch) || ch == ' ' || ch == '-' || ch == '_' || ch == '.' || ch == '(' || ch == ')') ? ch : '_';
    while (! o.empty() && (o.front() == '.' || o.front() == ' ')) o.erase (o.begin());
    auto dot = o.find_last_of ('.'); std::string stem = dot == std::string::npos ? o : o.substr (0, dot);
    if (stem.empty()) stem = "IR"; if (stem.size() > 60) stem.resize (60);
    return stem + ".wav";
}
static bool readAll (const std::string& p, std::vector<uint8_t>& out) { std::ifstream f (p, std::ios::binary); if (! f) return false; out.assign ((std::istreambuf_iterator<char> (f)), std::istreambuf_iterator<char>()); return true; }
std::string importIr (const uint8_t* data, size_t size, const std::string& originalName, const std::string& dir, std::string& err) {
    auto c = checkIr (data, size); if (! c.ok) { err = c.error; return {}; }
    { std::error_code ec; std::filesystem::create_directories (dir, ec); }   // first import: <data>/IRs may not exist yet
    const std::string name = safeIrFileName (originalName), stem = name.substr (0, name.size() - 4);
    for (int i = 1; i < 1000; ++i) {
        const std::string path = dir + "/" + (i == 1 ? stem : stem + " " + std::to_string (i)) + ".wav";
        std::vector<uint8_t> existing;
        if (readAll (path, existing)) { if (existing.size() == size && std::memcmp (existing.data(), data, size) == 0) return path; continue; }
        std::ofstream o (path, std::ios::binary); if (! o) { err = "Couldn't save into the app (storage full?)"; return {}; }
        o.write ((const char*) data, (std::streamsize) size); if (! o) { err = "Couldn't save into the app (storage full?)"; return {}; }
        return path;
    }
    err = "Too many IRs with that name"; return {};
}
void clearUserIr (RigPreset& r, int fallbackCab) { auto& a = ensureAmpSection (r, usingRealAmp (r)); a.file.clear(); a.params["cab"] = (float) std::clamp (fallbackCab, 0, userIrCabIndex() - 1); }
std::string userIrName (const RigPreset& r) {
    auto* a = ampSection (r); if (! a || a->file.empty()) return {};
    auto it = a->params.find ("cab"); if (it == a->params.end() || (int) std::lround (it->second) != userIrCabIndex()) return {};
    return a->file.substr (a->file.find_last_of ("/\\") + 1);
}
} // namespace wr
