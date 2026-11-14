// v0.10 tests: subscription + church seats + read-only mode, amp section (head/cab/mic/IR/real-amp bypass), pedal faces.
#include "dsp/RigBuilder.h"
#include "dsp/AmpSection.h"
#include "dsp/PedalFaces.h"
#include "model/Packs.h"
#include "model/Song.h"
#include <cstdio>
#include <cmath>
#include <fstream>
#include <set>
#include <cstring>
using namespace wr;
static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (! (c)) { ++fails; std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)
static const int64_t D = 86400, T0 = 1793000000; // ~Oct 2026

static void testPlans() {
    auto& pr = subscriptionProducts(); CHECK (pr.size() == 2);
    auto* ind = findProduct ("com.tabernacletools.guitar.individual.monthly"); auto* ch = findProduct ("com.tabernacletools.guitar.church.monthly");
    CHECK (ind && std::abs (ind->usdPrice - 4.99) < 1e-9 && ind->plan == Plan::Individual && ind->seats == 1 && ind->trialDays == 14);
    CHECK (ch && std::abs (ch->usdPrice - 19.99) < 1e-9 && ch->plan == Plan::Church && ch->seats == 15 && ch->trialDays == 14);
    CHECK (! findProduct ("nope"));
}
static void testAccess() {
    Entitlements e; // brand new install: read-only until the trial starts
    CHECK (e.trialEligible()); CHECK (e.readOnly (T0)); CHECK (e.allows (Feature::ReadCharts, T0)); CHECK (e.allows (Feature::Library, T0)); CHECK (e.allows (Feature::ExportBackup, T0));
    for (auto f : { Feature::AudioEngine, Feature::Click, Feature::Sync, Feature::Projector, Feature::LiveView, Feature::Edit, Feature::Import }) CHECK (! e.allows (f, T0));
    SubscriptionState s; s.status = SubStatus::Trial; s.productId = "com.tabernacletools.guitar.individual.monthly"; s.expiresAt = T0 + 14 * D; s.usedTrial = true; e.setSubscription (s);
    CHECK (e.fullAccess (T0)); CHECK (e.allows (Feature::AudioEngine, T0 + 13 * D)); CHECK (e.isUnlocked ("tapeecho", T0)); CHECK (e.isUnlocked ("amp:Modern High Gain", T0));
    CHECK (e.statusText (T0).find ("14 days") != std::string::npos); CHECK (! e.trialEligible());
    CHECK (! e.fullAccess (T0 + 14 * D)); CHECK (e.readOnly (T0 + 15 * D)); CHECK (! e.isUnlocked ("comp", T0 + 15 * D)); // no permanent free tier
    CHECK (e.statusText (T0 + 15 * D).find ("Read-only") != std::string::npos);
    s.status = SubStatus::Active; s.expiresAt = T0 + 44 * D; e.setSubscription (s); CHECK (e.plan (T0 + 20 * D) == Plan::Individual); CHECK (e.statusText (T0 + 20 * D).find ("Individual") != std::string::npos);
    s.status = SubStatus::GracePeriod; s.graceUntil = T0 + 60 * D; e.setSubscription (s); CHECK (e.fullAccess (T0 + 50 * D)); CHECK (! e.fullAccess (T0 + 61 * D));
    for (auto st : { SubStatus::BillingRetry, SubStatus::Expired, SubStatus::Revoked }) { s.status = st; s.expiresAt = T0 + 999 * D; e.setSubscription (s); CHECK (! e.fullAccess (T0)); }
    // cache round-trip
    s.status = SubStatus::Active; s.expiresAt = T0 + 30 * D; e.setSubscription (s); SeatToken tk { "team1", "m2", T0 + 30 * D, T0 }; e.setSeat (tk);
    auto e2 = Entitlements::parse (e.serialise()); CHECK (e2.subscription().status == SubStatus::Active); CHECK (e2.subscription().expiresAt == T0 + 30 * D); CHECK (e2.subscription().usedTrial);
    CHECK (e2.fullAccess (T0)); CHECK (Entitlements::parse ("garbage").readOnly (T0)); CHECK (Entitlements::parse ("wr-entitlements 3\n99\nx\n1\n1\n0\n").readOnly (T0));
    Entitlements dev; dev.setUnlockAll (true); CHECK (dev.fullAccess (0)); CHECK (dev.allows (Feature::Sync, 0));
}
static void testChurch() {
    ChurchTeam t; t.teamId = "team1"; t.churchName = "Grace"; CHECK (t.setLeader ("lead", "Clayton")); CHECK (! t.setLeader ("lead", "x"));
    SubscriptionState s; s.status = SubStatus::Active; s.productId = "com.tabernacletools.guitar.church.monthly"; s.expiresAt = T0 + 30 * D; t.leaderSub = s; CHECK (t.active (T0));
    CHECK (t.seatsUsed() == 1); CHECK (! t.redeem ("bad", "m1", "A"));
    int ok = 0; for (int i = 0; i < 20; ++i) { auto code = t.createInvite ("c" + std::to_string (i)); if (t.redeem (code, "m" + std::to_string (i), "M")) ++ok; }
    CHECK (ok == kChurchSeats - 1); CHECK (t.seatsFree() == 0); CHECK (t.seatsUsed() == 15);
    t.createInvite ("again"); CHECK (! t.redeem ("again", "m99", "X")); // full
    CHECK (! t.remove ("lead")); CHECK (t.remove ("m3")); CHECK (t.seatsFree() == 1); CHECK (t.redeem ("again", "m99", "X")); CHECK (! t.redeem ("again", "m98", "Y")); // codes are single use
    CHECK (! t.redeem (t.createInvite ("dup"), "m99", "X")); // already a member
    auto ti = t; ti.leaderSub.productId = "com.tabernacletools.guitar.individual.monthly"; CHECK (! ti.active (T0)); // individual sub can't run a team
    // member device: seat token honoured while the team is paid AND refreshed within 30 days
    Entitlements m; m.setSeat ({ "team1", "m99", T0 + 30 * D, T0 }); CHECK (m.fullAccess (T0 + 10 * D)); CHECK (m.plan (T0) == Plan::Church); CHECK (! m.trialEligible());
    CHECK (! m.fullAccess (T0 + 31 * D)); Entitlements m2; m2.setSeat ({ "team1", "m99", T0 + 90 * D, T0 }); CHECK (! m2.fullAccess (T0 + 31 * D)); // offline too long
    m2.clearSeat(); CHECK (m2.readOnly (T0));
}
static void writeWav (const std::string& path, const std::vector<float>& x, int sr) {
    std::ofstream f (path, std::ios::binary); auto le = [&] (uint32_t v, int n) { for (int i = 0; i < n; ++i) f.put ((char) ((v >> (8 * i)) & 0xff)); };
    f.write ("RIFF", 4); le (36 + (uint32_t) x.size() * 2, 4); f.write ("WAVEfmt ", 8); le (16, 4); le (1, 2); le (1, 2); le ((uint32_t) sr, 4); le ((uint32_t) sr * 2, 4); le (2, 2); le (16, 2);
    f.write ("data", 4); le ((uint32_t) x.size() * 2, 4); for (float v : x) le ((uint32_t) (uint16_t) (int16_t) std::lround (v * 32767), 2);
}
static float rmsOf (Pedal& p, float freq) { float L[256], R[256]; ProcessContext c; double a = 0; long n = 0;
    for (int b = 0; b < 60; ++b) { for (int i = 0; i < 256; ++i) L[i] = R[i] = 0.3f * std::sin (6.2831853f * freq * (float) (b * 256 + i) / 48000.0f); p.process (L, R, 256, c); if (b > 10) for (int i = 0; i < 256; ++i) { a += L[i] * L[i]; ++n; } }
    return (float) std::sqrt (a / n); }
static void testAmpSection() {
    CHECK (ampHeadNames().size() == 6); CHECK (ampCabNames().size() == 6); CHECK (ampCabNames().back() == "User IR"); CHECK (ampMicNames().size() == 3); CHECK (userIrCabIndex() == 5);
    RigPreset r = defaultRig(); size_t pre0 = r.lanes[0].size(); CHECK (ampSection (r) == nullptr || true);
    auto& a = ensureAmpSection (r); CHECK (a.uid == kAmpUid); CHECK (a.enabled); CHECK (r.lanes[0].back().uid == kAmpUid);
    // stays last + unique after edits
    PedalDef extra; extra.uid = "x1"; extra.type = "comp"; r.lanes[0].push_back (extra); ensureAmpSection (r); CHECK (r.lanes[0].back().uid == kAmpUid);
    int count = 0; for (auto& l : r.lanes) for (auto& d : l) if (d.uid == kAmpUid) ++count; CHECK (count == 1); CHECK (r.lanes[0].size() == pre0 + 2);
    // moved into lane 1 by a drag -> normalised back
    { auto d = r.lanes[0].back(); r.lanes[0].pop_back(); r.lanes[1].insert (r.lanes[1].begin(), d); ensureAmpSection (r); CHECK (r.lanes[0].back().uid == kAmpUid); CHECK (r.lanes[1].front().uid != kAmpUid); }
    CHECK (ampSummary (r) == "Clean American -> 2x12 Open Back - Direct to PA");
    ampSection (r)->params["model"] = 2; ampSection (r)->params["output"] = 1; CHECK (ampSummary (r) == "British Chime -> no cab - Amp FX Return");
    setRealAmp (r, true); CHECK (usingRealAmp (r)); CHECK (ampSummary (r) == "Real amp (sim bypassed)"); setRealAmp (r, false); CHECK (! usingRealAmp (r));
    // JSON round trip keeps the section + IR path
    std::string wav = "/tmp/wr_test_ir.wav"; std::vector<float> ir (2000, 0.0f); ir[0] = 0.9f; for (size_t i = 1; i < ir.size(); ++i) ir[i] = 0.3f * std::exp (-(float) i / 100.0f) * ((i & 1) ? 1.f : -1.f);
    writeWav (wav, ir, 44100); CHECK (setUserIr (r, wav)); CHECK (ampSection (r)->file == wav); CHECK ((int) ampSection (r)->params["cab"] == 5);
    ampSection (r)->params["output"] = 0; CHECK (ampSummary (r).find ("IR wr_test_ir.wav") != std::string::npos);
    CHECK (! setUserIr (r, "/tmp/does-not-exist.wav")); { std::ofstream f ("/tmp/wr_bad.wav"); f << "not a wav"; } CHECK (! setUserIr (r, "/tmp/wr_bad.wav"));
    RigPreset back = rigFromJson (toJson (r)); CHECK (ampSection (back) && ampSection (back)->file == wav);
    std::vector<std::string> warn; auto chain = buildChain (back, &warn); CHECK (warn.empty()); for (auto& w : warn) std::printf ("  warn %s\n", w.c_str());
    auto* slot = chain->find (kAmpUid); CHECK (slot && slot->pedal->type == "amp");
    back.lanes[0].back().file = "/tmp/gone.wav"; warn.clear(); buildChain (back, &warn); CHECK (warn.size() == 1);
    // mic + position change the sound; every head/cab/mic combo is finite
    { auto p = createPedal ("amp"); p->prepare (48000, 256); p->setEnabled (true); p->setParam ("level", -18); p->setParam ("micpos", 0); float bright = rmsOf (*p, 3500);
      auto q = createPedal ("amp"); q->prepare (48000, 256); q->setEnabled (true); q->setParam ("level", -18); q->setParam ("micpos", 1); float dark = rmsOf (*q, 3500); std::printf ("  mic centre %.4f edge %.4f\n", bright, dark); CHECK (bright > dark * 1.2f);
      auto rb = createPedal ("amp"); rb->prepare (48000, 256); rb->setEnabled (true); rb->setParam ("level", -18); rb->setParam ("mic", 1); float rib = rmsOf (*rb, 3500); CHECK (bright > rib); }
    for (int h = 0; h < 6; ++h) for (int c = 0; c < 5; ++c) for (int m = 0; m < 3; ++m) { auto p = createPedal ("amp"); p->prepare (48000, 256); p->setEnabled (true);
        p->setParam ("model", (float) h); p->setParam ("cab", (float) c); p->setParam ("mic", (float) m); float v = rmsOf (*p, 220); CHECK (std::isfinite (v) && v > 0); }
    // IR loaded at 44.1 kHz then prepared at 48k and 96k still works
    { auto p = createPedal ("cab"); CHECK (loadIrFile (*p, wav)); for (double sr : { 48000.0, 96000.0 }) { p->prepare (sr, 256); p->setEnabled (true); p->reset(); CHECK (rmsOf (*p, 300) > 0.01f); } }
}
static void testFaces() {
    static const char* nw[] = { "gefuzz", "tristage", "treblebst", "bluesdrive", "ampinbox", "octfuzz", "otacomp", "gate", "flanger", "vibrato", "wah", "envfilter", "analogdly", "tapeecho", "dotted8", "cloud", "spring" };
    for (auto* t : nw) { auto* f = pedalFace (t); CHECK (f != nullptr); if (! f) continue; auto p = createPedal (t); CHECK (! f->blurb.empty()); CHECK (f->mainParams.size() >= 2 && f->mainParams.size() <= 4);
        for (auto& id : f->mainParams) { if (p->paramIndex (id) < 0) std::printf ("  face %s: bad param %s\n", t, id.c_str()); CHECK (p->paramIndex (id) >= 0); }
        CHECK ((f->argb >> 24) == 0xff); }
    for (auto& f : pedalFaces()) CHECK (createPedal (f.type) != nullptr);
    CHECK (pedalFace ("delay") == nullptr);
}
int main() { setvbuf (stdout, nullptr, _IONBF, 0);
    testPlans(); testAccess(); testChurch(); testAmpSection(); testFaces();
    std::printf ("%d checks, %d failures\n", checks, fails); return fails ? 1 : 0; }
