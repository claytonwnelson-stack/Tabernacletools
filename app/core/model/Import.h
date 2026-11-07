// v0.7 import of shared / pasted chord charts (iOS Share sheet, "Copy to Tabernacle Tools", Paste chart).
// Only content the user shares or pastes is used - nothing is ever fetched from a URL.
// Handles ChordPro, chords-over-lyrics text, and Ultimate-Guitar-style text: [Verse] / [Chorus] tags, [ch]G[/ch] chord
// markup, [tab]...[/tab] blocks, HTML entities, and page clutter around the chart (menus, ratings, comments) when a whole
// page's text is shared. Picks up "Key:", "Capo:", "Tempo/BPM:", "<Title> Chords by <Artist>" / "<Artist> - <Title>".
#pragma once
#include "Chart.h"

namespace wr {

struct ImportPreview {
    Chart chart;                 // parsed (title / artist / key / capo / sections) - shown on the confirm screen, editable
    bool keyGuessed = false;     // key came from the chords, not from the text
    bool urlOnly = false;        // only a link was shared: nothing to import (we never download it)
    int chordCount = 0, lyricLines = 0;
    std::vector<std::string> notes;   // what was cleaned up ("removed 12 lines of page text around the chart")
    bool ok() const { return ! chart.sections.empty() && chordCount > 0; }
};

std::string decodeEntities (const std::string&);            // &amp; &#39; &quot; &nbsp; &lt; &gt; &#NNN;
std::string normalizeSharedText (const std::string& raw, std::vector<std::string>* notes = nullptr); // UG markup -> plain chart text
ImportPreview previewImport (const std::string& raw, const std::string& fallbackTitle = {});
std::string guessKey (const Chart&);                         // most likely key from the chords ("" if no chords)

} // namespace wr
