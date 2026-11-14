// v0.10 built-in public-domain hymn library tests.
#include "model/Hymns.h"
#include "model/Chart.h"
#include <cstdio>
#include <set>
#include <algorithm>
using namespace wr;
static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (! (c)) { ++fails; std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)
static std::string lower (std::string s) { for (auto& c : s) c = (char) std::tolower ((unsigned char) c); return s; }
int main() {
    auto& H = hymnLibrary(); CHECK (H.size() >= 30 && H.size() <= 50); std::printf ("  %d hymns\n", (int) H.size());
    std::set<std::string> ids, titles;
    for (auto& h : H) {
        CHECK (ids.insert (h.id).second); CHECK (titles.insert (h.title).second);
        CHECK (h.textYear > 1500 && h.textYear < 1931); CHECK (h.tuneYear > 1500 && h.tuneYear < 1931); CHECK (! h.tune.empty()); CHECK (! h.textAuthor.empty());
        CHECK (h.pdBasis.find ("before 1931") != std::string::npos); CHECK (h.chordPro.find ("{meta: public_domain") != std::string::npos);
        auto c = hymnChart (h); if (! c.warnings.empty()) std::printf ("  %s: %s\n", h.id.c_str(), c.warnings[0].c_str());
        CHECK (c.warnings.empty()); CHECK (c.title == h.title); CHECK (! c.key.empty()); CHECK (c.bpm >= 40 && c.bpm <= 140); CHECK (! c.time.empty()); CHECK (! c.sections.empty());
        int chords = 0; for (auto& s : c.sections) for (auto& it : s.items) chords += (int) it.line.chords.size(); CHECK (chords >= 8);
        CHECK (! hymnFirstLine (h).empty());
    }
    // excluded: not verifiably PD worldwide (or modern) - must never be bundled
    for (const char* bad : { "great is thy faithfulness", "be still, my soul", "old rugged cross", "how great thou art", "in christ alone", "10,000 reasons", "in the garden" })
        for (auto& h : H) CHECK (lower (h.title).find (bad) == std::string::npos);
    // no later added verse in Amazing Grace ("When we've been there ten thousand years" is from a different source, 1829+)
    { auto* a = findHymn ("amazing-grace"); CHECK (a); if (a) { CHECK (a->tune == "NEW BRITAIN"); CHECK (lower (a->chordPro).find ("ten thousand years") == std::string::npos); CHECK (hymnChart (*a).sections.size() == 4); } }
    { auto* b = findHymn ("be-thou-my-vision"); CHECK (b && b->textAuthor.find ("Hull") != std::string::npos); }
    // search
    { auto r = searchHymns ("amazing"); CHECK (! r.empty() && r[0]->id == "amazing-grace"); }
    { auto r = searchHymns ("HOLY"); CHECK (r.size() >= 2); CHECK (r[0]->title.rfind ("Holy", 0) == 0); }
    { auto r = searchHymns ("when peace like a river"); CHECK (! r.empty() && r[0]->id == "it-is-well"); }
    { auto r = searchHymns ("crosby"); CHECK (r.size() >= 2); for (auto* h : r) CHECK (h->textAuthor.find ("Crosby") != std::string::npos); }
    { auto r = searchHymns ("nettleton"); CHECK (r.size() == 1 && r[0]->id == "come-thou-fount"); }
    { auto r = searchHymns ("ebenezer"); CHECK (r.size() == 1 && r[0]->id == "come-thou-fount"); } // lyric match
    { auto r = searchHymns ("glaser"); CHECK (r.size() == 1 && r[0]->id == "o-for-a-thousand-tongues"); } // accent folding (Gläser)
    { auto r = searchHymns (""); CHECK (r.size() == H.size()); CHECK (std::is_sorted (r.begin(), r.end(), [] (auto* a, auto* b) { return a->title < b->title; })); }
    CHECK (searchHymns ("zzzz-not-a-hymn").empty());
    // copy into the user's library: editable, independent of the bundled hymn
    Library lib; auto id = copyHymnToLibrary (lib, "it-is-well"); CHECK (id == "it-is-well"); CHECK (lib.songs.size() == 1);
    auto id2 = copyHymnToLibrary (lib, "it-is-well"); CHECK (id2 == "it-is-well-2"); CHECK (lib.songs.size() == 2);
    lib.song (id)->chart.title = "It Is Well (our arrangement)"; lib.song (id)->chart.sections.pop_back();
    CHECK (hymnChart (*findHymn ("it-is-well")).title == "It Is Well with My Soul"); CHECK (hymnChart (*findHymn ("it-is-well")).sections.size() == 5);
    CHECK (copyHymnToLibrary (lib, "nope").empty());
    std::printf ("%d checks, %d failures\n", checks, fails); return fails ? 1 : 0;
}
