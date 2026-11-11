// Bearing Live v0.10 - the Workshop's AMP SECTION (head + cab + mic + output mode, or bypass for a real amp).
// Stored in the rig as a normal PedalDef (type "amp", uid kAmpUid) kept LAST in the before-amp lane, so in two-cable /
// direct use it sits between the lanes exactly where a real amp would. "Using a real amp" = the section is bypassed.
#pragma once
#include "dsp/Pedal.h"
#include "model/Song.h"
#include <string>
#include <vector>

namespace wr {
constexpr const char* kAmpUid = "__amp__";
enum class AmpOutput { DirectPA = 0, FxReturn = 1, Headphones = 2 };
const std::vector<std::string>& ampHeadNames();   // the 6 heads
const std::vector<std::string>& ampCabNames();    // the 5 cabs + "User IR"
const std::vector<std::string>& ampMicNames();    // Dynamic / Ribbon / Condenser
int userIrCabIndex();                             // index of "User IR"

PedalDef* ampSection (RigPreset& r);
const PedalDef* ampSection (const RigPreset& r);
// Adds the section if missing (default: Clean American, 2x12 Open Back, Direct to PA, ON unless realAmp) and moves it to the
// end of lane 0. Returns the section. Idempotent - call after every Workshop structural edit.
PedalDef& ensureAmpSection (RigPreset& r, bool realAmp = false);
void setRealAmp (RigPreset& r, bool realAmp);   // true = sim bypassed (player's own amp does the work)
bool usingRealAmp (const RigPreset& r);
bool setUserIr (RigPreset& r, const std::string& wavPath); // remembers the file + selects "User IR" (false if the WAV can't be read)
std::string ampSummary (const RigPreset& r);    // "British Chime -> 2x12 Open Back - Direct to PA" / "Real amp (sim bypassed)"

// Reads a WAV (16/24/32-bit PCM or 32-bit float, any rate) and loads it into an amp/cab pedal. False on any failure.
bool loadIrFile (Pedal& p, const std::string& wavPath);

// v0.12 cab IR picker support (pure C++, testable). The UI reads the picked file through its security-scoped URL, then:
struct IrCheck { bool ok = false; std::string error; double sampleRate = 0; int samples = 0; double ms = 0; bool truncated = false; };
IrCheck checkIr (const uint8_t* data, size_t size);          // validates a WAV in memory (8 MB max, >= 8 samples, finite, not silent)
std::string safeIrFileName (const std::string& name);        // strips path + odd characters, forces ".wav"
// Validates, then writes the bytes into `dir` (the app container's IRs folder) as a unique file name. Returns the full path
// or empty with `err` set. Never overwrites a different existing IR (adds " 2", " 3", ...); identical bytes reuse the file.
std::string importIr (const uint8_t* data, size_t size, const std::string& originalName, const std::string& dir, std::string& err);
void clearUserIr (RigPreset& r, int fallbackCab = 1);         // back to a built-in cab (default 2x12 Open Back); forgets the file
std::string userIrName (const RigPreset& r);                  // "" when the amp isn't using a user IR
} // namespace wr
