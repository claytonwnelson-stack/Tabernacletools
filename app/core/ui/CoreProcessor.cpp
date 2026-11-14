#include "CoreProcessor.h"
#include "model/Ccli.h"
#include "Platform.h"
#include "ProjectorView.h"
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

using namespace wr;
#if __has_include (<VoiceData.h>)
 #include <VoiceData.h>
 #define WR_HAS_VOICE_DATA 1
#endif
static void loadBundledVoice (VoiceBank& vb) {
   #ifdef WR_HAS_VOICE_DATA
    for (int i = 0; i < VoiceData::namedResourceListSize; ++i) {
        int size = 0; const char* data = VoiceData::getNamedResource (VoiceData::namedResourceList[i], size);
        juce::String word (VoiceData::getNamedResourceOriginalFilename (VoiceData::namedResourceList[i])); word = word.upToLastOccurrenceOf (".", false, false);
        std::vector<float> m; double sr = 0; if (data && decodeWav ((const uint8_t*) data, (size_t) size, m, sr)) vb.addWord (word.toStdString(), std::move (m), sr);
    }
   #else
    juce::ignoreUnused (vb);
   #endif
}
// v0.16: user recordings in <data>/Voice/<word>.wav override the bundled placeholder voice (no rebuild needed)
static int loadVoiceOverrides (VoiceBank& vb, const juce::File& dataDir) {
    int n = 0; auto dir = dataDir.getChildFile ("Voice"); if (! dir.isDirectory()) return 0;
    for (auto& f : dir.findChildFiles (juce::File::findFiles, false, "*.wav")) { juce::MemoryBlock mb; if (! f.loadFileAsData (mb)) continue;
        std::vector<float> m; double sr = 0; if (decodeWav ((const uint8_t*) mb.getData(), mb.getSize(), m, sr) && ! m.empty()) { vb.addWord (f.getFileNameWithoutExtension().toLowerCase().toStdString(), std::move (m), sr); ++n; } }
    return n;
}

CoreProcessor::CoreProcessor (const BusesProperties& b, const juce::String& name) : juce::AudioProcessor (b), appName_ (name) {
#ifndef WR_STORE_BUILD
    entitlements.setUnlockAll (true);
#endif
    refreshAccess();
    store.load();
    loadBundledVoice (voice); loadVoiceOverrides (voice, store.file().getParentDirectory()); voice.prepare (48000);
    live_ = std::make_unique<LiveSession> (store.lib, *this);
    sync_ = std::make_unique<BandSync> (*live_, store.lib); sync_->useLink = store.lib.settings.useLink;
    projector_ = std::make_unique<ProjectorOutput> (*this);
#ifdef WR_STORE_BUILD
    if (juce::MessageManager::existsAndIsCurrentThread()) {
        seats_ = std::make_unique<SeatSession> (*this);   // v0.11
        refreshIntroEligibility();   // v0.13 one trial per Apple ID across both plans (single subscription group)
        wr::store::start ([this] (std::vector<wr::StoreEntitlement> e) { applyStoreEntitlements (e); });   // v0.12 StoreKit 2 listener
    }
#endif
}
void CoreProcessor::applyStoreEntitlements (const std::vector<wr::StoreEntitlement>& in) {
    // v0.18 post every signed transaction to the seat service once (purchase, renewal, restore) so all apps unlock
    if (seats_ && seats_->client().signedIn())
        for (auto& x : in) if (! x.jws.empty() && postedJws_.insert (x.jws).second) {
            auto otid = x.originalTransactionId;
            seats_->async ([jws = x.jws] (wr::SeatClient& c) { return c.postPurchase (jws); },
                           [this, otid] (wr::SeatResult r) {
                               if (r.ok && ! r.body["trialOk"].boolean (true) && trialRefusedOtids_.insert (otid).second) applyStoreEntitlements (storeEnt_);
                               if (r.ok && ! r.body["duplicate"]["app"].str().empty())
                                   status = "You already subscribe in " + juce::String (r.body["duplicate"]["app"].str()) + " - one subscription covers every Bearing Live app. Cancel the extra one in Settings > Apple ID > Subscriptions.";
                               seats_->refreshEntitlement (true); });
        }
    std::vector<wr::StoreEntitlement> e;   // a StoreKit trial the server refused (second trial for this person) doesn't unlock
    for (auto& x : in) if (wr::honourLocalTrial (x.trial, trialRefusedOtids_.count (x.originalTransactionId) > 0, false)) e.push_back (x);
    storeEnt_ = in;
    for (auto& s : in) if (s.trial) intro_ = wr::IntroEligibility::Ineligible;
    auto ent = entitlements; ent.setSubscription (wr::subscriptionFromStore (e, (int64_t) juce::Time::currentTimeMillis() / 1000, intro_ == wr::IntroEligibility::Eligible));
    setEntitlements (ent);
}
void CoreProcessor::refreshIntroEligibility (std::function<void()> done) {
    wr::store::introEligibility ([this, done] (bool known, bool eligible) {
        intro_ = wr::introEligibilityFrom (known, eligible, storeEnt_);
        if (! storeEnt_.empty()) applyStoreEntitlements (storeEnt_);
        if (done) done(); });
}
void CoreProcessor::coreInit() {
    { VoiceClip n[13]; for (int i = 1; i <= 12; ++i) n[i] = voice.number (i); clickEngine().setNumbers (n); }
    clockInCb_.self = this;
    applySettings();
    platform::enableNetworkMidi();
    if (! store.lib.setlists.empty()) live_->start (store.lib.setlists.front(), 0, nowSec());
    footQ_.reserve (512); footDrain_.reserve (512);
    startTimerHz (100);
}
CoreProcessor::~CoreProcessor() { seats_.reset(); stopTimer(); footPorts_.clear(); projector_.reset(); sync_.reset(); store.closeClean(); }

void CoreProcessor::corePrepare (double sr, int) {
    fs_ = sr; platform::requestLowLatency (sr, 128); practice.prepare (sr); tracks.prepare (sr); cueBuf_.assign (8192, 0.0f);
    voice.prepare (sr); { VoiceClip n[13]; for (int i = 1; i <= 12; ++i) n[i] = voice.number (i); clickEngine().setNumbers (n); } // audio is stopped here
}

void CoreProcessor::processBlock (juce::AudioBuffer<float>& buf, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals nd;
    const int n = buf.getNumSamples();
    // read-only mode: no audio engine / click / cues - but v0.18: a lapse while the app is running never goes silent (LapseGuard);
    // read-only only applies from the next launch
    if (! lapse_.audioAllowed (fullAccess_.load (std::memory_order_relaxed))) { buf.clear(); midi.clear(); return; }
    // ---- MIDI -> router -> actions (realtime safe)
    {
        juce::SpinLock::ScopedTryLockType tl (routerLock_);
        if (tl.isLocked()) {
            if (profilesPending_) { router_.setProfiles (pendingProfiles_); router_.setExternal (store.lib.settings.externalChannel, store.lib.settings.noteBase, store.lib.settings.followMidiClock); profilesPending_ = false; }
            if (learnPending_.exchange (false)) { router_.beginLearn (learnReq_.unit, learnReq_.sw, learnReq_.longPress); learning_ = true; }
            footDrain_.clear();
            { std::lock_guard<std::mutex> lk (footMu_); footDrain_.swap (footQ_); }
            const double wall = juce::Time::getMillisecondCounterHiRes();
            for (auto& m : footDrain_) {
                router_.handle (m.st, m.d1, m.d2, m.ms, m.unit);
                if (m.st < 0xF8) { lastMidiStatus = m.st; lastMidiData1 = m.d1; ++midiActivity; }
            }
            router_.flushFactory (wall);
            const double t0 = sampleCount_ / fs_ * 1000.0;
            const bool ownPorts = footPortsOpen_.load();
            for (const auto m : midi) {
                auto msg = m.getMessage(); auto* d = msg.getRawData(); int sz = msg.getRawDataSize(); if (sz < 1) continue;
                const int type = d[0] & 0xF0; const int d1 = sz > 1 ? d[1] : 0;
                // Pedal ports are read above. Skip their CC here so the device-manager callback cannot double-fire.
                if (ownPorts && (type == 0xC0 || (type == 0xB0 && ((d1 >= 80 && d1 <= 83) || (d1 >= 20 && d1 <= 23))))) continue;
                router_.handle (d[0], d1, sz > 2 ? d[2] : 0, t0 + m.samplePosition / fs_ * 1000.0);
                if (d[0] < 0xF8) { lastMidiStatus = d[0]; lastMidiData1 = d1; ++midiActivity; }
            }
            router_.tick (t0 + n / fs_ * 1000.0);
            Action a[16]; int k = router_.pop (a, 16);
            for (int i = 0; i < k; ++i) { if (a[i].type == Action::Learned) learning_ = false; actions_.push (a[i]); }
        }
    }
    midi.clear();
    // ---- v0.17 MIDI Clock send (leader / solo only; followers keep Wi-Fi sync). Works with click + stems (uses live bpm/playing).
    const bool wantSend = store.lib.settings.midiClockMode == (int) MidiClockMode::Send && live_ && ! live_->following();
    if (wantSend) {
        clockOut_.setSampleRate (fs_);
        const double bpm = live_->bpm();
        if (std::abs (bpm - clockWasBpm_) >= 0.05) { clockOut_.setBpm (bpm); clockWasBpm_ = bpm; }
        const bool playing = live_->playing();
        if (playing && ! clockWasPlaying_) { clockOut_.setBpm (bpm); clockOut_.startFromTop(); lastMidiLocate_ = live_->midiLocateGen(); }
        else if (! playing && clockWasPlaying_) clockOut_.stop();
        else if (playing) {
            const int loc = live_->midiLocateGen();
            if (loc != lastMidiLocate_) { lastMidiLocate_ = loc; clockOut_.seekBeats (live_->absoluteBeats()); } // SPP on section jumps
        }
        clockWasPlaying_ = playing;
        MidiOutMsg msgs[64]; const int nm = clockOut_.process (n, msgs, 64);
        for (int i = 0; i < nm; ++i) {
            juce::MidiMessage m = msgs[i].size >= 3 ? juce::MidiMessage (msgs[i].status, msgs[i].d1, msgs[i].d2)
                : msgs[i].size == 2 ? juce::MidiMessage (msgs[i].status, msgs[i].d1) : juce::MidiMessage (msgs[i].status);
            midi.addEvent (m, msgs[i].sample);
            if (clockMidiOut_) clockMidiOut_->sendMessageNow (m);
            if (clockMidiOutSel_) clockMidiOutSel_->sendMessageNow (m);
        }
    } else if (clockWasPlaying_) { clockOut_.stop(); clockWasPlaying_ = false;
        MidiOutMsg msgs[8]; const int nm = clockOut_.process (0, msgs, 8);
        for (int i = 0; i < nm; ++i) { juce::MidiMessage m (msgs[i].status); if (clockMidiOut_) clockMidiOut_->sendMessageNow (m); if (clockMidiOutSel_) clockMidiOutSel_->sendMessageNow (m); }
    }
    sampleCount_ += n;
    renderAudio (buf, n);
}
int CoreProcessor::prepareOutputs (juce::AudioBuffer<float>& buf, int n) {
    int usable = std::min ({ getTotalNumOutputChannels(), buf.getNumChannels(), physOuts_.load() });
    for (int c = usable; c < buf.getNumChannels(); ++c) buf.clear (c, 0, n);
    if (sync_) sync_->audioBlock (clickEngine(), 0.005 + n / fs_, live_->beatsPerBar()); // Link: lock the click grid (output latency approx.)
    return usable;
}

void CoreProcessor::mixPractice (juce::AudioBuffer<float>& buf, int n) {
    if (tracks.loaded() && fullAccess() && buf.getNumChannels() > 0 && n <= (int) cueBuf_.size()) {   // v0.14 backing tracks
        const int ch = buf.getNumChannels(); std::fill (cueBuf_.begin(), cueBuf_.begin() + n, 0.0f);
        tracks.process (buf.getWritePointer (0), ch > 1 ? buf.getWritePointer (1) : nullptr, cueBuf_.data(), n);
        const int co = store.lib.settings.clickOut;   // the cue bus follows the click: Out 2 (default), outs 3/4, or outs 1/2
        if (co == 1 && ch >= 4) { buf.addFrom (2, 0, cueBuf_.data(), n); buf.addFrom (3, 0, cueBuf_.data(), n); }
        else if (co == 0) { buf.addFrom (0, 0, cueBuf_.data(), n); if (ch > 1) buf.addFrom (1, 0, cueBuf_.data(), n); }
        else if (ch > 1) buf.addFrom (1, 0, cueBuf_.data(), n);
    }
    if (! practice.playing()) return;
    const int ch = std::min (2, buf.getNumChannels()); if (ch <= 0) return;
    practice.process (buf.getWritePointer (0), ch > 1 ? buf.getWritePointer (1) : nullptr, n);
}

// ------------------------------------------------------------------ practice tracks (v0.7)
Song* CoreProcessor::practiceSong() { auto* s = live_->song(); return s ? store.lib.song (s->id) : nullptr; }
void CoreProcessor::setPracticeMode (bool on) {
    if (on && sync_->mode() == BandSync::Mode::Following) { status = "Practice mode is off while following a leader"; on = false; }
    practiceMode_ = on; if (! on) { practice.play (false); practice.clearLoop(); return; }
    loadPracticeTrack();
}
bool CoreProcessor::loadPracticeTrack() {
    auto* s = practiceSong(); practice.play (false);
    practiceSongId_ = s ? s->id : std::string();
    if (! s || s->practice.file.empty()) { practice.setTrack (nullptr); practiceAudio_.reset(); return false; }
    if (! wr::sync::sharedStemPlayable (s->practice.sharedSession, s->practice.sharedOwner, sync_->mode() == BandSync::Mode::Following, sync_->sessionId(), sync_->activeLeaderName())) {
        status = "Practice audio plays only while connected to the leader"; practice.setTrack (nullptr); practiceAudio_.reset(); return false; }
    juce::File f (s->practice.file); if (! f.existsAsFile()) { status = "Practice track missing: " + f.getFileName(); practice.setTrack (nullptr); return false; }
    juce::AudioFormatManager fm; fm.registerBasicFormats();   // wav / aiff everywhere; mp3 / m4a via CoreAudio on iOS + Mac
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    if (! r || r->lengthInSamples <= 0 || r->lengthInSamples > (juce::int64) r->sampleRate * 60 * 20) { status = "Can't read " + f.getFileName(); practice.setTrack (nullptr); return false; }
    auto a = std::make_shared<PracticeAudio>(); a->sampleRate = r->sampleRate; const int len = (int) r->lengthInSamples;
    juce::AudioBuffer<float> b ((int) std::min (2u, std::max (1u, r->numChannels)), len); r->read (&b, 0, len, 0, true, true);
    a->left.assign (b.getReadPointer (0), b.getReadPointer (0) + len); a->right.assign (b.getReadPointer (b.getNumChannels() - 1), b.getReadPointer (b.getNumChannels() - 1) + len);
    practiceAudio_ = a; practice.setTrack (a);
    if (s->practice.marks.size() != live_->chart().sections.size()) s->practice.marks.resize (live_->chart().sections.size(), -1.0);
    return true;
}
juce::String CoreProcessor::attachPracticeTrack (const juce::File& src) {
    auto* s = practiceSong(); if (! s) return "No song loaded";
    auto dir = store.file().getParentDirectory().getChildFile ("practice"); dir.createDirectory();
    auto dst = dir.getChildFile (juce::String (s->id) + src.getFileExtension().toLowerCase());
    if (src != dst && ! src.copyFileTo (dst)) return "Couldn't copy " + src.getFileName();   // iOS document picks are temporary
    s->practice.file = dst.getFullPathName().toStdString(); s->practice.marks.assign (live_->chart().sections.size(), -1.0);
    store.save(); return loadPracticeTrack() ? juce::String() : status;
}
void CoreProcessor::markSectionHere() {
    auto* s = practiceSong(); if (! s) return; auto& m = s->practice.marks; const int i = live_->sectionIndex();
    if ((int) m.size() < (int) live_->chart().sections.size()) m.resize (live_->chart().sections.size(), -1.0);
    if (i >= 0 && i < (int) m.size()) { m[(size_t) i] = practice.position(); store.saveSoon(); }
}
void CoreProcessor::autoMarks() {
    auto* s = practiceSong(); if (! s) return; s->practice.marks = estimatePracticeMarks (live_->chart(), live_->songBpm(), practice.position()); store.saveSoon();
}
void CoreProcessor::loopCurrentSection (bool on) {
    auto* s = practiceSong(); if (! on || ! s) { practice.clearLoop(); return; }
    const auto& m = s->practice.marks; const int i = live_->sectionIndex(); if (i < 0 || i >= (int) m.size() || m[(size_t) i] < 0) { status = "Mark this section first"; return; }
    double end = practice.length(); for (size_t k = (size_t) i + 1; k < m.size(); ++k) if (m[k] > m[(size_t) i]) { end = m[k]; break; }
    practice.setLoop (m[(size_t) i], end); practice.seek (m[(size_t) i]);
}
void CoreProcessor::practiceTick() {
    if (! practiceMode_) return;
    auto* s = live_->song(); if (! s || s->id != practiceSongId_) { loadPracticeTrack(); return; }    // song changed: its own track
    if (! practice.playing()) return;
    auto* ps = practiceSong(); if (! ps) return;
    const int idx = practiceSectionAt (ps->practice.marks, practice.position());
    if (idx >= 0) live_->practiceSection (idx, nowSec());                  // chart follows the recording (no cues)
}

// ------------------------------------------------------------------ message thread
void CoreProcessor::timerCallback() {
    // Standalone: open the two Chocolate ports ourselves (so each message knows which pedal it came from)
    // and keep every other input enabled for the host buffer. AUv3 has no holder; the host feeds processBlock.
    if (++midiScan_ >= 50) { midiScan_ = 0; refreshFootswitchPorts(); }
    Action a; const double now = nowSec();
    { static int t = 0; if (++t >= 100) { t = 0; refreshAccess();   // ~1 s: trial / subscription expiry
        if (! fullAccess() && projector_ && projector_->externalOn()) projector_->showExternal (false); } }   // v0.12: projector locks on lapse
    while (actions_.pop (a)) {
        if (a.type == Action::Learned) {
            juce::SpinLock::ScopedLockType l (routerLock_);
            if (a.unit >= 0 && a.unit < (int) store.lib.controllers.size()) store.lib.controllers[(size_t) a.unit] = router_.profile (a.unit);
            store.saveSoon(); status = "Learned switch " + juce::String::charToString ((juce::juce_wchar) ('A' + a.sw)) + " on " + juce::String (store.lib.controllers[(size_t) a.unit].name);
        } else if (a.type == Action::BankChanged) {   // v0.8 footswitch bank (router already switched; persist it)
            if (a.unit >= 0 && a.unit < (int) store.lib.controllers.size()) { auto& c = store.lib.controllers[(size_t) a.unit]; c.bank = a.value;
                store.saveSoon(); status = juce::String (c.name) + ": bank " + juce::String (a.value + 1) + (a.value < (int) c.banks.size() ? " - " + juce::String (c.banks[(size_t) a.value].name) : juce::String()); }
        } else {
            if (a.type == Action::TracksPlay) a.type = Action::Play;                       // v0.14: tracks play = the normal PLAY
            if (tracksLive_.handle (a, *live_, now, allows (wr::Feature::AudioEngine) || lapse_.latched())) continue;
            if (! sync_->interceptAction (a, now)) live_->handle (a, now);
        } // incl. tuner (hold guitar A), click mute (hold guitar D)
    }
    live_->tick (now);
    sync_->poll (now);
    if (sync_->takeLibrarySave()) store.saveSoon();
    // v0.8 song stats: a song counts as played when PLAY runs on it (leader, follower or alone), once per day
    { const bool pl = live_->playing(); auto* s = live_->song();
      if (pl && s) { auto key = s->id + "@" + today(); if (key != loggedKey_ && wr::logPlay (store.lib, s->id, today())) store.saveSoon(); loggedKey_ = key; }
      wasPlaying_ = pl;
      // v0.15 CCLI usage log: live sets only (never practice mode); chart = PLAY ran on it, projected = lyrics on a screen
      if (s && ! practiceMode_ && ! live_->setlist().entries.empty()) { const bool proj = projector_ && projector_->showing();
          if ((pl || proj) && wr::ccli::logUsage (store.lib, s->id, today(), live_->setlist().name, proj, pl)) store.saveSoon(); } }
    if (recorder.recording()) recorder.drain();
    if (live_->consumeSettingsDirty()) store.saveSoon();
    if (++prerenderTick_ % 200 == 0) prerenderCustomCues();
    practiceTick();
    tracksTick();
    if (++healthTick_ % 100 == 1) { auto h = platform::deviceHealth(); health_ = { h.batteryPct, h.charging, h.thermal }; sync_->setHealth (health_); }
    instrumentTick();
    if (store.dirty && ++saveCounter_ > 50) { saveCounter_ = 0; store.save(); }
}

juce::String CoreProcessor::clickOutLabel (int i) const {
    const char* o[] = { "Outs 1/2", "Outs 3/4", "Out 2 only (mono)" }; return i >= 0 && i < 3 ? juce::String (o[i]) : juce::String();
}
void CoreProcessor::syncLocalKey() {
    auto& L = *live_;
    if (L.song() && sync_->mode() == BandSync::Mode::Following && L.songIndex() < (int) L.setlist().entries.size()) {
        auto& e = L.setlist().entries[(size_t) L.songIndex()]; sync_->setLocalKey (L.song()->id, e.keyOverride.empty() ? L.key() : e.keyOverride, e.capoOverride); }
}
void CoreProcessor::setBpm (double b) { post (cmdBpm (b)); }
void CoreProcessor::setClick (bool on, bool muted) { post (cmdClick (Command::ClickOn, on ? 1.0f : 0.0f)); post (cmdClick (Command::ClickMute, muted ? 1.0f : 0.0f)); }
void CoreProcessor::setMeter (int b) { post (cmdClick (Command::ClickMeter, (float) b)); }
void CoreProcessor::restartBeat() { if (sync_ && sync_->linked()) return; /* grid follows Link */ post (cmdClick (Command::ClickReset, 0)); }
void CoreProcessor::announceSection (const std::string& name, int seq) {
    std::vector<float> ph; if (! name.empty()) ph = voice.phrase (name);
    if (ph.empty() && ! name.empty()) { // nothing sayable yet: speak the section kind ("chorus", "verse") while the TTS render is pending
        std::string kind; sectionLabelFor (name, &kind); if (kind != "other") ph = voice.phrase (kind == "prechorus" ? std::string ("pre-chorus") : kind); }
    post (cmdAnnounce (clickEngine().stageCue (std::move (ph)), seq)); cueSeqSent_ = seq; cueSentAt_ = nowSec();
}
bool CoreProcessor::cueDue (int seq) {
    auto& c = clickEngine();
    if (c.cueAck.load() == seq && c.clickBar.load() >= c.cueDueBar.load()) return true;
    // audio not running (no interface / suspended): don't strand the change - give up waiting after 2 bars + 0.5 s
    const double barSec = 60.0 / std::max (30.0, live_->bpm()) * live_->beatsPerBar();
    return seq == cueSeqSent_ && nowSec() - cueSentAt_ > 2 * barSec + 0.5;
}
void CoreProcessor::countIn (int beats, bool spoken) { post (cmdCountIn (beats, spoken)); tracksLive_.onCountIn (beats, live_->bpm()); } // v0.14: stems start after the same count-in
void CoreProcessor::prerenderCustomCues() {
    if (! store.lib.settings.voiceCues) return;
    const auto& set = live_->setlist();
    for (auto& e : set.entries) if (auto* s = store.lib.song (e.songId)) for (auto& sec : s->chart.sections) {
        const std::string n = sec.name; if (voice.covers (n) || ttsRequested_.count (n)) continue; ttsRequested_.insert (n);
        const double sr = fs_; platform::renderSpeech (juce::String (spokenName (n)), sr, [this, n, sr] (std::vector<float> m) { if (! m.empty()) voice.addPhrase (n, std::move (m), sr); });
    }
}
void CoreProcessor::onFoot (void* ctx, uint8_t st, uint8_t d1, uint8_t d2, double ms) {
    auto* tap = (FootTap*) ctx; if (tap && tap->self) tap->self->pushFootMidi (tap->unit, st, d1, d2, ms);
}
void CoreProcessor::pushFootMidi (int unit, uint8_t st, uint8_t d1, uint8_t d2, double ms) {
    std::lock_guard<std::mutex> lk (footMu_);
    if (footQ_.size() < 512) footQ_.push_back ({ st, d1, d2, unit, ms });
}
void CoreProcessor::refreshFootswitchPorts() {
    auto* holder = juce::StandalonePluginHolder::getInstance();
    if (holder == nullptr) { footPorts_.clear(); footPortsOpen_ = false; footPedalCount_ = 0; return; } // AUv3: host delivers one merged buffer
    const auto all = juce::MidiInput::getAvailableDevices();
    std::vector<FootPortDesc> desc; desc.reserve ((size_t) all.size());
    for (int i = 0; i < all.size(); ++i) desc.push_back ({ all[i].identifier.toStdString(), all[i].name.toStdString(), i });
    auto& s = store.lib.settings;
    auto asg = assignFootPedals (desc, s.guitarPedalId, s.sectionPedalId, s.midiClockInId, s.onePedalSection);
    footPedalCount_ = asg.detected;
    bool dirty = false;
    if (s.guitarPedalId != asg.guitar.id) { s.guitarPedalId = asg.guitar.id; dirty = true; }
    if (s.sectionPedalId != asg.section.id) { s.sectionPedalId = asg.section.id; dirty = true; }
    if (s.guitarPedalName != asg.guitar.name) { s.guitarPedalName = asg.guitar.name; dirty = true; }
    if (s.sectionPedalName != asg.section.name) { s.sectionPedalName = asg.section.name; dirty = true; }
    if (dirty) store.saveSoon();
    struct Want { std::string id; int unit; int index; };
    Want want[2]; int nWant = 0;
    if (asg.guitar.connected) want[nWant++] = { asg.guitar.id, 0, asg.guitar.index };
    if (asg.section.connected) want[nWant++] = { asg.section.id, 1, asg.section.index };
    bool same = (int) footPorts_.size() == nWant;
    for (int i = 0; same && i < nWant; ++i) if (footPorts_[(size_t) i].id != want[i].id || footPorts_[(size_t) i].unit != want[i].unit || footPorts_[(size_t) i].index != want[i].index) same = false;
    if (! same) {
        footPorts_.clear();
        auto open = [&] (const Want& w) {
            FootPort p; p.id = w.id; p.unit = w.unit; p.index = w.index;
            p.tap = std::make_unique<FootTap>(); p.tap->self = this; p.tap->unit = w.unit;
            p.input = platform::openMidiSourceIndex (w.index, &CoreProcessor::onFoot, p.tap.get());
            if (! p.input) return;
            p.input->start(); footPorts_.push_back (std::move (p));
        };
        for (int i = 0; i < nWant; ++i) open (want[i]);
    }
    footPortsOpen_ = ! footPorts_.empty();
    for (auto& p : footPorts_) if (p.index >= 0 && p.index < all.size()) holder->deviceManager.setMidiInputDeviceEnabled (all[p.index].identifier, false);
    for (int i = 0; i < all.size(); ++i) {
        bool ours = false; for (auto& p : footPorts_) if (p.index == i) ours = true;
        if (! ours && ! holder->deviceManager.isMidiInputDeviceEnabled (all[i].identifier))
            holder->deviceManager.setMidiInputDeviceEnabled (all[i].identifier, true);
    }
}
void CoreProcessor::applySettings() {
    auto& s = store.lib.settings;
    s.followMidiClock = s.midiClockMode == (int) MidiClockMode::Receive;
    post (cmdClick (Command::ClickOut, (float) s.clickOut)); post (cmdClick (Command::ClickDb, s.clickDb)); post (cmdClick (Command::ClickAccent, s.clickAccent ? 1.0f : 0.0f));
    post (cmdClick (Command::ClickMute, s.clickMuted ? 1.0f : 0.0f));
    post (cmdClick (Command::ClickPreset, (float) s.clickPreset)); post (cmdClick (Command::DownbeatDb, s.downbeatDb));
    post (cmdClick (Command::CuesOn, s.voiceCues ? 1.0f : 0.0f)); post (cmdClick (Command::CueDb, s.cueDb)); post (cmdClick (Command::CuesWhenMuted, s.cuesWhenClickMuted ? 1.0f : 0.0f));
    applyInstrumentSettings();
    pushControllerProfiles();
    refreshMidiClockDevices();
    refreshFootswitchPorts();
}
void CoreProcessor::setMidiClockMode (MidiClockMode m) {
    store.lib.settings.midiClockMode = (int) m;
    store.lib.settings.followMidiClock = m == MidiClockMode::Receive;
    applySettings(); store.save();
}
void CoreProcessor::refreshMidiClockDevices() {
    clockInCb_.self = this;
    clockMidiIn_.reset(); clockMidiOutSel_.reset();
    auto& s = store.lib.settings;
    const auto mode = midiClockModeFromInt (s.midiClockMode);
    // Virtual "Bearing Live Clock" out so other iPad apps (AUM, Loopy, GarageBand, ...) can see us; kept while Send is on.
    if (mode == MidiClockMode::Send) {
       #if JUCE_MAC || JUCE_IOS || JUCE_LINUX || JUCE_BSD
        if (! clockMidiOut_) clockMidiOut_ = juce::MidiOutput::createNewDevice ("Bearing Live Clock"); // virtual port for other apps
       #endif
        if (! s.midiClockOutId.empty()) {
            for (auto& d : juce::MidiOutput::getAvailableDevices())
                if (d.identifier.toStdString() == s.midiClockOutId || d.name.toStdString() == s.midiClockOutId) { clockMidiOutSel_ = juce::MidiOutput::openDevice (d.identifier); break; }
        }
    } else { clockMidiOut_.reset(); clockOut_.reset(); clockWasPlaying_ = false; }
    // Dedicated input when Receive + a specific port is chosen (USB / BT / network). Empty = host MidiBuffer (all enabled inputs).
    if (mode == MidiClockMode::Receive && ! s.midiClockInId.empty()) {
        for (auto& d : juce::MidiInput::getAvailableDevices())
            if (d.identifier.toStdString() == s.midiClockInId || d.name.toStdString() == s.midiClockInId) { clockMidiIn_ = juce::MidiInput::openDevice (d.identifier, &clockInCb_); if (clockMidiIn_) clockMidiIn_->start(); break; }
    }
}
void CoreProcessor::pushControllerProfiles() { juce::SpinLock::ScopedLockType l (routerLock_); pendingProfiles_ = store.lib.controllers; profilesPending_ = true; }
void CoreProcessor::beginMidiLearn (int unit, int sw, bool lp) { learnReq_ = { unit, sw, lp }; learnPending_ = true; }
void CoreProcessor::goLive (const std::string& date, int idx) { if (auto* sl = store.lib.setlist (date)) live_->start (*sl, idx, nowSec()); }

void CoreProcessor::getStateInformation (juce::MemoryBlock& mb) {
    juce::DynamicObject::Ptr o = new juce::DynamicObject();
    o->setProperty ("setlist", juce::String (live_->setlist().date)); o->setProperty ("song", live_->songIndex());
    juce::MemoryOutputStream (mb, false).writeString (juce::JSON::toString (juce::var (o.get())));
    if (store.dirty) store.save();
}
void CoreProcessor::setStateInformation (const void* d, int sz) {
    auto v = juce::JSON::parse (juce::MemoryInputStream (d, (size_t) sz, false).readString());
    auto date = v.getProperty ("setlist", "").toString().toStdString(); if (! date.empty()) goLive (date, (int) v.getProperty ("song", 0));
}

// ------------------------------------------------------------------ v0.8 recorder, stats, backup bundle
std::string CoreProcessor::today() { return juce::Time::getCurrentTime().formatted ("%Y-%m-%d").toStdString(); }
static juce::File recDir() { auto d = wr::platform::dataDirectory().getChildFile ("Recordings"); d.createDirectory(); return d; }
juce::String CoreProcessor::startRecording() {
    if (recorder.recording()) return {};
    auto* s = live_->song(); juce::String nm = juce::File::createLegalFileName (s ? juce::String (s->chart.title) : juce::String ("Rehearsal"));
    auto f = recDir().getChildFile (nm + " " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S") + ".wav");
    if (! recorder.start (f.getFullPathName().toStdString(), fs_, store.lib.settings.recordClick ? 2 : 1)) return "Can't create " + f.getFileName();
    lastTake_ = f; return {};
}
juce::File CoreProcessor::stopRecording() { recorder.stop(); return lastTake_; }
juce::Array<juce::File> CoreProcessor::takes() const {
    auto a = recDir().findChildFiles (juce::File::findFiles, false, "*.wav");
    std::sort (a.begin(), a.end(), [] (const juce::File& x, const juce::File& y) { return x.getLastModificationTime() > y.getLastModificationTime(); }); return a;
}
bool CoreProcessor::playTake (const juce::File& f) {
    if (! f.existsAsFile() || recorder.recording()) return false;
    juce::AudioFormatManager fm; fm.registerBasicFormats(); std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    if (! r || r->lengthInSamples <= 0 || r->lengthInSamples > (juce::int64) r->sampleRate * 60 * 30) { status = "Can't read " + f.getFileName(); return false; }
    auto a = std::make_shared<wr::PracticeAudio>(); a->sampleRate = r->sampleRate; const int len = (int) r->lengthInSamples;
    juce::AudioBuffer<float> b ((int) std::min (2u, std::max (1u, r->numChannels)), len); r->read (&b, 0, len, 0, true, true);
    a->left.assign (b.getReadPointer (0), b.getReadPointer (0) + len); a->right.assign (b.getReadPointer (b.getNumChannels() - 1), b.getReadPointer (b.getNumChannels() - 1) + len);
    practice.play (false); practiceAudio_ = a; practice.setTrack (a); practiceSongId_ = "take:" + f.getFileName().toStdString(); practice.seek (0); practice.play (true); return true;
}
juce::File CoreProcessor::exportEverything() {
    auto dir = wr::platform::dataDirectory().getChildFile ("Exports"); dir.createDirectory();
    auto f = dir.getChildFile ("TabernacleTools-backup-" + juce::String (today()) + ".wrbackup.json");
    f.replaceWithText (juce::String (wr::exportBundle (store.lib, appName_.toStdString(), (long long) juce::Time::currentTimeMillis() / 1000))); return f;
}
wr::ImportReport CoreProcessor::importEverything (const juce::File& f, wr::ImportMode m) {
    store.save();   // the current state goes into the rotating backups first (undo = Backups > restore)
    auto r = wr::importBundle (f.loadFileAsString().toStdString(), store.lib, m);
    if (r.ok) { store.save(); applySettings(); live_->refreshChart(); }
    return r;
}
