// v0.15 CCLI song-usage log + export for MANUAL entry on CCLI's website. No CCLI API, no automatic reporting, nothing
// leaves the iPad unless the user shares the exported files.
//  * logged automatically when a song is used in a live set (PLAY on it, or its lyrics on the projector) - never in practice
//  * one row per song per day per setlist; "projected" and "chart" flags are OR-ed over the day
//  * public-domain songs (built-in hymns, or ticked by the user) are flagged and left out of exports by default
#pragma once
#include "Song.h"

namespace wr::ccli {

// returns true when the log changed. date "YYYY-MM-DD". Song details are copied at log time (later edits don't rewrite history,
// except that an empty CCLI # is filled in at export time from the current song).
bool logUsage (Library&, const std::string& songId, const std::string& date, const std::string& setlist, bool projected, bool chartUsed);
std::string effectiveTitle (const Song&);
bool validDate (const std::string& d);

struct ExportOptions { std::string from, to; bool includePublicDomain = false; };   // inclusive; "" = open-ended
std::vector<UsageEntry> usageInRange (const Library&, const ExportOptions&);
std::string exportCsv (const Library&, const ExportOptions&);       // header + one row per log entry
std::string exportSummary (const Library&, const ExportOptions&);   // readable totals per song, missing-CCLI# list, how to enter
// v0.16 one row per song, shaped like CCLI's manual "Search & Report" flow (find the song by CCLI # or title + author, then
// report the number of uses with the (+) button): CCLI Song Number, Title, Authors, Times Used (services), Digital uses
// (lyrics projected / displayed), Charts Displayed (on band iPads - include under Digital only if your licence asks),
// First Used, Last Used. Print / Record / Translate aren't tracked by the app: add those by hand.
std::string exportTotalsCsv (const Library&, const ExportOptions&);
std::string csvField (const std::string&);

} // namespace wr::ccli
