// Ableton Link implementation of BeatClock (header-only; needs the Link SDK on the include path - see CMakeLists
// WR_LINK_DIR / FetchContent). Link is GPLv2+ (or a proprietary licence from Ableton); see STATUS.md "Licensing".
//
// Usage in the app: one LinkClock while a sync session is active (leader or follower). Message thread: beatAt(),
// setTempo(). Audio thread: audioBeat (hostTimeMicros) with captureAudioSessionState (realtime-safe) -> RigEngine::setLinkBeat.
// Quantum = beats per bar, so Link bar lines are every device's bar lines.
#pragma once
#include "Sync.h"
#include <ableton/Link.hpp>
#include <atomic>

namespace wr::sync {

class LinkClock : public BeatClock {
public:
    explicit LinkClock (double bpm = 120) : link_ (bpm) { link_.enable (true); link_.enableStartStopSync (false); }
    ~LinkClock() override { link_.enable (false); }
    void setQuantum (int beatsPerBar) { quantum_.store (std::max (1, beatsPerBar)); }
    double quantum() const { return (double) quantum_.load(); }
    // BeatClock (message thread). nowSec is ignored: Link uses its own monotonic clock so every peer agrees.
    double beatAt (double) const override { auto st = link_.captureAppSessionState(); return st.beatAtTime (link_.clock().micros(), quantum()); }
    double tempo() const override { return link_.captureAppSessionState().tempo(); }
    void setTempo (double bpm, double) override { auto st = link_.captureAppSessionState(); st.setTempo (bpm, link_.clock().micros()); link_.commitAppSessionState (st); }
    int peers() const override { return (int) link_.numPeers(); }
    // audio thread: Link beat at a host time (micros of the first output sample of the block, incl. output latency)
    double audioBeat (std::chrono::microseconds hostTime) const { auto st = link_.captureAudioSessionState(); return st.beatAtTime (hostTime, quantum()); }
    double audioTempo() const { return link_.captureAudioSessionState().tempo(); }
    std::chrono::microseconds now() const { return link_.clock().micros(); }
    // leader joining an existing Link session at a song start: line our bar up with Link's (no tempo jump)
    void alignBar (double beat) { auto st = link_.captureAppSessionState(); st.requestBeatAtTime (beat, link_.clock().micros(), quantum()); link_.commitAppSessionState (st); }
private:
    mutable ableton::Link link_; std::atomic<int> quantum_ { 4 };
};

} // namespace wr::sync
