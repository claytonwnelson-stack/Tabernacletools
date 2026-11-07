// v0.8 core helpers (every app): song stats / play history, one-file backup bundle (export + import), capo helper,
// auto-scroll position, stage-mode text scale.
#pragma once
#include "Song.h"
#include "Chart.h"

namespace wr {
// ---- song stats. Dates are "YYYY-MM-DD".
int daysFromCivil (const std::string& date);                          // days since 1970-01-01, INT_MIN-ish (-1000000) if bad
bool logPlay (Library&, const std::string& songId, const std::string& date);   // once per song per day; false = already logged / bad
std::vector<std::string> playHistory (const Library&, const std::string& songId); // newest first
int daysSinceLastPlayed (const Library&, const std::string& songId, const std::string& today, bool excludeToday = true); // -1 never
int playsWithin (const Library&, const std::string& songId, const std::string& today, int days);
bool playedRecently (const Library&, const std::string& songId, const std::string& today);  // within settings.repeatWarnDays, not today
std::string recentText (const Library&, const std::string& songId, const std::string& today); // "Played 9 days ago" / ""
struct SongStat { std::string songId, title, last; int plays = 0; };
std::vector<SongStat> songStats (const Library&);                     // most played first

// ---- backup bundle: everything (songs, setlists, rig presets, controllers + banks, settings, play log) in one file
std::string exportBundle (const Library&, const std::string& appName, long long nowUnix);
enum class ImportMode { Replace, Merge };   // Merge: add songs / setlists / rigs not present (by id / date / name); keeps local settings
struct ImportReport { bool ok = false; std::string error; int songs = 0, setlists = 0, rigs = 0, skipped = 0; };
ImportReport importBundle (const std::string& text, Library& lib, ImportMode);   // accepts a bundle or a bare library.json

// ---- capo helper: for a target sounding key, capo positions with the open shapes you'd play (in play order)
struct CapoHelp { int capo = 0; std::string shapesKey; std::vector<std::string> shapes; int openShapes = 0; int score = 0; };
std::vector<CapoHelp> capoHelper (const Chart&, const std::string& targetKey, int maxCapo = 7); // best first
std::string shapesText (const CapoHelp&);                             // "Capo 2 - G shapes: G C D Em"

// ---- auto-scroll: where the top of the view should be so the current line keeps pace with the bar position
// sectionTop/Height in content px; progress 0..1 through the section; returns the scroll offset (clamped)
float autoScrollOffset (float sectionTop, float sectionHeight, double progress, float viewHeight, float contentHeight, float lead = 40.0f);
double sectionProgress (double beatInSection, int beatsPerBar, int bars, int repeat = 1); // 0..1 (0 when bars unknown)

// ---- stage mode
inline float stageTextScale (const AppSettings& s) { return s.stageMode ? 1.35f : 1.0f; }
} // namespace wr
