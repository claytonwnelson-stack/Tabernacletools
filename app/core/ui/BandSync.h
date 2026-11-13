// App glue for band sync (v0.5): owns the TCP server/client, Bonjour discovery, the Leader/Follower protocol objects
// and the Ableton Link clock. Message thread: poll() from the processor timer. Audio thread: audioBlock().
#pragma once
#include "model/LiveSession.h"
#include "sync/Sync.h"
#include "sync/Net.h"
#include "sync/Discovery.h"
#include "sync/Roster.h"
#include "audio/ClickEngine.h"
#include <atomic>
#include <memory>

namespace wr::sync { class LinkClock; }

class BandSync {
public:
    enum class Mode { Off, Leading, Following };
    BandSync (wr::LiveSession& live, wr::Library& lib);
    ~BandSync();
    void lead (const std::string& deviceName);                       // advertise + accept followers
    void join (const std::string& host, int port, const std::string& deviceName); // one tap from the browse list (or IP)
    void stop();
    void browse (bool on);                                           // follower list ("Join...")
    std::vector<wr::sync::Peer> leaders() const { return disco_.peers(); }
    void poll (double nowSec);
    bool interceptAction (const wr::Action& a, double nowSec);       // true = handled (PLAY while leading is bar-quantised)
    void requestPlay (bool on, double nowSec);                       // on-screen PLAY
    void setLocalKey (const std::string& songId, const std::string& key, int capo) { follower_.localKey[songId] = { key, capo }; }
    Mode mode() const { return mode_; }
    bool linked() const { return linkActive_.load(); }               // click grid follows Link (restartBeat ignored)
    int followers() const { return server_.clients(); }
    std::vector<std::string> followerNames() const { return server_.clientNames(); }
    int linkPeers() const;
    // leader: follower roster (BAND n indicator + panel), join/drop toasts, kick
    const wr::sync::Roster& roster() const { return roster_; }
    std::vector<wr::sync::Toast> takeToasts() { return roster_.takeToasts(); }
    void kick (int followerId, double nowSec) { server_.kick (followerId); roster_.kicked (followerId, nowSec); }
    void setRole (const std::string& r) { follower_.role = r; }
    void setHealth (const wr::sync::DeviceHealth& h) { follower_.health = h; }   // v0.7: sent in heartbeats while following
    std::string banner (double nowSec) const;                        // follower: "LEADER LOST..." / "Joining..."
    std::string statusLine() const;
    std::string clockStatus() const;
    bool takeLibrarySave() { bool d = saveLib_; saveLib_ = false; return d; }
    std::string sessionId() const;
    std::string activeLeaderName() const;
    bool canTakeLead() const;
    void takeLead (double nowSec);                               // co-leader: one tap. Asks while the leader is up; takes over if they have dropped
    void markCoLeader (int followerId);                          // leader: toggle that connected member
    void requestLeaderAudio();
    float leaderAudioProgress() const { return follower_.audioProgress(); }
    bool useLink = true;
    // v0.14 backing tracks: leader publishes its transport; followers read the leader's
    void setTracksTransport (const wr::mt::Transport& t) { if (leader_) leader_->setTransport (t); }
    wr::mt::Transport leaderTracks() const { return follower_.transport; }
    void setFollowLeaderKey (bool on) { follower_.followLeaderKey = on; }
    // audio thread, before the click engine runs: phase-lock the click to Link (outputLatencySec = device output latency)
    void audioBlock (wr::ClickEngine& rig, double outputLatencySec, int beatsPerBar);
private:
    wr::sync::BeatClock& clock();
    wr::LiveSession& live_; wr::Library& lib_; Mode mode_ = Mode::Off;
    wr::sync::Server server_; wr::sync::Client client_; wr::sync::Discovery disco_;
    std::unique_ptr<wr::sync::Leader> leader_; wr::sync::Follower follower_;
    wr::sync::FakeClock localClock_;                                  // no Link: beats from local time at the song tempo
    std::unique_ptr<wr::sync::LinkClock> link_; std::atomic<wr::sync::LinkClock*> linkRt_ { nullptr }; std::atomic<bool> linkActive_ { false };
    void showChartPrompt();
    void showRigPrompt (wr::sync::ChartImportChoice choice, bool applyAll);
    void showSetlistPrompt();
    void showTakeLeadPrompt();
    void sendLeaderAudio (int followerId, const std::string& songId);
    void installSharedAudio();
    void assumeFrom (const wr::sync::Handoff& h, double now);
    void followHandoff (const std::string& host, int port, double beat, double bpm, double now);
    void rejoin (const std::string& host, int port);
    void maybeSongHandoff (double now);
    std::string host_, deviceName_; int port_ = 0, pendingSong_ = -1; wr::sync::Roster roster_;
    bool chartPromptOpen_ = false, setlistPromptOpen_ = false, takePromptOpen_ = false, versionShown_ = false, saveLib_ = false;
};
