// Builds an audio Chain from a RigPreset and turns Scenes into rig Commands.
#pragma once
#include "Rig.h"
#include "model/Song.h"
#include "AmpSection.h"

namespace wr {
inline std::unique_ptr<Chain> buildChain (const RigPreset& r, std::vector<std::string>* warnings = nullptr) {
    auto c = std::make_unique<Chain>();
    for (int l = 0; l < 2; ++l) for (auto& d : r.lanes[l]) {
        auto p = createPedal (d.type); if (! p) { if (warnings) warnings->push_back ("Unknown pedal type: " + d.type); continue; }
        for (auto& kv : d.params) if (! p->setParam (kv.first, kv.second) && warnings) warnings->push_back (d.uid + ": unknown param " + kv.first);
        if (! d.file.empty() && ! loadIrFile (*p, d.file) && warnings) warnings->push_back (d.uid + ": could not load IR " + d.file);
        p->setEnabled (d.enabled); c->lanes[l].push_back ({ d.uid, std::move (p) });
    }
    return c;
}
inline std::vector<Command> sceneCommands (const Chain& c, const Scene& s) {
    std::vector<Command> out;
    for (auto& kv : s.pedals) {
        auto* slot = const_cast<Chain&> (c).find (kv.first); if (! slot) continue;
        for (auto& pv : kv.second.params) { bool ok = false; auto cm = cmdParam (c, kv.first, pv.first, pv.second, &ok); if (ok) out.push_back (cm); }
        out.push_back (cmdEnable (c, kv.first, kv.second.enabled));
    }
    return out;
}
} // namespace wr
