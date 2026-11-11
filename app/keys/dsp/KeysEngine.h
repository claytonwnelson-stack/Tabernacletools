// Bearing Live Keys v0.9 STUB - design target in docs/SOUND_LIBRARY.md. Wraps the existing wk synth engine
// (tonewheel/pipe organ, pads, grand/EP piano, delay, FDN reverb) with patch presets + a key-matched worship pad drone.
#pragma once
#include "../../guitar/third_party/wk/Engine.h"
#include <string>
#include <vector>

namespace wr::keys {
struct KeysPatch { std::string id, name; wk::Params params; };
const std::vector<KeysPatch>& starterPatches();
const KeysPatch* findPatch (const std::string& id);
int keyToPitchClass (const std::string& key); // "B" -> 11, "Eb" -> 3, "F#m" -> 6; -1 if unknown

class KeysEngine {
public:
    void prepare (double sr) { fs_ = sr; eng_.prepare (sr); eng_.setParams (eng_.p); }
    bool setPatch (const std::string& id);
    // Key-matched pad: holds root + fifth + octave of the song key (drone), re-voices smoothly on key change.
    void setPadKey (const std::string& songKey, bool on);
    int padRoot() const { return padRoot_; }
    void noteOn (int n, float v) { eng_.noteOn (n, v); } void noteOff (int n) { eng_.noteOff (n); }
    void render (float* L, float* R, int n) { eng_.render (L, R, n); }
    const std::string& patch() const { return patch_; }
private:
    wk::Engine eng_; double fs_ = 48000; std::string patch_; int padRoot_ = -1; std::vector<int> held_;
};
} // namespace wr::keys
