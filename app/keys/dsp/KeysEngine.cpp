#include "KeysEngine.h"
#include <cctype>

namespace wr::keys {
static wk::Params base() { wk::Params p; p.organOn = p.padOn = p.pianoOn = false; return p; }
const std::vector<KeysPatch>& starterPatches() {
    static const std::vector<KeysPatch> v = [] {
        std::vector<KeysPatch> o;
        { auto p = base(); p.pianoOn = true; p.pianoType = 0; p.revMix = 0.2f; o.push_back ({ "keys:Grand", "Grand", p }); }
        { auto p = base(); p.pianoOn = true; p.pianoType = 1; p.epTrem = 0.3f; o.push_back ({ "keys:Tine EP", "Tine EP", p }); }
        { auto p = base(); p.organOn = true; p.organType = 0; p.rotaryOn = true; o.push_back ({ "keys:Tonewheel", "Tonewheel Organ", p }); }
        { auto p = base(); p.padOn = true; p.padAttack = 1.5f; p.revMix = 0.4f; o.push_back ({ "keys:Warm Pad", "Warm Pad", p }); }
        { auto p = base(); p.padOn = true; p.padAttack = 3.0f; p.padRelease = 6.0f; p.padShimmer = 0.6f; p.revMix = 0.6f; p.revDecay = 10; p.revShimmer = 0.4f; o.push_back ({ "keys:Key Pad", "Key-Matched Pad", p }); }
        return o; }();
    return v;
}
const KeysPatch* findPatch (const std::string& id) { for (auto& p : starterPatches()) if (p.id == id) return &p; return nullptr; }
int keyToPitchClass (const std::string& k) {
    if (k.empty()) return -1; static const int pc[] = { 9, 11, 0, 2, 4, 5, 7 }; char c = (char) std::toupper ((unsigned char) k[0]); if (c < 'A' || c > 'G') return -1;
    int v = pc[c - 'A']; if (k.size() > 1) { if (k[1] == '#') ++v; else if (k[1] == 'b') --v; } return (v + 12) % 12;
}
bool KeysEngine::setPatch (const std::string& id) { auto* p = findPatch (id); if (! p) return false; eng_.allNotesOff(); held_.clear(); eng_.setParams (p->params); patch_ = id; padRoot_ = -1; return true; }
void KeysEngine::setPadKey (const std::string& key, bool on) {
    for (int n : held_) eng_.noteOff (n); held_.clear(); padRoot_ = -1;
    int pc = keyToPitchClass (key); if (! on || pc < 0) return;
    if (patch_ != "keys:Key Pad") setPatch ("keys:Key Pad");
    padRoot_ = pc; int root = 48 + pc; for (int n : { root, root + 7, root + 12 }) { eng_.noteOn (n, 0.6f); held_.push_back (n); }
}
} // namespace wr::keys
