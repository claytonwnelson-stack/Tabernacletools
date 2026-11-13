// v0.5 tests: band sync protocol (format, sequencing, late-join snapshot, leader/follower over real 127.0.0.1 TCP),
// Link phase lock of the click grid, and projector lyrics (slides, state machine, layout).
#include "sync/Sync.h"
#include "sync/Net.h"
#include "sync/Discovery.h"
#include "sync/Roster.h"
#include "model/Projector.h"
#include "model/Chart.h"
#include "audio/Click.h"
#include <chrono>
#include <thread>
#include <cstdio>
using namespace wr; using namespace wr::sync;

static int fails = 0, checks = 0;
template <typename T> static std::string toS (const T& v) { if constexpr (std::is_same_v<T, std::string>) return v; else if constexpr (std::is_arithmetic_v<T>) return std::to_string (v); else return "?"; }
#define CHECK(c) do { ++checks; if (! (c)) { ++fails; std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_EQ(a, b) do { ++checks; auto _a = (a); auto _b = (b); if (! (_a == _b)) { ++fails; std::printf ("FAIL %s:%d  %s == %s\n   got [%s] want [%s]\n", __FILE__, __LINE__, #a, #b, toS (_a).c_str(), toS (_b).c_str()); } } while (0)

static const char* kSong = R"({title: Morning Song}
{artist: Test Band}
{key: G}
{tempo: 120}
{time: 4/4}
{start_of_verse: Verse 1}
[G]Light breaks over the [D]hills
[Em]We lift our [C]voices high
[G]Every heart awake [D]again
[Em]Singing a new song
[C]Singing all day long
{end_of_verse}
{start_of_tab: Riff}
e|-----3-----|
{end_of_tab}
{comment: Build}
{start_of_chorus: Chorus}
{bars: 4}
[G]Rise up, morning is here
[D]Open the door
[Em]Bright as the sunrise
[C]Joy evermore
{end_of_chorus}
{start_of_part: Instrumental}
G  D/F#  Em  C
{end_of_part}
{start_of_bridge: Bridge}
[G]Even when the night is long we wait for the day
{end_of_bridge}
)";

struct Sink : RigSink {
    std::vector<std::string> said; int countBeats = -1; double bpm = 0; int restarts = 0; bool clickOn = false, muted = false;
    void loadRig (const RigPreset&) override {} void applyScene (const Scene&) override {} void setPedal (const std::string&, bool) override {}
    void setBpm (double b) override { bpm = b; } void setClick (bool on, bool m) override { clickOn = on; muted = m; }
    void restartBeat() override { ++restarts; }
    void announceSection (const std::string& n, int) override { said.push_back (n); }
    bool cueDue (int seq) override { return dueSeq >= seq; } int dueSeq = -1;
    void countIn (int b, bool) override { countBeats = b; }
};
static Library leaderLib() {
    Library l = seedLibrary(); Song s; s.id = "morning-song"; s.chart = parseChart (kSong); l.songs.push_back (s);
    Setlist set; set.name = "Sunday AM"; set.date = "2026-11-08"; set.entries.push_back ({ "he-who-was", "", 0, "lead" }); set.entries.push_back ({ "morning-song", "", 0, "lead" });
    l.setlists = { set }; return l;
}
struct Band {   // leader + one follower wired directly (no sockets)
    Library LL = leaderLib(), FL = seedLibrary(); Sink ls, fs; LiveSession lead { LL, ls }, fol { FL, fs }; FakeClock clock;
    Leader leader { "S1" }; std::vector<std::string> toLeader; Follower follower { [this] (const std::string& l) { toLeader.push_back (l); }, "Keys" };
    bool dropNext = false;
    void join (double now) { follower.onConnected(); flushUp (now); }
    void flushUp (double now) { auto up = toLeader; toLeader.clear(); for (auto& l : up) { auto r = leader.onFollowerLine (l, lead, clock, now); if (! r.empty()) follower.onLine (r, fol, FL, clock, now); } }
    void step (double now) {   // leader app timer + follower app timer
        lead.tick (now); leader.tick (lead, clock, now);
        for (auto& l : leader.update (lead, clock, now)) { if (dropNext) { dropNext = false; continue; } follower.onLine (l, fol, FL, clock, now); }
        flushUp (now); follower.tick (fol, clock, now); fol.tick (now);
    }
};

static void testProtocol() {
    Msg m; auto line = encode ("cue", 7, Json::object().set ("idx", 2).set ("cueBeat", 12.0));
    CHECK (line.back() == '\n' && line.find ('\n') == line.size() - 1);
    CHECK (decode (line.substr (0, line.size() - 1), m)); CHECK_EQ (m.type, std::string ("cue")); CHECK_EQ ((int) m.seq, 7); CHECK_EQ (m.body["idx"].integer(), 2);
    CHECK (! decode ("{\"t\":\"cue\",\"v\":99}", m)); CHECK (! decode ("garbage", m)); CHECK (! decode ("{\"v\":1}", m)); CHECK (! decode ("[1,2]", m));
    LineBuffer lb; std::vector<std::string> got; auto on = [&] (const std::string& s) { got.push_back (s); };
    lb.feed ("ab", 2, on); lb.feed ("c\nde\n\nf", 7, on); CHECK_EQ ((int) got.size(), 3); CHECK_EQ (got[0], std::string ("abc")); CHECK_EQ (got[1], std::string ("de")); CHECK_EQ (lb.buf, std::string ("f"));
    CHECK_EQ (nextBarBeat (1.3, 4), 4.0); CHECK_EQ (nextBarBeat (4.0, 4), 8.0); CHECK_EQ (nextBarBeat (5.9, 3), 6.0); CHECK_EQ (nextBarBeat (-0.5, 4), 0.0);
    // state snapshot round trip
    Band b; b.lead.start (b.LL.setlists[0], 1, 0); State s = b.leader.capture (b.lead, b.clock, 0); s.pendingIdx = 2; s.cueBeat = 8; s.dueBeat = 12; s.playing = true; s.countIn = 8;
    State r = stateFromJson (Json::parse (toJson (s).dump (0)));
    CHECK_EQ ((int) r.songs.size(), 2); CHECK_EQ (r.songs[1].song.chart.title, std::string ("Morning Song")); CHECK_EQ (r.songs[1].key, std::string ("G")); CHECK_EQ (r.songIndex, 1);
    CHECK_EQ (r.pendingIdx, 2); CHECK_EQ (r.dueBeat, 12.0); CHECK (r.playing); CHECK_EQ (r.countIn, 8); CHECK_EQ (r.songs[1].bpm, 120.0);
    CHECK_EQ (std::string (serviceType()), std::string ("_tabernacle._tcp"));
}

static void testLeaderFollower() {
    Band b; b.follower.localKey["morning-song"] = { "A", 2 };           // the keys player reads it in A with capo 2 shapes... their call
    b.lead.start (b.LL.setlists[0], 1, 0); b.lead.setClickOn (true);
    b.step (0.0); CHECK (b.follower.banner (0) != "");                // not connected yet
    b.join (0.0);
    CHECK (b.follower.synced()); CHECK (b.fol.following()); CHECK_EQ (b.follower.banner (0), std::string());
    CHECK_EQ (b.fol.songIndex(), 1); CHECK (b.fol.song() && b.fol.song()->chart.title == "Morning Song"); CHECK (b.FL.song ("morning-song") != nullptr); // imported
    CHECK_EQ (b.lead.key(), std::string ("G")); CHECK_EQ (b.fol.key(), std::string ("A")); CHECK_EQ (b.fol.capo(), 2);  // own key + capo kept
    CHECK_EQ (b.fol.bpm(), 120.0);
    b.fol.toggleClickMute(); const bool fm = b.fol.clickMuted(); b.fol.handle ({ Action::ToggleView }, 0.1);

    // section cue: leader presses NEXT at beat 1.3 -> cue bar = beats 4..8 on the Link timeline, change at beat 8
    double t = 0.65; b.lead.handle ({ Action::NextSection }, t); CHECK (b.lead.sectionPending());
    b.step (t);
    CHECK (b.fol.sectionPending()); CHECK_EQ (b.fol.pendingSection(), 1); CHECK_EQ (b.fol.sectionIndex(), 0);
    CHECK_EQ ((int) b.fs.said.size(), 1); if (! b.fs.said.empty()) CHECK_EQ (b.fs.said[0], std::string ("Chorus"));   // spoken by the follower's own click
    CHECK_EQ ((int) b.ls.said.size(), 1); if (! b.ls.said.empty()) CHECK_EQ (b.ls.said[0], b.fs.said[0]);             // same words as the leader, same cue bar
    State st = b.leader.capture (b.lead, b.clock, t); CHECK_EQ (st.cueBeat, 4.0); CHECK_EQ (st.dueBeat, 8.0);
    b.fol.handle ({ Action::NextSection }, t); b.fol.handle ({ Action::NextSong }, t); CHECK_EQ (b.fol.pendingSection(), 1); CHECK_EQ (b.fol.songIndex(), 1); // local nav ignored
    b.step (3.95); CHECK_EQ (b.fol.sectionIndex(), 0);                // beat 7.9: still in the cue bar
    b.ls.dueSeq = 99; b.step (4.0);                                   // beat 8: leader's click grid says due; both land together
    CHECK_EQ (b.lead.sectionIndex(), 1); CHECK_EQ (b.fol.sectionIndex(), 1); CHECK (! b.fol.sectionPending());
    CHECK_EQ (b.fol.clickMuted(), fm); CHECK (b.fol.view() == LiveSession::View::Section);   // follower's own click mute / view untouched

    // tempo: leader changes, Link + follower follow
    b.lead.setBpm (132); b.step (4.1); CHECK_EQ (b.clock.tempo(), 132.0); CHECK_EQ (b.fol.bpm(), 132.0); CHECK_EQ (b.fs.bpm, 132.0);
    // play: quantised to the next Link bar; count-in starts together on every device
    const double tp = 4.5; const double beatNow = b.clock.beatAt (tp);
    auto line = b.leader.requestPlay (b.lead, b.clock, tp, true); b.follower.onLine (line, b.fol, b.FL, b.clock, tp);
    const double at = b.leader.scheduledPlayBeat(); CHECK (at > beatNow && std::fmod (at, 4.0) == 0.0);
    CHECK (! b.lead.playing()); CHECK (! b.fol.playing());
    const double tAt = b.clock.t0 + (at - b.clock.b0) * 60.0 / b.clock.bpm;
    b.step (tAt - 0.01); CHECK (! b.lead.playing() && ! b.fol.playing());
    b.step (tAt + 0.001); CHECK (b.lead.playing() && b.fol.playing()); CHECK_EQ (b.ls.countBeats, 8); CHECK_EQ (b.fs.countBeats, 8);
    b.step (tAt + 0.05); CHECK (b.fol.playing());
    line = b.leader.requestPlay (b.lead, b.clock, tAt + 0.2, false); b.follower.onLine (line, b.fol, b.FL, b.clock, tAt + 0.2); CHECK (! b.fol.playing() && ! b.lead.playing());

    // sequencing: a lost event -> gap -> resync -> snapshot; duplicates dropped
    const long long before = b.follower.lastSeq(); double t2 = tAt + 1; b.ls.dueSeq = -1;
    b.dropNext = true; b.lead.handle ({ Action::NextSection }, t2); b.step (t2);             // cue event lost
    CHECK (! b.fol.sectionPending()); CHECK_EQ (b.follower.lastSeq(), before);
    b.lead.setBpm (128); b.step (t2 + 0.01);                                                // next event -> gap detected
    CHECK_EQ (b.follower.resyncs(), 1); CHECK (b.fol.sectionPending()); CHECK_EQ (b.fol.bpm(), 128.0); CHECK_EQ (b.follower.lastSeq(), b.leader.seq()); // healed by the snapshot
    auto dup = encode ("tempo", b.leader.seq(), Json::object().set ("bpm", 60.0)); b.follower.onLine (dup, b.fol, b.FL, b.clock, t2); CHECK_EQ (b.fol.bpm(), 128.0);

    // late join in the middle of a cue bar: shows the cue (no voice - the bar already started), lands with everyone
    { Library F2 = seedLibrary(); Sink s2; LiveSession f2 (F2, s2); std::vector<std::string> up; Follower late ([&] (const std::string& l) { up.push_back (l); }, "Projector");
      State cs = b.leader.capture (b.lead, b.clock, t2); const double tMid = b.clock.t0 + (cs.cueBeat + 1.5 - b.clock.b0) * 60.0 / b.clock.bpm;
      late.onConnected(); for (auto& l : up) late.onLine (b.leader.onFollowerLine (l, b.lead, b.clock, tMid), f2, F2, b.clock, tMid);
      CHECK (f2.sectionPending()); CHECK_EQ (f2.pendingSection(), b.lead.pendingSection()); CHECK (s2.said.empty()); CHECK_EQ (f2.key(), std::string ("G")); // leader's key (no local override)
      const double tDue = b.clock.t0 + (cs.dueBeat - b.clock.b0) * 60.0 / b.clock.bpm; late.tick (f2, b.clock, tDue); CHECK (! f2.sectionPending()); CHECK_EQ (f2.sectionIndex(), cs.pendingIdx);
      // joining after the change already landed: lands straight away
      Library F3 = seedLibrary(); Sink s3; LiveSession f3 (F3, s3); up.clear(); Follower later ([&] (const std::string& l) { up.push_back (l); });
      later.onConnected(); for (auto& l : up) later.onLine (b.leader.onFollowerLine (l, b.lead, b.clock, tDue + 1), f3, F3, b.clock, tDue + 1);
      CHECK (! f3.sectionPending()); CHECK_EQ (f3.sectionIndex(), cs.pendingIdx); }

    // dropped connection: follower holds its section and shows the banner, then rejoins with a snapshot
    double t3 = t2 + 5; b.ls.dueSeq = 999; b.step (t3); const int held = b.fol.sectionIndex();
    b.follower.onDisconnected (t3); CHECK (! b.follower.connected()); CHECK (b.follower.banner (t3).find ("LEADER LOST") != std::string::npos);
    b.lead.handle ({ Action::GotoSection, 0 }, t3); b.lead.handle ({ Action::NextSong }, t3 + 0.1); b.leader.update (b.lead, b.clock, t3 + 0.1); // follower misses these
    b.fol.tick (t3 + 1); CHECK_EQ (b.fol.sectionIndex(), held); CHECK (b.fol.following());
    b.join (t3 + 2); CHECK (b.follower.synced()); CHECK_EQ (b.fol.songIndex(), b.lead.songIndex()); CHECK_EQ (b.fol.sectionIndex(), b.lead.sectionIndex());

    // song change + a new setlist from the leader
    b.lead.handle ({ Action::PrevSong }, t3 + 3); b.step (t3 + 3); CHECK_EQ (b.fol.songIndex(), 0); CHECK_EQ (b.fol.song()->id, std::string ("he-who-was"));
    Setlist other; other.name = "Wednesday"; other.date = "2026-11-11"; other.entries.push_back ({ "morning-song", "A", 0, "lead" });
    b.lead.start (other, 0, t3 + 4); b.step (t3 + 4); CHECK_EQ (b.fol.setlist().name, std::string ("Wednesday")); CHECK_EQ ((int) b.fol.setlist().entries.size(), 1);
    CHECK_EQ (b.fol.key(), std::string ("A")); // its local override (A, capo 2) still applies
}

static void pumpUntil (const std::function<bool()>& done, const std::function<void (double)>& step, double maxSec) {
    static const auto base = std::chrono::steady_clock::now();   // one monotonic clock for the whole run
    auto secs = [] { return std::chrono::duration<double> (std::chrono::steady_clock::now() - base).count() + 100.0; };
    const double start = secs();
    for (;;) { double now = secs(); step (now); if (done() || now - start > maxSec) break; std::this_thread::sleep_for (std::chrono::milliseconds (2)); }
}
static void testTcp() {
    Library LL = leaderLib(); Sink ls; LiveSession lead (LL, ls); lead.start (LL.setlists[0], 1, 0); lead.setClickOn (true);
    FakeClock clock; Leader leader ("TCP1"); Server srv; CHECK (srv.listen (0)); const int port = srv.port(); CHECK (port > 0);
    struct F { Library lib = seedLibrary(); Sink sink; LiveSession live { lib, sink }; Client cli; std::unique_ptr<Follower> f; };
    std::vector<std::unique_ptr<F>> fs; const char* names[] = { "Keys", "Bass", "Projector" };
    for (int i = 0; i < 3; ++i) { auto x = std::make_unique<F>(); F* p = x.get(); x->f = std::make_unique<Follower> ([p] (const std::string& l) { p->cli.send (l); }, names[i]); x->cli.connectTo ("127.0.0.1", port); fs.push_back (std::move (x)); }
    Server::Events sev; sev.line = [&] (int id, const std::string& l) { auto r = leader.onFollowerLine (l, lead, clock, 0); if (! r.empty()) srv.send (id, r); };
    auto step = [&] (double now) {
        srv.poll (now, sev); lead.tick (now); for (auto& l : leader.update (lead, clock, now)) srv.broadcast (l);
        for (auto& x : fs) { F* p = x.get(); Client::Events ce; ce.connected = [p] { p->f->onConnected(); }; ce.disconnected = [p, now] { p->f->onDisconnected (now); };
            ce.line = [p, &clock, now] (const std::string& l) { p->f->onLine (l, p->live, p->lib, clock, now); }; p->cli.poll (now, ce); p->f->tick (p->live, clock, now); }
    };
    auto allSynced = [&] { for (auto& x : fs) if (! x->f->synced()) return false; return true; };
    pumpUntil (allSynced, step, 3.0);
    CHECK (allSynced()); CHECK_EQ (srv.clients(), 3);
    { auto n = srv.clientNames(); std::sort (n.begin(), n.end()); CHECK (n.size() == 3 && n[0] == "Bass" && n[1] == "Keys" && n[2] == "Projector"); }
    for (auto& x : fs) { CHECK_EQ (x->live.songIndex(), 1); CHECK_EQ (x->live.song() ? x->live.song()->id : std::string(), std::string ("morning-song")); }
    lead.handle ({ Action::JumpChorus }, 0); // cue to the chorus
    pumpUntil ([&] { for (auto& x : fs) if (! x->live.sectionPending()) return false; return true; }, step, 2.0);
    for (auto& x : fs) { CHECK (x->live.sectionPending()); CHECK_EQ (x->live.pendingSection(), lead.pendingSection()); CHECK (x->sink.said.size() == 1 && x->sink.said[0] == "Chorus"); }
    // leader goes away (router reboot / app killed): banner + hold; leader back on the same port -> automatic rejoin
    const int held = fs[0]->live.sectionIndex(); srv.close();
    pumpUntil ([&] { for (auto& x : fs) if (x->f->connected()) return false; return true; }, step, 6.0);
    for (auto& x : fs) { CHECK (! x->f->connected()); CHECK (x->f->banner (0).find ("LEADER LOST") != std::string::npos); }
    CHECK_EQ (fs[0]->live.sectionIndex(), held);
    lead.handle ({ Action::NextSong }, 0); lead.handle ({ Action::PrevSong }, 0); lead.setBpm (90); leader.update (lead, clock, 0);
    CHECK (srv.listen (port));
    pumpUntil (allSynced, step, 6.0);
    CHECK (allSynced()); for (auto& x : fs) { CHECK_EQ (x->live.bpm(), 90.0); CHECK_EQ (x->live.songIndex(), 0); }
    // one follower leaves; the others stay
    fs[2]->cli.disconnect(); pumpUntil ([&] { return srv.clients() == 2; }, step, 3.0); CHECK_EQ (srv.clients(), 2);
    lead.handle ({ Action::NextSong }, 0); pumpUntil ([&] { return fs[0]->live.songIndex() == 1 && fs[1]->live.songIndex() == 1; }, step, 2.0);
    CHECK_EQ (fs[0]->live.songIndex(), 1); CHECK_EQ (fs[1]->live.songIndex(), 1);
    Discovery d; CHECK (! Discovery::available() || d.browse()); // Bonjour itself is exercised on device
}

static void testLinkLock() {
    const double fs = 48000; ClickGen::Params p; p.bpm = 120; p.on = true; p.gain = 0; p.cueGain = 0; p.beatsPerBar = 4;
    FakeClock clock; clock.bpm = 120;
    ClickGen a, b; a.prepare (fs); b.prepare (fs); const int blk = 256; std::vector<float> buf (blk);
    long long sa = 0, sb = 0; int reseats = 0;
    auto run = [&] (ClickGen& g, long long& s, int blocks) { for (int i = 0; i < blocks; ++i) { if (g.lockTo (clock.beatAt ((double) s / fs), 4)) ++reseats; std::fill (buf.begin(), buf.end(), 0.0f); g.process (buf.data(), buf.data(), blk, p); s += blk; } };
    run (a, sa, 100);                         // a has been in the session for a while
    const long long bStart = sa + 7919; sb = bStart; run (b, sb, 1);   // b joins mid-beat, 7919 samples "later" on the shared clock
    CHECK (reseats >= 1);
    sa = bStart; run (a, sa, 1);              // a over the same block of time
    CHECK (std::abs (a.position() - b.position()) < 2e-3);
    CHECK (std::abs (b.position() - std::fmod (clock.beatAt ((double) sb / fs), 4.0)) < 2e-3);
    // both keep counting the same bars/beats from here, block by block, with a slightly fast local clock on b (drift)
    long long ba = a.beats(), bb = b.beats(); int r0 = reseats;
    for (int i = 0; i < 400; ++i) { run (a, sa, 1); clock.bpm = 120; run (b, sb, 1); }
    CHECK_EQ (a.beats() - ba, b.beats() - bb); CHECK_EQ (reseats, r0); CHECK (std::abs (a.position() - b.position()) < 2e-3);
    // tiny drift is trimmed without re-seating
    ClickGen c; c.prepare (fs); long long sc = sa; run (c, sc, 1); r0 = reseats; p.bpm = 120.6; run (c, sc, 50); p.bpm = 120; CHECK_EQ (reseats, r0);
    CHECK (std::abs (c.position() - std::fmod (clock.beatAt ((double) sc / fs), 4.0)) < 0.02);
}

static void testProjector() {
    Chart c = parseChart (kSong); ProjectorSettings st;
    ChartLine l; l.lyric = "Holy [D]is the Lord"; CHECK_EQ (lyricOnly (l), std::string ("Holy is the Lord"));
    l.lyric = "G  D/F#  Em  C"; CHECK_EQ (lyricOnly (l), std::string());  l.lyric = "| G | D |"; CHECK_EQ (lyricOnly (l), std::string());
    l.lyric = "  A   new   day  "; CHECK_EQ (lyricOnly (l), std::string ("A new day"));
    CHECK_EQ ((int) c.sections.size(), 4);
    auto v = sectionLyrics (c.sections[0]); CHECK_EQ ((int) v.size(), 5); CHECK_EQ (v[0], std::string ("Light breaks over the hills")); // chords stripped
    for (auto& sec : c.sections) for (auto& x : sectionLyrics (sec)) { CHECK (x.find ('[') == std::string::npos); CHECK (x.find ("e|") == std::string::npos); CHECK (x != "Build"); }
    auto s2 = sectionSlides (c.sections[0], 0, 2); CHECK_EQ ((int) s2.size(), 3); CHECK (s2[0].lines.size() == 2 && s2[1].lines.size() == 2 && s2[2].lines.size() == 1); CHECK_EQ (s2[2].count, 3);
    auto s4 = sectionSlides (c.sections[0], 0, 4); CHECK_EQ ((int) s4.size(), 2); CHECK (s4[0].lines.size() == 3 && s4[1].lines.size() == 2); // balanced, not 4+1
    CHECK (sectionSlides (c.sections[2], 2, 2).empty());  // instrumental: chords only
    auto all = songSlides (c, st); CHECK (all[0].kind == Slide::Title && all[0].lines[0] == "Morning Song" && all[0].lines[1] == "Test Band"); CHECK_EQ ((int) all.size(), 1 + 3 + 2 + 1);
    st.titleSlide = false; CHECK_EQ ((int) songSlides (c, st).size(), 6); st.titleSlide = true;

    // state machine on a live session
    Library L = leaderLib(); Sink sk; LiveSession live (L, sk); live.start (L.setlists[0], 1, 0); ProjectorState P;
    P.update (live, st); CHECK (P.current().kind == Slide::Title);
    st.titleSlide = false; P.update (live, st); CHECK (P.current().kind == Slide::Blank); st.titleSlide = true;
    live.handle ({ Action::NextSection }, 1); P.update (live, st); CHECK (P.current().kind == Slide::Lyrics && P.current().section == 1 && P.current().index == 0);
    CHECK_EQ (P.current().lines[0], std::string ("Rise up, morning is here"));
    P.nextSlide(); P.update (live, st); CHECK_EQ (P.current().index, 1); P.nextSlide(); P.update (live, st); CHECK_EQ (P.current().index, 1); // clamps at last
    P.prevSlide(); P.prevSlide(); P.update (live, st); CHECK_EQ (P.current().index, 0);
    live.handle ({ Action::GotoSection, 0 }, 2); P.update (live, st); CHECK (P.current().section == 0 && P.current().index == 0);
    st.uppercase = true; P.update (live, st); CHECK_EQ (P.current().lines[0], std::string ("LIGHT BREAKS OVER THE HILLS")); st.uppercase = false;
    P.setBlank (true); P.update (live, st); CHECK (P.current().kind == Slide::Blank); P.setBlank (false);
    live.handle ({ Action::GotoSection, 2 }, 3); P.update (live, st); CHECK (P.current().kind == Slide::Blank); // instrumental: blank
    // bar-based auto-advance: chorus has 4 bars and 2 slides -> slide 2 from bar 3
    Library L2 = leaderLib(); L2.settings.countIn = 0; L2.settings.quantizeSections = false; Sink sk2; LiveSession lv (L2, sk2); lv.start (L2.setlists[0], 1, 0); ProjectorState Q;
    lv.handle ({ Action::GotoSection, 1 }, 0); lv.setPlaying (true, 0); lv.tick (0); Q.update (lv, st); CHECK_EQ (Q.current().index, 0);
    lv.tick (3.9); Q.update (lv, st); CHECK_EQ (Q.current().index, 0); lv.tick (4.1); Q.update (lv, st); CHECK_EQ (Q.current().index, 1); // 120 bpm: bar 3 at 4 s
    // a new song starts on its title slide; running off the end of the song -> blank between songs
    lv.handle ({ Action::GotoSection, 3 }, 9); lv.setPlaying (true, 9); Q.update (lv, st); lv.setPlaying (false, 10); Q.update (lv, st); CHECK (Q.current().kind == Slide::Blank);
    lv.handle ({ Action::PrevSong }, 11); Q.update (lv, st); CHECK (Q.current().kind == Slide::Title); CHECK_EQ (Q.current().lines[0], std::string ("He who was / He who is"));
    // projector as a follower of the leader: lands on the chorus slide with the band
    { Band b; b.lead.start (b.LL.setlists[0], 1, 0); b.lead.setClickOn (true); b.join (0); ProjectorState pj; ProjectorSettings ps;
      pj.update (b.fol, ps); CHECK (pj.current().kind == Slide::Title);
      b.lead.handle ({ Action::JumpChorus }, 0.1); b.step (0.1); pj.update (b.fol, ps); CHECK (pj.current().kind == Slide::Title); // cue bar: not yet
      b.ls.dueSeq = 99; b.step (4.0); pj.update (b.fol, ps); CHECK (pj.current().kind == Slide::Lyrics && pj.current().section == 1); }
    // layout: wraps long lines, shrinks to fit, centres vertically, titles bigger
    MeasureFn m = [] (const std::string& s, float px) { return (float) s.size() * px * 0.55f; };
    Slide sl; sl.kind = Slide::Lyrics; sl.lines = { "Light breaks over the hills", "We lift our voices high" };
    auto lay = layoutSlide (sl, 1920, 1080, st, m); CHECK (lay.fontPx > 80); CHECK (lay.lines.size() >= 2);
    for (auto& x : lay.lines) CHECK (m (x, lay.fontPx) <= 1920 * 0.9f + 0.5f);
    CHECK (lay.baselines.front() > 1080 * 0.25f && lay.baselines.back() < 1080 * 0.8f);
    Slide big = sl; for (int i = 0; i < 6; ++i) big.lines.push_back ("Another very long lyric line that goes on and on");
    auto lb = layoutSlide (big, 1920, 1080, st, m); CHECK (lb.shrunk && lb.fontPx < lay.fontPx); CHECK (lb.lines.size() * lb.fontPx * 1.25f <= 1080 * 0.86f + 1);
    Slide ti; ti.kind = Slide::Title; ti.lines = { "Morning Song" }; CHECK (layoutSlide (ti, 1920, 1080, st, m).fontPx > lay.fontPx);
    ProjectorSettings xl; xl.fontSize = 3; CHECK (layoutSlide (sl, 1920, 1080, xl, m).fontPx >= lay.fontPx);
    CHECK (layoutSlide (Slide(), 1920, 1080, st, m).lines.empty());
    // settings persist
    Library ls2 = seedLibrary(); ls2.settings.projector.linesPerSlide = 4; ls2.settings.projector.background = "/bg.jpg"; ls2.settings.projector.fontSize = 3; ls2.settings.deviceName = "Bass iPad"; ls2.settings.lastLeaderHost = "10.0.0.2";
    Library r = libraryFromJson (toJson (ls2)); CHECK_EQ (r.settings.projector.linesPerSlide, 4); CHECK_EQ (r.settings.projector.background, std::string ("/bg.jpg"));
    CHECK_EQ (r.settings.projector.fontSize, 3); CHECK_EQ (r.settings.deviceName, std::string ("Bass iPad")); CHECK_EQ (r.settings.lastLeaderHost, std::string ("10.0.0.2"));
    CHECK_EQ (seedLibrary().settings.projector.linesPerSlide, 2);
}


static Msg mk (const std::string& t, Json body) { Msg m; decode (encode (t, -1, std::move (body)), m); return m; }
static void testRoster() {
    Roster R; const std::string H = "morning-song|1|1"; const double bpm = 120;
    auto hb = [&] (const std::string& hash, long long last, double beat) { return mk ("hb", Json::object().set ("name", "Keys iPad").set ("role", "player").set ("hash", hash).set ("last", (double) last).set ("beat", beat).set ("q", 4).set ("section", "Chorus")); };
    R.connected (1, 0); auto ms = R.members(); CHECK_EQ ((int) ms.size(), 1); CHECK (ms[0].status == Member::Yellow); CHECK_EQ (R.connectedCount(), 1);
    R.line (1, mk ("hello", Json::object().set ("name", "Keys iPad").set ("role", "player")), H, 5, 0, bpm, 0.01);
    auto t = R.takeToasts(); CHECK (t.size() == 1 && t[0].text == "Keys iPad joined" && t[0].severity == 0); CHECK (R.takeToasts().empty());
    R.pingSent (7, 1.0); R.line (1, mk ("pong", Json::object().set ("id", 7.0)), H, 5, 2, bpm, 1.012); CHECK (std::abs (R.members()[0].rttMs - 12) < 0.5);
    R.line (1, hb (H, 5, 2.0 - 0.006 * 2), H, 5, 2.0, bpm, 1.1);   // beat as of half a round trip ago -> 0 ms error
    ms = R.members(); CHECK (ms[0].status == Member::Green); CHECK_EQ (ms[0].name, std::string ("Keys iPad")); CHECK_EQ (ms[0].section, std::string ("Chorus")); CHECK (std::abs (ms[0].beatErrMs) < 2);
    // different section: tolerated for the grace period (change in flight), then yellow; recovers to green
    R.line (1, hb ("morning-song|0|1", 5, 4.0), H, 5, 4.0, bpm, 2.0); CHECK (R.members()[0].status == Member::Green);
    R.tick (H, 5, 3.0); CHECK (R.members()[0].status == Member::Green);
    R.line (1, hb ("morning-song|0|1", 5, 7.0), H, 5, 7.0, bpm, 3.6); ms = R.members(); CHECK (ms[0].status == Member::Yellow); CHECK_EQ (ms[0].reason, std::string ("showing a different section"));
    R.line (1, hb (H, 5, 8.0), H, 5, 8.0, bpm, 4.1); CHECK (R.members()[0].status == Member::Green);
    // behind on updates
    R.line (1, hb (H, 3, 9.0), H, 5, 9.0, bpm, 4.6); R.tick (H, 5, 6.2); R.line (1, hb (H, 3, 12.2), H, 5, 12.2, bpm, 6.2);
    ms = R.members(); CHECK (ms[0].status == Member::Yellow); CHECK_EQ (ms[0].reason, std::string ("behind 2 update(s)"));
    R.line (1, hb (H, 5, 13.0), H, 5, 13.0, bpm, 6.6); CHECK (R.members()[0].status == Member::Green);
    // Link beat 0.1 beat late (50 ms at 120) -> yellow after grace; a whole bar apart is still "in phase"
    R.line (1, hb (H, 5, 14.0 - 0.012 - 0.1), H, 5, 14.0, bpm, 7.0); R.line (1, hb (H, 5, 17.2 - 0.012 - 0.1), H, 5, 17.2, bpm, 8.6);
    ms = R.members(); CHECK (ms[0].status == Member::Yellow); CHECK (std::abs (ms[0].beatErrMs + 50) < 2); CHECK_EQ (ms[0].reason, std::string ("beat -50 ms"));
    R.line (1, hb (H, 5, 22.0 - 0.012 + 4.0), H, 5, 22.0, bpm, 9.0); CHECK (R.members()[0].status == Member::Green);
    // heartbeat late -> yellow at once; silent 6 s -> red + "dropped" toast; removed after 30 s
    R.tick (H, 5, 11.6); ms = R.members(); CHECK (ms[0].status == Member::Yellow); CHECK (ms[0].reason.rfind ("no heartbeat", 0) == 0);
    R.tick (H, 5, 15.1); ms = R.members(); CHECK (ms[0].status == Member::Red && ! ms[0].connected); CHECK_EQ (R.connectedCount(), 0);
    t = R.takeToasts(); CHECK (t.size() == 1 && t[0].text == "Keys iPad dropped" && t[0].severity == 1);
    R.line (1, hb (H, 5, 0), H, 5, 0, bpm, 16); CHECK (R.members()[0].status == Member::Red);   // stale socket data after the drop is ignored
    R.tick (H, 5, 44); CHECK_EQ ((int) R.members().size(), 1); R.tick (H, 5, 46); CHECK (R.members().empty());
    // socket closed -> red right away; kick -> "removed", no "dropped" toast; ordering: connected first
    R.connected (2, 50); R.line (2, mk ("hello", Json::object().set ("name", "Projector").set ("role", "projector")), H, 5, 0, bpm, 50);
    R.connected (3, 51); R.line (3, mk ("hello", Json::object().set ("name", "Bass")), H, 5, 0, bpm, 51); R.takeToasts();
    R.disconnected (2, 52); ms = R.members(); CHECK (ms.size() == 2 && ms[0].name == "Bass" && ms[1].name == "Projector" && ms[1].status == Member::Red && ms[1].role == "projector");
    t = R.takeToasts(); CHECK (t.size() == 1 && t[0].text == "Projector dropped");
    R.kicked (3, 53); t = R.takeToasts(); CHECK (t.size() == 1 && t[0].text == "Bass removed"); { auto mm = R.members(); auto it = std::find_if (mm.begin(), mm.end(), [] (const Member& x) { return x.name == "Bass"; }); CHECK (it != mm.end() && it->reason == "removed"); }
    R.disconnected (3, 53.1); CHECK (R.takeToasts().empty());
    CHECK_EQ (stateHash (LiveSession (*new Library (seedLibrary()), *new Sink())), std::string ("-|0|1")); // nothing loaded
}

static void testRosterTcp() {
    Library LL = leaderLib(); Sink ls; LiveSession lead (LL, ls); lead.start (LL.setlists[0], 1, 0); lead.setClickOn (true);
    FakeClock clock; Leader leader ("R1"); Server srv; CHECK (srv.listen (0)); Roster R;
    struct F { Library lib = seedLibrary(); Sink sink; LiveSession live { lib, sink }; Client cli; std::unique_ptr<Follower> f; };
    std::vector<std::unique_ptr<F>> fs;
    for (int i = 0; i < 2; ++i) { auto x = std::make_unique<F>(); F* p = x.get(); x->f = std::make_unique<Follower> ([p] (const std::string& l) { p->cli.send (l); }, i ? "Projector" : "Keys iPad");
        if (i) x->f->role = "projector"; x->cli.connectTo ("127.0.0.1", srv.port()); fs.push_back (std::move (x)); }
    double clockNow = 0; std::vector<std::string> toasts;
    auto step = [&] (double now) {
        clockNow = now; const std::string h = stateHash (lead);
        Server::Events sev; sev.connected = [&] (int id) { R.connected (id, now); }; sev.disconnected = [&] (int id) { R.disconnected (id, now); }; sev.pinged = [&] (long long id) { R.pingSent (id, now); };
        sev.line = [&] (int id, const std::string& l) { Msg m; if (decode (l, m)) R.line (id, m, h, leader.seq(), clock.beatAt (now), lead.bpm(), now);
            auto r = leader.onFollowerLine (l, lead, clock, now); if (! r.empty()) srv.send (id, r); };
        srv.poll (now, sev); lead.tick (now); for (auto& l : leader.update (lead, clock, now)) srv.broadcast (l); R.tick (stateHash (lead), leader.seq(), now);
        for (auto& t : R.takeToasts()) toasts.push_back (t.text);
        for (auto& x : fs) { F* p = x.get(); Client::Events ce; ce.connected = [p] { p->f->onConnected(); }; ce.disconnected = [p, now] { p->f->onDisconnected (now); };
            ce.line = [p, &clock, now] (const std::string& l) { p->f->onLine (l, p->live, p->lib, clock, now); }; p->cli.poll (now, ce); p->f->tick (p->live, clock, now);
            if (p->f->wasKicked()) p->cli.disconnect(); }
    };
    auto allGreen = [&] { auto m = R.members(); if (m.size() != 2) return false; for (auto& x : m) if (x.status != Member::Green || x.rttMs < 0) return false; return true; };
    pumpUntil (allGreen, step, 4.0);
    auto m = R.members(); CHECK (allGreen());
    if (m.size() == 2) { CHECK_EQ (m[0].name, std::string ("Keys iPad")); CHECK_EQ (m[0].role, std::string ("player")); CHECK_EQ (m[1].role, std::string ("projector"));
        CHECK_EQ (m[0].section, std::string ("Verse 1")); CHECK (m[0].rttMs >= 0 && m[0].rttMs < 100); }
    CHECK (std::count (toasts.begin(), toasts.end(), std::string ("Keys iPad joined")) == 1 && std::count (toasts.begin(), toasts.end(), std::string ("Projector joined")) == 1);
    // the leader moves on; both followers follow -> still green, showing the new section
    lead.handle ({ Action::GotoSection, 3 }, clockNow); ls.dueSeq = 99;
    pumpUntil ([&] { auto mm = R.members(); return mm.size() == 2 && mm[0].section == "Bridge" && mm[1].section == "Bridge" && allGreen(); }, step, 3.0);
    m = R.members(); CHECK (m.size() == 2 && m[0].section == "Bridge" && m[1].status == Member::Green);
    // a follower gets out of step (stuck on another section) -> yellow after the grace period
    fs[0]->live.followSection (0, 1, 0);
    pumpUntil ([&] { return R.members()[0].status == Member::Yellow; }, step, 4.0);
    m = R.members(); CHECK (m[0].status == Member::Yellow); CHECK_EQ (m[0].reason, std::string ("showing a different section")); CHECK (m[1].status == Member::Green);
    // kick the projector: its banner says removed, it doesn't reconnect, roster shows it red then forgets it
    const int pid = m[1].id; srv.kick (pid); R.kicked (pid, clockNow);
    pumpUntil ([&] { return srv.clients() == 1; }, step, 3.0);
    CHECK_EQ (srv.clients(), 1); CHECK (fs[1]->f->wasKicked()); CHECK (fs[1]->f->banner (0).find ("Removed") != std::string::npos);
    pumpUntil ([] { return false; }, step, 1.2); CHECK_EQ (srv.clients(), 1); CHECK (! fs[1]->cli.connected());
    m = R.members(); CHECK (m.size() == 2 && m[1].status == Member::Red && m[1].reason == "removed"); CHECK_EQ (R.connectedCount(), 1);
    CHECK (std::count (toasts.begin(), toasts.end(), std::string ("Projector removed")) == 1); CHECK (std::count (toasts.begin(), toasts.end(), std::string ("Projector dropped")) == 0);
    // the other follower's app dies -> red + "dropped"
    fs[0]->cli.disconnect(); pumpUntil ([&] { return R.connectedCount() == 0; }, step, 8.0);
    CHECK_EQ (R.connectedCount(), 0); CHECK (std::count (toasts.begin(), toasts.end(), std::string ("Keys iPad dropped")) == 1);
}

static void testClockLock() {
    auto even = ntpReading (10.0, 10.02, 10.02, 10.04);
    CHECK (even.ok); CHECK (std::abs (even.offsetSec) < 1e-9); CHECK (std::abs (even.rttSec - 0.04) < 1e-9);
    auto ahead = ntpReading (12.0, 10.02, 10.03, 12.05);   // follower clock is 2 s ahead, ~20 ms each way
    CHECK (ahead.ok); CHECK (std::abs (ahead.offsetSec + 2.0) < 0.02); CHECK (ahead.rttSec > 0.03 && ahead.rttSec < 0.06);
    CHECK (! ntpReading (1, 2, 1, 1.5).ok);               // negative round trip is dropped
    ClockLock lock;
    CHECK (lock.observe (12.0, 10.02, 10.03, 12.05, 4.0, 120, 12.05));
    CHECK (lock.ready()); CHECK (std::abs (lock.leaderBeatAt (12.05) - 4.0) < 0.05);
    const double before = lock.beatAt (12.05);
    lock.steer (12.10);
    CHECK (std::abs (lock.beatAt (12.10) - before) < 0.2);   // no multi-beat jump while settling
    double err = 1;
    for (int i = 0; i < 40; ++i) { lock.observe (20.0 + i, 18.02 + i, 18.03 + i, 20.06 + i, 8.0 + i * 120.0 / 60.0, 120, 20.06 + i); lock.steer (20.06 + i); err = std::abs (lock.errorBeats()); }
    CHECK (err < 0.05); CHECK (lock.samples() >= 3);
    ClockLock tempo; tempo.observe (5, 5.01, 5.01, 5.02, 0, 100, 5.02);
    for (int i = 0; i < 5; ++i) tempo.steer (5.02 + i * 0.05);
    CHECK (tempo.observe (6, 6.01, 6.01, 6.02, tempo.leaderBeatAt (6.02), 140, 6.02));
    double e0 = std::abs (tempo.errorBeats());
    for (int i = 0; i < 30; ++i) tempo.steer (6.05 + i * 0.05);
    CHECK (std::abs (tempo.errorBeats()) < e0 + 0.05);
    CHECK (tempo.label().find ("Clock") == 0);
}

static void testChartConflict() {
    CHECK_EQ (sharedChartTitle (seedLibrary(), "Morning Song"), std::string ("Morning Song (shared)"));
    Library named = seedLibrary(); Song extra; extra.id = "already"; extra.chart.title = "Morning Song (shared)"; named.songs.push_back (extra);
    CHECK_EQ (sharedChartTitle (named, "Morning Song"), std::string ("Morning Song (shared 2)"));
    Chart same = parseChart ("[Verse 1]\nG\nhello\n[Chorus]\nD\nsing\n");
    Chart coloured = same; coloured.sections[0].customColor = "red"; coloured.sections[1].color = "orange";
    CHECK (chartContentEquals (same, coloured));
    coloured.sections[1].items[0].line.lyric = "other";
    CHECK (! chartContentEquals (same, coloured));
    std::vector<std::string> names { "Verse 1", "Chorus", "Bridge" };
    Chart chorusFirst = parseChart ("[Chorus]\nD\nsing\n[Verse 1]\nG\nhello\n");
    CHECK_EQ (mapLeaderSection (chorusFirst, names, 1), 0);
    Chart one = parseChart ("[Verse]\nG\nhi\n");
    CHECK_EQ (mapLeaderSection (one, names, 5), 0);

    Band b;
    Song mine; mine.id = "morning-song"; mine.chart = parseChart ("[Chorus]\nD\nmine\n[Verse 1]\nG\nhello\n");
    mine.rigPreset = "My Rig"; Scene big; big.name = "Big"; mine.scenes.push_back (big); mine.sectionScene["Chorus"] = "Big"; mine.defaultScene = "Big";
    b.FL.songs.push_back (mine);
    b.LL.song ("morning-song")->rigPreset = "Leader Rig";
    b.lead.start (b.LL.setlists[0], 1, 0); b.join (0.0); b.step (0.1);
    CHECK (b.follower.chartPromptWaiting());
    CHECK_EQ (b.follower.chartPrompt().title, std::string ("Morning Song"));
    CHECK (b.FL.song ("morning-song")->chart.sections[0].items[0].line.lyric == "mine");
    CHECK_EQ (b.fol.chart().sections[(size_t) b.fol.sectionIndex()].name, std::string ("Verse 1"));
    b.lead.handle ({ Action::GotoSection, 1 }, 0.2); b.step (0.2);
    CHECK_EQ (b.fol.chart().sections[(size_t) b.fol.sectionIndex()].name, std::string ("Chorus"));
    b.follower.answerChartPrompt (ChartImportChoice::KeepMine, false, RigImportChoice::CopyMine, b.fol, b.FL, 0.3);
    CHECK (! b.follower.chartPromptWaiting());
    CHECK (b.FL.song ("morning-song")->chart.sections[0].items[0].line.lyric == "mine");
    CHECK_EQ (b.FL.song ("morning-song")->rigPreset, std::string ("My Rig"));
    b.step (0.4); CHECK (! b.follower.chartPromptWaiting());

    Band rep;
    Song mine2 = mine; rep.FL.songs.push_back (mine2); rep.lead.start (rep.LL.setlists[0], 1, 0); rep.join (0); rep.step (0.1);
    CHECK (rep.follower.chartPromptWaiting());
    rep.follower.answerChartPrompt (ChartImportChoice::Replace, false, RigImportChoice::CopyMine, rep.fol, rep.FL, 0.2);
    CHECK (! rep.follower.chartPromptWaiting());
    CHECK (rep.FL.song ("morning-song")->chart.sections[0].name == "Verse 1");
    CHECK (rep.FL.song ("morning-song")->chart.sections[0].items[0].line.lyric != "mine");
    CHECK_EQ (rep.FL.song ("morning-song")->rigPreset, std::string ("My Rig"));
    CHECK_EQ (rep.FL.song ("morning-song")->sectionScene["Chorus"], std::string ("Big"));
    CHECK (rep.FL.song ("morning-song")->rigPreset != "Leader Rig");
    rep.step (0.3); CHECK (! rep.follower.chartPromptWaiting());
    rep.LL.song ("morning-song")->chart.sections[0].items[0].line.lyric = "leader edited";
    rep.step (0.4); CHECK (rep.follower.chartPromptWaiting());

    Band fresh;
    fresh.FL.songs.push_back (mine); fresh.lead.start (fresh.LL.setlists[0], 1, 0); fresh.join (0); fresh.step (0.1);
    fresh.follower.answerChartPrompt (ChartImportChoice::Replace, false, RigImportChoice::Fresh, fresh.fol, fresh.FL, 0.2);
    CHECK_EQ (fresh.FL.song ("morning-song")->rigPreset, std::string ("Worship Ambient"));
    CHECK (fresh.FL.song ("morning-song")->scenes.empty());
    CHECK (fresh.FL.song ("morning-song")->sectionScene.empty());

    Band neu;
    Song titled = mine; neu.FL.songs.push_back (titled);
    Song blocker; blocker.id = "block"; blocker.chart.title = "Morning Song (shared)"; neu.FL.songs.push_back (blocker);
    neu.lead.start (neu.LL.setlists[0], 1, 0); neu.join (0); neu.step (0.1);
    const int before = (int) neu.FL.songs.size();
    neu.follower.answerChartPrompt (ChartImportChoice::SaveAsNew, false, RigImportChoice::CopyMine, neu.fol, neu.FL, 0.2);
    CHECK_EQ ((int) neu.FL.songs.size(), before + 1);
    const Song* shared = nullptr; for (auto& s : neu.FL.songs) if (s.chart.title == "Morning Song (shared 2)") shared = &s;
    CHECK (shared != nullptr);
    CHECK (shared && shared->id != "morning-song");
    CHECK (neu.FL.song ("morning-song")->chart.sections[0].items[0].line.lyric == "mine");
    CHECK_EQ (neu.fol.song()->id, shared->id);
    CHECK_EQ (shared->rigPreset, std::string ("My Rig"));
    CHECK_EQ (shared->sectionScene.count ("Chorus"), (size_t) 1);
    CHECK (shared->rigPreset != "Leader Rig");
    neu.step (0.4); CHECK (! neu.follower.chartPromptWaiting());

    Band blank;
    Song onlyLeader; onlyLeader.id = "brand-new"; onlyLeader.chart = parseChart ("[Verse]\nA\nnew song\n"); onlyLeader.rigPreset = "Leader Rig";
    blank.LL.songs.push_back (onlyLeader);
    blank.LL.setlists[0].entries.push_back ({ "brand-new", "", 0, "lead" });
    blank.lead.start (blank.LL.setlists[0], (int) blank.LL.setlists[0].entries.size() - 1, 0);
    blank.join (0); blank.step (0.1);
    CHECK (! blank.follower.chartPromptWaiting());
    CHECK (blank.FL.song ("brand-new") != nullptr);
    CHECK_EQ (blank.FL.song ("brand-new")->rigPreset, std::string ("Worship Ambient"));
}

int main() { setvbuf (stdout, nullptr, _IONBF, 0);
    testProtocol(); testLeaderFollower(); testTcp(); testLinkLock(); testProjector(); testRoster(); testRosterTcp(); testClockLock(); testChartConflict();
    std::printf ("%d checks, %d failures\n", checks, fails); return fails ? 1 : 0; }
