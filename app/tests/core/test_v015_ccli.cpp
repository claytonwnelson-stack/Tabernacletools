// v0.15 tests: CCLI song-usage log + CSV / summary export (manual entry; no API)
#include "model/Ccli.h"
#include "model/Hymns.h"
#include "model/Chart.h"
#include <cstdio>
using namespace wr;
static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (! (c)) { ++fails; std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)
int main() {
    Library lib; Song a; a.id = "a"; a.chart.title = "Way Maker"; a.ccli.number = "7115744"; a.ccli.authors = "Osinachi Okoro"; lib.songs.push_back (a);
    Song b; b.id = "b"; b.chart.title = "New Song, \"Live\""; lib.songs.push_back (b);
    const auto& hy = hymnLibrary(); CHECK (! hy.empty());
    const std::string hid = copyHymnToLibrary (lib, hy.front().id); CHECK (! hid.empty() && lib.song (hid)->ccli.publicDomain);
    // logging: merge per song/day/setlist, OR flags, reject bad input
    CHECK (ccli::logUsage (lib, "a", "2026-11-08", "Sunday", false, true));
    CHECK (! ccli::logUsage (lib, "a", "2026-11-08", "Sunday", false, true));      // same: no change
    CHECK (ccli::logUsage (lib, "a", "2026-11-08", "Sunday", true, false));       // now projected too
    CHECK (lib.usageLog.size() == 1 && lib.usageLog[0].projected && lib.usageLog[0].chart);
    CHECK (ccli::logUsage (lib, "a", "2026-11-15", "Sunday", false, true));
    CHECK (ccli::logUsage (lib, "b", "2026-11-15", "Sunday", true, false));
    CHECK (ccli::logUsage (lib, hid, "2026-11-15", "Sunday", true, true));
    CHECK (! ccli::logUsage (lib, "nope", "2026-11-15", "Sunday", true, true));
    CHECK (! ccli::logUsage (lib, "a", "11/15/2026", "Sunday", true, true));
    CHECK (! ccli::logUsage (lib, "a", "2026-11-22", "Sunday", false, false));
    CHECK (lib.usageLog.size() == 4);
    // persistence
    Library back = libraryFromJson (toJson (lib));
    CHECK (back.usageLog.size() == 4 && back.usageLog[0].ccliNumber == "7115744" && back.usageLog[0].projected);
    CHECK (back.song ("a")->ccli.number == "7115744" && back.song (hid)->ccli.publicDomain);
    // range + PD exclusion
    ccli::ExportOptions o; o.from = "2026-11-10"; o.to = "2026-11-30";
    auto r = ccli::usageInRange (lib, o); CHECK (r.size() == 2);                    // PD hymn left out by default
    o.includePublicDomain = true; CHECK (ccli::usageInRange (lib, o).size() == 3); o.includePublicDomain = false;
    // a CCLI # added later is filled in at export
    lib.song ("b")->ccli.number = "1234567";
    const auto csv = ccli::exportCsv (lib, o);
    CHECK (csv.rfind ("Date,Song Title,CCLI Song Number,Authors,Public Domain,Lyrics Projected,Chart Used,Setlist\r\n", 0) == 0);
    CHECK (csv.find ("\"New Song, \"\"Live\"\"\",1234567") != std::string::npos);
    CHECK (csv.find ("2026-11-08") == std::string::npos && csv.find (hy.front().title) == std::string::npos);
    CHECK (ccli::csvField ("=HYPERLINK(1)") == "'=HYPERLINK(1)");
    ccli::ExportOptions all; const auto sum = ccli::exportSummary (lib, all);
    CHECK (sum.find ("Way Maker  |  CCLI # 7115744  |  used 2x") != std::string::npos);
    CHECK (sum.find ("public-domain songs left out") != std::string::npos && sum.find ("by hand") != std::string::npos);
    lib.song ("b")->ccli.number.clear(); for (auto& e : lib.usageLog) if (e.songId == "b") e.ccliNumber.clear();
    CHECK (ccli::exportSummary (lib, all).find ("1 song(s) have no CCLI number") != std::string::npos);
    std::printf ("v0.15 ccli: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
