#include "dsp/PedalFaces.h"

namespace wr {
const std::vector<PedalFace>& pedalFaces() {
    static const std::vector<PedalFace> v = {
        { "gefuzz",     0xff8a5a2b, { "attack", "level" }, "Two-knob germanium fuzz. Roll your guitar volume back to clean up.", "On/Off" },
        { "tristage",   0xff6d6f74, { "sustain", "tone", "volume" }, "Thick, singing sustain fuzz with a scooped tone knob. Great for big bridges.", "On/Off" },
        { "treblebst",  0xffb0a060, { "boost", "range" }, "Pushes the amp with extra top end. Put it first, into a slightly dirty amp.", "On/Off" },
        { "bluesdrive", 0xff2f5fa8, { "gain", "tone", "level" }, "Touch-sensitive low-to-mid gain drive for verses.", "On/Off" },
        { "ampinbox",   0xff3c3c3c, { "gain", "tone", "volume" }, "Flat, transparent drive that just sounds like a louder amp.", "On/Off" },
        { "octfuzz",    0xffc04a2a, { "fuzz", "volume" }, "Octave-up fuzz. Best above the 12th fret, neck pickup.", "On/Off" },
        { "otacomp",    0xffd23a3a, { "sensitivity", "output" }, "Classic squash compressor for clean funk and chime.", "On/Off" },
        { "gate",       0xff404040, { "threshold", "release" }, "Silences hiss between phrases. Put it before the drives.", "Always on" },
        { "flanger",    0xff7a4fa0, { "rate", "depth", "regen", "mix" }, "Jet-sweep flanger. Slow rate and low mix for pads.", "On/Off" },
        { "vibrato",    0xff4f8aa0, { "rate", "depth" }, "True pitch vibrato. Tiny depth makes a lush 'tape' wobble.", "On/Off" },
        { "wah",        0xff222222, { "position", "mode", "q", "mix" }, "Classic wah. Use an expression pedal, or the Auto and Envelope modes.", "Expression" },
        { "envfilter",  0xff3fa070, { "sens", "peak", "mode" }, "Quacky envelope filter. Dig in harder for more sweep.", "On/Off" },
        { "analogdly",  0xff3a6fbf, { "time", "feedback", "mix" }, "Warm, dark repeats that sit behind the vocal. Tap or sync it to the song.", "On/Off (spillover)" },
        { "tapeecho",   0xff5a7a3a, { "time", "intensity", "mix", "wear" }, "Wobbly tape echo. Turn Intensity past noon for runaway swells.", "On/Off (spillover)" },
        { "dotted8",    0xff2f9fbf, { "feedback", "mix", "quarter", "duck" }, "Worship's dotted-eighth delay, locked to the song BPM with no tapping.", "On/Off (spillover)" },
        { "cloud",      0xff9fc7ff, { "mix", "decay", "shimmer", "swell" }, "Huge ambient wash with an auto-swell. Your pad under prayer.", "On/Off (spillover)" },
        { "spring",     0xff8f8f6a, { "dwell", "mix", "tone" }, "Drippy spring tank, like a vintage combo amp.", "On/Off (spillover)" },
        { "hall",       0xff3d6ea8, { "mix", "decay", "predelay", "tone" }, "Medium hall. The everyday worship reverb.", "On/Off (spillover)" },
        { "room",       0xff3f8f62, { "mix", "decay", "predelay", "tone" }, "Small room. Tight, just off the guitar.", "On/Off (spillover)" },
        { "plate",      0xffc4a15a, { "mix", "decay", "predelay", "tone" }, "Bright plate. Smooth vocal-style sheen on leads.", "On/Off (spillover)" },
        { "chamber",    0xff8d5a78, { "mix", "decay", "predelay", "tone" }, "Close chamber. Shorter than a hall, wider than a room.", "On/Off (spillover)" },
        { "church",     0xff6a5ac8, { "mix", "decay", "predelay", "tone" }, "Long church wash. Keep the mix low so the vocal stays in front.", "On/Off (spillover)" },
        { "reverse",    0xffc46b3a, { "mix", "time", "tone" }, "Backwards swell into the note. Time is the reverse window.", "On/Off (spillover)" },
        { "gated",      0xffc4475c, { "mix", "hold", "tone" }, "A short burst that slams shut. Hold is how long the gate stays open.", "On/Off (spillover)" },
        { "modverb",    0xff2f9aa8, { "mix", "decay", "depth", "tone" }, "Hall with a slow chorus in the tank.", "On/Off (spillover)" },
        { "bloom",      0xffd07ab0, { "mix", "decay", "swell", "tone" }, "Notes open into the reverb instead of splashing on the attack.", "On/Off (spillover)" },
        { "cab",        0xff505050, { "cab", "mic", "micpos" }, "Cab sim or your own IR, for a real amp's line out.", "-" },
    };
    return v;
}
const PedalFace* pedalFace (const std::string& t) { for (auto& f : pedalFaces()) if (f.type == t) return &f; return nullptr; }

PedalLook pedalLook (const std::string& type) {
    struct Row { const char* type; uint32_t argb; const char* tag; };
    static const Row rows[] = {
        { "comp", 0xffd23a3a, "CMP" }, { "boost", 0xffe07a2f, "BST" }, { "overdrive", 0xffe0a030, "OD" }, { "klon", 0xffd4b24a, "KLN" },
        { "distortion", 0xffc4512a, "DST" }, { "fuzz", 0xff9a3d22, "FZ" }, { "octdown", 0xff7a4b2a, "OCT" }, { "polyoct", 0xffb06a3a, "PO" },
        { "harmonizer", 0xffc47a8a, "HRM" }, { "key9", 0xff5a7ad4, "K9" }, { "synth9", 0xff3a6ad0, "S9" },
        { "chorus", 0xff3a8fd4, "CHO" }, { "univibe", 0xff6a5a9a, "VIB" }, { "rotary", 0xff4a7a6a, "ROT" }, { "tremolo", 0xff3a9a7a, "TR" },
        { "phaser", 0xff7a4fa0, "PH" }, { "delay", 0xff3a6fbf, "DLY" }, { "reverb", 0xff4a78b0, "REV" }, { "shimmer", 0xff8eb4ee, "SHM" },
        { "freeze", 0xff6a8aaa, "FRZ" }, { "looper", 0xff5a6a7a, "LP" }, { "eq", 0xff8a93a6, "EQ" }, { "swell", 0xffc080a0, "SW" },
        { "amp", 0xfff5a524, "AMP" }, { "gefuzz", 0xff8a5a2b, "GF" }, { "tristage", 0xff6d6f74, "3FZ" }, { "treblebst", 0xffb0a060, "TB" },
        { "bluesdrive", 0xff2f5fa8, "BD" }, { "ampinbox", 0xff3c3c3c, "AIB" }, { "octfuzz", 0xffc04a2a, "OF" }, { "otacomp", 0xffd23a3a, "OC" },
        { "gate", 0xff404040, "GT" }, { "flanger", 0xff7a4fa0, "FL" }, { "vibrato", 0xff4f8aa0, "VB" }, { "wah", 0xff222222, "WAH" },
        { "envfilter", 0xff3fa070, "ENV" }, { "analogdly", 0xff3a6fbf, "AD" }, { "tapeecho", 0xff5a7a3a, "TP" }, { "dotted8", 0xff2f9fbf, "D8" },
        { "cloud", 0xff7eb0e8, "CLD" }, { "spring", 0xff8f8f6a, "SPR" }, { "cab", 0xff505050, "CAB" },
        { "hall", 0xff3d6ea8, "HAL" }, { "room", 0xff3f8f62, "RM" }, { "plate", 0xffc4a15a, "PLT" }, { "chamber", 0xff8d5a78, "CHM" },
        { "church", 0xff6a5ac8, "CHR" }, { "reverse", 0xffc46b3a, "RVS" }, { "gated", 0xffc4475c, "GAT" }, { "modverb", 0xff2f9aa8, "MOD" },
        { "bloom", 0xffd07ab0, "BLM" },
    };
    if (auto* f = pedalFace (type)) {
        const char* tag = "FX";
        for (auto& r : rows) if (type == r.type) { tag = r.tag; break; }
        return { f->argb, tag };
    }
    for (auto& r : rows) if (type == r.type) return { r.argb, r.tag };
    return { 0xff6b7489, "FX" };
}
} // namespace wr
