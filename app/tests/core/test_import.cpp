// v0.7 share/paste import parser tests (UG [ch]/[tab] markup, [Verse] tags, page cruft, metadata, key guess, links).
#include "model/Import.h"
#include <cstdio>

static int fails = 0, checks = 0;
template <typename T> static std::string toS (const T& v) { if constexpr (std::is_same_v<T, std::string>) return v; else if constexpr (std::is_arithmetic_v<T>) return std::to_string (v); else return "?"; }
#define CHECK(c) do { ++checks; if (! (c)) { ++fails; std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_EQ(a, b) do { ++checks; auto _a = (a); auto _b = (b); if (! (_a == _b)) { ++fails; std::printf ("FAIL %s:%d  %s == %s\n   got [%s] want [%s]\n", __FILE__, __LINE__, #a, #b, toS (_a).c_str(), toS (_b).c_str()); } } while (0)
using namespace wr;

static const char* kUg =
 "Ultimate Guitar\nTabs  Courses  Shop\nMorning Song Chords by Test Band\n4,312 views\nKey: G\nCapo: 2nd fret\n\n"
 "[Intro]\n[ch]G[/ch]  [ch]D[/ch]  [ch]Em[/ch]  [ch]C[/ch]\n\n[Verse 1]\n[ch]G[/ch]          [ch]D[/ch]\nLight breaks over the hills\n"
 "[ch]Em[/ch]            [ch]C[/ch]\nWe lift our voices high\n\n[Chorus]\n[tab][ch]G[/ch]        [ch]D[/ch]\nRise up, morning is here[/tab]\n\n"
 "Last update: Oct 2026\nRating 4.8\n12 Comments\nLog in to comment\n";

int main() {
    { auto p = previewImport (kUg);
      CHECK (p.ok()); CHECK_EQ (p.chart.title, std::string ("Morning Song")); CHECK_EQ (p.chart.artist, std::string ("Test Band"));
      CHECK_EQ (p.chart.capo, 2); CHECK_EQ (p.chart.key, std::string ("A")); CHECK (! p.keyGuessed);   // G shapes + capo 2 = sounds in A
      CHECK_EQ ((int) p.chart.sections.size(), 3); CHECK_EQ (p.chart.sections[1].label, std::string ("V1")); CHECK_EQ (p.chart.sections[2].label, std::string ("CH"));
      CHECK_EQ (p.chordCount, 10); CHECK_EQ (p.lyricLines, 3);
      CHECK_EQ (p.chart.sections[1].items[0].line.chords[1].pos, 11);       // column kept after [ch] removal
      CHECK_EQ (p.chart.sections[1].items[0].line.lyric, std::string ("Light breaks over the hills"));
      CHECK_EQ (p.chart.sections[2].items[0].line.lyric, std::string ("Rise up, morning is here"));   // [/tab] gone
      CHECK_EQ ((int) p.notes.size() >= 2, true); }
    { std::vector<std::string> n; auto s = normalizeSharedText ("[ch]Am[/ch] &amp; [ch]F[/ch]\nIt&#39;s &quot;here&quot;&nbsp;now\n", &n);
      CHECK (s.find ("[ch]") == std::string::npos); CHECK (s.find ("It's \"here\" now") != std::string::npos); }
    CHECK_EQ (decodeEntities ("a&#8217;b &lt;c&gt;"), std::string ("a'b <c>"));
    { auto p = previewImport ("Test Band - Morning Song Chords\r\n\r\n[Verse]\r\nG    C    D    G\r\nHello there\r\n");   // CRLF, "Artist - Title Chords"
      CHECK_EQ (p.chart.title, std::string ("Morning Song")); CHECK_EQ (p.chart.artist, std::string ("Test Band"));
      CHECK_EQ (p.chart.key, std::string ("G")); CHECK (p.keyGuessed); CHECK_EQ (p.chordCount, 4); }
    { auto p = previewImport ("{title: CP Song}\n{key: D}\n{start_of_chorus}\n[D]Sing [G]out\n{end_of_chorus}\n");   // ChordPro passes straight through
      CHECK_EQ (p.chart.title, std::string ("CP Song")); CHECK_EQ (p.chart.key, std::string ("D")); CHECK (! p.keyGuessed); CHECK (p.ok()); }
    { auto p = previewImport ("https://tabs.ultimate-guitar.com/tab/x/y-chords-123");   // never fetched
      CHECK (p.urlOnly); CHECK (! p.ok()); CHECK (! p.notes.empty()); }
    { auto p = previewImport ("Just some words\nno chords here\n"); CHECK (! p.ok()); }
    { auto p = previewImport ("[Chorus]\nAm      F\nWe sing\nC       G\nAll day\nAm\nYes\n", "Pasted");
      CHECK_EQ (p.chart.title, std::string ("Pasted")); CHECK_EQ (p.chart.key, std::string ("Am")); }
    CHECK_EQ (guessKey (parseChart ("[Bb]x [Eb]y [F]z [Gm]w [Bb]q")), std::string ("Bb"));
    CHECK_EQ (guessKey (parseChart ("[E]x [A]y [B]z [C#m]w [E]q")), std::string ("E"));
    CHECK_EQ (guessKey (Chart{}), std::string ());
    { auto p = previewImport ("[Verse 2]\n[ch]D[/ch]   [ch]A/C#[/ch]  [ch]Bm[/ch]\nWords go here\n[Bridge]\n[ch]G[/ch] [ch]A[/ch]\nMore\n");   // slash chords, no header
      CHECK_EQ (p.chart.sections[0].label, std::string ("V2")); CHECK_EQ (p.chart.sections[1].label, std::string ("BR")); CHECK_EQ (p.chart.key, std::string ("D")); }
    { auto p = previewImport ("Key:\n\nnot a chart\n"); CHECK (p.chart.title.size() < 200); }
    { std::string junk (300, '\0'); junk += "\xff\xfe<<<>>>"; auto p = previewImport (junk, "Bad"); CHECK (p.chart.title.size() < 500); }
    { auto p = previewImport ("", "Empty"); CHECK_EQ (p.chart.title, std::string ("Empty")); CHECK (! p.ok()); }
    { auto p = previewImport (std::string (8000, 'A') + "\n[Verse]\nG C\nhello\n"); CHECK (p.chordCount >= 1); }
    { auto p = previewImport ("[Verse 1]\nG\nalpha line\n\n[Chorus]\nD\nwe sing this\n\n[Verse 2]\nG\nbeta line\n\nD\nwe sing this\n");
      CHECK_EQ ((int) p.chart.sections.size(), 4);
      CHECK_EQ (p.chart.sections[2].label, std::string ("V2"));
      CHECK_EQ (p.chart.sections[3].kind, std::string ("chorus"));
      CHECK_EQ (p.chart.sections[3].items[0].line.lyric, std::string ("we sing this")); }
    { auto p = previewImport ("Verse 1\nG\nhello\n\nChorus\nD\nsing\n\nVerse 2 (soft)\nG\nagain\n\nV2\nA\nthird\n");
      CHECK ((int) p.chart.sections.size() >= 3);
      CHECK_EQ (p.chart.sections[0].label, std::string ("V1"));
      CHECK_EQ (p.chart.sections[1].kind, std::string ("chorus"));
      bool sawSoft = false, sawV2 = false;
      for (auto& s : p.chart.sections) { if (s.name == "Verse 2") sawSoft = true; if (s.label == "V2") sawV2 = true; }
      CHECK (sawSoft); CHECK (sawV2); }
    { Chart c = parseChart ("[Verse 1]\nG\nhello there\nD\nsecond line\n[Chorus]\nA\nwe sing\n");
      c.arrangement = { "V1", "CH" };
      Chart before = c;
      auto dup = duplicateSection (c, 1);
      CHECK (dup.ok); CHECK_EQ ((int) c.sections.size(), 3); CHECK_EQ (c.sections[2].name, std::string ("Chorus"));
      CHECK_EQ (c.sections[2].items[0].line.lyric, std::string ("we sing")); CHECK_EQ (c.sections[2].kind, std::string ("chorus"));
      CHECK (c.sections[2].customColor.empty());
      auto spl = splitSection (c, 0, 1, "Verse 2");
      CHECK (spl.ok); CHECK_EQ (c.sections[0].items[0].line.lyric, std::string ("hello there"));
      CHECK_EQ (c.sections[1].name, std::string ("Verse 2")); CHECK_EQ (c.sections[1].label, std::string ("V2"));
      CHECK_EQ (c.sections[1].kind, std::string ("verse")); CHECK (c.sections[1].customColor.empty());
      CHECK_EQ (c.sections[1].items[0].line.lyric, std::string ("second line"));
      auto ren = renameSection (c, 1, "Bridge");
      CHECK (ren.ok); CHECK_EQ (c.sections[1].kind, std::string ("bridge")); CHECK_EQ (c.sections[1].label, std::string ("BR"));
      CHECK_EQ (c.sections[1].color, std::string ("purple")); CHECK (c.sections[1].customColor.empty());
      std::vector<std::string> tokens = before.arrangement;
      remapArrangement (tokens, before, c, ren.oldToNew);
      auto merged = mergeSections (c, 0);
      CHECK (merged.ok); CHECK_EQ (c.sections[0].items[0].line.lyric, std::string ("hello there"));
      CHECK_EQ (c.sections[0].items[1].line.lyric, std::string ("second line"));
      CHECK_EQ ((int) c.sections.size(), 3);
      auto moved = moveSection (c, 0, 2);
      CHECK (moved.ok); CHECK_EQ (c.sections[2].items[0].line.lyric, std::string ("hello there"));
      CHECK_EQ (moved.oldToNew[0], 2);
      auto dead = deleteSection (c, 2); CHECK (dead.ok); CHECK_EQ ((int) c.sections.size(), 2); CHECK_EQ (dead.oldToNew[2], -1);
      CHECK (setSectionBody (c.sections[0], "G\nnew lyric\n"));
      CHECK_EQ (c.sections[0].items[0].line.lyric, std::string ("new lyric"));
      CHECK (sectionBodyText (c.sections[0]).find ("new lyric") != std::string::npos); }
    { const char* tab = "Intro riff\ne|--0--3--|\nB|--1--0--|\nG|--0--0--|\nD|--2--0--|\nA|--3--2--|\nE|--0--3--|\n\nChorus lick\ne|--5--|\nB|--5--|\nG|--5--|\nD|--5--|\nA|--5--|\nE|--5--|\n";
      auto parts = parseTabText (tab); CHECK_EQ ((int) parts.size(), 2); CHECK_EQ (parts[0].title, std::string ("Intro riff")); CHECK_EQ ((int) parts[1].tab.strings.size(), 6);
      Chart c = parseChart ("[Verse 1]\nG\nhello\n[Chorus]\nD\nsing\n");
      CHECK (addSectionTab (c.sections[0], parts[0].tab)); CHECK (addSectionTab (c.sections[0], parts[1].tab));
      CHECK_EQ (sectionTabCount (c.sections[0]), 2);
      CHECK (replaceSectionTab (c.sections[0], 1, parts[0].tab)); CHECK_EQ (c.sections[0].items.back().tab.title, std::string ("Intro riff"));
      Chart up = transposeChart (c, "A"); CHECK (sectionTabCount (up.sections[0]) == 2);
      CHECK (up.sections[0].items.back().tab.strings[0] != parts[0].tab.strings[0]);
      CHECK (removeSectionTab (c.sections[0], 0)); CHECK_EQ (sectionTabCount (c.sections[0]), 1);
      Library lib = seedLibrary(); Song song; song.id = "edit-me"; song.chart = parseChart ("[Verse 1]\nG\none\n[Chorus]\nD\ntwo\n"); song.chart.arrangement = { "V1", "CH" };
      lib.songs.push_back (song); Setlist set; set.date = "2026-11-11"; set.entries.push_back ({ "edit-me", "", 0, "lead" }); set.entries[0].arrangement = { "V1", "CH" }; lib.setlists.push_back (set);
      Chart before = lib.songs.back().chart; auto ed = renameSection (lib.songs.back().chart, 0, "Verse 2");
      commitChartEdit (lib, "edit-me", before, lib.songs.back().chart, ed.oldToNew);
      CHECK_EQ (lib.song ("edit-me")->chart.sections[0].label, std::string ("V2"));
      CHECK (lib.song ("edit-me")->chart.arrangement[0] == "V2" || lib.song ("edit-me")->chart.arrangement[0] == "Verse 2");
      CHECK_EQ (lib.setlists.back().entries[0].arrangement.size(), (size_t) 2); }
    std::printf ("%d/%d import checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
