// Band sync (v0.5): one leader, any number of followers on the local network (works on an offline travel router).
//
//  * Discovery: Bonjour / mDNS service "_tabernacle._tcp" (Discovery.h). Join = one TCP connection to the leader.
//  * Beat/tempo phase: Ableton Link (BeatClock). Every device renders its own click and voice locally, phase-locked to
//    the shared Link timeline, so cues and changes are expressed in Link beats, not wall-clock time.
//  * This file: message format, sequencing, late-join snapshot, leader diffing and follower apply. No sockets, no
//    threads - fully testable on Linux (tests/test_sync.cpp, including real 127.0.0.1 TCP through Net.h).
//
// Wire format: one JSON object per line ("\n"-terminated, UTF-8), protocol version "v".
//   follower -> leader   {"t":"hello","v":1,"name":"Keys iPad","last":<last seq seen or -1>,"session":"<leader session id or ''>"}
//                        {"t":"resync"}                       (gap detected - please send a snapshot)
//                        {"t":"pong","id":n}
//   leader -> follower   {"t":"snap","seq":n,"session":"..","state":{...}}   full state (join, resync, reconnect)
//                        {"t":"song","seq":n,"song":i}                       song index in the setlist
//                        {"t":"section","seq":n,"idx":i,"pass":p}            landed (no cue / immediate)
//                        {"t":"cue","seq":n,"idx":i,"pass":p,"cueBeat":b,"dueBeat":d}  queued: one cue bar [cueBeat, dueBeat)
//                        {"t":"tempo","seq":n,"bpm":x}
//                        {"t":"play","seq":n,"on":true,"atBeat":b,"countIn":8}  transport, quantised to a Link bar line
//                        {"t":"mt","seq":n,"loaded":b,"on":b,"sec":i,"loop":i,"pending":i,"beat":x}  v0.14 backing-track transport
//                        {"t":"ping","id":n}                                  keep-alive (no seq)
// Every leader event carries seq = previous + 1 within a session; a follower that sees a gap (or a new session id)
// ignores the event and asks for a snapshot. Duplicates (seq <= last) are dropped.
#pragma once
#include "../model/LiveSession.h"
#include "ClockLock.h"
#include <deque>
#include <functional>

namespace wr::sync {

constexpr int kProtocolVersion = 1;
constexpr int kAppVersion = 11;
constexpr int kDefaultPort = 52741;
// A newer leader than this build is incompatible. An older or equal leader is fine, and says nothing.
inline bool protocolCompatible (int localProto, int remoteProto) { if (remoteProto <= 0) remoteProto = 1; return remoteProto <= localProto; }
inline const char* versionWarningText() { return "Update Bearing Live on this iPad to join this leader"; }
inline const char* serviceType() { return "_tabernacle._tcp"; }

// The shared beat timeline. App: Ableton Link (Source/LinkClock.h). Tests: FakeClock.
struct BeatClock {
    virtual ~BeatClock() = default;
    virtual double beatAt (double nowSec) const = 0;      // Link timeline beat at this time
    virtual double tempo() const = 0;
    virtual void setTempo (double bpm, double nowSec) = 0; // leader only (Link propagates to peers)
    virtual int peers() const { return 0; }
};
// next bar line on the shared timeline (quantum = beats per bar)
inline double nextBarBeat (double beat, int bpb) { const double q = std::max (1, bpb); return (std::floor (beat / q + 1e-9) + 1.0) * q; }

// ---------------------------------------------------------------- v0.7 device health (battery / thermal)
struct DeviceHealth {
    int batteryPct = -1;          // 0..100, -1 unknown (desktop / simulator)
    bool charging = false;        // plugged in (charging or full)
    int thermal = 0;              // iOS ProcessInfo.thermalState: 0 nominal, 1 fair, 2 serious, 3 critical
    bool operator== (const DeviceHealth& o) const { return batteryPct == o.batteryPct && charging == o.charging && thermal == o.thermal; }
};
constexpr int kLowBatteryPct = 20;
// "" when fine; else a short warning: "Battery 15% - plug in", "Hot - iPad may slow down" (serious), "Overheating" (critical)
std::string healthWarning (const DeviceHealth&);
inline int healthSeverity (const DeviceHealth& h) { return h.thermal >= 3 ? 2 : (h.thermal == 2 || (h.batteryPct >= 0 && h.batteryPct < kLowBatteryPct && ! h.charging)) ? 1 : 0; }
std::string healthText (const DeviceHealth&);   // "87% charging - fair"

// Follower chart import. Rig settings stay on the follower's iPad; the leader's rig is never copied.
enum class ChartImportChoice { Replace = 1, SaveAsNew = 2, KeepMine = 3 };
enum class RigImportChoice { CopyMine = 1, Fresh = 2 };
std::string sharedChartTitle (const Library&, const std::string& title);   // "Title (shared)", then "Title (shared 2)"
// Leader section index -> a section on the follower's chart. Match name (nth time), then clamp.
int mapLeaderSection (const Chart& local, const std::vector<std::string>& leaderNames, int leaderIndex);
bool setlistNeedsSave (const Library& lib, const Setlist& current);   // false when empty or already stored unchanged
// Shared leader audio plays only while connected to that session. An empty tag is the follower's own file.
inline bool sharedAudioPlayable (const std::string& sharedSession, bool connected, const std::string& currentSession) {
    return sharedSession.empty() || (connected && ! currentSession.empty() && sharedSession == currentSession);
}
struct AudioChunk { std::string songId, name, session, kind; int index = 0, count = 0; std::string data; };
std::vector<AudioChunk> splitAudio (const std::string& songId, const std::string& name, const std::string& session, const std::string& kind, const std::string& bytes, size_t chunkBytes);
struct AudioAssembler {
    void add (const AudioChunk&);
    float progress() const { return count_ <= 0 ? 0 : (float) got_ / (float) count_; }
    bool complete() const { return count_ > 0 && got_ == count_; }
    const std::string& bytes() const { return data_; }
    const std::string& session() const { return session_; }
    const std::string& name() const { return name_; }
    const std::string& songId() const { return songId_; }
    const std::string& kind() const { return kind_; }
private:
    std::string songId_, name_, session_, kind_, data_; int count_ = 0, got_ = 0; std::vector<char> have_; std::vector<std::string> parts_;
};

// ---------------------------------------------------------------- shared state (snapshot)
struct SongRef { Song song; std::string key; double bpm = 0;       // leader's song (chart + leader's sounding key + tempo)
                 std::vector<std::string> arrangement;              // v0.7 effective play order (so section indices match)
                 std::vector<int> passBars;                         // bars for each arrangement pass (0 = the section's own count)
                 std::vector<ServiceNote> notes;                    // v0.7 service notes for this setlist item
                 std::vector<std::string> leaderStemNames; bool leaderPractice = false; };
struct State {
    std::string setlistName, setlistDate; std::vector<SongRef> songs;
    int songIndex = 0, sectionIndex = 0, pass = 1;
    int pendingIdx = -1, pendingPass = 1; double cueBeat = 0, dueBeat = 0;  // queued one-bar cue (Link beats)
    double bpm = kDefaultBpm; bool playing = false; double playBeat = 0; int countIn = 0;
    bool hasColors = false; SectionColorMap sectionColors;   // v0.8 leader's per-kind section colours (per-song overrides ride in songs)
};
Json toJson (const State&);
State stateFromJson (const Json&);

struct Msg { std::string type; long long seq = -1; Json body; };
std::string encode (const std::string& type, long long seq, Json body);  // one line incl. '\n'
bool decode (const std::string& line, Msg& out);                          // false for junk / other protocol versions

// split a byte stream into lines (TCP framing); caps runaway lines
struct LineBuffer {
    std::string buf; static constexpr size_t kMax = 4u << 20;
    template <typename F> void feed (const char* d, size_t n, F&& onLine) {
        buf.append (d, n); size_t s = 0;
        for (size_t i; (i = buf.find ('\n', s)) != std::string::npos; s = i + 1) onLine (buf.substr (s, i - s));
        buf.erase (0, s); if (buf.size() > kMax) buf.clear();
    }
};

// ---------------------------------------------------------------- leader
// Watches the leader's LiveSession and emits events; answers hello/resync with a snapshot.
class Leader {
public:
    explicit Leader (std::string sessionId) : session_ (std::move (sessionId)) {}
    const std::string& sessionId() const { return session_; }
    // call after every LiveSession change / on a timer. Returns event lines to broadcast to every follower.
    std::vector<std::string> update (const LiveSession& live, BeatClock& clock, double nowSec); // also pushes tempo to Link
    // PLAY while leading: transport is quantised to the next Link bar line so every device starts the count-in together.
    // Returns the beat it will start on; the leader's own session starts in update() when the clock gets there.
    std::string requestPlay (LiveSession& live, const BeatClock& clock, double nowSec, bool on); // returns the event line
    double scheduledPlayBeat() const { return playAt_; }
    bool playScheduled() const { return playPending_; }
    void tick (LiveSession& live, const BeatClock& clock, double nowSec);           // fires the leader's own scheduled play
    // follower -> leader line; returns the reply for that follower only (snapshot) or ""
    std::string onFollowerLine (const std::string& line, const LiveSession& live, const BeatClock& clock, double nowSec);
    std::string snapshotLine (const LiveSession& live, const BeatClock& clock, double nowSec);
    long long seq() const { return seq_; }
    State capture (const LiveSession& live, const BeatClock& clock, double nowSec) const;
    // v0.14 multitrack: the leader's backing-track transport (set every tick; an "mt" event goes out when it changes;
    // sections themselves still travel as "section"/"cue", so followers' charts, highlights and patches follow as before)
    void setTransport (const mt::Transport& t) { mt_ = t; }
    const mt::Transport& transport() const { return mt_; }
private:
    mt::Transport mt_, lastMt_;
    std::string session_; long long seq_ = 0; bool have_ = false; State last_;
    int lastCueSeq_ = -1; double cueBeat_ = 0, dueBeat_ = 0;
    bool playPending_ = false, announcedPlay_ = false; double playAt_ = 0; int playCountIn_ = 0; double playBeat_ = 0;
    std::string ev (const char* t, Json body) { return encode (t, ++seq_, std::move (body)); }
};

// ---------------------------------------------------------------- follower
class Follower {
public:
    // send: line to the leader (hello / resync / pong)
    explicit Follower (std::function<void (const std::string&)> send, std::string name = "Follower") : send_ (std::move (send)), name_ (std::move (name)) {}
    void onConnected();                                    // sends hello (with last seq + session for a fast resume)
    void onDisconnected (double nowSec);                   // hold the current section; banner until reconnected
    void onLine (const std::string& line, LiveSession& live, Library& lib, const BeatClock& clock, double nowSec);
    void tick (LiveSession& live, const BeatClock& clock, double nowSec); // lands queued cues / starts play on the Link beat
    bool connected() const { return connected_; }
    bool synced() const { return connected_ && haveSnap_; }
    double disconnectedSince() const { return discAt_; }
    long long lastSeq() const { return last_; }
    int resyncs() const { return resyncs_; }
    std::string banner (double nowSec) const;              // "" when fine
    // per-song local overrides kept across leader updates (followers keep their own key/capo)
    std::map<std::string, std::pair<std::string, int>> localKey; // songId -> (key, capo)
    std::string role = "player";                           // "player" | "projector" (shown in the leader's roster)
    DeviceHealth health;                                   // v0.7 battery / thermal, sent in every heartbeat (app sets it)
    bool wasKicked() const { return kicked_; }             // leader removed this device: don't auto-reconnect
    ClockLock& clockLock() { return lock_; }
    const ClockLock& clockLock() const { return lock_; }
    void setName (const std::string& n) { name_ = n; }
    // v0.14: leader's backing-track transport (display only - followers have no stems)
    mt::Transport transport;
    // v0.14 key changes: true = follow the leader's key even for songs this player transposed locally (chart transposes;
    // the leader's stems are NOT pitch-shifted - change the key on the tracks before the service)
    bool followLeaderKey = false;
    // A song the follower already has, whose chords/lyrics/tabs differ from the leader's. One prompt at a time.
    struct ChartPrompt { std::string songId, title; int remaining = 0; };
    bool chartPromptWaiting() const { return ! chartQueue_.empty(); }
    ChartPrompt chartPrompt() const;
    // choice applies to the first queued song, or to every queued song when applyAll is set.
    // rig is ignored for KeepMine. CopyMine is the default the UI offers.
    void answerChartPrompt (ChartImportChoice choice, bool applyAll, RigImportChoice rig, LiveSession& live, Library& lib, double now);
    bool setlistSaveWaiting() const { return haveSaveWait_; }
    void answerSetlistSave (bool save, LiveSession& live, Library& lib, const BeatClock& clock, double now);
    const std::string& versionWarning() const { return versionWarn_; }
    void clearVersionWarning() { versionWarn_.clear(); }
    const std::string& sessionId() const { return session_; }
    // Leader audio offer. Empty until a transfer finishes. progress is 0..1 while chunks arrive.
    float audioProgress() const { return audio_.progress(); }
    bool takeFinishedAudio (AudioChunk& out);   // one completed file (stem or practice)
private:
    void applySnapshot (const State& st, LiveSession& live, Library& lib, const BeatClock& clock, double now);
    void applyCue (int idx, int pass, double cueBeat, double dueBeat, LiveSession& live, const BeatClock& clock, double now);
    std::function<void (const std::string&)> send_; std::string name_, session_;
    double lastHb_ = -1, lastClk_ = -1; int clkId_ = 0; bool kicked_ = false; ClockLock lock_;
    bool connected_ = false, haveSnap_ = false, wantSnap_ = false; long long last_ = -1; int resyncs_ = 0; double discAt_ = -1;
    int mapSection (const LiveSession& live, int leaderIdx) const;
    struct Pending { bool active = false; int idx = 0, pass = 1; double dueBeat = 0; } cue_;
    struct PlayAt { bool active = false, on = false; double atBeat = 0; int countIn = 0; } play_;
    struct QueuedChart { Song leader; std::string contentKey; };
    struct ChartDecision { int choice = 0; int rig = 0; std::string contentKey, adoptedId; };
    std::vector<QueuedChart> chartQueue_;
    std::map<std::string, ChartDecision> chartDecisions_;   // session + song id -> last answer for that chart content
    std::vector<std::vector<std::string>> leaderNames_;
    std::vector<char> leaderMatch_;
    int leaderSection_ = 0, leaderPass_ = 1;
    bool haveDeferred_ = false, haveSaveWait_ = false, skipSaveCheck_ = false, haveApplied_ = false;
    Setlist lastApplied_;
    State deferred_, saveWait_;
    Library* libHold_ = nullptr;
    std::string versionWarn_;
    AudioAssembler audio_;
    std::vector<AudioChunk> finishedAudio_;
};

// fixed-tempo shared timeline for tests (and for "no Link" fallback: beats from local time at the leader's tempo)
struct FakeClock : BeatClock {
    double bpm = 120, t0 = 0, b0 = 0; int nPeers = 0;
    double beatAt (double now) const override { return b0 + (now - t0) * bpm / 60.0; }
    double tempo() const override { return bpm; }
    void setTempo (double b, double now) override { b0 = beatAt (now); t0 = now; bpm = b; }
    int peers() const override { return nPeers; }
};

} // namespace wr::sync
