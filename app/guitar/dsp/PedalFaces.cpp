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
        { "cab",        0xff505050, { "cab", "mic", "micpos" }, "Cab sim or your own IR, for a real amp's line out.", "-" },
    };
    return v;
}
const PedalFace* pedalFace (const std::string& t) { for (auto& f : pedalFaces()) if (f.type == t) return &f; return nullptr; }
} // namespace wr
