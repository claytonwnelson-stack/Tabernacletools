// Tabernacle Tools - data model: charts, songs, scenes, rig presets, setlists, controller profiles.
#pragma once
#include "Json.h"
#include "SectionColors.h"
#include "Multitrack.h"
#include <string>
#include <vector>
#include <map>

namespace wr {

// ---------------------------------------------------------------- chart
struct ChordAt { int pos = 0; std::string chord; };              // pos = character index into the lyric
struct ChartLine { std::vector<ChordAt> chords; std::string lyric; std::string highlight; }; // highlight: "", "orange", "pink", ...
struct TabBlock {
    std::string title, color;                // color: "orange", "pink", "blue", ...
    std::vector<std::string> strings;         // high e first, e.g. "e|-----5---|"
    std::string note;                         // e.g. capo advice after a transpose
};
struct ChartItem { enum Kind { Line, Tab, Comment } kind = Line; ChartLine line; TabBlock tab; std::string text; };
struct Section {
    std::string name;      // "Verse 1"
    std::string label;     // "V1", "PC", "CH" ... (chips + MIDI cues)
    std::string kind;      // verse, prechorus, chorus, bridge, tag, intro, outro, interlude, other
    std::string color;     // ui accent: the effective colour (kind default or override), see SectionColors.h
    std::string customColor; // v0.8 per-song override ("" = use the kind default). ChordPro {x_section_color: Verse 1=teal}
    int repeat = 1;        // "x2"
    int bars = 0;          // length in bars for beat-based auto-advance (0 = manual only)
    std::vector<ChartItem> items;
    int occurrence = 1;    // v0.7 arrangement: 2nd, 3rd... time this section is played in the arranged order ("Chorus again")
    int source = -1;       // index of the written section this copy came from (-1 = not arranged)
};
// ---------------------------------------------------------------- tempo
constexpr double kMinBpm = 30.0, kMaxBpm = 300.0, kDefaultBpm = 72.0;
inline double clampBpm (double b) { if (! (b == b) || b <= 0) return 0; b = b < kMinBpm ? kMinBpm : b > kMaxBpm ? kMaxBpm : b; return (double) (long long) (b * 10.0 + 0.5) / 10.0; } // 0 = "not set"

inline int countInBeats (int code, int beatsPerBar) { return code >= 101 && code <= 104 ? (code - 100) * beatsPerBar : code < 0 ? 0 : code; }

struct Chart { std::string title, artist, key; double bpm = 0; std::string time; int capo = 0; /* key = sounding key; capo > 0 -> chords shown as shapes */ std::vector<Section> sections; std::vector<std::string> warnings;
               std::vector<std::string> arrangement; /* v0.7: play order as section tokens ("V1 C V2 C B C C"); empty = as written. ChordPro {arrangement: ...} */ };

// v0.7 practice track: an audio file per song + where each arranged section starts in it (seconds, -1 = not marked)
struct PracticeTrack { std::string file; std::vector<double> marks; bool empty() const { return file.empty(); } };

// ---------------------------------------------------------------- rig + scenes
struct PedalDef { std::string uid, type; bool enabled = false; std::map<std::string, float> params; std::string file; }; // file: v0.10 user IR path (amp/cab)
struct RigPreset { std::string name; std::vector<PedalDef> lanes[2]; };     // [0] before amp, [1] after amp
struct PedalState { bool enabled = false; std::map<std::string, float> params; };
struct Scene { std::string name; std::map<std::string, PedalState> pedals; }; // only listed pedals change

struct SwitchAssign { std::string label, pedalUid; };                          // guitar unit C/D per song

struct Song {
    std::string id; Chart chart; std::string rigPreset = "Worship Ambient";
    std::vector<Scene> scenes; std::map<std::string, std::string> sectionScene; // section label or name -> scene name
    std::string defaultScene;
    SwitchAssign switchC, switchD;
    std::map<std::string, std::string> cues;
    bool clickDefault = false;                                                 // click on when this song loads
    int countIn = -1;                                                          // -1 = global setting; else as AppSettings::countIn
    // v0.15 CCLI details (optional; for the usage log the user enters on CCLI's site by hand - nothing is sent anywhere)
    struct Ccli { std::string number, title, authors; bool publicDomain = false; /* built-in hymns: PD, excluded from exports by default */ } ccli;
    mt::MultitrackSet multitrack;                                              // v0.14 backing-track stems (files stay on this device; mutes remembered)
    PracticeTrack practice;                                                    // v0.7 (library only; the file stays on this device)
    const Scene* scene (const std::string& n) const { for (auto& s : scenes) if (s.name == n) return &s; return nullptr; }
    const Scene* sceneForSection (const Section& s) const;
};

// ---------------------------------------------------------------- setlist
// v0.7 service note on a setlist item ("Pastor prays after this song"). at: "start" (while the song starts), "end" (last
// section + the gap after), "after" (only in the gap after the song), or a section token ("Bridge", "C", "V2") = while in it
struct ServiceNote { std::string text, at = "start"; bool operator== (const ServiceNote& o) const { return text == o.text && at == o.at; } };
struct SetlistEntry { std::string songId; std::string keyOverride; double bpmOverride = 0; std::string role = "lead"; int capoOverride = -1; // role: lead / sing / both
                      std::vector<std::string> arrangement;   // v0.7: per-setlist arrangement override (empty = song's)
                      std::vector<ServiceNote> notes; };      // v0.7
// a musician on the team: their own key (e.g. a capo player or a different sounding key) per song, used for exports
struct PlayerPart { std::string key; int capo = -1; };                         // key "" = setlist key; capo -1 = player default
struct Player { std::string name, instrument; int capo = 0; std::map<std::string, PlayerPart> songs; }; // songId -> part
struct Setlist { std::string name, date; std::vector<SetlistEntry> entries; std::vector<Player> players; };   // date "YYYY-MM-DD"

// ---------------------------------------------------------------- MIDI controllers
struct MidiBinding { enum Kind { None, CC, PC, Note } kind = None; int number = -1; int channel = 0; // channel 1..16, 0 = profile channel
                     bool matches (int st, int d1, int profileCh, bool ignoreChannel = false) const; };
struct SwitchBinding { MidiBinding press, longPress; std::string action, longAction; int longMs = 0; }; // longMs 0 = profile default
struct ControllerProfile {
    std::string name, role;       // role: "guitar" | "section"
    int channel = 1;              // 1..16
    SwitchBinding sw[4];          // A..D
    int longPressMs = 500;
    // v0.8 banks / modes: when banks is non-empty the actions come from banks[bank] (MIDI bindings stay in sw[]).
    // "bankNext" / "bankPrev" actions step through them. Saved in the library (per device / user).
    struct Bank { std::string name; std::string action[4], longAction[4]; };
    std::vector<Bank> banks; int bank = 0;
    // Factory Chocolate: E is A+B together, F is C+D together. Empty means the user cleared it.
    std::string comboE, comboF;
};
void enableDefaultBanks (ControllerProfile&);   // bank 1 = the current mapping, bank 2 = a template (guitar: click/transport, section: songs)

struct ProjectorSettings {
    int linesPerSlide = 2;            // 1..8; a section's lyric lines are split into balanced slides of at most N lines
    bool titleSlide = true;           // song title (+ artist) before the first lyric
    bool blankBetweenSongs = true;    // blank / logo slide after the song ends (and before the title, if no title slide)
    int fontSize = 2;                 // 0 S, 1 M, 2 L (default), 3 XL - text auto-shrinks to fit
    bool uppercase = false;
    std::string background;           // image file, "" = black
    float backgroundDim = 0.45f;      // darken the image so white text stays readable
    std::string logo;                 // image shown on the blank slide ("" = plain black)
    bool showNotes = false;           // v0.7: service notes as a strip at the bottom of the projector (off by default)
};

ProjectorSettings projectorSettingsFromJson (const Json&);

// ---------------------------------------------------------------- library = everything persisted
struct AppSettings {
    int routing = 0;              // 0 four-cable, 1 two-cable
    float trimPreDb = -10, trimPostDb = 0; // line -> guitar-level pad on out1 by default
    int autoReturnSec = 10; int externalChannel = 16; int noteBase = 36;
    // v0.17 MIDI Clock: 0 Off, 1 Send (24 ppqn + Start/Stop/Continue + SPP on section jumps), 2 Receive (follow external tempo/transport).
    // followMidiClock is kept as a convenience mirror of (midiClockMode == 2) for older call sites.
    int midiClockMode = 0;        // Off by default (Clayton Nov 4: MIDI clock send/receive, leave Link off)
    bool followMidiClock = false; // == (midiClockMode == Receive); updated when the mode changes
    std::string midiClockOutId;   // empty = virtual "Tabernacle Tools Clock" (+ every enabled host MIDI out when sending)
    std::string midiClockInId;    // empty = all MIDI inputs (host / Audio MIDI settings); else match by device identifier
    // click track
    int clickOut = 2;             // 0 = outs 1/2, 1 = outs 3/4, 2 = out 2 only (mono; default - free in four-cable, where outs 3/4 are the amp return)
    // spoken section cues on the click output; section commands wait for the next bar line while the grid runs
    bool voiceCues = true; float cueDb = -6; bool cuesWhenClickMuted = false; bool quantizeSections = true;
    // count-in before PLAY: 0 off, N clicks, or 101/102 = 1/2 bars of the song's meter
    int countIn = 8; bool countInSpoken = false;
    float clickDb = -12; bool clickMuted = false; bool clickAccent = true;
    int clickPreset = 0;          // ClickGen::Preset: beep, woodblock, cowbell, rimshot/sidestick
    float downbeatDb = 4.4f;      // downbeat level vs beats 2..n (its sound always differs)
    float tunerRefHz = 440;       // A4 reference
    bool autoAdvance = true;      // beat-based section advance (sections with bars, no MIDI clock, after Play)
    float rigTailSec = 3;         // old rig's delay/reverb ring-out on a rig change
    // v0.5 band sync + projector
    std::string deviceName = "Tabernacle Tools";   // shown to the band (Bonjour name / follower list)
    std::string lastLeaderHost; int lastLeaderPort = 0;   // one-tap rejoin
    bool useLink = false;                     // Ableton Link (off: Clayton chose MIDI clock + Wi-Fi sync; WR_WITH_LINK defaults OFF)
    ProjectorSettings projector;
    SectionColorMap sectionColors;            // v0.8 per-kind default colours (only kinds changed from the built-ins)
    bool stageMode = false;                   // ignored: one dark appearance (old libraries may still contain the key)
    bool autoScroll = true;                   // v0.8 full-song view scrolls with the bar position (not only per section)
    bool recordClick = false;                 // v0.8 rehearsal recorder: add the click as a 2nd channel
    int repeatWarnDays = 21;                  // v0.8 song stats: flag a song played within this many days
    int ampOversampling = 4;                  // v0.12 amp/drive anti-aliasing: 1 (Off), 2, 4 (default), 8
    bool autoOversampling = true;
    int trackJumpAt = 0;                      // v0.14 backing tracks: section jumps land 0 = on the next bar line, 1 = at the end of the section             // v0.12 step it down automatically when the DSP gets close to the audio deadline
    // Two factory Chocolates send the same messages. These CoreMIDI identifiers say which USB port is which.
    std::string guitarPedalId, sectionPedalId, guitarPedalName, sectionPedalName;
};
// v0.15 CCLI song-usage log: one row per song per day per setlist (live sets only, never practice); flags are OR-ed
struct UsageEntry { std::string date, songId, title, ccliNumber, authors, setlist; bool publicDomain = false, projected = false, chart = false; };
struct PlayEntry { std::string songId, date; bool operator== (const PlayEntry& o) const { return songId == o.songId && date == o.date; } }; // date "YYYY-MM-DD"
struct Library {
    std::vector<Song> songs; std::vector<Setlist> setlists; std::vector<RigPreset> rigs; std::vector<ControllerProfile> controllers; AppSettings settings;
    std::vector<PlayEntry> playLog;           // v0.8 song stats
    std::vector<UsageEntry> usageLog;         // v0.15 CCLI usage log (local; exported as CSV for manual entry)
    Song* song (const std::string& id) { for (auto& s : songs) if (s.id == id) return &s; return nullptr; }
    const RigPreset* rig (const std::string& n) const { for (auto& r : rigs) if (r.name == n) return &r; return nullptr; }
    Setlist* setlist (const std::string& date) { for (auto& s : setlists) if (s.date == date) return &s; return nullptr; }
};

// JSON
Json toJson (const ProjectorSettings&);
Json toJson (const Chart&); Chart chartFromJson (const Json&);
Json toJson (const Song&);  Song songFromJson (const Json&);
Json toJson (const RigPreset&); RigPreset rigFromJson (const Json&);
Json toJson (const Setlist&); Setlist setlistFromJson (const Json&);
Json toJson (const ControllerProfile&); ControllerProfile controllerFromJson (const Json&);
Json toJson (const Library&); Library libraryFromJson (const Json&);

// Defaults
ControllerProfile defaultSectionUnit();  // M-Vave Chocolate Plus on ch 2
ControllerProfile defaultGuitarUnit();   // M-Vave Chocolate Plus on ch 1
RigPreset defaultRig();                  // "Worship Ambient"
Library seedLibrary();                   // default rig, controllers, seed song "He who was / He who is", a sample setlist

} // namespace wr
