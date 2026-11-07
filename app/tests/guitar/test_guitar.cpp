// Guitar app unit tests: rig build from presets, scenes, every pedal in the catalog.
#include "model/Backup.h"
#include "audio/Voice.h"
#include "model/Export.h"
#include <filesystem>
#include "model/Chart.h"
#include "model/LiveSession.h"
#include "dsp/RigBuilder.h"
#include <cstdio>
#include <fstream>
#include <sstream>
using namespace wr;

static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (! (c)) { ++fails; std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_EQ(a, b) do { ++checks; auto _a = (a); auto _b = (b); if (! (_a == _b)) { ++fails; std::printf ("FAIL %s:%d  %s == %s\n   got [%s] want [%s]\n", __FILE__, __LINE__, #a, #b, toS (_a).c_str(), toS (_b).c_str()); } } while (0)
static std::string toS (const std::string& s) { return s; } static std::string toS (const char* s) { return s; } template <class T> static std::string toS (T v) { return std::to_string (v); }

static void testRigBuild() {
    auto r = defaultRig(); std::vector<std::string> w; auto chain = buildChain (r, &w); CHECK (w.empty()); for (auto& x : w) std::printf ("  warn: %s\n", x.c_str());
    CHECK_EQ ((int) chain->lanes[0].size(), 5); CHECK_EQ ((int) chain->lanes[1].size(), 7);
    Scene sc = seedLibrary().songs[0].scenes[2]; auto cmds = sceneCommands (*chain, sc); CHECK (cmds.size() >= 3);
    // every catalog pedal can be created, prepared and processed without NaN
    for (auto& info : pedalCatalog()) {
        auto p = createPedal (info.type); CHECK (p != nullptr); if (! p) continue; p->prepare (48000, 128); p->setEnabled (true);
        float L[128], R[128]; ProcessContext ctx; bool fin = true;
        for (int b = 0; b < 50; ++b) { for (int i = 0; i < 128; ++i) L[i] = R[i] = 0.3f * std::sin ((b * 128 + i) * 0.05f); p->process (L, R, 128, ctx); for (int i = 0; i < 128; ++i) fin = fin && std::isfinite (L[i]) && std::isfinite (R[i]); }
        if (! fin) std::printf ("  non-finite: %s\n", info.type.c_str()); CHECK (fin);
    }
}

int main() { setvbuf (stdout, nullptr, _IONBF, 0);
    testRigBuild();
    std::printf ("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
