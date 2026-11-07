#include "model/Packs.h"
#include <sstream>
#include <algorithm>

namespace wr {
const std::vector<SoundPack>& packManifest() {
    static const std::vector<SoundPack> m = {
        { "guitar.starter", "Guitar Starter", "Everything a Sunday needs: comp, boost, drives, fuzz, chorus, trem, delays, reverbs, shimmer, freeze, swell, 2 amps + direct-to-PA", Rig::Guitar, true,
          { "comp", "boost", "overdrive", "distortion", "fuzz", "chorus", "tremolo", "phaser", "delay", "dotted8", "reverb", "shimmer", "freeze", "swell", "eq", "gate", "octdown",
            "amp", "cab", "amp:Clean American", "amp:British Chime", "cab:1x12 Open Back", "cab:2x12 Open Back", "cab:User IR" } },
        { "guitar.ambient", "Ambient Worship", "Cloud reverb, swells, poly octave, harmonizer, pad synths from your guitar", Rig::Guitar, false,
          { "cloud", "polyoct", "harmonizer", "synth9", "key9", "preset:Swell Pad", "preset:Shimmer Wash", "preset:Cathedral Lead" } },
        { "guitar.vintage", "Vintage Pedals", "Germanium & tri-stage fuzz, treble booster, octave fuzz, OTA comp, flanger, vibrato, vibe, rotary, wah, envelope filter, BBD & tape echo, spring",
          Rig::Guitar, false,
          { "gefuzz", "tristage", "treblebst", "octfuzz", "otacomp", "flanger", "vibrato", "univibe", "rotary", "wah", "envfilter", "analogdly", "tapeecho", "spring", "klon", "bluesdrive", "ampinbox" } },
        { "guitar.amps", "Amp Collection", "Tweed, plexi crunch, boutique lead, modern high gain + 3 more cabs", Rig::Guitar, false,
          { "amp:Tweed Breakup", "amp:British Crunch", "amp:Boutique Lead", "amp:Modern High Gain", "cab:2x12 Blue Alnico", "cab:4x12 Closed Back", "cab:4x10 Tweed" } },
        { "keys.starter", "Keys Starter", "Grand, tine EP, tonewheel organ, warm pad, key-matched worship pad", Rig::Keys, true,
          { "keys:Grand", "keys:Tine EP", "keys:Tonewheel", "keys:Warm Pad", "keys:Key Pad" } },
        { "keys.ambient", "Ambient Pads", "12 evolving worship pads, all key-matched, swells and textures", Rig::Keys, false,
          { "keys:Shimmer Pad", "keys:Air Pad", "keys:Choir Pad", "keys:Strings Pad", "keys:Texture Pad" } },
        { "keys.vintage", "Vintage Keys", "Reed EP, pipe organ, string machine, poly synths", Rig::Keys, false,
          { "keys:Reed EP", "keys:Pipe Organ", "keys:String Machine", "keys:Poly Synth" } },
    };
    return m;
}
const SoundPack* findPack (const std::string& id) { for (auto& p : packManifest()) if (p.id == id) return &p; return nullptr; }
const SoundPack* packForContent (const std::string& c) {
    for (int pass = 0; pass < 2; ++pass) for (auto& p : packManifest()) if (p.free == (pass == 0)) for (auto& x : p.content) if (x == c) return &p;
    return nullptr;
}
const std::vector<SubscriptionProduct>& subscriptionProducts() {
    static const std::vector<SubscriptionProduct> v = {
        { "com.tabernacletools.guitar.individual.monthly", "Tabernacle Tools Individual", Plan::Individual, 4.99, kTrialDays, 1 },
        { "com.tabernacletools.guitar.church.monthly", "Tabernacle Tools Church (whole band)", Plan::Church, 19.99, kTrialDays, kChurchSeats } };
    return v;
}
const SubscriptionProduct* findProduct (const std::string& id) { for (auto& p : subscriptionProducts()) if (p.productId == id) return &p; return nullptr; }
static bool subLive (const SubscriptionState& s, int64_t now) {
    switch (s.status) {
        case SubStatus::Trial: case SubStatus::Active: return now < s.expiresAt;
        case SubStatus::GracePeriod: return now < std::max (s.expiresAt, s.graceUntil);
        default: return false; // None, BillingRetry (after grace), Expired, Revoked (refund)
    }
}
bool readOnlyFeature (Feature f) { return f == Feature::Library || f == Feature::ReadCharts || f == Feature::ExportBackup || f == Feature::Hymnal; }
const char* featureLockedMessage (Feature f) {
    switch (f) {
        case Feature::AudioEngine: return "Playing your rig needs a subscription";
        case Feature::Click: return "The click and voice cues need a subscription";
        case Feature::Sync: return "Band sync needs a subscription";
        case Feature::Projector: return "Projector lyrics need a subscription";
        case Feature::LiveView: return "The Live view needs a subscription";
        case Feature::Edit: return "Editing songs, setlists and rigs needs a subscription";
        case Feature::Import: return "Importing charts needs a subscription";
        case Feature::ShareSetlist: return "Sharing setlists needs a subscription";
        default: return "This is included - no subscription needed";
    }
}

bool ChurchTeam::setLeader (const std::string& id, const std::string& name) { if (id.empty() || hasMember (id)) return false; members_.insert (members_.begin(), { id, name, true }); if ((int) members_.size() > kChurchSeats) members_.pop_back(); return true; }
std::string ChurchTeam::createInvite (const std::string& code) { if (! code.empty()) invites_.push_back (code); return code; }
bool ChurchTeam::redeem (const std::string& code, const std::string& id, const std::string& name) {
    auto it = std::find (invites_.begin(), invites_.end(), code); if (it == invites_.end() || id.empty() || hasMember (id) || seatsFree() <= 0) return false;
    invites_.erase (it); members_.push_back ({ id, name, false }); return true;
}
bool ChurchTeam::remove (const std::string& id) { for (auto it = members_.begin(); it != members_.end(); ++it) if (it->id == id && ! it->leader) { members_.erase (it); return true; } return false; }
bool ChurchTeam::hasMember (const std::string& id) const { for (auto& m : members_) if (m.id == id) return true; return false; }
bool ChurchTeam::active (int64_t now) const { auto* p = findProduct (leaderSub.productId); return p && p->plan == Plan::Church && subLive (leaderSub, now); }

bool Entitlements::ownActive (int64_t now) const { return subLive (sub_, now); }
bool Entitlements::seatActive (int64_t now) const { return hasSeat_ && now < seat_.teamExpiresAt && now < seat_.refreshedAt + (int64_t) kOfflineDays * 86400; }
bool Entitlements::fullAccess (int64_t now) const { return unlockAll_ || ownActive (now) || seatActive (now); }
Plan Entitlements::plan (int64_t now) const {
    if (ownActive (now)) { auto* p = findProduct (sub_.productId); return p ? p->plan : Plan::Individual; }
    return seatActive (now) ? Plan::Church : Plan::None;
}
std::string Entitlements::statusText (int64_t now) const {
    if (unlockAll_) return "Developer build - everything unlocked";
    auto days = [&] (int64_t t) { return std::to_string (std::max<int64_t> (0, (t - now + 86399) / 86400)); };
    if (ownActive (now)) {
        if (sub_.status == SubStatus::Trial) return "Free trial - " + days (sub_.expiresAt) + " days left";
        if (sub_.status == SubStatus::GracePeriod) return "Payment issue - full access for " + days (std::max (sub_.expiresAt, sub_.graceUntil)) + " more days";
        return std::string (plan (now) == Plan::Church ? "Church plan" : "Individual plan") + " - renews in " + days (sub_.expiresAt) + " days";
    }
    if (seatActive (now)) return seat_.role == "individual" || seat_.role == "church" ? "Covered by your Tabernacle Tools subscription (all Tabernacle Tools apps)" : "Church seat - via your team leader";
    return sub_.status == SubStatus::None && ! hasSeat_ ? "Start your free 14-day trial" : "Read-only - charts, library and backup export still work. Subscribe or join your church team to play.";
}
std::string Entitlements::serialise() const {
    std::string s = "wr-entitlements 3\n" + std::to_string ((int) sub_.status) + "\n" + sub_.productId + "\n" + std::to_string (sub_.expiresAt) + "\n" + std::to_string (sub_.graceUntil) + "\n" + (sub_.usedTrial ? "1" : "0") + "\n";
    if (hasSeat_) s += "seat\n" + seat_.teamId + "\n" + seat_.memberId + "\n" + std::to_string (seat_.teamExpiresAt) + "\n" + std::to_string (seat_.refreshedAt) + "\n";
    return s;
}
Entitlements Entitlements::parse (const std::string& t) {
    Entitlements e; std::istringstream in (t); std::string l[6];
    if (! std::getline (in, l[0]) || l[0] != "wr-entitlements 3") return e;
    for (int i = 1; i < 6; ++i) if (! std::getline (in, l[i])) return e;
    try { int st = std::stoi (l[1]); if (st < 0 || st > (int) SubStatus::Revoked) return e;
          SubscriptionState s; s.status = (SubStatus) st; s.productId = l[2]; s.expiresAt = std::stoll (l[3]); s.graceUntil = std::stoll (l[4]); s.usedTrial = l[5] == "1"; e.setSubscription (s);
          std::string k; if (std::getline (in, k) && k == "seat") { SeatToken tk; std::string a, b; std::getline (in, tk.teamId); std::getline (in, tk.memberId); std::getline (in, a); std::getline (in, b);
              tk.teamExpiresAt = std::stoll (a); tk.refreshedAt = std::stoll (b); e.setSeat (tk); } } catch (...) {}
    return e;
}
} // namespace wr
