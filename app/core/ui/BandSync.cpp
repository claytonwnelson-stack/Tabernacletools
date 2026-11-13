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
            wr::sync::Msg got; if (wr::sync::decode (l, got) && got.type == "getaudio" && ! live_.playing()) sendLeaderAudio (id, got.body["song"].str());
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
        if (! live_.playing()) installSharedAudio();
        if (follower_.wasKicked() && client_.attempts() >= 0) client_.disconnect();   // removed by the leader: stay off until the user rejoins
        if (! linkActive_.load() && std::abs (localClock_.tempo() - live_.bpm()) > 0.05) localClock_.setTempo (live_.bpm(), now);
        if (! versionShown_ && ! follower_.versionWarning().empty()) { versionShown_ = true; auto text = follower_.versionWarning(); follower_.clearVersionWarning(); juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, "Band sync", text); }
        if (! live_.playing() && ! setlistPromptOpen_ && follower_.setlistSaveWaiting()) showSetlistPrompt();
        if (! live_.playing() && ! chartPromptOpen_ && ! follower_.setlistSaveWaiting()) showChartPrompt();
    }
}
void BandSync::showSetlistPrompt() {
    if (live_.playing() || ! follower_.setlistSaveWaiting()) return;
    setlistPromptOpen_ = true;
    auto* w = new juce::AlertWindow ("Save your setlist?", "The leader's setlist will replace the one loaded here. Save yours first?", juce::AlertWindow::QuestionIcon);
    w->addButton ("Save", 1); w->addButton ("Don't save", 2);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int result) {
        setlistPromptOpen_ = false;
        follower_.answerSetlistSave (result == 1, live_, lib_, clock(), wr::sync::nowSec());
        saveLib_ = true;
    }), true);
}
void BandSync::requestLeaderAudio() {
    if (mode_ != Mode::Following || live_.playing()) return;
    const auto& set = live_.setlist();
    std::string song = live_.songIndex() >= 0 && live_.songIndex() < (int) set.entries.size() ? set.entries[(size_t) live_.songIndex()].songId : std::string();
    client_.send (encode ("getaudio", -1, Json::object().set ("song", song)));
}
void BandSync::sendLeaderAudio (int followerId, const std::string& songId) {
    if (live_.playing()) return;
    auto hexOf = [] (const std::string& in) { static const char* h = "0123456789abcdef"; std::string o; o.resize (in.size() * 2); for (size_t i = 0; i < in.size(); ++i) { auto c = (unsigned char) in[i]; o[i * 2] = h[c >> 4]; o[i * 2 + 1] = h[c & 15]; } return o; };
    auto sendFile = [&] (const std::string& id, const std::string& name, const std::string& kind, const juce::File& file) {
        if (! file.existsAsFile()) return;
        juce::MemoryBlock mb; if (! file.loadFileAsData (mb) || mb.getSize() == 0) return;
        auto chunks = wr::sync::splitAudio (id, name, leader_->sessionId(), kind, std::string ((const char*) mb.getData(), mb.getSize()), 24 * 1024);
        for (auto& c : chunks) server_.send (followerId, wr::sync::encode ("audio", -1, Json::object().set ("song", c.songId).set ("name", c.name).set ("session", c.session).set ("kind", c.kind).set ("i", c.index).set ("n", c.count).set ("data", hexOf (c.data))));
    };
    auto one = [&] (const Song& s) {
        for (auto& st : s.multitrack.stems) sendFile (s.id, st.name.empty() ? st.id : st.name, "stem", juce::File (st.file));
        if (! s.practice.file.empty()) sendFile (s.id, "Practice", "practice", juce::File (s.practice.file));
    };
    if (! songId.empty()) { if (auto* s = lib_.song (songId)) one (*s); return; }
    for (auto& e : live_.setlist().entries) if (auto* s = lib_.song (e.songId)) one (*s);
}
void BandSync::installSharedAudio() {
    wr::sync::AudioChunk done;
    while (follower_.takeFinishedAudio (done)) {
        auto* s = lib_.song (done.songId); if (! s || done.data.empty()) continue;
        auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("BearingLive").getChildFile ("SharedAudio").getChildFile (done.session).getChildFile (done.songId);
        dir.createDirectory();
        auto file = dir.getChildFile (juce::File::createLegalFileName (done.name.empty() ? done.kind : done.name) + ".wav");
        file.replaceWithData (done.data.data(), done.data.size());
        if (done.kind == "practice") { s->practice.file = file.getFullPathName().toStdString(); s->practice.sharedSession = done.session; }
        else { mt::Stem st; st.id = done.name; st.name = done.name; st.file = file.getFullPathName().toStdString(); st.sharedSession = done.session; s->multitrack.stems.push_back (st); }
        s->leaderAudioNote.clear(); saveLib_ = true;
    }
}
std::string BandSync::sessionId() const { if (mode_ == Mode::Leading && leader_) return leader_->sessionId(); if (mode_ == Mode::Following) return follower_.sessionId(); return {}; }
void BandSync::showChartPrompt() {
    if (! follower_.chartPromptWaiting()) return;
    auto p = follower_.chartPrompt();
    chartPromptOpen_ = true;
    auto* w = new juce::AlertWindow ("Chart from the leader",
        "\"" + juce::String (p.title) + "\" is already in your library, and the leader's chords, lyrics or tabs are different."
        + (p.remaining > 0 ? "\n\n" + juce::String (p.remaining) + " other song" + (p.remaining == 1 ? "" : "s") + " in this set also differ." : juce::String())
        + "\n\nYour rig stays on this iPad. Nothing is taken from the leader's rig.",
        juce::MessageBoxIconType::QuestionIcon);
    const bool more = p.remaining > 0;
    w->addButton ("Import and replace my version", 1);
    if (more) w->addButton ("Replace all differing songs", 2);
    w->addButton ("Import and save as new version", more ? 3 : 2);
    if (more) w->addButton ("Save all as new versions", 4);
    w->addButton ("Use my version", more ? 5 : 3);
    if (more) w->addButton ("Use my version for all", 6);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, more] (int r) {
        using C = wr::sync::ChartImportChoice;
        C choice = C::KeepMine; bool all = false;
        if (! more) { choice = r == 1 ? C::Replace : r == 2 ? C::SaveAsNew : C::KeepMine; }
        else if (r == 1) choice = C::Replace;
        else if (r == 2) { choice = C::Replace; all = true; }
        else if (r == 3) choice = C::SaveAsNew;
        else if (r == 4) { choice = C::SaveAsNew; all = true; }
        else if (r == 6) { choice = C::KeepMine; all = true; }
        if (choice == C::KeepMine) {
            follower_.answerChartPrompt (choice, all, wr::sync::RigImportChoice::CopyMine, live_, lib_, juce::Time::getMillisecondCounterHiRes() * 0.001);
            chartPromptOpen_ = false;
        } else showRigPrompt (choice, all);
    }), true);
}
void BandSync::showRigPrompt (wr::sync::ChartImportChoice choice, bool applyAll) {
    auto* w = new juce::AlertWindow ("Rig for this song",
        juce::String (applyAll ? "These songs keep a rig from this iPad. The leader's rig is not copied.\n\n" : "This song keeps a rig from this iPad. The leader's rig is not copied.\n\n")
        + "Copy rig settings from my existing version uses your pedal chain and section patches (matched by section name, then by position). Set up new rig settings starts from the usual empty rig.",
        juce::MessageBoxIconType::QuestionIcon);
    w->addButton ("Copy rig settings from my existing version", 1);
    w->addButton ("Set up new rig settings", 2);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, choice, applyAll] (int r) {
        auto rig = r == 2 ? wr::sync::RigImportChoice::Fresh : wr::sync::RigImportChoice::CopyMine;
        follower_.answerChartPrompt (choice, applyAll, rig, live_, lib_, juce::Time::getMillisecondCounterHiRes() * 0.001);
        saveLib_ = true; chartPromptOpen_ = false;
    }), true);
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
