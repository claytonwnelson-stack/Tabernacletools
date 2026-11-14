#include "Sync.h"
#include "Roster.h"
#include "model/Chart.h"
#include <algorithm>
#include <cmath>

namespace wr::sync {

// ---------------------------------------------------------------- JSON
Json toJson (const State& s) {
    Json songs = Json::array();
    for (auto& r : s.songs) { Json ar = Json::array(), ns = Json::array(); for (auto& t : r.arrangement) ar.push (t);
        for (auto& n : r.notes) ns.push (Json::object().set ("text", n.text).set ("at", n.at));
        Json audio = Json::array(); for (auto& n : r.leaderStemNames) audio.push (n);
        Json pb = Json::array(); for (int b : r.passBars) pb.push (b);
        songs.push (Json::object().set ("song", wr::toJson (r.song)).set ("key", r.key).set ("bpm", r.bpm).set ("arr", ar).set ("notes", ns)
            .set ("passBars", pb).set ("bandLeader", r.bandLeader).set ("audioStems", audio).set ("audioPractice", r.leaderPractice)); }
    return Json::object().set ("v", kProtocolVersion).set ("setlist", s.setlistName).set ("date", s.setlistDate).set ("songs", songs)
        .set ("song", s.songIndex).set ("section", s.sectionIndex).set ("pass", s.pass)
        .set ("pending", s.pendingIdx).set ("pendingPass", s.pendingPass).set ("cueBeat", s.cueBeat).set ("dueBeat", s.dueBeat)
        .set ("bpm", s.bpm).set ("playing", s.playing).set ("playBeat", s.playBeat).set ("countIn", s.countIn)
        .set ("sectionColors", [&] { Json c = Json::object(); for (auto& kv : s.sectionColors) c.set (kv.first, kv.second); return c; }());
}
State stateFromJson (const Json& j) {
    State s; s.setlistName = j["setlist"].str(); s.setlistDate = j["date"].str();
    for (auto& o : j["songs"].items()) { SongRef r; r.song = songFromJson (o["song"]); r.key = o["key"].str(); r.bpm = o["bpm"].num();
        for (auto& t : o["arr"].items()) r.arrangement.push_back (t.str());
        for (auto& b : o["passBars"].items()) r.passBars.push_back (b.integer());
        r.bandLeader = o["bandLeader"].str();
        for (auto& n : o["notes"].items()) r.notes.push_back ({ n["text"].str(), n["at"].str ("start") });
        for (auto& n : o["audioStems"].items()) r.leaderStemNames.push_back (n.str());
        r.leaderPractice = o["audioPractice"].boolean();
        s.songs.push_back (std::move (r)); }
    s.songIndex = j["song"].integer(); s.sectionIndex = j["section"].integer(); s.pass = j["pass"].integer (1);
    s.pendingIdx = j["pending"].integer (-1); s.pendingPass = j["pendingPass"].integer (1); s.cueBeat = j["cueBeat"].num(); s.dueBeat = j["dueBeat"].num();
    s.bpm = j["bpm"].num (kDefaultBpm); s.playing = j["playing"].boolean(); s.playBeat = j["playBeat"].num(); s.countIn = j["countIn"].integer();
    if (j.has ("sectionColors")) { s.hasColors = true; for (auto& kv : j["sectionColors"].members()) { auto n = normalizeColor (kv.second.str()); if (! n.empty()) s.sectionColors[kv.first] = n; } }
    return s;
}
std::string encode (const std::string& type, long long seq, Json body) {
    if (! body.isObject()) body = Json::object();
    body.set ("t", type).set ("v", kProtocolVersion); if (seq >= 0) body.set ("seq", (double) seq);
    return body.dump (0) + "\n";
}
bool decode (const std::string& line, Msg& m) {
    bool ok = false; Json j = Json::parse (line, &ok); if (! ok || ! j.isObject()) return false;
    if (j["v"].integer (-1) != kProtocolVersion) return false;
    m.type = j["t"].str(); if (m.type.empty()) return false;
    m.seq = j.has ("seq") ? (long long) j["seq"].num() : -1; m.body = std::move (j); return true;
}

// ---------------------------------------------------------------- device health
std::string healthWarning (const DeviceHealth& h) {
    if (h.thermal >= 3) return "Overheating - iPad may shut down";
    if (h.thermal == 2) return "Hot - iPad may slow down";
    if (h.batteryPct >= 0 && h.batteryPct < kLowBatteryPct && ! h.charging) return "Battery " + std::to_string (h.batteryPct) + "% - plug in";
    return {};
}
std::string healthText (const DeviceHealth& h) {
    static const char* th[] = { "cool", "warm", "hot", "critical" };
    std::string t = h.batteryPct >= 0 ? std::to_string (h.batteryPct) + "%" + (h.charging ? " charging" : "") : std::string ("battery ?");
    return t + " - " + th[std::clamp (h.thermal, 0, 3)];
}

// ---------------------------------------------------------------- leader
State Leader::capture (const LiveSession& live, const BeatClock&, double) const {
    State s; const auto& set = live.setlist(); const Library& lib = live.library();
    s.setlistName = set.name; s.setlistDate = set.date;
    for (auto& e : set.entries) {
        SongRef r; if (auto* so = const_cast<Library&> (lib).song (e.songId)) {
            r.song = *so;
            for (auto& st : so->multitrack.stems) if (! st.name.empty()) r.leaderStemNames.push_back (st.name);
            r.leaderPractice = ! so->practice.file.empty();
        } else r.song.id = e.songId;
        r.key = e.keyOverride.empty() ? r.song.chart.key : e.keyOverride; r.bpm = e.bpmOverride > 0 ? e.bpmOverride : r.song.chart.bpm;
        r.arrangement = e.arrangement.empty() ? r.song.chart.arrangement : e.arrangement;
        r.passBars = e.arrangement.empty() ? r.song.chart.arrangementBars : e.arrangementBars; r.bandLeader = e.bandLeader; r.notes = e.notes;
        r.song.practice = PracticeTrack();   // the audio file stays on the leader's device
        r.song.multitrack = mt::MultitrackSet(); // names travel in leaderStemNames; the bytes are a separate transfer
        s.songs.push_back (std::move (r));
    }
    s.songIndex = live.songIndex(); s.sectionIndex = live.sectionIndex(); s.pass = live.pass();
    if (live.sectionPending()) { s.pendingIdx = live.pendingSection(); s.pendingPass = live.pendingPass(); s.cueBeat = cueBeat_; s.dueBeat = dueBeat_; }
    s.hasColors = true; s.sectionColors = live.sectionColorDefaults();
    s.bpm = live.bpm(); s.playing = playPending_ ? false : live.playing(); s.playBeat = playBeat_; s.countIn = playCountIn_;
    return s;
}
static bool sameSetlist (const State& a, const State& b) {
    if (a.setlistDate != b.setlistDate || a.setlistName != b.setlistName || a.songs.size() != b.songs.size()) return false;
    for (size_t i = 0; i < a.songs.size(); ++i) if (a.songs[i].song.id != b.songs[i].song.id || a.songs[i].key != b.songs[i].key
        || a.songs[i].arrangement != b.songs[i].arrangement || a.songs[i].passBars != b.songs[i].passBars || a.songs[i].bandLeader != b.songs[i].bandLeader || ! (a.songs[i].notes == b.songs[i].notes)) return false;
    if (a.sectionColors != b.sectionColors) return false;   // v0.8: colour edits on the leader re-send the snapshot
    for (size_t i = 0; i < a.songs.size(); ++i) {
        if (! chartContentEquals (a.songs[i].song.chart, b.songs[i].song.chart)) return false;   // chords, lyrics, tabs, section names
        const auto& x = a.songs[i].song.chart.sections; const auto& y = b.songs[i].song.chart.sections;
        if (x.size() != y.size()) return false; for (size_t k = 0; k < x.size(); ++k) if (x[k].customColor != y[k].customColor) return false;
    }
    return true;
}
void Leader::markCoLeader (const std::string& name, bool on) {
    if (name.empty()) return;
    auto it = std::find (coleaders_.begin(), coleaders_.end(), name);
    if (on) { if (it == coleaders_.end()) coleaders_.push_back (name); }
    else if (it != coleaders_.end()) coleaders_.erase (it);
}
bool Leader::isCoLeader (const std::string& name) const { return std::find (coleaders_.begin(), coleaders_.end(), name) != coleaders_.end(); }
void Leader::promoteDeferred() { if (deferred_) { ask_.waiting = true; deferred_ = false; } }
Json Leader::snapBody (const State& s) const {
    return Json::object().set ("session", session_).set ("leader", device_).set ("state", toJson (s)).set ("mt", mt::toJson (mt_)).set ("proto", kProtocolVersion).set ("app", kAppVersion);
}
std::string Leader::handoffLine (const std::string& name, const std::string& host, int port, double beat, double bpm) {
    Json co = Json::array(); std::vector<std::string> names = coleaders_;
    if (! device_.empty() && std::find (names.begin(), names.end(), device_) == names.end()) names.push_back (device_);
    for (auto& n : names) if (n != name) co.push (n);
    return encode ("handoff", -1, Json::object().set ("session", session_).set ("leader", name).set ("host", host).set ("port", port)
        .set ("beat", beat).set ("bpm", bpm).set ("previous", device_).set ("coleaders", co));
}
std::string Leader::approveTakeLead() {
    if (! ask_.waiting || ask_.name.empty()) return {};
    grant_ = ask_; ask_ = {}; deferred_ = false;
    Json co = Json::array(); for (auto& n : coleaders_) if (n != grant_.name) co.push (n); if (! device_.empty()) co.push (device_);
    return encode ("leadok", -1, Json::object().set ("session", session_).set ("leader", grant_.name).set ("previous", device_)
        .set ("beat", grant_.beat).set ("bpm", grant_.bpm).set ("coleaders", co));
}
std::string Leader::completeHandoff (const std::string& name, const std::string& host, int port, double beat, double bpm) {
    if (grant_.name.empty() || grant_.name != name || host.empty() || port <= 0) return {};
    auto line = handoffLine (name, host, port, beat, bpm); grant_ = {}; return line;
}
std::string Leader::handoffTo (const std::string& name, const std::string& host, int port, double beat, double bpm) {
    if (! isCoLeader (name)) return {};
    return handoffLine (name, host, port, beat, bpm);
}
std::vector<std::string> Leader::update (const LiveSession& live, BeatClock& clock, double now) {
    if (deferred_ && ! live.playing()) promoteDeferred();
    std::vector<std::string> out;
    if (std::abs (clock.tempo() - live.bpm()) >= 0.05) clock.setTempo (live.bpm(), now);   // Link carries tempo + phase to everyone
    // a new queued cue: its cue bar is the next bar line on the shared timeline (the local click is phase-locked to it)
    if (live.sectionPending() && live.cueSeq() != lastCueSeq_) {
        lastCueSeq_ = live.cueSeq(); cueBeat_ = nextBarBeat (clock.beatAt (now), live.beatsPerBar()); dueBeat_ = cueBeat_ + live.beatsPerBar();
    }
    State s = capture (live, clock, now);
    if (! have_) { last_ = s; have_ = true; return out; }
    if (! sameSetlist (s, last_)) { out.push_back (encode ("snap", ++seq_, snapBody (s))); last_ = s; lastMt_ = mt_; announcedPlay_ = s.playing; return out; }
    if (s.songIndex != last_.songIndex) out.push_back (ev ("song", Json::object().set ("song", s.songIndex)));
    if (std::abs (s.bpm - last_.bpm) >= 0.05) out.push_back (ev ("tempo", Json::object().set ("bpm", s.bpm)));
    if (s.pendingIdx >= 0 && (s.pendingIdx != last_.pendingIdx || s.pendingPass != last_.pendingPass || s.cueBeat != last_.cueBeat))
        out.push_back (ev ("cue", Json::object().set ("idx", s.pendingIdx).set ("pass", s.pendingPass).set ("cueBeat", s.cueBeat).set ("dueBeat", s.dueBeat)));
    if (s.sectionIndex != last_.sectionIndex || s.pass != last_.pass || s.songIndex != last_.songIndex || (last_.pendingIdx >= 0 && s.pendingIdx < 0))
        out.push_back (ev ("section", Json::object().set ("idx", s.sectionIndex).set ("pass", s.pass)));
    if (! playPending_ && live.playing() != announcedPlay_) { // transport changed locally (song ended, PLAY not routed through requestPlay)
        announcedPlay_ = live.playing(); playBeat_ = clock.beatAt (now); playCountIn_ = 0;
        out.push_back (ev ("play", Json::object().set ("on", announcedPlay_).set ("atBeat", playBeat_).set ("countIn", 0)));
    }
    if (! (mt_ == lastMt_)) { lastMt_ = mt_; out.push_back (ev ("mt", mt::toJson (mt_))); }
    last_ = s; return out;
}
std::string Leader::requestPlay (LiveSession& live, const BeatClock& clock, double now, bool on) {
    const double b = clock.beatAt (now);
    if (on) { playPending_ = true; playAt_ = nextBarBeat (b, live.beatsPerBar()); playCountIn_ = live.countInBeatsForSong(); playBeat_ = playAt_; }
    else { playPending_ = false; playAt_ = b; playCountIn_ = 0; playBeat_ = b; live.followPlay (false, 0, now); }
    announcedPlay_ = on;
    return ev ("play", Json::object().set ("on", on).set ("atBeat", playAt_).set ("countIn", playCountIn_));
}
void Leader::tick (LiveSession& live, const BeatClock& clock, double now) {
    if (playPending_ && clock.beatAt (now) >= playAt_ - 1e-6) { playPending_ = false; live.followPlay (true, playCountIn_, now); }
}
std::string Leader::snapshotLine (const LiveSession& live, const BeatClock& clock, double now) {
    State s = capture (live, clock, now); s.playing = live.playing() || playPending_;
    return encode ("snap", seq_, snapBody (s));
}
std::string Leader::onFollowerLine (const std::string& line, const LiveSession& live, const BeatClock& clock, double now) {
    Msg m; if (! decode (line, m)) return {};
    if (m.type == "hello" || m.type == "resync") {
        const int remote = m.body.has ("proto") ? m.body["proto"].integer() : kProtocolVersion;
        if (! protocolCompatible (kProtocolVersion, remote)) return encode ("ver", -1, Json::object().set ("ok", false).set ("proto", kProtocolVersion).set ("app", kAppVersion));
        return snapshotLine (live, clock, now);
    }
    if (m.type == "takelead") {
        const std::string who = m.body["name"].str();
        const auto decision = decideTakeLead (isCoLeader (who), true, live.playing());
        if (decision == TakeLead::Ignore) return encode ("leaddenied", -1, Json::object().set ("name", who));
        ask_.name = who; ask_.beat = m.body["beat"].num(); ask_.bpm = m.body["bpm"].num (live.bpm()); ask_.waiting = decision == TakeLead::Ask; deferred_ = decision == TakeLead::WaitUntilStopped;
        if (deferred_) ask_.waiting = false;
        return {};
    }
    if (m.type == "clk") return encode ("clk", -1, Json::object().set ("id", m.body["id"].num()).set ("t1", m.body["t1"].num()).set ("t2", now).set ("t3", now).set ("beat", clock.beatAt (now)).set ("bpm", clock.tempo()));
    return {};
}

// ---------------------------------------------------------------- follower
void Follower::onConnected() {
    connected_ = true; discAt_ = -1; haveSnap_ = false; wantSnap_ = true;
    kicked_ = false; lastHb_ = -1; lastClk_ = -1; lock_.reset();
    send_ (encode ("hello", -1, Json::object().set ("name", name_).set ("role", role).set ("last", (double) last_).set ("session", session_).set ("proto", kProtocolVersion).set ("app", kAppVersion)));
}
void Follower::onDisconnected (double now) { if (connected_ || discAt_ < 0) discAt_ = now; connected_ = false; haveSnap_ = false; }
std::string Follower::banner (double) const {
    if (kicked_) return "Removed from the session by the leader";
    if (! connected_) return discAt_ >= 0 ? "LEADER LOST - holding this section, reconnecting..." : "Not connected to a leader";
    if (! haveSnap_) return "Joining leader...";
    return {};
}
namespace {
void clearFollowerRig (Song& s) {
    s.rigPreset = "Worship Ambient"; s.scenes.clear(); s.sectionScene.clear(); s.defaultScene.clear(); s.switchC = {}; s.switchD = {};
}
std::string sceneAssigned (const Song& mine, const Section& sec) {
    auto it = mine.sectionScene.find (sec.name);
    if (it == mine.sectionScene.end()) it = mine.sectionScene.find (sec.label);
    if (it == mine.sectionScene.end()) it = mine.sectionScene.find (sec.kind);
    return it == mine.sectionScene.end() ? std::string() : it->second;
}
int sectionByName (const Chart& chart, const std::string& name, int nth) {
    if (name.empty() || nth < 1) return -1;
    int seen = 0;
    for (int i = 0; i < (int) chart.sections.size(); ++i) if (chart.sections[(size_t) i].name == name && ++seen == nth) return i;
    return -1;
}
int matchOldSection (const Chart& mine, const Chart& neu, int neuIndex) {
    if (neuIndex < 0 || neuIndex >= (int) neu.sections.size()) return -1;
    const auto& name = neu.sections[(size_t) neuIndex].name;
    int nth = 0; for (int i = 0; i <= neuIndex; ++i) if (neu.sections[(size_t) i].name == name) ++nth;
    int byName = sectionByName (mine, name, nth);
    if (byName >= 0) return byName;
    return neuIndex < (int) mine.sections.size() ? neuIndex : -1;
}
void copyFollowerRig (Song& dest, const Song& mine, const Chart& neu) {
    dest.rigPreset = mine.rigPreset; dest.scenes = mine.scenes; dest.defaultScene = mine.defaultScene;
    dest.switchC = mine.switchC; dest.switchD = mine.switchD; dest.sectionScene.clear();
    for (int i = 0; i < (int) neu.sections.size(); ++i) {
        int src = matchOldSection (mine.chart, neu, i); if (src < 0) continue;
        auto scene = sceneAssigned (mine, mine.chart.sections[(size_t) src]);
        if (! scene.empty()) dest.sectionScene[neu.sections[(size_t) i].name] = scene;
    }
}
std::string freshSharedId (Library& lib, const std::string& base) {
    auto id = base + "-shared"; if (! lib.song (id)) return id;
    for (int n = 2; n < 1000; ++n) { id = base + "-shared-" + std::to_string (n); if (! lib.song (id)) return id; }
    return base + "-shared-x";
}
Song withoutLeaderRig (Song s) { clearFollowerRig (s); s.multitrack = {}; s.practice = {}; return s; }
}
static bool setlistBodyEquals (const Setlist& a, const Setlist& b) {
    if (a.name != b.name || a.date != b.date || a.entries.size() != b.entries.size()) return false;
    for (size_t i = 0; i < a.entries.size(); ++i) {
        const auto& x = a.entries[i]; const auto& y = b.entries[i];
        if (x.songId != y.songId || x.keyOverride != y.keyOverride || x.bpmOverride != y.bpmOverride || x.role != y.role || x.capoOverride != y.capoOverride || x.arrangement != y.arrangement || x.arrangementBars != y.arrangementBars || x.bandLeader != y.bandLeader || ! (x.notes == y.notes)) return false;
    }
    return true;
}
bool setlistNeedsSave (const Library& lib, const Setlist& current) {
    if (current.entries.empty()) return false;
    for (auto& s : lib.setlists) if (s.date == current.date && setlistBodyEquals (s, current)) return false;
    return true;
}
static bool setlistIdentityChanged (const Setlist& cur, const State& st) {
    if (cur.name != st.setlistName || cur.date != st.setlistDate || cur.entries.size() != st.songs.size()) return true;
    for (size_t i = 0; i < st.songs.size(); ++i) if (cur.entries[i].songId != st.songs[i].song.id) return true;
    return false;
}
std::vector<AudioChunk> splitAudio (const std::string& songId, const std::string& name, const std::string& session, const std::string& kind, const std::string& bytes, size_t chunkBytes) {
    if (chunkBytes < 1) chunkBytes = 1;
    const int n = std::max (1, (int) ((bytes.size() + chunkBytes - 1) / chunkBytes));
    std::vector<AudioChunk> out;
    for (int i = 0; i < n; ++i) {
        AudioChunk c; c.songId = songId; c.name = name; c.session = session; c.kind = kind; c.index = i; c.count = n;
        const size_t off = (size_t) i * chunkBytes; c.data = off < bytes.size() ? bytes.substr (off, chunkBytes) : std::string();
        out.push_back (std::move (c));
    }
    return out;
}
void AudioAssembler::add (const AudioChunk& c) {
    if (count_ == 0) { songId_ = c.songId; name_ = c.name; session_ = c.session; owner_ = c.owner; kind_ = c.kind; count_ = std::max (1, c.count); have_.assign ((size_t) count_, 0); parts_.assign ((size_t) count_, {}); }
    if (owner_.empty() && ! c.owner.empty()) owner_ = c.owner;
    if (c.index < 0 || c.index >= count_ || have_[(size_t) c.index]) return;
    have_[(size_t) c.index] = 1; parts_[(size_t) c.index] = c.data; ++got_;
    if (complete()) { data_.clear(); for (auto& p : parts_) data_ += p; }
}
std::string sharedChartTitle (const Library& lib, const std::string& title) {
    auto taken = [&] (const std::string& t) { for (auto& s : lib.songs) if (s.chart.title == t) return true; return false; };
    const std::string base = title + " (shared)"; if (! taken (base)) return base;
    for (int n = 2; n < 1000; ++n) { auto t = title + " (shared " + std::to_string (n) + ")"; if (! taken (t)) return t; }
    return base;
}
int mapLeaderSection (const Chart& local, const std::vector<std::string>& leaderNames, int leaderIndex) {
    if (local.sections.empty()) return 0;
    if (leaderIndex < 0) return 0;
    if (leaderIndex < (int) leaderNames.size()) {
        const auto& name = leaderNames[(size_t) leaderIndex];
        int nth = 0; for (int i = 0; i <= leaderIndex; ++i) if (leaderNames[(size_t) i] == name) ++nth;
        int byName = sectionByName (local, name, nth);
        if (byName >= 0) return byName;
    }
    return std::clamp (leaderIndex, 0, (int) local.sections.size() - 1);
}
int Follower::mapSection (const LiveSession& live, int leaderIdx) const {
    const Chart& c = live.chart(); if (c.sections.empty()) return 0;
    const int song = live.songIndex();
    const bool matched = song >= 0 && song < (int) leaderMatch_.size() && leaderMatch_[(size_t) song];
    if (matched || song < 0 || song >= (int) leaderNames_.size()) return std::clamp (leaderIdx, 0, (int) c.sections.size() - 1);
    return mapLeaderSection (c, leaderNames_[(size_t) song], leaderIdx);
}
void Follower::applyCue (int idx, int pass, double cueBeat, double dueBeat, LiveSession& live, const BeatClock& clock, double now) {
    idx = mapSection (live, idx);
    const double b = clock.beatAt (now);
    if (b >= dueBeat - 1e-6) { cue_.active = false; live.followSection (idx, pass, now); return; } // already landed at the leader
    live.followCue (idx, pass, b < cueBeat - 1e-6);   // in time: the local (Link-locked) click speaks it in the same cue bar
    cue_ = { true, idx, pass, dueBeat };
}
Follower::ChartPrompt Follower::chartPrompt() const {
    ChartPrompt p; if (chartQueue_.empty()) return p;
    p.songId = chartQueue_.front().leader.id; p.title = chartQueue_.front().leader.chart.title; p.remaining = (int) chartQueue_.size() - 1; return p;
}
void Follower::answerSetlistSave (bool save, LiveSession& live, Library& lib, const BeatClock& clock, double now) {
    if (! haveSaveWait_) return;
    if (save) {
        auto cur = live.setlist(); bool found = false;
        for (auto& s : lib.setlists) if (s.date == cur.date) { s = cur; found = true; break; }
        if (! found) lib.setlists.push_back (cur);
    }
    auto st = saveWait_; haveSaveWait_ = false; skipSaveCheck_ = true;
    applySnapshot (st, live, lib, clock, now); skipSaveCheck_ = false;
}
bool Follower::takeFinishedAudio (AudioChunk& out) { if (finishedAudio_.empty()) return false; out = std::move (finishedAudio_.front()); finishedAudio_.erase (finishedAudio_.begin()); return true; }
void Follower::answerChartPrompt (ChartImportChoice choice, bool applyAll, RigImportChoice rig, LiveSession& live, Library& lib, double now) {
    if (chartQueue_.empty()) return;
    std::vector<QueuedChart> batch; batch.push_back (std::move (chartQueue_.front())); chartQueue_.erase (chartQueue_.begin());
    if (applyAll) { batch.insert (batch.end(), std::make_move_iterator (chartQueue_.begin()), std::make_move_iterator (chartQueue_.end())); chartQueue_.clear(); }
    Setlist set = live.setlist();
    for (auto& p : batch) {
        const std::string key = session_ + "\n" + p.leader.id;
        ChartDecision d; d.choice = (int) choice; d.rig = (int) rig; d.contentKey = p.contentKey; d.adoptedId = p.leader.id;
        if (choice == ChartImportChoice::Replace) {
            if (auto* s = lib.song (p.leader.id)) {
                Song mine = *s; s->chart = p.leader.chart;
                if (rig == RigImportChoice::CopyMine) copyFollowerRig (*s, mine, s->chart); else clearFollowerRig (*s);
            }
        } else         if (choice == ChartImportChoice::SaveAsNew) {
            Song n = withoutLeaderRig (p.leader);
            n.id = freshSharedId (lib, p.leader.id); n.chart.title = sharedChartTitle (lib, p.leader.chart.title);
            if (rig == RigImportChoice::CopyMine) if (auto* mine = lib.song (p.leader.id)) copyFollowerRig (n, *mine, n.chart);
            d.adoptedId = n.id; lib.songs.push_back (std::move (n));
            for (auto& e : set.entries) if (e.songId == p.leader.id) e.songId = d.adoptedId;
        }
        chartDecisions_[key] = d;
    }
    bool idsChanged = false;
    const auto& cur = live.setlist().entries;
    if (cur.size() == set.entries.size()) { for (size_t i = 0; i < cur.size(); ++i) if (cur[i].songId != set.entries[i].songId) idsChanged = true; }
    else idsChanged = true;
    if (idsChanged) live.replaceFollowSet (set);
    else live.refreshChart();
    if (live.song()) live.followSection (mapSection (live, leaderSection_), leaderPass_, now);
}
void Follower::applySnapshot (const State& st, LiveSession& live, Library& lib, const BeatClock& clock, double now) {
    libHold_ = &lib;
    if (live.playing()) { deferred_ = st; haveDeferred_ = true; return; }   // setup changes wait until the song stops
    const bool unchanged = haveApplied_ && setlistBodyEquals (live.setlist(), lastApplied_);
    if (! skipSaveCheck_ && setlistIdentityChanged (live.setlist(), st) && setlistNeedsSave (lib, live.setlist()) && ! unchanged) { saveWait_ = st; haveSaveWait_ = true; return; }
    leaderSection_ = st.sectionIndex; leaderPass_ = st.pass;
    leaderNames_.clear(); leaderMatch_.clear();
    Setlist set; set.name = st.setlistName; set.date = st.setlistDate;
    for (auto& r : st.songs) {
        std::vector<std::string> names; for (auto& sec : r.song.chart.sections) names.push_back (sec.name);
        std::string useId = r.song.id;
        bool matched = true;
        if (! r.song.id.empty()) {
            auto* local = lib.song (r.song.id);
            if (! local) lib.songs.push_back (withoutLeaderRig (r.song));
            else if (! chartContentEquals (local->chart, r.song.chart)) {
                const std::string key = chartContentKey (r.song.chart);
                const std::string decKey = session_ + "\n" + r.song.id;
                auto dec = chartDecisions_.find (decKey);
                if (dec != chartDecisions_.end() && dec->second.contentKey == key) {
                    if (dec->second.choice == (int) ChartImportChoice::Replace) local->chart = r.song.chart;
                    else if (dec->second.choice == (int) ChartImportChoice::SaveAsNew && lib.song (dec->second.adoptedId)) useId = dec->second.adoptedId;
                    else matched = false;
                } else {
                    matched = false;
                    bool queued = false; for (auto& q : chartQueue_) if (q.leader.id == r.song.id) { q.leader = r.song; q.contentKey = key; queued = true; break; }
                    if (! queued) chartQueue_.push_back ({ r.song, key });
                }
            }
        }
        leaderNames_.push_back (std::move (names)); leaderMatch_.push_back (matched ? 1 : 0);
        if (auto* noted = lib.song (useId)) if ((! r.leaderStemNames.empty() || r.leaderPractice) && noted->multitrack.empty() && noted->practice.file.empty())
            noted->leaderAudioNote = "Audio is on the leader's iPad";
        SetlistEntry e; e.songId = useId; e.bpmOverride = r.bpm; e.arrangement = r.arrangement; e.arrangementBars = r.passBars; e.bandLeader = r.bandLeader; e.notes = r.notes;
        auto lk = localKey.find (r.song.id);
        if (lk != localKey.end() && ! followLeaderKey) { e.keyOverride = lk->second.first; e.capoOverride = lk->second.second; } else e.keyOverride = r.key;
        set.entries.push_back (e);
    }
    if (st.hasColors) { std::map<std::string, std::vector<std::string>> per;
        for (auto& r : st.songs) { auto& v = per[r.song.id]; for (auto& sec : r.song.chart.sections) v.push_back (sec.customColor); }
        live.setFollowColors (st.sectionColors, per); }
    const auto& cur = live.setlist(); bool same = cur.date == set.date && cur.name == set.name && cur.entries.size() == set.entries.size();
    for (size_t i = 0; same && i < set.entries.size(); ++i) same = cur.entries[i].songId == set.entries[i].songId && cur.entries[i].keyOverride == set.entries[i].keyOverride
        && cur.entries[i].arrangement == set.entries[i].arrangement && cur.entries[i].arrangementBars == set.entries[i].arrangementBars && cur.entries[i].bandLeader == set.entries[i].bandLeader && cur.entries[i].notes == set.entries[i].notes;
    if (! same) live.loadSetlist (set, st.songIndex, now); else live.followSong (st.songIndex, now);
    lastApplied_ = live.setlist(); haveApplied_ = true;
    if (std::abs (live.bpm() - st.bpm) >= 0.05) live.setBpm (st.bpm);
    cue_.active = false;
    { const int mapped = mapSection (live, st.sectionIndex);
      if (live.sectionIndex() != mapped || live.pass() != st.pass || live.sectionPending()) live.followSection (mapped, st.pass, now); }
    if (st.pendingIdx >= 0) applyCue (st.pendingIdx, st.pendingPass, st.cueBeat, st.dueBeat, live, clock, now);
    play_.active = false;
    if (st.playing) {
        const double b = clock.beatAt (now);
        if (b < st.playBeat) play_ = { true, true, st.playBeat, st.countIn };
        else if (! live.playing()) live.followPlay (true, std::max (0, st.countIn - (int) std::floor (b - st.playBeat)), now);
    } else if (live.playing()) live.followPlay (false, 0, now);
}
void Follower::requestTakeLead (const BeatClock& clock, double now) {
    if (role != "coleader" || ! connected_) return;
    send_ (encode ("takelead", -1, Json::object().set ("name", name_).set ("beat", clock.beatAt (now)).set ("bpm", clock.tempo())));
}
bool Follower::takeClaim (Handoff& out) { if (! haveClaim_) return false; out = claim_; haveClaim_ = false; return true; }
bool Follower::takeRedirect (Handoff& out) { if (! haveRedirect_) return false; out = redirect_; haveRedirect_ = false; return true; }
void Follower::onLine (const std::string& line, LiveSession& live, Library& lib, BeatClock& clock, double now) {
    Msg m; if (! decode (line, m)) return; const Json& j = m.body;
    libHold_ = &lib;
    if (m.type == "kick") { kicked_ = true; return; }
    if (m.type == "role") { auto r = j["role"].str(); if (r == "coleader" || r == "player" || r == "projector") role = r; return; }
    if (m.type == "leadok" || m.type == "handoff") {
        Handoff h; h.session = j["session"].str(); h.leaderName = j["leader"].str(); h.host = j["host"].str(); h.port = j["port"].integer();
        h.previous = j["previous"].str(); h.beat = j["beat"].num(); h.bpm = j["bpm"].num();
        for (auto& n : j["coleaders"].items()) h.coLeaders.push_back (n.str());
        if (! h.session.empty()) session_ = h.session;
        if (! h.leaderName.empty()) leaderName_ = h.leaderName;
        clock.adoptTimeline (h.beat, h.bpm > 0 ? h.bpm : clock.tempo(), now);
        if (name_ == h.previous) role = "coleader";
        if (m.type == "leadok" || name_ == h.leaderName) { claim_ = h; haveClaim_ = true; }
        else { redirect_ = h; haveRedirect_ = true; }
        return;
    }
    if (m.type == "ver" && ! m.body["ok"].boolean (true)) { versionWarn_ = versionWarningText(); return; }
    if (m.type == "audio") {
        auto unhex = [] (const std::string& in) {
            if (in.size() % 2) return in;
            auto v = [] (char c) { if (c >= '0' && c <= '9') return c - '0'; if (c >= 'a' && c <= 'f') return c - 'a' + 10; if (c >= 'A' && c <= 'F') return c - 'A' + 10; return -1; };
            std::string o (in.size() / 2, '\0');
            for (size_t i = 0; i < o.size(); ++i) { int a = v (in[i * 2]), b = v (in[i * 2 + 1]); if (a < 0 || b < 0) return in; o[i] = (char) ((a << 4) | b); }
            return o;
        };
        AudioChunk c; c.songId = j["song"].str(); c.name = j["name"].str(); c.session = j["session"].str(); c.kind = j["kind"].str();
        c.index = j["i"].integer(); c.count = j["n"].integer (1); c.owner = j["owner"].str(); c.data = unhex (j["data"].str());
        audio_.add (c); if (audio_.complete()) { finishedAudio_.push_back (AudioChunk { audio_.songId(), audio_.name(), audio_.session(), audio_.owner(), audio_.kind(), 0, 1, audio_.bytes() }); audio_ = {}; }
        return;
    }
    if (m.type == "ping") { send_ (encode ("pong", -1, Json::object().set ("id", j["id"].num()))); return; }
    if (m.type == "clk" && j.has ("t2")) { lock_.observe (j["t1"].num(), j["t2"].num(), j["t3"].num(), now, j["beat"].num(), j["bpm"].num(), now); return; }
    if (m.type == "snap") {
        const int remote = j.has ("proto") ? j["proto"].integer() : kProtocolVersion;
        if (! protocolCompatible (kProtocolVersion, remote)) { versionWarn_ = versionWarningText(); return; }
        session_ = j["session"].str(); if (j.has ("leader")) leaderName_ = j["leader"].str(); last_ = m.seq; haveSnap_ = true; wantSnap_ = false;
        transport = mt::transportFromJson (j["mt"]);
        live.setFollowing (true); applySnapshot (stateFromJson (j["state"]), live, lib, clock, now); return;
    }
    if (! haveSnap_) return;                                                  // wait for the snapshot
    if (m.seq <= last_) return;                                               // duplicate
    if (m.seq != last_ + 1) { if (! wantSnap_) { wantSnap_ = true; ++resyncs_; send_ (encode ("resync", -1, Json::object())); } return; }
    last_ = m.seq;
    if (m.type == "song") { cue_.active = false; play_.active = false; live.followSong (j["song"].integer(), now); }
    else if (m.type == "section") { cue_.active = false; int i = mapSection (live, j["idx"].integer()), p = j["pass"].integer (1);
        if (live.sectionIndex() != i || live.pass() != p || live.sectionPending()) live.followSection (i, p, now); }
    else if (m.type == "cue") applyCue (j["idx"].integer(), j["pass"].integer (1), j["cueBeat"].num(), j["dueBeat"].num(), live, clock, now);
    else if (m.type == "mt") transport = mt::transportFromJson (j);
    else if (m.type == "tempo") live.setBpm (j["bpm"].num());
    else if (m.type == "play") {
        const bool on = j["on"].boolean(); const double at = j["atBeat"].num(); const int ci = j["countIn"].integer();
        if (! on) { play_.active = false; live.followPlay (false, 0, now); }
        else { const double b = clock.beatAt (now);
            if (b < at - 1e-6) play_ = { true, true, at, ci };
            else live.followPlay (true, std::max (0, ci - (int) std::floor (b - at)), now); }  // late: join the count-in where it is
    }
}
void Follower::tick (LiveSession& live, const BeatClock& clock, double now) {
    if (connected_ && (lastClk_ < 0 || now - lastClk_ >= 0.25)) { lastClk_ = now; send_ (encode ("clk", -1, Json::object().set ("id", (double) ++clkId_).set ("t1", now))); }
    lock_.steer (now);
    if (haveDeferred_ && ! live.playing() && libHold_ != nullptr) { auto st = deferred_; haveDeferred_ = false; applySnapshot (st, live, *libHold_, clock, now); }
    const double b = lock_.engaged() ? lock_.beatAt (now) : clock.beatAt (now);
    if (connected_ && (lastHb_ < 0 || now - lastHb_ >= 1.0)) {   // heartbeat for the leader's roster
        lastHb_ = now; const auto& c = live.chart(); const int si = live.sectionIndex();
        std::string sec = si >= 0 && si < (int) c.sections.size() ? c.sections[(size_t) si].name : std::string();
        if (live.sectionPending() && live.pendingSection() < (int) c.sections.size()) sec += " > " + c.sections[(size_t) live.pendingSection()].name;
        send_ (encode ("hb", -1, Json::object().set ("name", name_).set ("role", role).set ("hash", stateHash (live)).set ("last", (double) last_)
            .set ("beat", b).set ("q", live.beatsPerBar()).set ("section", sec)
            .set ("bat", health.batteryPct).set ("chg", health.charging).set ("therm", health.thermal)));
    }
    if (cue_.active && b >= cue_.dueBeat - 1e-6) { cue_.active = false; live.followSection (cue_.idx, cue_.pass, now); }
    if (play_.active && b >= play_.atBeat - 1e-6) { play_.active = false; live.followPlay (true, play_.countIn, now); }
}

} // namespace wr::sync
