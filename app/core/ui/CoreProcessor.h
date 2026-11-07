// Shared app processor for every Tabernacle Tools app (Guitar now; Bass, Keys, Chart-only later; the Core Test Harness).
// Owns everything instrument-agnostic: the library (AppStore), LiveSession, MIDI router (footswitches, external cues,
// MIDI clock), voice cues + TTS, the click engine hooks, band sync (leader / follower + Link + roster) and projector.
// An instrument app derives, owns its DSP, returns its ClickEngine from clickEngine(), and renders in renderAudio().
#pragma once
#include "SeatSession.h"
#include "store/StoreBridge.h"
#include <JuceHeader.h>
#include <set>
#include "midi/MidiRouter.h"
#include "midi/MidiClock.h"
#include "model/LiveSession.h"
#include "audio/Voice.h"
#include "audio/ClickEngine.h"
#include "audio/PracticePlayer.h"
#include "audio/MultitrackLive.h"
#include "audio/Recorder.h"
#include "model/StageTools.h"
#include "AppStore.h"
#include "BandSync.h"
#include "model/Packs.h"
#include "model/CrossApp.h"
class ProjectorOutput;

class CoreProcessor : public juce::AudioProcessor, protected juce::Timer, public wr::RigSink {
public:
    CoreProcessor (const BusesProperties& buses, const juce::String& appName);
    ~CoreProcessor() override;

    // ---- the instrument layer
    virtual wr::ClickEngine& clickEngine() = 0;                  // click / cues / count-in / bar grid
    virtual void post (const wr::Command& c) { clickEngine().post (c); } // Guitar: rig.post (forwards click types)
    virtual void renderAudio (juce::AudioBuffer<float>& buf, int numSamples) = 0; // after MIDI; call prepareOutputs()
    virtual void applyInstrumentSettings() {}                     // routing, trims, tails...
    virtual void instrumentTick() {}                              // message thread, 100 Hz
    virtual juce::String clickOutLabel (int i) const;             // AUDIO/MIDI > Click track > output choices ("" = hide)

    // ---- juce::AudioProcessor (shared parts)
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return appName_; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return store.lib.settings.midiClockMode == (int) wr::MidiClockMode::Send; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- RigSink: instrument-neutral defaults (no rig); click / cue paths go to clickEngine()
    void loadRig (const wr::RigPreset&) override {}
    void applyScene (const wr::Scene&) override {}
    void setPedal (const std::string&, bool) override {}
    void setBpm (double) override;
    void setClick (bool on, bool muted) override;
    void setMeter (int beatsPerBar) override;
    void restartBeat() override;
    void announceSection (const std::string& name, int seq) override;
    bool cueDue (int seq) override;
    void countIn (int beats, bool spoken) override;

    // ---- message-thread API for the UI
    wr::VoiceBank voice;                                          // bundled words (+ TTS renders of custom names)
    wr::AppStore store;
    wr::LiveSession& live() { return *live_; }
    void applySettings();                                         // click/cues + instrument settings + controller profiles
    void pushControllerProfiles();
    void beginMidiLearn (int unit, int sw, bool longPress);
    bool midiLearning() const { return learning_.load(); }
    // v0.17 MIDI Clock send/receive ports (USB / Bluetooth / network / virtual). Call from the message thread.
    void setMidiClockMode (wr::MidiClockMode m);
    void refreshMidiClockDevices();   // (re)open the virtual out + selected devices for the current mode
    wr::MidiClockOut& midiClockOut() { return clockOut_; }
    void setPhysicalOutputs (int n) { physOuts_.store (n); }
    void goLive (const std::string& setlistDate, int songIndex = 0);
    double nowSec() const { return juce::Time::getMillisecondCounterHiRes() * 0.001; }
    std::atomic<int> lastMidiStatus { 0 }, lastMidiData1 { 0 }, midiActivity { 0 };
    juce::String status;
    BandSync& sync() { return *sync_; }
    void syncLocalKey();
    ProjectorOutput& projector() { return *projector_; }
    // ---- v0.7 practice tracks (per song audio file; off by default, local only - not while following a leader)
    wr::PracticePlayer practice;
    bool practiceMode() const { return practiceMode_; }
    void setPracticeMode (bool on);                               // loads the current song's track
    juce::String attachPracticeTrack (const juce::File& f);       // copies into <data>/practice, sets the song's track; "" or error
    bool loadPracticeTrack();                                     // current song's file -> player (decodes mp3 / m4a / wav / aiff)
    wr::Song* practiceSong();                                     // library song for the live song (marks are edited here)
    void markSectionHere();                                       // current section starts at the playhead
    void autoMarks();                                             // estimate every section from bars x tempo, first one at the playhead
    void loopCurrentSection (bool on);
    std::string practiceSongId() const { return practiceSongId_; }
    // ---- v0.14 multitrack backing tracks (Tracks.cpp). Stems stream from <data>/Multitracks/<songId>/ (float WAV, session rate).
    wr::mt::MultitrackPlayer tracks;
    wr::mt::MultitrackLive& tracksLive() { return tracksLive_; }
    juce::String importStems (const juce::Array<juce::File>& files);   // files or one folder; converts + copies; "" or error
    void reloadTracks();                                            // after an import / mix change of structure / song change
    void tracksMixChanged();                                        // volume / pan / mute / solo / bus edits (saved with the song)
    void tracksArrangementChanged();
    void injectAction (wr::Action a);                               // UI buttons: same path as a footswitch (message thread)
    void markTracksSectionHere();                                   // current section starts at the stem playhead
    wr::Song* tracksSong() { return practiceSong(); }
    juce::String tracksWarning() const;                             // disk / CPU / rate (shown with the battery / temperature banner)
    // ---- v0.7 this device's battery / thermal (also sent to the leader while following)
    wr::sync::DeviceHealth health() const { return health_; }
    // ---- v0.8 rehearsal recorder: instrument input (+ click as ch 2 when settings.recordClick) -> <data>/Recordings/*.wav
    wr::RehearsalRecorder recorder;
    juce::String startRecording();                                // "" or error
    juce::File stopRecording();                                   // the finished take
    bool playTake (const juce::File& f);                          // plays a take through the practice player (outs 1/2)
    juce::File lastTake() const { return lastTake_; }
    juce::Array<juce::File> takes() const;                        // newest first
    static std::string today();                                   // "YYYY-MM-DD" (local)
    // ---- v0.8 backup bundle (Files app / share sheet)
    juce::File exportEverything();
    juce::Array<juce::File> exportCcliUsage (const std::string& from, const std::string& to, bool includePd); // v0.15: CSV + summary .txt in <data>/Exports                                // <data>/Exports/TabernacleTools-backup-<date>.wrbackup
    wr::ImportReport importEverything (const juce::File& f, wr::ImportMode m);

public:
    // v0.10 subscription gate. Sideload/dev builds (no WR_STORE_BUILD) unlock everything. The StoreKit 2 layer (TODO) calls
    // setEntitlements() from Transaction.updates / seat-token refresh. Read-only = library, charts, backup export only.
    wr::Entitlements entitlements;
    void setEntitlements (const wr::Entitlements& e) { entitlements = e; refreshAccess(); }
    bool fullAccess() const { return fullAccess_.load(); }
    void refreshAccess() { fullAccess_.store (entitlements.fullAccess ((int64_t) juce::Time::currentTimeMillis() / 1000)); }
    // v0.11 church seats: only App Store builds talk to the seat service (nullptr otherwise)
    SeatSession* seats() { return seats_.get(); }
    // v0.12 StoreKit 2: latest Transaction.currentEntitlements snapshot -> entitlements (message thread)
    void applyStoreEntitlements (const std::vector<wr::StoreEntitlement>& e);
    // v0.13 intro offer (2-week trial) eligibility for the single subscription group; Unknown until StoreKit answers.
    wr::IntroEligibility introEligibility() const { return intro_; }
    void refreshIntroEligibility (std::function<void()> done = {});
    const std::vector<wr::StoreEntitlement>& storeEntitlements() const { return storeEnt_; }
    bool allows (wr::Feature f) const { return entitlements.allows (f, (int64_t) juce::Time::currentTimeMillis() / 1000); }
    std::vector<wr::StoreEntitlement> storeEnt_; wr::IntroEligibility intro_ = wr::IntroEligibility::Unknown;
    // v0.18 cross-app: StoreKit trials the seat service refused (this person already had a trial in another Tabernacle Tools app)
    std::set<std::string> trialRefusedOtids_; std::set<std::string> postedJws_;
    wr::LapseGuard lapse_;   // a lapse never silences audio (rig, click, backing tracks) in the running app
    bool audioKeepsRunning() const { return lapse_.latched(); }
    std::unique_ptr<SeatSession> seats_;
protected:
    std::atomic<bool> fullAccess_ { true };
    void coreInit();                                              // end of the derived constructor (engines exist)
    void corePrepare (double sampleRate, int block);              // from the derived prepareToPlay (audio stopped)
    int prepareOutputs (juce::AudioBuffer<float>& buf, int n);    // clears unused outs, Link lock; returns usable outs
    void mixPractice (juce::AudioBuffer<float>& buf, int n);      // + v0.14 backing tracks: main -> outs 1/2, cue bus -> the click output      // practice track -> main outs 1/2 (call after the instrument renders)
    double fs_ = 48000;
private:
    void timerCallback() override;
    void prerenderCustomCues();                                   // TTS for section names the word set can't say
    juce::String appName_;
    std::unique_ptr<wr::LiveSession> live_;
    std::unique_ptr<BandSync> sync_; std::unique_ptr<ProjectorOutput> projector_;
    wr::MidiRouter router_; juce::SpinLock routerLock_; std::vector<wr::ControllerProfile> pendingProfiles_; bool profilesPending_ = false;
    wr::MidiClockOut clockOut_; std::unique_ptr<juce::MidiOutput> clockMidiOut_, clockMidiOutSel_;
    std::unique_ptr<juce::MidiInput> clockMidiIn_;
    struct ClockInCb : juce::MidiInputCallback {
        CoreProcessor* self = nullptr;
        void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& msg) override {
            if (! self) return; auto* d = msg.getRawData(); int sz = msg.getRawDataSize(); if (sz < 1) return;
            juce::SpinLock::ScopedLockType l (self->routerLock_);
            self->router_.handle (d[0], sz > 1 ? d[1] : 0, sz > 2 ? d[2] : 0, juce::Time::getMillisecondCounterHiRes());
            wr::Action a[16]; int k = self->router_.pop (a, 16);
            for (int i = 0; i < k; ++i) { if (a[i].type == wr::Action::Learned) self->learning_ = false; self->actions_.push (a[i]); }
        }
    } clockInCb_;
    int lastMidiLocate_ = -1; bool clockWasPlaying_ = false; double clockWasBpm_ = 0;
    struct LearnReq { int unit = -1, sw = 0; bool longPress = false; } learnReq_; std::atomic<bool> learnPending_ { false }, learning_ { false };
    wr::SpscQueue<wr::Action, 256> actions_;
    std::atomic<int> physOuts_ { 4 };
    double sampleCount_ = 0;
    bool practiceMode_ = false; std::string practiceSongId_; std::shared_ptr<const wr::PracticeAudio> practiceAudio_;
    wr::sync::DeviceHealth health_; int healthTick_ = 0;
    void practiceTick();
    void tracksTick();
    wr::mt::MultitrackLive tracksLive_ { tracks }; std::string tracksSongId_; int tracksArrVersion_ = -1; std::vector<float> cueBuf_;
    juce::File lastTake_; bool wasPlaying_ = false; std::string loggedKey_;
    std::set<std::string> ttsRequested_; int cueSeqSent_ = -1; double cueSentAt_ = 0; int prerenderTick_ = 0, saveCounter_ = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CoreProcessor)
};
