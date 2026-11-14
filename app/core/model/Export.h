// Setlist sharing: per-player charts (own key / capo) as ChordPro and PDF. Pure C++17; the iOS share sheet sends the files.
#pragma once
#include "Song.h"
#include "Chart.h"

namespace wr {
struct ExportFile { std::string name, mime, data; };
enum class ExportFormat { ChordPro, Pdf };

// key/capo a player sees for setlist entry i. player == nullptr -> the setlist's own key/capo (the band chart)
struct PartKey { std::string soundingKey; int capo = 0; };
PartKey partKeyFor (const Library& lib, const Setlist& set, size_t entry, const Player* player);
Chart chartForPlayer (const Library& lib, const Setlist& set, size_t entry, const Player* player);

std::string setlistToChordPro (const Library& lib, const Setlist& set, const Player* player);   // one file, songs split by {new_song}
std::string setlistToPdf (const Library& lib, const Setlist& set, const Player* player);       // one PDF, each song starts a page
std::vector<ExportFile> exportSetlist (const Library& lib, const Setlist& set, const std::vector<const Player*>& players, ExportFormat f); // nullptr in players = band chart
std::vector<Chart> parseChordProSongs (const std::string& text);                                 // splits on {new_song} / {ns}
std::string safeFileName (const std::string& s);
} // namespace wr
