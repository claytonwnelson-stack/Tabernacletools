#include "Sync.h"
#include "Roster.h"
#include <cmath>

namespace wr::sync {

// ---------------------------------------------------------------- JSON
Json toJson (const State& s) {
    Json songs = Json::array();
    for (auto& r : s.songs) { Json ar = Json::array(), ns = Json::array(); for (auto& t : r.arrangement) ar.push (t);
        for (auto& n : r.notes) ns.push (Json::object().set ("text", n.text).set ("at", n.at));
        songs.push (Json::object().set ("song", wr::toJson (r.song)).set ("key", r.key).set ("bpm", r.bpm).set ("arr", ar).set ("notes", ns)); }
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
        for (auto& n : o["notes"].items()) r.notes.push_back ({ n["text"].str(), n["at"].str ("start") });
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
        SongRef r; if (auto* so = const_cast<Library&> (lib).song (e.songId)) r.song = *so; else r.song.id = e.songId;
        r.key = e.keyOverride.empty() ? r.song.chart.key : e.keyOverride; r.bpm = e.bpmOverride > 0 ? e.bpmOverride : r.song.chart.bpm;
        r.arrangement = e.arrangement.empty() ? r.song.chart.arrangement : e.arrangement; r.notes = e.notes;
        r.song.practice = PracticeTrack();   // the audio file stays on the leader's device
        r.song.multitrack = mt::MultitrackSet(); // v0.14 stems stay on the leader too: followers only follow the transport
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
        || a.songs[i].arrangement != b.songs[i].arrangement || ! (a.songs[i].notes == b.songs[i].notes)) return false;
    if (a.sectionColors != b.sectionColors) return false;   // v0.8: colour edits on the leader re-send the snapshot
    for (size_t i = 0; i < a.songs.size(); ++i) { const auto& x = a.songs[i].song.chart.sections; const auto& y = b.songs[i].song.chart.sections;
        if (x.size() != y.size()) return false; for (size_t k = 0; k < x.size(); ++k) if (x[k].customColor != y[k].customColor) return false; }
    return true;
}
std::vector<std::string> Leader::update (const LiveSession& live, BeatClock& clock, double now) {
    std::vector<std::string> out;
    if (std::abs (clock.tempo() - live.bpm()) >= 0.05) clock.setTempo (live.bpm(), now);   // Link carries tempo + phase to everyone
    // a new queued cue: its cue bar is the next bar line on the shared timeline (the local click is phase-locked to it)
    if (live.sectionPending() && live.cueSeq() != lastCueSeq_) {
        lastCueSeq_ = live.cueSeq(); cueBeat_ = nextBarBeat (clock.beatAt (now), live.beatsPerBar()); dueBeat_ = cueBeat_ + live.beatsPerBar();
    }
    State s = capture (live, clock, now);
    if (! have_) { last_ = s; have_ = true; return out; }
    if (! sameSetlist (s, last_)) { out.push_back (encode ("snap", ++seq_, Json::object().set ("session", session_).set ("state", toJson (s)).set ("mt", mt::toJson (mt_)))); last_ = s; lastMt_ = mt_; announcedPlay_ = s.playing; return out; }
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
    return encode ("snap", seq_, Json::object().set ("session", session_).set ("state", toJson (s)).set ("mt", mt::toJson (mt_)));
}
std::string Leader::onFollowerLine (const std::string& line, const LiveSession& live, const BeatClock& clock, double now) {
    Msg m; if (! decode (line, m)) return {};
    if (m.type == "hello" || m.type == "resync") return snapshotLine (live, clock, now);
    return {};
}

// ---------------------------------------------------------------- follower
void Follower::onConnected() {
    connected_ = true; discAt_ = -1; haveSnap_ = false; wantSnap_ = true;
    kicked_ = false; lastHb_ = -1;
    send_ (encode ("hello", -1, Json::object().set ("name", name_).set ("role", role).set ("last", (double) last_).set ("session", session_)));
}
void Follower::onDisconnected (double now) { if (connected_ || discAt_ < 0) discAt_ = now; connected_ = false; haveSnap_ = false; }
std::string Follower::banner (double) const {
    if (kicked_) return "Removed from the session by the leader";
    if (! connected_) return discAt_ >= 0 ? "LEADER LOST - holding this section, reconnecting..." : "Not connected to a leader";
    if (! haveSnap_) return "Joining leader...";
    return {};
}
void Follower::applyCue (int idx, int pass, double cueBeat, double dueBeat, LiveSession& live, const BeatClock& clock, double now) {
    const double b = clock.beatAt (now);
    if (b >= dueBeat - 1e-6) { cue_.active = false; live.followSection (idx, pass, now); return; } // already landed at the leader
    live.followCue (idx, pass, b < cueBeat - 1e-6);   // in time: the local (Link-locked) click speaks it in the same cue bar
    cue_ = { true, idx, pass, dueBeat };
}
void Follower::applySnapshot (const State& st, LiveSession& live, Library& lib, const BeatClock& clock, double now) {
    Setlist set; set.name = st.setlistName; set.date = st.setlistDate;
    for (auto& r : st.songs) {
        if (! lib.song (r.song.id) && ! r.song.id.empty()) lib.songs.push_back (r.song);   // a song this device doesn't have yet
        SetlistEntry e; e.songId = r.song.id; e.bpmOverride = r.bpm; e.arrangement = r.arrangement; e.notes = r.notes;
        auto lk = localKey.find (r.song.id);
        if (lk != localKey.end() && ! followLeaderKey) { e.keyOverride = lk->second.first; e.capoOverride = lk->second.second; } else e.keyOverride = r.key;
        set.entries.push_back (e);
    }
    if (st.hasColors) { std::map<std::string, std::vector<std::string>> per;
        for (auto& r : st.songs) { auto& v = per[r.song.id]; for (auto& sec : r.song.chart.sections) v.push_back (sec.customColor); }
        live.setFollowColors (st.sectionColors, per); }
    const auto& cur = live.setlist(); bool same = cur.date == set.date && cur.name == set.name && cur.entries.size() == set.entries.size();
    for (size_t i = 0; same && i < set.entries.size(); ++i) same = cur.entries[i].songId == set.entries[i].songId && cur.entries[i].keyOverride == set.entries[i].keyOverride
        && cur.entries[i].arrangement == set.entries[i].arrangement && cur.entries[i].notes == set.entries[i].notes;
    if (! same) live.loadSetlist (set, st.songIndex, now); else live.followSong (st.songIndex, now);
    if (std::abs (live.bpm() - st.bpm) >= 0.05) live.setBpm (st.bpm);
    cue_.active = false;
    if (live.sectionIndex() != st.sectionIndex || live.pass() != st.pass || live.sectionPending()) live.followSection (st.sectionIndex, st.pass, now);
    if (st.pendingIdx >= 0) applyCue (st.pendingIdx, st.pendingPass, st.cueBeat, st.dueBeat, live, clock, now);
    play_.active = false;
    if (st.playing) {
        const double b = clock.beatAt (now);
        if (b < st.playBeat) play_ = { true, true, st.playBeat, st.countIn };
        else if (! live.playing()) live.followPlay (true, std::max (0, st.countIn - (int) std::floor (b - st.playBeat)), now);
    } else if (live.playing()) live.followPlay (false, 0, now);
}
void Follower::onLine (const std::string& line, LiveSession& live, Library& lib, const BeatClock& clock, double now) {
    Msg m; if (! decode (line, m)) return; const Json& j = m.body;
    if (m.type == "kick") { kicked_ = true; return; }
    if (m.type == "ping") { send_ (encode ("pong", -1, Json::object().set ("id", j["id"].num()))); return; }
    if (m.type == "snap") {
        session_ = j["session"].str(); last_ = m.seq; haveSnap_ = true; wantSnap_ = false;
        transport = mt::transportFromJson (j["mt"]);
        live.setFollowing (true); applySnapshot (stateFromJson (j["state"]), live, lib, clock, now); return;
    }
    if (! haveSnap_) return;                                                  // wait for the snapshot
    if (m.seq <= last_) return;                                               // duplicate
    if (m.seq != last_ + 1) { if (! wantSnap_) { wantSnap_ = true; ++resyncs_; send_ (encode ("resync", -1, Json::object())); } return; }
    last_ = m.seq;
    if (m.type == "song") { cue_.active = false; play_.active = false; live.followSong (j["song"].integer(), now); }
    else if (m.type == "section") { cue_.active = false; int i = j["idx"].integer(), p = j["pass"].integer (1);
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
    const double b = clock.beatAt (now);
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
