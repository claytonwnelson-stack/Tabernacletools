#include "BandSync.h"
#include <JuceHeader.h>
#include <random>
#if __has_include(<ableton/Link.hpp>)
 #include "sync/LinkClock.h"   // last: asio pulls in <termios.h> (B0/B1 macros)
 #define WR_HAS_LINK 1
#else
namespace wr::sync { class LinkClock {}; } // built without the Link SDK (WR_WITH_LINK=OFF): never created
#endif

using namespace wr; using namespace wr::sync;

BandSync::BandSync (LiveSession& live, Library& lib) : live_ (live), lib_ (lib), follower_ ([this] (const std::string& l) { client_.send (l); }) {}
BandSync::~BandSync() { stop(); linkRt_.store (nullptr); }

BeatClock& BandSync::clock() {
#if WR_HAS_LINK
    if (useLink) { if (! link_) { link_ = std::make_unique<LinkClock> (live_.bpm()); linkRt_.store (link_.get()); } return *link_; }
#endif
    return localClock_;
}
int BandSync::linkPeers() const {
#if WR_HAS_LINK
    return link_ ? link_->peers() : 0;
#else
    return 0;
#endif
}

void BandSync::lead (const std::string& name) {
    stop(); roster_ = Roster(); std::random_device rd; leader_ = std::make_unique<Leader> (name + "-" + std::to_string (rd() % 100000));
    if (! server_.listen (kDefaultPort)) server_.listen (0);
    disco_.advertise (name, server_.port()); mode_ = Mode::Leading; live_.setFollowing (false);
    clock(); linkActive_.store (useLink && linkRt_.load() != nullptr);
}
void BandSync::join (const std::string& host, int port, const std::string& name) {
    stop(); { auto role = follower_.role; follower_ = Follower ([this] (const std::string& l) { client_.send (l); }, name); follower_.role = role; }
    host_ = host; port_ = port; client_.connectTo (host, port); mode_ = Mode::Following; live_.setFollowing (true);
    clock(); linkActive_.store (useLink && linkRt_.load() != nullptr);
    lib_.settings.lastLeaderHost = host; lib_.settings.lastLeaderPort = port;
}
void BandSync::stop() {
    server_.close(); client_.disconnect(); disco_.stopAdvertising(); leader_.reset();
    if (mode_ == Mode::Following) live_.setFollowing (false);
    mode_ = Mode::Off; linkActive_.store (false);
}
void BandSync::browse (bool on) { if (on) disco_.browse(); else disco_.stopBrowsing(); }

void BandSync::poll (double now) {
    disco_.poll();
#if WR_HAS_LINK
    if (link_) link_->setQuantum (live_.beatsPerBar());   // Link bar lines = this song's bar lines
#endif
    if (mode_ == Mode::Leading && leader_) {
        const std::string h = stateHash (live_);
        Server::Events ev;
        ev.connected = [&] (int id) { roster_.connected (id, now); };
        ev.disconnected = [&] (int id) { roster_.disconnected (id, now); };
        ev.pinged = [&] (long long pid) { roster_.pingSent (pid, now); };
        ev.line = [&] (int id, const std::string& l) {
            Msg m; if (decode (l, m)) roster_.line (id, m, h, leader_->seq(), clock().beatAt (now), live_.bpm(), now);
            auto r = leader_->onFollowerLine (l, live_, clock(), now); if (! r.empty()) server_.send (id, r); };
        server_.poll (now, ev); leader_->tick (live_, clock(), now);
        for (auto& l : leader_->update (live_, clock(), now)) server_.broadcast (l);
        roster_.tick (stateHash (live_), leader_->seq(), now);
        if (! linkActive_.load()) localClock_.setTempo (live_.bpm(), now);
    } else if (mode_ == Mode::Following) {
        Client::Events ev; ev.connected = [this] { follower_.onConnected(); }; ev.disconnected = [this, now] { follower_.onDisconnected (now); };
        ev.line = [&] (const std::string& l) { follower_.onLine (l, live_, lib_, clock(), now); };
        client_.poll (now, ev);
        if (! linkActive_.load() && follower_.clockLock().engaged()) { localClock_.b0 = follower_.clockLock().beatAt (now); localClock_.t0 = now; localClock_.bpm = follower_.clockLock().bpm(); }
        follower_.tick (live_, clock(), now);
        if (follower_.wasKicked() && client_.attempts() >= 0) client_.disconnect();   // removed by the leader: stay off until the user rejoins
        if (! linkActive_.load() && std::abs (localClock_.tempo() - live_.bpm()) > 0.05) localClock_.setTempo (live_.bpm(), now);
    }
}
bool BandSync::interceptAction (const Action& a, double now) {
    if (mode_ == Mode::Leading && a.type == Action::Play) { requestPlay (! (live_.playing() || (leader_ && leader_->playScheduled())), now); return true; }
    return false;
}
void BandSync::requestPlay (bool on, double now) {
    if (mode_ == Mode::Leading && leader_) { server_.broadcast (leader_->requestPlay (live_, clock(), now, on)); return; }
    if (mode_ == Mode::Following) return;                 // the leader runs the transport
    live_.setPlaying (on, now);
}
std::string BandSync::banner (double now) const { return mode_ == Mode::Following ? follower_.banner (now) : std::string(); }
std::string BandSync::clockStatus() const {
    if (mode_ == Mode::Leading) return "Clock: you are the leader";
    if (mode_ == Mode::Following) return follower_.clockLock().label();
    return "Clock off";
}
std::string BandSync::statusLine() const {
    std::string link = linkActive_.load() ? " - Link " + std::to_string (linkPeers()) + " peer(s)" : " - " + clockStatus();
    if (mode_ == Mode::Leading) return "LEADING - " + std::to_string (server_.clients()) + " follower(s)" + link;
    if (mode_ == Mode::Following) return std::string (follower_.synced() ? "FOLLOWING " : "JOINING ") + host_ + link;
    return "Band sync off";
}
void BandSync::audioBlock (ClickEngine& rig, double latency, int) {
#if WR_HAS_LINK
    if (linkActive_.load()) { auto* L = linkRt_.load(); if (! L) return;
        const auto host = L->now() + std::chrono::microseconds ((long long) (latency * 1e6));
        rig.setLinkBeat (L->audioBeat (host)); rig.bpm.store (L->audioTempo()); return; }
#endif
    if (mode_ != Mode::Following || ! follower_.clockLock().engaged()) return;
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001 + latency;
    rig.setLinkBeat (follower_.clockLock().beatAt (now));
    rig.bpm.store (follower_.clockLock().audioBpm (now));
}
