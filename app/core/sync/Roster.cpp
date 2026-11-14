#include "Roster.h"
#include <cmath>
#include <algorithm>

namespace wr::sync {

std::string stateHash (const LiveSession& live) {
    const Song* s = live.song();
    std::string h = (s ? s->id : std::string ("-")) + "|" + std::to_string (live.sectionIndex()) + "|" + std::to_string (live.pass());
    if (live.sectionPending()) h += ">" + std::to_string (live.pendingSection()) + "." + std::to_string (live.pendingPass());
    return h;
}

void Roster::connected (int id, double now) { Member m; m.id = id; m.joinedAt = now; m.lastHb = now; m_[id] = m; }
void Roster::disconnected (int id, double now) {
    auto it = m_.find (id); if (it == m_.end() || ! it->second.connected) return;
    auto& m = it->second; m.connected = false; m.droppedAt = now; m.status = Member::Red; m.reason = m.kicked ? "removed" : "dropped";
    if (! m.kicked) toasts_.push_back ({ m.name + " dropped", 1 });
}
void Roster::kicked (int id, double now) { auto it = m_.find (id); if (it == m_.end()) return; it->second.kicked = true; toasts_.push_back ({ it->second.name + " removed", 0 }); disconnected (id, now); }
void Roster::setRole (int id, const std::string& role) { auto it = m_.find (id); if (it == m_.end()) return; it->second.role = role; }

void Roster::line (int id, const Msg& msg, const std::string& lh, long long lseq, double lbeat, double bpm, double now) {
    auto it = m_.find (id); if (it == m_.end() || ! it->second.connected) return; auto& m = it->second; const Json& j = msg.body;
    if (msg.type == "pong") { auto p = pings_.find ((long long) j["id"].num()); if (p != pings_.end()) { const double r = (now - p->second) * 1000.0; m.rttMs = m.rttMs < 0 ? r : m.rttMs * 0.7 + r * 0.3; } return; }
    if (msg.type == "hello") { const bool first = m.name == "?"; m.name = j["name"].str ("Follower"); m.role = j["role"].str ("player"); if (first) toasts_.push_back ({ m.name + " joined", 0 }); return; }
    if (msg.type != "hb") return;
    if (m.name == "?") { m.name = j["name"].str ("Follower"); toasts_.push_back ({ m.name + " joined", 0 }); } else m.name = j["name"].str (m.name);
    m.role = j["role"].str (m.role); m.hash = j["hash"].str(); m.lastSeq = (long long) j["last"].num (-1); m.section = j["section"].str(); m.lastHb = now;
    if (j.has ("bat") || j.has ("therm")) { // v0.7 device health: warn the leader once when a follower goes low / hot
        DeviceHealth h; h.batteryPct = j["bat"].integer (-1); h.charging = j["chg"].boolean(); h.thermal = std::clamp (j["therm"].integer (0), 0, 3);
        const int sev = healthSeverity (h); if (sev > m.healthSev) toasts_.push_back ({ m.name + ": " + healthWarning (h), 1 });
        m.health = h; m.healthSev = sev; }
    // Link beat check: the follower's beat when it sent the hb vs ours then (half a round trip ago)
    if (j.has ("beat") && bpm > 0) {
        const double lat = m.rttMs > 0 ? m.rttMs * 0.0005 : 0.0, ours = lbeat - lat * bpm / 60.0;
        const double q = std::max (1.0, j["q"].num (4)); double d = std::fmod (j["beat"].num() - ours, q); if (d > q / 2) d -= q; if (d < -q / 2) d += q; // phase within the bar (Link quantum)
        m.beatErrMs = d * 60000.0 / bpm;
    }
    evaluate (m, lh, lseq, now);
}
void Roster::evaluate (Member& m, const std::string& lh, long long lseq, double now) {
    if (! m.connected) return;
    std::string why;
    if (now - m.lastHb > kLateSec) why = "no heartbeat " + std::to_string ((int) (now - m.lastHb)) + " s";
    else if (! m.hash.empty() && m.hash != lh) why = "showing a different section";
    else if (m.lastSeq >= 0 && m.lastSeq < lseq) why = "behind " + std::to_string (lseq - m.lastSeq) + " update(s)";
    else if (std::abs (m.beatErrMs) > kBeatTolMs) why = std::string ("beat ") + (m.beatErrMs > 0 ? "+" : "") + std::to_string ((int) std::lround (m.beatErrMs)) + " ms";
    else if (m.hash.empty()) why = "joining";
    if (why.empty()) { m.badSince = -1; m.status = Member::Green; m.reason.clear(); return; }
    if (m.badSince < 0) m.badSince = now;
    m.reason = why;
    // an in-flight change (cue just sent, hb from before it) is normal: only yellow if it lasts
    if (now - m.badSince >= kGraceSec || why.rfind ("no heartbeat", 0) == 0 || why == "joining") m.status = Member::Yellow;
}
void Roster::tick (const std::string& lh, long long lseq, double now) {
    for (auto it = m_.begin(); it != m_.end();) {
        auto& m = it->second;
        if (m.connected && now - m.lastHb > kDeadSec) disconnected (m.id, now);
        if (! m.connected && now - m.droppedAt > kRedKeepSec) { it = m_.erase (it); continue; }
        evaluate (m, lh, lseq, now); ++it;
    }
}
std::vector<Member> Roster::members() const {
    std::vector<Member> v; for (auto& kv : m_) v.push_back (kv.second);
    std::stable_sort (v.begin(), v.end(), [] (const Member& a, const Member& b) { if (a.connected != b.connected) return a.connected; return a.joinedAt < b.joinedAt; });
    return v;
}
int Roster::connectedCount() const { int n = 0; for (auto& kv : m_) n += kv.second.connected; return n; }

} // namespace wr::sync
