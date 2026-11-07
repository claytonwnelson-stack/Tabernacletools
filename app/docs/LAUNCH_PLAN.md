# Tabernacle Tools — Launch Plan (sideload → App Store)

*Draft v0.10 (subscription pricing, church seats, hymnal); first drafted v0.9 · Nov 3, 2026 · prices checked Nov 3, 2026 (US App Store / vendor sites)*

## 0. Where we are
- Shared core plus the **Guitar** app (AUv3 + standalone). Planned next: Chart-only (acoustic players and singers), Bass, Keys.
- Every non-UI test passes on Linux. **Nothing has been built for iOS or run on an iPad yet.** The first real build is on Clayton's Mac (BUILD_ON_MAC.md).
- Sideloading with a free Apple ID: apps expire after 7 days, you get 3 apps and 10 App IDs per week, and there are no App Groups. That's fine for personal testing, but it isn't a way to distribute.

- **v0.14 (Nov 4, 2026): multitrack backing tracks** (Prime-style stems, per-song mix, in-ears routing, arrangement re-sequencing, vamps, leader-driven). 
- **v0.15 (Nov 4, 2026): CCLI song-usage log + CSV/summary export** for manual entry on CCLI's site. Automatic CCLI reporting deliberately left out until there's a following (Clayton, Nov 4).
- **v0.16 (Nov 4, 2026): planned items finished**: Terms/Privacy drafts, Link off until licensed, voice recording script + overrides, tempo-map editor, stem sharing, CCLI per-song totals.
- **v0.17 (Nov 4, 2026): MIDI Clock send/receive** (Off/Send/Receive). Ableton Link left off - Clayton chose MIDI clock + Wi-Fi sync.
- **v0.18 (Nov 4, 2026): one subscription unlocks every app** (Guitar, Keys, Bass, Charts stay separate apps). Cross-app entitlement through the seat service; one free trial per person across all apps.
- **v0.19 (Nov 4, 2026): renamed to Tabernacle Tools** (Tabernacle Tools LLC, Texas, planned). Apps: Tabernacle Tools - Electric Guitar / Keys / Bass / Charts / Hub; home-screen "TT Guitar" etc.; bundle ids `com.tabernacletools.<app>`.

## 1. Two licence blockers (must fix before anyone else gets a build)
| Blocker | Why | Fix | Cost / effort |
|---|---|---|---|
| **Ableton Link** | ~~Was a licence blocker.~~ **Decided (Clayton, Nov 4):** leave Link off. Band sync uses Wi-Fi (`core/sync`) + **MIDI Clock** send/receive for DAWs and other apps. `WR_WITH_LINK` stays OFF. | Done - no Ableton email needed. | 0 |
| **Piper TTS voice cues** ("Verse two, 2, 3, 4") | The current voice (en_US *lessac*) was trained on a dataset licensed for personal or non-commercial use only. | Record our own voice pack (Clayton or a hired VO). Roughly 120 words: numbers 1–16, section names, "count-in", "key change", and so on. Record at 48 kHz, then trim and normalise with `tools/gen_voice.py`. As a fallback, use a commercially licensed TTS voice and keep the licence on file. | 1 day of recording, or about $150–400 for a VO on Voices.com or Fiverr. |

Also:
- Keep **JUCE** on the commercial or Personal licence that fits our revenue. The JUCE 8 Starter tier is free under a revenue cap; check [juce.com/get-juce](https://juce.com/get-juce) before launch. Leave the splash screen off only if the licence allows it.
- **Hymnal and user charts (copyright).** The app ships a **read-only hymnal of 40 public-domain hymns**. In every one, the text, any translation and the tune were published **before 1931**, so they're public domain in the US. Their authors, translators and composers all died more than 70 years ago, which covers life+70 countries. Each hymn records its source and why it's public domain. The chord arrangements are our own simple ones, and they include **original verses only**, with no modern added verses or arrangements.
  - We left out anything we weren't sure about. *Great Is Thy Faithfulness* (1923) is public domain in the US but still protected in the UK and EU (Chisholm died in 1960). We also left out *The Old Rugged Cross* and *In the Garden* (US-only public domain), *Be Still, My Soul* (FINLANDIA was arranged as a hymn tune after 1930), and *How Great Thou Art* (the English text dates from 1949).
  - **There's no public pool of user charts.** Users keep their own private library, and the band leader syncs songs only to that band's devices. We don't host or redistribute anyone's uploads. That avoids hosting copyrighted worship songs, which would need CCLI/SongSelect-style licensing and DMCA handling. Churches should keep reporting their CCLI usage as they do today.
  - Users can copy a hymn into their own library to edit it.
- Keep the trademark-free naming rules in SOUND_LIBRARY.md §0. A unit test (`tests/guitar/test_v09.cpp::testNames`) enforces them.
- We need a privacy policy. The app collects nothing and only uses the local network, so the App Store "Data Not Collected" label applies.

## 2. Accounts and setup
1. Join the **Apple Developer Program** for $99/yr ([developer.apple.com/programs](https://developer.apple.com/programs/)). Enrol as **Tabernacle Tools LLC** (form the Texas LLC first, then get a free D-U-N-S number, 1–2 weeks) so the seller name isn't a personal name.
2. Join the **App Store Small Business Program** ([link](https://developer.apple.com/app-store/small-business-program/)). It drops Apple's commission from 30% to **15%** while proceeds stay under $1M/yr.
3. In Xcode: turn on App Groups (`-DWR_USE_APP_GROUP=ON`) so the AUv3 shares the library, and the Share extension (`-DWR_SHARE_EXTENSION=ON`).
4. Set up GitHub Actions on a macOS runner for CI builds, plus fastlane for TestFlight uploads.

## 3. Beta: TestFlight with real churches (8–10 weeks)
- **Phase 1, internal (2 weeks):** Clayton's own church, 3–5 players, on real stage Wi-Fi with a travel router. Measure sync drift, audio dropouts, and how long a battery lasts through a 90-minute service.
- **Phase 2, external TestFlight (6–8 weeks):** 10–20 churches. You can have up to 10,000 testers, and each build lasts 90 days ([TestFlight](https://developer.apple.com/testflight/)). Recruit a mix:
  - small churches (1 guitarist plus keys, no tech team)
  - mid-size churches (full band, in-ear monitors, a projector)
  - 2–3 that already use Loop Community's Prime or MultiTracks Playback, so we hear how we compare.
- What to collect: crash logs, a weekly 5-question form, and one 20-minute call per church. Success means 4 Sundays in a row with no stop-the-song failures, and 70% or more of testers saying they would pay.
- Recruit through local worship leader networks, r/WorshipTeam, Facebook groups like "Worship Leaders" and "Church Sound & Media Techs", and Loopy Pro / AUM forums for the AUv3 crowd.

## 4. Pricing (decided by Clayton, Nov 3, 2026): subscription only
| Plan | Price | Who | Product id |
|---|---|---|---|
| **Individual** | **$4.99 / month** | One person, all their devices (one Apple ID), every Tabernacle Tools app (Guitar, Chart-only, Keys, Bass) | `com.tabernacletools.guitar.individual.monthly` |
| **Church** | **$19.99 / month** | A whole church team. The leader subscribes and invites up to **15 people** (the leader included), each on their own Apple ID and devices | `com.tabernacletools.guitar.church.monthly` |

Both are in one App Store subscription group (`worship-rig`), so upgrading or downgrading between them is handled by Apple. Yearly plans: **none at launch.** Optionally add ~$49.99/yr (Individual) and ~$199/yr (Church) after a few months of data; a yearly church plan suits church budgets.

**Free trial:** **14 days (2 weeks) with every feature, usable ONCE per person** (v0.13). That covers at least one rehearsal plus one Sunday.
- Apple: the introductory offer is once per Apple ID **per subscription group**. Both plans are in the same single group, so a trial on Individual rules out a trial on Church and vice versa. The app asks StoreKit (`Product.SubscriptionInfo.isEligibleForIntroOffer(for: groupID)`) and only shows "Start 2-week free trial" when Apple says yes; otherwise the buttons just say "Subscribe". If StoreKit can't answer, it shows "Subscribe" (never promises a trial it can't give).
- Seat service: records each trial against a hashed Sign in with Apple id (`trial_history`, kept even when an account is deleted), so a leader can't build a church team on a second trial; such a team only unlocks once the first paid month starts. `/v1/entitlement` reports `trialEligible`.

**App Store Connect setup (subscriptions) - do exactly this:**
1. App → Monetization → Subscriptions → create **one** subscription group, reference name `worship-rig`. Do not create a second group.
2. In that group add `com.tabernacletools.guitar.individual.monthly` (1 month, $4.99, level 2) and `com.tabernacletools.guitar.church.monthly` (1 month, $19.99, level 1 = higher tier, so moving to Church is an upgrade).
3. On **each** product: Subscription Prices → Introductory Offers → **Free**, duration **2 weeks**, all countries, no end date. (Apple enforces one intro offer per Apple ID across the whole group.)
4. Add localized display names/descriptions, the review screenshot of the paywall, and submit both with the first app version.
5. Sandbox check: a fresh sandbox tester sees "Start 2-week free trial"; after taking the trial on either plan, both buttons say "Subscribe". (Clearing the tester's purchase history resets this in Sandbox only.)

**No permanent free tier.** When the trial or subscription ends, the app goes **read-only**:
- Still works: open the library, read charts and setlists, browse the hymnal, and **export a backup**. Users never lose their data.
- Off: the audio engine (tones, amp, click, voice cues, practice tracks), band sync, projector lyrics, the Live view, and editing/import.
- Rigs, presets and songs are untouched and come back instantly on resubscribe.
- Billing problems: Apple's **16-day billing grace period** keeps full access while the card is retried. After that the app is read-only.
- Refunds or revoked access: read-only right away.

**Sound packs** stay as content groupings (Starter, Ambient Worship, Vintage Pedals, Amp Collection, Keys packs) for browsing and "new this month" marketing. Any active plan unlocks all of them. There are no one-time pack purchases and no consumables.

### How church seats work
- Apple Family Sharing is limited to family groups of 6, so it can't share a church plan. We need **our own small account service**: Sign in with Apple, plus a tiny backend (CloudKit public DB or one serverless function plus a DB).
- **The leader** buys the Church plan in the app. The app sends the App Store transaction (verified with the App Store Server API) to our service, which creates a team.
- **Invites:** the leader shares a single-use invite code or QR. The member signs in with Apple, redeems it, and gets a seat. The leader sees the roster and can remove anyone to free a seat. The cap is 15, the leader included.
- **Member devices** hold a signed *seat token* (team id, member id, team paid-through date, last refresh). It works offline for up to **30 days** after its last refresh, and never past the team's paid-through date. Sundays in church basements with no Wi-Fi are fine.
- Already modelled and tested in `core/model/Packs.{h,cpp}`: `ChurchTeam` (leader, invites, redeem, remove, cap) and `Entitlements` (trial/active/grace/expired, seat tokens, `Feature` gating, offline cache). v0.11 adds the seat service (`seat-service/`: Cloudflare Workers + D1, ~$0/mo; see its DEPLOY.md) and the app's `SeatClient` + team UI. The StoreKit 2 bridge landed in v0.12; v0.13 adds one-trial-per-person (single group + intro-offer eligibility + seat-service trial_history).
- Abuse guardrails for later: one seat per Apple ID, and seat changes rate-limited (for example 5 swaps per month).

### Apple's cut and the money (Small Business Program: we keep 85%)
| | Customer pays | We keep (85%) |
|---|---|---|
| Individual | $4.99/mo | **$4.24/mo** ($50.90/yr) |
| Church | $19.99/mo | **$16.99/mo** ($203.90/yr) |

(US prices, before sales tax. Apple's subscription rate drops to 15% after a year of paid service on standard terms anyway, but under the Small Business Program it's 15% from day one.)

**Fixed costs per year (estimated):** Apple developer $99, account backend ~$0–300 (CloudKit is free at our scale; a small hosted DB is ~$25/mo), domain and email ~$50, JUCE licence $0 under the Starter revenue cap (check). **About $500/yr all-in**, before voice recordings and any ads.

**Break-even:** about **10 Individual** or **3 Church** subscribers.

**Revenue examples (net, after Apple's 15%):**
| Scenario | Individual subs | Church subs | Net / month | Net / year |
|---|---|---|---|---|
| Hobby | 50 | 10 | $382 | ~$4.6k |
| Steady | 300 | 80 | $2,632 | ~$31.6k |
| Strong | 1,000 | 300 | $9,339 | ~$112k |

Assumptions to watch: trial-to-paid conversion is typically 20–40% for music tools, and monthly churn of 3–6% is normal. Churches churn less but take longer to decide (often one per budget meeting).

**Why this works against competitors:** OnSong Premium is $5.99/mo for charts alone, Playback Premium is $9.99/mo for tracks, and BIAS FX/ToneStack charge $20–60 for tones. $4.99 covers charts, click, cues, tones and sync. $19.99 for the whole band costs less than one MultiTracks seat.

## 4b. App Store Connect: separate apps, one subscription (decided by Clayton, Nov 4, 2026)
Separate apps keep downloads small and performance focused: **Tabernacle Tools - Electric Guitar** (`com.tabernacletools.guitar`), **Keys** (`.keys`), **Bass** (`.bass`), **Charts** (`.charts`).

StoreKit subscriptions belong to one app, so **each app has its own subscription group and products**:

| App | Individual $4.99/mo | Church $19.99/mo |
|---|---|---|
| Guitar | `com.tabernacletools.guitar.individual.monthly` | `com.tabernacletools.guitar.church.monthly` |
| Keys | `com.tabernacletools.keys.individual.monthly` | `com.tabernacletools.keys.church.monthly` |
| Bass | `com.tabernacletools.bass.individual.monthly` | `com.tabernacletools.bass.church.monthly` |
| Charts | `com.tabernacletools.charts.individual.monthly` | `com.tabernacletools.charts.church.monthly` |

Each group: 2-week free intro offer, Billing Grace Period 16 days, Server Notifications v2 pointing at the same Worker URL.

**How one purchase unlocks all apps:**
1. The person signs in with Apple (all four App IDs grouped under one primary App ID, so Apple gives the same user id in every app).
2. After a purchase, renewal (Transaction.updates) or Restore Purchases, the app posts the signed StoreKit 2 transaction (JWS) to `POST /v1/purchases`. The Worker verifies it to Apple's root certificate, re-checks it with the App Store Server API, and records it against that person.
3. App Store Server Notifications v2 from any of the four apps keep it current (renewals, billing retry/grace, expiry, refunds).
4. Every app reads `GET /v1/entitlement` and gets a signed token that works offline for up to 30 days (inside the paid period).

**No double billing:** if the person is already covered, the paywall in another app says **"You're covered"** and hides the buy buttons (an Individual subscriber still sees the Church upgrade). If someone buys twice anyway, the app tells them which app already covers them and to cancel the extra one in Settings > Apple ID > Subscriptions (we can't cancel it for them).

**One trial per person, all apps:** Apple allows one intro offer per app's group, so the Worker's `trial_history` records it; a second trial in another app gives no access until its first paid month. The paywall promises a trial only when StoreKit and (if signed in) the Worker both agree.

**Lapse:** a subscription running out never silences the running app (rig, click, backing tracks keep playing); read-only starts at the next launch. Library, charts and backup export always work.

**App Review notes (guidelines 3.1.1 / 3.1.3(b) / 5.1.1):**
- Unlocking content in one app from a purchase made in another of our apps through an account is allowed ("multiplatform services" / account-based access), **as long as each app also offers the same subscription as In-App Purchase**. Every app keeps its own paywall and products.
- A purchase in an app must work **without signing in**; signing in is only needed to share it across apps or for church teams. Explain this in the App Review notes and give a demo account.
- Restore Purchases stays in every paywall. Account deletion stays in the app (Settings > Subscription).
- Don't mention other ways to pay, and don't link out to buy.

**Hosting:** unchanged - Cloudflare Workers + D1 free tiers (est. $0-5/month at launch scale).

## 5. Competitors (checked Nov 3, 2026)
| Product | What it is | Price | Where we differ |
|---|---|---|---|
| [Loop Community Prime](https://loopcommunity.com/en-US/prime-multitrack-app) | Multitrack playback, click and cues, Tonic pads | Free app; Prime Pro from $4.99/mo; [TrackSelect](https://loopcommunity.com/en-US/trackselect) $9–$74.67/mo | Prime plays *tracks*. We are the *instrument rig* plus chart plus click that sits alongside it. No guitar tones. |
| [MultiTracks.com Playback](https://www.multitracks.com/pricing/) | Multitrack playback, ChartBuilder, pads | Premium $9.99/mo; ChartBuilder $1.99/mo; MultiTracks One $134.99/mo | Same as above. Their charts are subscription-based. |
| [OnSong](https://apps.onsongapp.com/pricing/) | Chord charts, sets, projection, pedals | Essentials $3.99/mo, Premium $5.99/mo ([App Store](https://apps.apple.com/us/app/onsong-2026/id1330350343)) | The chart leader. No tones, no instrument engine. We match charts and projector and add sound plus band sync. |
| [Planning Center Music Stand](https://www.planningcenter.com/music-stand) | Chart reader tied to Services | $5–10/mo add-on | Needs Planning Center Services. We work standalone (Clayton chose to skip Planning Center). |
| [ToneStack / ToneStack PRO](https://apps.apple.com/us/app/tonestack-pro-guitar-amps-fx/id1529842791) (Yonac) | Circuit-modelled amps and effects, AUv3 | $9.99 + IAP ($59.99 unlock-all, $19.99 V2 pack) | A huge gear list, but nothing for worship: no setlist, scenes, or click. |
| [BIAS FX 2 Mobile](https://apps.apple.com/us/app/bias-fx-2-1-guitar-tone-app/id1475438828) (Positive Grid) | Amps, effects, IRs, ToneCloud | Free + licences $7.99/$19.99/$39.99 + packs | Strong tones but a general-purpose rig. Song-by-song scene changes are clumsy. |
| [Nembrini Audio](https://apps.apple.com/us/developer/nembrini-audio/id1493296895) (the brief's "Nembrionic") | Single-amp and single-pedal AUv3 apps | About $9.99–$19.99 each | Great amps, but you need a host app (AUM) and a separate chart app. |
| [Line 6 Mobile POD 2.0](https://line6.com/mobile-pod-guitar-app/) | 32 amps, 16 cabs, 16 effects | Free limited version + IAP | Old, with no worship features. |
| Neural DSP | Desktop plugins and Quad Cortex hardware. **No iOS amp app** (only the [Cortex Cloud app](https://apps.apple.com/us/developer/neural-dsp/id1458496080)) | €99–149 per plugin | Shows the top end of what tone costs. Players with a Quad Cortex would use us as a chart, click, and cue sync box. |
| Sunday Keys / MainStage (keys, for context) | Worship keys patches | MainStage $29.99 on Mac; Sunday Keys sold per patch pack | Target for the Keys app: key-matched pads plus song-aware patch changes on an iPad. |

**Our gap, in one sentence:** the only iPad app where *each player's instrument rig, the chart, and the click all follow the leader's song and section changes*, with the songs' section names spoken in the cues.

## 6. Marketing to worship leaders
- **Position it as:** "Your whole Sunday rig on one iPad. Charts, click, cues, and tones that change with the song."
- **Content:** 60-second Reels/Shorts like "dotted-eighth worship lead with only an iPad, no amp", "Verse two, 2, 3, 4 cue demo", and "two iPads in sync". Clayton's own band works as the demo team.
- **Partnerships:** worship guitar YouTubers (preset packs designed by them, sold as a pack with a revenue split), worship-leader podcasts, and small-church networks such as denominational tech groups.
- **App Store optimisation:** keywords like worship, church, chord chart, click, metronome, guitar amp, pads, ChordPro, AUv3.
- **Launch-week offer:** an introductory discount on the All Packs bundle, and a press kit for Loopy Pro, AUM, and the iOS-music forums.
- **Website:** a one-page site with an email list of beta churches and a "Sunday-ready" setup guide (4-cable method, travel router, M-Vave pedals).

## 7. Support
- In-app Help with "Export diagnostics", which zips the logs, rig, and settings (we already have a backup bundle).
- A support email, a Discord or Discourse for presets, and a 48-hour reply target. Keep a FAQ on interfaces, latency, and the sync router.
- Releases: TestFlight first, then a phased App Store release over 7 days. Never ship an update on a Saturday.

## 8. Timeline (realistic, part-time)
| When | Milestone |
|---|---|
| Nov 2026 | First Mac build and an iPad smoke test (Guitar + Core Test). Fix the first crop of Xcode and runtime issues. |
| Dec 2026 | Apple Developer + LLC, record our own voice pack. (Link licence dropped - MIDI Clock instead.) Hook up StoreKit 2 subscriptions and deploy the seat service (Sign in with Apple + Cloudflare Workers/D1, built in v0.11). |
| Jan 2027 | Internal TestFlight at Clayton's church. Chart-only app (reuses the core, about 2–3 weeks). |
| Feb–Mar 2027 | External TestFlight with 10–20 churches. Finish the Vintage and Amp packs, plus presets. |
| Apr 2027 | **App Store launch: Guitar + Chart-only** (Easter is Mar 28, 2027, so test across Holy Week rather than launch then). |
| Summer 2027 | Keys app (key-matched pads as the hero feature). |
| Fall 2027 | Bass app. Optional church cloud subscription. |

## 9. Open decisions for Clayton
1. Whose voice records the cues. *(Ableton Link: decided Nov 4 - leave off; use MIDI Clock + Wi-Fi sync.)*
2. Whether to add yearly plans after launch, and the church seat cap (15 for now).
3. Seller name / LLC: decided Nov 4 - **Tabernacle Tools LLC** (Texas, to be formed).
