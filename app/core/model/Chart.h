// Chart import (ChordPro / chords-over-lyrics text / text extracted from PDFs) and transposition.
#pragma once
#include "Song.h"

namespace wr {

// --- import
// Accepts: ChordPro ({title:}, {key:}, {tempo:}, {start_of_verse: Verse 1}, {soc}, {comment: X}, [A]inline chords,
// {start_of_tab: Riff color=orange} ... {end_of_tab}, {start_of_highlight: pink} ... {end_of_highlight}),
// plain "chords over lyrics" text with [Verse 1] / "Chorus:" headers and "x2" repeat marks, and ASCII tab (e|--5--).
Chart parseChart (const std::string& text, const std::string& fallbackTitle = {});
std::string sectionLabelFor (const std::string& name, std::string* kindOut = nullptr, std::string* colorOut = nullptr);

// --- export
std::string chartToChordPro (const Chart& c);                               // full ChordPro writer (round-trips through parseChart)
std::string setChordProTempo (const std::string& text, double bpm);         // rewrite / insert the {tempo:} line, keep everything else
std::string formatBpm (double bpm);                                         // "72", "72.5"

// --- capo: a chart's key is the sounding key; capo > 0 means show chords as shapes in shapesKey()
struct CapoOption { int capo = 0; std::string shapes; int score = 0; };
std::string shapesKey (const std::string& soundingKey, int capo);              // A, 2 -> G
std::vector<CapoOption> capoOptions (const std::string& soundingKey, int maxCapo = 7); // best first (open shapes, low capo)
Chart chartForPart (const Chart& song, const std::string& soundingKey, int capo); // capo -1 = song's capo; returned chords are what to play
std::string capoLabel (const std::string& soundingKey, int capo);

// --- chords
bool isChordToken (const std::string& tok);
int noteIndex (const std::string& name);               // "C#" -> 1, "Bb" -> 10, -1 if not a note
bool keyPrefersFlats (const std::string& key);          // F, Bb, Eb, Ab, Db, Gb, Dm, Gm, Cm, Fm, Bbm, Ebm
std::string spellNote (int idx, bool flats);
std::string transposeKey (const std::string& key, int semis);   // "A" +1 -> "Bb"; "F#m" +1 -> "Gm"
int semitonesBetween (const std::string& fromKey, const std::string& toKey); // -5..+6
std::string transposeChord (const std::string& chord, int semis, const std::string& targetKey);

// --- v0.7 arrangement: play order as section tokens. A token is a section label ("CH", "V1"), a name ("Verse 2"), or
// shorthand (C, V2, PC, B, I, O, T: n-th section of that kind). Repeated tokens make copies (occurrence 2, 3... -> "again").
std::vector<std::string> parseArrangement (const std::string& text);        // "V1 C V2 C, B C C" -> tokens
std::string formatArrangement (const std::vector<std::string>&);
int resolveArrangementToken (const Chart&, const std::string& token);       // written section index or -1
Chart arrangedChart (const Chart&, const std::vector<std::string>& arrangement, std::vector<std::string>* unresolved = nullptr); // empty = as written
std::string firstChord (const Chart&);
// --- v0.7 practice track markers (marks[i] = start second of arranged section i, -1 = not marked)
int practiceSectionAt (const std::vector<double>& marks, double sec);       // last marked section starting at or before sec; -1 none
std::vector<double> estimatePracticeMarks (const Chart& arranged, double bpm, double firstDownbeatSec); // from bars x meter x tempo (stops at a section without bars)                                      // first chord in play order ("" if none)

// --- tabs
struct TabTransposeResult { TabBlock tab; int fretShift = 0; bool playable = true; int capo = -1; std::string advice; };
TabTransposeResult transposeTab (const TabBlock& tab, int semis, int maxFret = 22);

// --- whole chart (chords + tabs + key), returns a copy
Chart transposeChart (const Chart& c, const std::string& targetKey);

} // namespace wr
