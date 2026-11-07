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
    std::printf ("%d/%d import checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
