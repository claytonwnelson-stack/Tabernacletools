// Bearing Live v0.10 - sound packs (content groupings) + SUBSCRIPTION entitlements (IAP-ready, NO StoreKit calls yet).
// Business model: one auto-renewable subscription ("Bearing Live Plus", $1.99/mo or $19.99/yr) unlocks EVERY paid pack in
// every rig app. Free packs (Starters) are always available. Packs stay as browsing/marketing groupings only.
// Content ids: pedal types ("tapeecho"), amp heads ("amp:British Crunch"), cabs ("cab:..."), presets ("preset:..."), keys patches ("keys:...").
#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace wr {
enum class Rig { Guitar, Keys, Bass, Any };
struct SoundPack {
    std::string id, name, description; Rig rig = Rig::Any; bool free = false; // free = Starter, never locked
    std::vector<std::string> content;
};
const std::vector<SoundPack>& packManifest();
const SoundPack* findPack (const std::string& id);
// Which pack provides a content id (free packs win). nullptr = not in any pack => always available.
const SoundPack* packForContent (const std::string& contentId);

// ---- v0.10 business model (Clayton, Nov 3 2026): subscription only, NO permanent free tier.
//  * Individual  $4.99/mo  - one person, all their devices (Apple ID), every rig app.
//  * Church      $19.99/mo - one church team: leader buys, invites up to kChurchSeats members (each on their own Apple ID / devices).
//  * Free trial  14 days, full features (covers at least one Sunday + rehearsal). One intro offer per Apple ID per subscription group.
//  * After trial / lapse: READ-ONLY mode - open the library, read charts and setlists, export a backup. No audio engine,
//    click, cues, band sync, projector, live view or editing. Saved rigs/presets are untouched and come back on resubscribe.
// Packs above are content groupings only (browse/marketing); a full-access entitlement unlocks all of them.
constexpr int kChurchSeats = 15;          // including the leader
constexpr int kTrialDays = 14;
constexpr int kGraceDays = 16;            // App Store Connect billing grace period (3 / 16 / 28) - we pick 16
constexpr int kOfflineDays = 30;          // church seat token stays valid offline this long past its last refresh
constexpr const char* kSubscriptionGroup = "tabernacle-tools";

enum class Plan { None, Individual, Church };
struct SubscriptionProduct { std::string productId, name; Plan plan; double usdPrice; int trialDays; int seats; };
const std::vector<SubscriptionProduct>& subscriptionProducts();
const SubscriptionProduct* findProduct (const std::string& productId);

// StoreKit 2 renewal states, mirrored. The StoreKit layer (later) maps Transaction / RenewalInfo into this.
enum class SubStatus { None, Trial, Active, GracePeriod, BillingRetry, Expired, Revoked };
struct SubscriptionState {
    SubStatus status = SubStatus::None; std::string productId;
    int64_t expiresAt = 0;      // unix seconds (end of paid / trial period)
    int64_t graceUntil = 0;     // unix seconds; only for GracePeriod
    bool usedTrial = false;     // intro offer already used on this Apple ID
};

// Church team seats. The leader's App Store subscription is the source of truth; seats are shared through OUR account
// service (Sign in with Apple + small backend / CloudKit - Apple Family Sharing is family-only, max 6, so it can't do this).
// A member device holds a signed seat token {teamId, memberId, teamExpiresAt, refreshedAt}; this class is the pure logic.
struct TeamMember { std::string id, name; bool leader = false; };
class ChurchTeam {
public:
    std::string teamId, churchName; SubscriptionState leaderSub; // the leader's Church subscription
    const std::vector<TeamMember>& members() const { return members_; }
    int seatsUsed() const { return (int) members_.size(); }
    int seatsFree() const { return kChurchSeats - seatsUsed(); }
    bool setLeader (const std::string& id, const std::string& name);
    std::string createInvite (const std::string& code);           // returns code; leader shares it (link / QR)
    bool redeem (const std::string& code, const std::string& memberId, const std::string& name); // false: bad code / full / already in
    bool remove (const std::string& memberId);                    // leader frees a seat (not the leader)
    bool hasMember (const std::string& memberId) const;
    bool active (int64_t now) const;                              // leader's Church sub currently grants access
private:
    std::vector<TeamMember> members_; std::vector<std::string> invites_;
};
struct SeatToken { std::string teamId, memberId; int64_t teamExpiresAt = 0, refreshedAt = 0; std::string role; }; // v0.18 role: leader | member (church seat) | individual | church (own purchase, possibly in another Bearing Live app)

enum class Feature { Library, ReadCharts, ExportBackup, AudioEngine, Click, Sync, Projector, LiveView, Edit, Import, Hymnal, ShareSetlist };
// v0.12 read-only (lapsed) = Library, ReadCharts, Hymnal (browse + copy into the library), ExportBackup. Everything else locks.
const char* featureLockedMessage (Feature f);   // one line for the paywall ("Live view needs a subscription")
bool readOnlyFeature (Feature f);   // available with NO entitlement (Library, ReadCharts, ExportBackup)

class Entitlements {
public:
    void setSubscription (const SubscriptionState& s) { sub_ = s; }
    const SubscriptionState& subscription() const { return sub_; }
    void setSeat (const SeatToken& t) { seat_ = t; hasSeat_ = true; }
    void clearSeat() { hasSeat_ = false; }
    bool fullAccess (int64_t now) const;                  // own subscription/trial OR a valid church seat
    bool allows (Feature f, int64_t now) const { return readOnlyFeature (f) || fullAccess (now); }
    bool isUnlocked (const std::string& contentId, int64_t now) const { (void) contentId; return fullAccess (now); } // every pack, one gate
    bool readOnly (int64_t now) const { return ! fullAccess (now); }
    bool trialEligible() const { return ! sub_.usedTrial && sub_.status == SubStatus::None && ! hasSeat_; }
    Plan plan (int64_t now) const;
    void setUnlockAll (bool on) { unlockAll_ = on; }      // dev / sideload builds (WR_UNLOCK_ALL)
    bool unlockAll() const { return unlockAll_; }
    std::string statusText (int64_t now) const;
    std::string serialise() const;                        // offline cache "wr-entitlements 3" (StoreKit / seat service = truth)
    static Entitlements parse (const std::string&);
private:
    bool ownActive (int64_t now) const; bool seatActive (int64_t now) const;
    SubscriptionState sub_; SeatToken seat_; bool hasSeat_ = false, unlockAll_ = false;
};
} // namespace wr
