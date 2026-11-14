// Leader-side follower roster (v0.5): who is connected, their role, link quality and whether they are in sync.
// Fed by follower heartbeats (Follower sends {"t":"hb"} every second) and the leader's ping/pong round trips.
//   hb: {"t":"hb","name":"Keys iPad","role":"player"|"projector","hash":"<stateHash>","last":<seq>,"beat":<Link beat>,"section":"Chorus","rtt":<ms, follower view>}
// Status: Green  = connected, heartbeat fresh, same state hash as the leader, Link beat within 30 ms, no missing events
//         Yellow = lagging / out of sync for > 1.5 s (state hash differs, seq behind, beat off, or heartbeat late > 2.5 s)
//         Red    = dropped (shown for 30 s, then removed); heartbeats silent 6 s = dropped
#pragma once
#include "Sync.h"
#include <map>

namespace wr::sync {

std::string stateHash (const LiveSession& live);   // song id | section | pass | queued cue (not key/capo/click - those are per player)

struct Member {
    enum Status { Green, Yellow, Red } status = Yellow;
    int id = 0; std::string name = "?", role = "player", section, reason;   // reason: why yellow/red ("state differs", "beat +45 ms", ...)
    double rttMs = -1, beatErrMs = 0, joinedAt = 0, lastHb = -1, droppedAt = -1, badSince = -1; bool connected = true, kicked = false;
    std::string hash; long long lastSeq = -1;
    DeviceHealth health; int healthSev = 0;                                 // v0.7 battery / thermal from heartbeats (0 ok, 1 warn, 2 critical)
    std::string warning() const { return healthWarning (health); }
};
struct Toast { std::string text; int severity = 0; };   // 0 info (joined), 1 warn (dropped)

class Roster {
public:
    void connected (int id, double now);
    void disconnected (int id, double now);
    void kicked (int id, double now);
    void setRole (int id, const std::string& role);
    void pingSent (long long pingId, double now) { pings_[pingId] = now; if (pings_.size() > 64) pings_.erase (pings_.begin()); }
    // a follower line (hb / pong / hello). leaderHash/leaderSeq/leaderBeat: leader state right now.
    void line (int id, const Msg& m, const std::string& leaderHash, long long leaderSeq, double leaderBeat, double bpm, double now);
    void tick (const std::string& leaderHash, long long leaderSeq, double now);   // timeouts, yellow/red, expiry
    std::vector<Member> members() const;           // connected first (by join time), then recently dropped
    int connectedCount() const;
    std::vector<Toast> takeToasts() { auto t = std::move (toasts_); toasts_.clear(); return t; }
    static constexpr double kLateSec = 2.5, kDeadSec = 6.0, kGraceSec = 1.5, kRedKeepSec = 30.0, kBeatTolMs = 30.0;
private:
    void evaluate (Member& m, const std::string& leaderHash, long long leaderSeq, double now);
    std::map<int, Member> m_; std::map<long long, double> pings_; std::vector<Toast> toasts_;
};

} // namespace wr::sync
