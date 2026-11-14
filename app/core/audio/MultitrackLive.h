// v0.14 glue between the backing-track player and the live session (message thread, no JUCE; tested on Linux).
//  * loads the live song's stems (opener = how to open a stem file: WavFileSource in the app, memory in tests)
//  * PLAY: stems start after the song's count-in (same click count-in, 8 clicks by default), stop with the transport
//  * stems drive the chart: when the playhead enters an arranged section the LiveSession lands there (so the leader's
//    existing "section" events carry it to every follower's chart, highlights and per-section patches)
//  * footswitch / UI section commands become quantised stem jumps (next bar line or section end) with the usual spoken
//    1-bar cue on the click bus; "loop" vamps the current section until pressed again
//  * arrangement edits re-sequence the stems (crossfaded) without stopping
//  * locked (subscription lapsed) = nothing loads or plays, matching the read-only locks
#pragma once
#include "MultitrackPlayer.h"
#include "../model/LiveSession.h"
#include "../midi/MidiRouter.h"
#include <functional>

namespace wr::mt {

using Opener = std::function<std::unique_ptr<StemSource> (const std::string& path)>;

class MultitrackLive {
public:
    explicit MultitrackLive (MultitrackPlayer& p) : p_ (p) {}
    // (re)load the live song's stems. Returns "" or a short status. sync: tests (no reader thread).
    std::string loadSong (const Song* librarySong, const LiveSession& live, const Opener& open, bool allowed, bool sync = false);
    void unload() { p_.unload(); songId_.clear(); warning_.clear(); }
    void reloadNeeded() { songId_.clear(); }
    const std::string& songId() const { return songId_; }
    bool active() const { return p_.loaded(); }
    const std::string& sequenceWarning() const { return warning_; }
    void arrangementChanged (const Song* librarySong, const LiveSession& live);  // re-sequence (keeps playing)
    // a footswitch / UI action. Returns true when the stems handled it (the caller must not pass it on).
    // Play / TracksPlay are never consumed: the caller maps TracksPlay to Play and runs its usual PLAY path.
    bool handle (const Action& a, LiveSession& live, double now, bool allowed);
    void onCountIn (int beats, double bpm) { if (! active()) return; p_.play (beats > 0 && bpm > 0 ? beats * 60.0 / bpm : 0.0); started_ = true; } // RigSink::countIn hook
    void tick (LiveSession& live, double now);
    Transport transport() const { return p_.transport(); }
    Load load (double cpu) const { Load l; l.underruns = p_.underruns(); l.cpu = cpu; l.stems = stemsWanted_; l.rateMismatch = p_.rateMismatch(); return l; }
private:
    MultitrackPlayer& p_; std::string songId_, warning_; int stemsWanted_ = 0;
    bool wasPlaying_ = false, started_ = false; int lastChanges_ = -1;
};

} // namespace wr::mt
