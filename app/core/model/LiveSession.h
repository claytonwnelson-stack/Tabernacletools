// Live performance state machine: setlist position, section navigation, scenes, footswitch actions, chart view mode, cues.
// Runs on the message thread; talks to the audio rig through RigSink.
#pragma once
#include "Song.h"
#include "Chart.h"
#include "../midi/MidiRouter.h"
#include <cmath>

namespace wr {

struct RigSink {
    virtual ~RigSink() = default;
    virtual void loadRig (const RigPreset&) = 0;           // structural change (only when the song uses a different rig)
    virtual void applyScene (const Scene&) = 0;            // partial: only listed pedals change; tails spill over
    virtual void setPedal (const std::string& uid, bool on) = 0;
    virtual void setBpm (double) = 0;
    virtual void setClick (bool /*on*/, bool /*muted*/) {}
    virtual void setMeter (int /*beatsPerBar*/) {}
    virtual void restartBeat() {}                          // click / bar grid restarts on a downbeat now
    virtual void setTunerOpen (bool /*open, mutes rig output*/) {}
    // spoken cue for an upcoming section; the change is due at a bar line the sink decides (quantised to its grid)
    virtual void announceSection (const std::string& /*sectionName*/, int /*seq*/) {}
    virtual bool cueDue (int /*seq*/) { return true; }      // true once the bar line for that announce has passed
    virtual void countIn (int /*beats*/, bool /*spoken*/) {}
};
int beatsPerBarFor (const std::string& timeSig);           // "4/4" -> 4, "6/8" -> 6, "" -> 4

struct Cue { std::string unit;   // "section" | "guitar" | ""
             char sw = 0;         // 'A'..'D'
             std::string text; };

// v0.7 next-song card: shown during the last section of a song and in the gap after it (this player's key/capo)
struct NextSongInfo { bool valid = false; int index = -1; std::string title, key, capoText, time, firstChord; int capo = 0; double bpm = 0; };

class LiveSession {
public:
    enum class View { Full, Section };
    LiveSession (Library& lib, RigSink& sink) : lib_ (lib), sink_ (sink) {}

    void start (const Setlist& set, int songIndex = 0, double nowSec = 0);
    void handle (const Action& a, double nowSec);
    void tick (double nowSec);

    // --- state for the UI
    const Setlist& setlist() const { return set_; }
    int songIndex() const { return songIdx_; }
    int sectionIndex() const { return secIdx_; }
    int pass() const { return pass_; }
    const Song* song() const;
    const Chart& chart() const { return chart_; }            // transposed for this setlist entry
    std::string key() const { return chart_.key; }
    // tempo: song tempo (per song, nudge / tap / editor) unless an external MIDI clock is present, which overrides it
    double bpm() const { return clockActive() ? clockBpm_ : songBpm_; } // effective tempo the rig runs at
    double songBpm() const { return songBpm_; }                          // manual / song tempo (what "save" writes)
    double storedSongBpm() const;                                        // what the library currently holds for this song
    bool tempoEdited() const { return song() && std::abs (songBpm_ - storedSongBpm()) > 0.05; }
    bool clockActive() const { return lastClockT_ >= 0; }
    double clockBpm() const { return clockBpm_; }
    static double clockTimeoutSec (double clockBpm) { return std::max (1.0, 2.5 * 60.0 / std::max (kMinBpm, clockBpm)); }
    View view() const { return view_; }
    bool boostOn() const { return boost_; }
    bool switchCOn() const { return swC_; }
    bool switchDOn() const { return swD_; }
    std::string currentSceneName() const { return sceneName_; }
    Cue nextCue() const;
    std::string performerCue (int section) const;             // song cue for a section ("Orange riff")
    std::string nextSongTitle() const;
    int version() const { return version_; }                  // bumps on every visible change
    // --- v0.7
    std::vector<std::string> arrangement() const;             // effective play order (setlist override, else song's; empty = as written)
    void setArrangement (const std::vector<std::string>& a, bool forSetlistOnly); // edits the song (or this setlist entry) + rebuilds
    bool songEnded() const { return songEnded_; }             // auto-advance ran off the end (or the leader's song ended)
    bool inLastSection() const { return ! chart_.sections.empty() && secIdx_ == (int) chart_.sections.size() - 1 && pass_ >= chart_.sections.back().repeat; }
    bool showNextSong() const { return nextSongInfo().valid && (songEnded_ || (inLastSection() && pendingIdx_ < 0)); }
    NextSongInfo nextSongInfo() const;
    std::vector<ServiceNote> activeNotes() const;             // service notes due now (song start / section / end / gap)
    void practiceSection (int idx, double nowSec);
    void setNotes (const std::vector<ServiceNote>& n);         // this setlist item's service notes (also saved to the library setlist)
    void refreshSetlist (const Setlist& s);                   // setlist edited elsewhere (same date): keep the position if the songs are unchanged            // practice track playhead crossed a marker: land now, no cue

    // click
    bool clickOn() const { return clickOn_; }
    bool clickMuted() const { return lib_.settings.clickMuted; }
    void setClickOn (bool on);
    void toggleClickMute();                                   // remembered in settings (settingsDirty)
    // tuner (full-screen overlay; rig output muted while open)
    bool tunerOpen() const { return tunerOpen_; }
    void setTunerOpen (bool open);
    // beat-based auto-advance (no MIDI clock): Play starts the bar grid; sections with bars > 0 advance on bar boundaries
    bool playing() const { return playing_; }
    void setPlaying (bool p, double nowSec);
    int beatsPerBar() const { return beatsPerBarFor (chart_.time); }
    double beatInSection() const { return beatPos_; }
    int barInSection() const { return (int) (beatPos_ / beatsPerBar()) + 1; }
    // capo (per song, per setlist entry override)
    int capo() const { return chart_.capo; }
    std::string capoText() const { return capoLabel (chart_.key, chart_.capo); }
    void setCapoOverride (int capo);                          // -1 = song's capo
    void refreshChart() { rebuildChart(); ++version_; }        // after editing the song (capo, bars) in the library
    // quantised section changes: while the bar grid runs (click on, PLAY, or MIDI clock) next/prev/jump/goto wait for the bar line
    bool sectionPending() const { return pendingIdx_ >= 0; }
    int pendingSection() const { return pendingIdx_; }
    bool quantizing() const { return lib_.settings.quantizeSections && (clickOn_ || playing_ || clockActive()); }
    int countInBeatsForSong() const;
    // --- band sync (v0.5): a follower mirrors the leader. Its own key/capo overrides, click mute/volume and chart view stay local;
    // song/section/play come from the leader, so local navigation actions are ignored while following.
    void setFollowing (bool f) { following_ = f; if (! f) { followColors_ = false; rebuildChart(); } ++version_; }
    // v0.8 section colours: the chart's Section::color is baked from the per-song override, else the kind default (local
    // settings; while following, the leader's defaults + the leader's per-song overrides, so every iPad shows the same colours)
    void setFollowColors (const SectionColorMap& defaults, const std::map<std::string, std::vector<std::string>>& perSong) { followColors_ = true; followDefaults_ = defaults; followSongColors_ = perSong; rebuildChart(); ++version_; }
    const SectionColorMap& sectionColorDefaults() const { return following_ && followColors_ ? followDefaults_ : lib_.settings.sectionColors; }
    bool following() const { return following_; }
    // v0.16: backing tracks are playing and drive the sections (MultitrackLive): no beat-based auto-advance, and queued
    // cues land when the stems get there (practiceSection), not on this session's own bar grid
    void setTracksDrive (bool on) { tracksDrive_ = on; }
    bool tracksDrive() const { return tracksDrive_; }
    // v0.17 MIDI Clock send: quarter-note beats from song start (completed section bars + position in the current section)
    double absoluteBeats() const;
    // bumps when a section lands while playing - CoreProcessor emits Song Position Pointer
    int midiLocateGen() const { return midiLocateGen_; }
    void seekAbsoluteBeats (double beats, double nowSec); // MIDI SPP receive
    const Library& library() const { return lib_; }
    void loadSetlist (const Setlist& set, int songIndex, double nowSec);   // leader's setlist (keeps the current rig if unchanged)
    void followSong (int idx, double nowSec) { if (idx != songIdx_ || chart_.sections.empty()) loadSong (idx, nowSec); }
    void followSection (int idx, int pass, double nowSec);                 // land now (clears any queued cue)
    void followCue (int idx, int pass, bool speak);                        // queued one-bar cue (speak = announce on the local click)
    void followPlay (bool on, int countInBeats, double nowSec);
    int cueSeq() const { return cueSeq_; }
    int pendingPass() const { return pendingPass_; }
    std::string cueName (int idx, int pass) const;
    bool consumeSettingsDirty() { bool d = settingsDirty_; settingsDirty_ = false; return d; }

    // --- direct UI calls
    void gotoSection (int idx, double nowSec);
    void gotoSong (int idx, double nowSec);
    void setKeyOverride (const std::string& key);              // per-setlist key for the current song
    void setBpm (double b);                                    // manual song tempo, clamped to 30..300
    void nudgeBpm (double delta) { setBpm (songBpm_ + delta); }
    void tapTempo (double nowSec);
    bool saveTempoToSong();                                    // write the live tempo back to the song (and clear a setlist BPM override)

private:
    void loadSong (int idx, double now);
    void enterSection (int idx, int pass, double now);
    void sectionCommand (double now) { view_ = View::Section; lastSectionCmd_ = now; beatPos_ = std::fmod (beatPos_, (double) beatsPerBar()); } // manual: new section starts at the current bar
    void rebuildChart();
    Library& lib_; RigSink& sink_;
    Setlist set_; int songIdx_ = 0, secIdx_ = 0, pass_ = 1; Chart chart_; std::string rigName_;
    void applyTempo() { sink_.setBpm (bpm()); }
    void requestSection (int idx, int pass, double now);   // immediate or queued to the bar line
    bool following_ = false;
    bool followColors_ = false; SectionColorMap followDefaults_; std::map<std::string, std::vector<std::string>> followSongColors_;
    int pendingIdx_ = -1, pendingPass_ = 1, cueSeq_ = 0;
    bool clickOn_ = false, clickHeldOff_ = false, tunerOpen_ = false, playing_ = false, settingsDirty_ = false; double beatPos_ = 0, lastTick_ = -1;
    double songBpm_ = kDefaultBpm, clockBpm_ = 0, lastClockT_ = -1; View view_ = View::Full; double lastSectionCmd_ = -1e9;
    bool songEnded_ = false, tracksDrive_ = false;
    bool boost_ = false, swC_ = false, swD_ = false; std::string sceneName_; TapTempo tap_; int version_ = 0;
    int midiLocateGen_ = 0;
};

} // namespace wr
