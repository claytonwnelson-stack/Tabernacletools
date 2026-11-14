// v0.12 tests: StoreKit 2 entitlement mapping, read-only feature locks, Live footswitch section targets, cab IR import.
#include "model/StoreMapping.h"
#include "model/Packs.h"
#include "model/SectionColors.h"
#include "model/Chart.h"
#include "dsp/AmpSection.h"
#include "dsp/RigBuilder.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <vector>
using namespace wr;
static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (! (c)) { ++fails; std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)
static const std::string IND = "com.tabernacletools.guitar.individual.monthly", CH = "com.tabernacletools.guitar.church.monthly";
static StoreEntitlement ent (const std::string& p, int64_t exp, const std::string& st = "subscribed", bool trial = false) {
    StoreEntitlement e; e.productId = p; e.expiresAt = exp; e.state = st; e.trial = trial; e.jws = "jws-" + p + "-" + std::to_string (exp); return e; }

static std::vector<uint8_t> wav (int n, int sr = 48000, float amp = 0.5f) {
    std::vector<uint8_t> b; auto u32 = [&] (uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back ((uint8_t) (v >> (8 * i))); };
    auto u16 = [&] (uint16_t v) { b.push_back ((uint8_t) v); b.push_back ((uint8_t) (v >> 8)); };
    b.insert (b.end(), { 'R','I','F','F' }); u32 (36 + 2 * n); b.insert (b.end(), { 'W','A','V','E','f','m','t',' ' });
    u32 (16); u16 (1); u16 (1); u32 (sr); u32 (sr * 2); u16 (2); u16 (16); b.insert (b.end(), { 'd','a','t','a' }); u32 (2 * n);
    for (int i = 0; i < n; ++i) u16 ((uint16_t) (int16_t) (amp * 32767 * std::exp (-i / 200.0) * (i % 2 ? -1 : 1)));
    return b;
}

int main() {
    const int64_t now = 1'800'000'000;
    // ---- StoreKit mapping
    { auto s = subscriptionFromStore ({}, now, true); CHECK (s.status == SubStatus::None); CHECK (! s.usedTrial); }
    { auto s = subscriptionFromStore ({}, now, false); CHECK (s.usedTrial); }
    { auto s = subscriptionFromStore ({ ent (IND, now + 100, "subscribed", true) }, now, false); CHECK (s.status == SubStatus::Trial); CHECK (s.productId == IND); CHECK (s.usedTrial); }
    { auto s = subscriptionFromStore ({ ent (IND, now + 100), ent (CH, now + 50) }, now, true); CHECK (s.status == SubStatus::Active); CHECK (s.productId == CH); }   // Church wins
    { auto s = subscriptionFromStore ({ ent (IND, now - 10) }, now, true); CHECK (s.status == SubStatus::Expired); }
    { auto e = ent (IND, now - 10, "grace"); e.graceUntil = now + 3600; auto s = subscriptionFromStore ({ e }, now, true); CHECK (s.status == SubStatus::GracePeriod); CHECK (s.graceUntil == now + 3600); }
    { auto s = subscriptionFromStore ({ ent (IND, now + 100, "billingRetry") }, now, true); CHECK (s.status == SubStatus::BillingRetry); }
    { auto e = ent (CH, now + 100); e.revoked = true; auto s = subscriptionFromStore ({ e }, now, true); CHECK (s.status == SubStatus::Revoked); }
    { auto e = ent (IND, now + 100); e.upgraded = true; auto s = subscriptionFromStore ({ e, ent (CH, now + 90) }, now, true); CHECK (s.productId == CH); }
    { auto s = subscriptionFromStore ({ ent ("com.other.app", now + 100) }, now, true); CHECK (s.status == SubStatus::None); }
    // church JWS for team creation
    CHECK (churchTransactionJws ({ ent (IND, now + 100) }, now).empty());
    CHECK (churchTransactionJws ({ ent (CH, now + 100), ent (CH, now + 200) }, now) == "jws-" + CH + "-" + std::to_string (now + 200));
    CHECK (churchTransactionJws ({ ent (CH, now - 1) }, now).empty());
    // mapping -> entitlements -> locks
    { Entitlements en; en.setSubscription (subscriptionFromStore ({ ent (IND, now + 100) }, now, true)); CHECK (en.fullAccess (now)); CHECK (en.allows (Feature::Projector, now)); CHECK (! en.fullAccess (now + 200)); }

    // ---- read-only: lapsed = library, charts, hymnal, backup export only
    { Entitlements en; en.setSubscription (subscriptionFromStore ({ ent (IND, now - 1) }, now, true));
      CHECK (en.readOnly (now));
      for (auto f : { Feature::Library, Feature::ReadCharts, Feature::ExportBackup, Feature::Hymnal }) CHECK (en.allows (f, now));
      for (auto f : { Feature::AudioEngine, Feature::Click, Feature::Sync, Feature::Projector, Feature::LiveView, Feature::Edit, Feature::Import, Feature::ShareSetlist }) {
          CHECK (! en.allows (f, now)); CHECK (std::strlen (featureLockedMessage (f)) > 5); } }

    // ---- footswitch section targets (match LiveSession prev/next/chorus)
    { auto c = parseChart ("{title: T}\n{key: G}\n[Verse 1]\nG C\n[Chorus]\nG D\n[Verse 2]\nG\n[Bridge]\nC\n");
      CHECK (c.sections.size() == 4);
      auto t = sectionSwitchTargets (c, 0, 1); CHECK (t.prev == -1); CHECK (t.next == 1); CHECK (t.chorus == 1);
      t = sectionSwitchTargets (c, 2, 1); CHECK (t.prev == 1); CHECK (t.next == 3); CHECK (t.chorus == 1);   // wraps back to last chorus
      t = sectionSwitchTargets (c, 3, 1); CHECK (t.next == -1);
      auto none = parseChart ("{title: N}\n[Verse]\nG\n[Bridge]\nC\n"); CHECK (sectionSwitchTargets (none, 0, 1).chorus == -1);
      CHECK (sectionSwitchTargets (c, 9, 1).next == -1); }

    // ---- cab IR import
    { auto good = wav (2000); auto k = checkIr (good.data(), good.size()); CHECK (k.ok); CHECK (k.samples == 2000); CHECK (k.sampleRate == 48000); CHECK (k.truncated);
      auto shortIr = wav (500); CHECK (checkIr (shortIr.data(), shortIr.size()).ok); CHECK (! checkIr (shortIr.data(), shortIr.size()).truncated);
      auto silent = wav (500, 48000, 0.0f); CHECK (! checkIr (silent.data(), silent.size()).ok);
      const char junk[] = "not a wav file at all, just text"; CHECK (! checkIr ((const uint8_t*) junk, sizeof junk).ok);
      CHECK (! checkIr (good.data(), 20).ok);
      std::vector<uint8_t> huge (9u << 20, 0); CHECK (! checkIr (huge.data(), huge.size()).ok);
      CHECK (safeIrFileName ("../../etc/My Cab:1.wav") .find ('/') == std::string::npos);
      CHECK (safeIrFileName ("Mesa 4x12.WAV").size() > 4); CHECK (safeIrFileName ("x.aiff").rfind (".wav") == safeIrFileName ("x.aiff").size() - 4);
      auto dir = (std::filesystem::temp_directory_path() / "wr_v012_irs").string(); std::filesystem::remove_all (dir);
      std::string err; auto p1 = importIr (good.data(), good.size(), "Cab.wav", dir, err); CHECK (! p1.empty()); CHECK (std::filesystem::exists (p1));
      auto p2 = importIr (good.data(), good.size(), "Cab.wav", dir, err); CHECK (p2 == p1);   // identical bytes reuse
      auto other = wav (800); auto p3 = importIr (other.data(), other.size(), "Cab.wav", dir, err); CHECK (! p3.empty()); CHECK (p3 != p1);
      CHECK (importIr ((const uint8_t*) junk, sizeof junk, "bad.wav", dir, err).empty()); CHECK (! err.empty());
      RigPreset r = defaultRig(); ensureAmpSection (r);
      CHECK (userIrName (r).empty()); CHECK (setUserIr (r, p1)); CHECK (! userIrName (r).empty());
      clearUserIr (r); CHECK (userIrName (r).empty());
      std::filesystem::remove_all (dir); }

    std::printf ("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
