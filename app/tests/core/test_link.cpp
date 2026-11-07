// Ableton Link check (built only with WR_LINK_DIR): two Link peers in one process over the loopback/LAN interface
// discover each other, share tempo, and agree on the beat/bar phase that the click grid locks to.
#include "audio/Click.h"
#include "sync/LinkClock.h"   // after the DSP headers: asio pulls in <termios.h>, whose B0/B1 macros clash with filter coefficient names
#include <thread>
#include <cstdio>
using namespace wr::sync;
int main() {
    int fails = 0; auto check = [&] (bool c, const char* m) { std::printf ("%s %s\n", c ? "ok  " : "FAIL", m); if (! c) ++fails; };
    LinkClock leader (120), follower (99); leader.setQuantum (4); follower.setQuantum (4);
    for (int i = 0; i < 100 && (leader.peers() < 1 || follower.peers() < 1); ++i) std::this_thread::sleep_for (std::chrono::milliseconds (50));
    check (leader.peers() >= 1 && follower.peers() >= 1, "Link peers discover each other");
    leader.setTempo (73, 0);
    for (int i = 0; i < 100 && std::abs (follower.tempo() - 73) > 0.01; ++i) std::this_thread::sleep_for (std::chrono::milliseconds (20));
    char m[160]; std::snprintf (m, sizeof m, "leader tempo reaches the follower (%.2f bpm)", follower.tempo()); check (std::abs (follower.tempo() - 73) < 0.01, m);
    std::this_thread::sleep_for (std::chrono::milliseconds (300));
    auto t = leader.now(); double a = leader.audioBeat (t), b = follower.audioBeat (t);
    double pa = std::fmod (a, 4.0), pb = std::fmod (b, 4.0), d = std::abs (pa - pb); d = std::min (d, 4.0 - d);
    std::snprintf (m, sizeof m, "same bar phase on both peers (%.4f vs %.4f beats)", pa, pb); check (d < 0.01, m);
    // two click grids locked to the two peers land on the same beat position
    wr::ClickGen ga, gb; ga.prepare (48000); gb.prepare (48000); ga.lockTo (a, 4); gb.lockTo (b, 4);
    check (std::abs (ga.position() - gb.position()) < 0.01 || std::abs (std::abs (ga.position() - gb.position()) - 4.0) < 0.01, "click grids locked to Link agree");
    std::printf ("%s\n", fails ? "LINK TESTS FAILED" : "all link tests passed"); return fails ? 1 : 0;
}
