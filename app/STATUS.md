# Bearing Live: STATUS

The public name is **Bearing Live**. The company is **Bearing**. The apps are Bearing Live Guitar, Keys, Bass, Charts, and Hub. Bundle ids stay `com.tabernacletools.*`, and the Xcode project and scheme names are unchanged. The iPad home-screen name is set by hand in Xcode to **Bearing Live**. The notes under v0.19 record the earlier working name.

# Earlier notes (v0.19 renamed from Worship Rig; v0.18 one subscription for every app; v0.17 MIDI Clock send/receive; v0.16 legal docs, Link off by default, voice overrides, tempo-map editor, stem sharing, CCLI per-song totals; v0.15 CCLI usage log + export; v0.14 multitrack backing tracks; v0.13 one free trial per person; v0.12 anti-aliasing, cab IR picker, StoreKit 2, read-only locks; v0.11 church seat service; v0.8 - editable section colours, footswitch banks, capo helper, song stats, backup bundle, stage mode, auto-scroll, rehearsal recorder)

_Last updated: 2026-11-04 (America/Chicago)_


## v0.19 - renamed to Tabernacle Tools (2026-11-04, America/Chicago)
- Clayton (Nov 4, 9:53 PM): "Worship Rig" is now **Tabernacle Tools** (planned Texas LLC: Tabernacle Tools LLC). Rename only - no behaviour changes.
- App Store names / home-screen names (iOS truncates ~12 chars, so "TT <instrument>"):

| App | App Store name | Home screen | Bundle id |
|---|---|---|---|
| Guitar | Bearing Live Guitar | Bearing Live | `com.tabernacletools.guitar` |
| Keys | Bearing Live Keys | Bearing Live | `com.tabernacletools.keys` |
| Bass | Bearing Live Bass | Bearing Live | `com.tabernacletools.bass` |
| Charts | Bearing Live Charts | Bearing Live | `com.tabernacletools.charts` |
| Hub | Bearing Live Hub | Bearing Live | `com.tabernacletools.hub` |
| Core test harness | - | TT Core Test | `com.tabernacletools.coretest` |

- Old -> new IDs: bundle prefix `com.bearthe.worshiprig` -> `com.tabernacletools` (Guitar was `com.bearthe.worshiprig.guitar`; the seat service had it as `com.bearthe.worshiprig`, now consistent). Products `<bundle>.individual.monthly` / `<bundle>.church.monthly` (Guitar: `com.tabernacletools.guitar.individual.monthly`). Subscription group "Tabernacle Tools" (id `tabernacle-tools`). App group `group.com.tabernacletools.guitar`. Share extension `com.tabernacletools.guitar.share`. Sign in with Apple: the four App IDs grouped under primary `com.tabernacletools.guitar`. Entitlement token audience `com.tabernacletools`. No keychain group is used (seat session lives in `<data>/seat.json`).
- Other renamed IDs: Bonjour `_worshiprig._tcp` -> `_tabernacle._tcp`, pasteboard type `com.tabernacletools.shared-chart`, virtual MIDI port "Tabernacle Tools Clock", data folder `WorshipRig` -> `TabernacleTools` (nothing shipped, so no migration), backup files `TabernacleTools-backup-<date>.wrbackup.json`, legal URLs `https://tabernacletools.app/terms|privacy` (placeholders), company name "Tabernacle Tools LLC" (Terms + Privacy name it).
- CMake: project `TabernacleTools`, targets `TabernacleGuitar`, `TabernacleCoreTest`, `TabernacleShare`, `TabernacleVoice`; classes `TabernacleGuitarProcessor/Editor`. Internal `wr::` namespace, `WR_` options and the `/workspace/worship-rig` folder are unchanged. AU manufacturer/plugin codes (BtyY / WRig) unchanged.
- Seat service: worker `tabernacle-tools-seats`, D1 database `tabernacle-tools-seats` (migrations 0001-0003 unchanged; a fresh D1 gets them all). Test fixture tokens regenerated for the new audience.
- Tests: all 23 ctest suites, seat-service 29/29, tsc, JUCE syntax check pass. Mockups re-rendered (they had no product name in them).

## v0.18 - one subscription unlocks every Tabernacle Tools app (2026-11-04, America/Chicago)
- Clayton (Nov 4, 9:02 PM): separate Guitar / Keys / Bass / Charts apps; one $4.99 Individual or $19.99 Church subscription unlocks all of them.
- Seat service: `POST /v1/purchases` (signed StoreKit 2 transaction, verified to Apple Root G3 + Server API, recorded against the Sign in with Apple user; one purchase can't unlock two accounts), `GET /v1/entitlement` (own purchases in any app + church seat -> family token, `aud` com.tabernacletools.guitar), Server Notifications v2 from all four apps, migration `0003_cross_app.sql`, app catalog `src/catalog.ts`. Church teams can be created from any app's Church product.
- One trial per person across all apps (`trial_history`); a second trial elsewhere unlocks nothing until paid.
- App: `core/model/CrossApp.*` (paywall mode, "You're covered" text, trial honesty, LapseGuard). `SeatClient::postPurchase / coverage`; refresh uses GET (falls back to the old POST). `CoreProcessor::applyStoreEntitlements` posts each transaction once and drops refused trials. Paywall hides buy buttons when covered (Church upgrade still shown to Individual subscribers) and warns about duplicates.
- Lapse: once the running app has had access, audio never goes silent (rig, click, backing tracks); read-only applies from the next launch. Offline grace: token valid up to 30 days inside the paid period.
- Tests: seat-service 29/29 (6 new cross-app), `v018_cross_app_entitlement` (29 checks). All 23 ctest suites pass; JUCE syntax check clean; tsc clean.
- **Not verified:** nothing on a device; Swift StoreKit bridge unchanged (it already returns each transaction's JWS); Keys / Bass / Charts App IDs and products don't exist yet.

## v0.17 - MIDI Clock send + receive (2026-11-04, America/Chicago)
- Clayton (Nov 4, 5:55 PM): "lets do midi clock and leave off link."
- **Already had:** MIDI Clock *receive* (24 ppqn tempo from Cubase / network MIDI) via `MidiRouter`, with Start/Continue/Stop and a MIDI CLOCK badge.
- **Added send:** `core/midi/MidiClock.h` (`MidiClockOut`) emits 24 ppqn Clock, Start / Stop / Continue, and Song Position Pointer on section jumps. Driven from `CoreProcessor::processBlock` from the live tempo and PLAY state (works with the click and with stems).
- **Settings > MIDI Clock:** Off / Send / Receive. Send opens a virtual **"Tabernacle Tools Clock"** port (other iPad apps) plus an optional chosen USB / Bluetooth / network output. Receive can use all inputs or a chosen input. Leader-only send; followers keep Wi-Fi band sync.
- Receive smoothing: EMA on the beat period; Start/Stop follow transport; SPP seeks the song position.
- Ableton Link stays off by default; removed from open decisions (Clayton chose MIDI clock + Wi-Fi sync). Band sync menu shows "Ableton Link: off" when the SDK is not built in.
- Tests: `v017_midi_clock_send_receive` (26 checks). All ctest suites pass; JUCE syntax check clean; seat-service tests + tsc pass.

## v0.16 - finish planned items (2026-11-04, America/Chicago)
- Clayton (Nov 4, 5:40 PM): "do anything planned and not yet done."
- Mockups: "Great Is Thy Faithfulness" (still in copyright) replaced with "It Is Well with My Soul" (PD); keys-live.png / keys-workshop.png re-rendered.
- Legal: docs/TERMS.md + docs/PRIVACY.md drafts (placeholders: date, email, developer name; URLs in core/model/Legal.h). Links in the paywall and Settings > About & legal / Subscription.
- Hymns: Christ the Lord Is Risen Today line fixed ("Where thy victory, O grave?"); AZMON composer date duplicate removed. 40 hymns regenerated.
- Ableton Link: `WR_WITH_LINK` defaults OFF; store builds with Link need `WR_LINK_LICENSED=ON`. See docs/LINK_LICENSE.md.
- Voice cues: resources/voice/RECORDING_SCRIPT.md (49 words); recorded WAVs dropped in `<data>/Voice/` replace the TTS files with the same name.
- Multitrack: stems through Open in / Copy to (public.audio), tempo-map editor (TEMPO button), streaming import conversion, arrangement edits without dropped audio blocks (lock-free swap), chart auto-advance follows the stems while tracks play.
- CCLI: extra per-song totals CSV in Search & Report order + entry steps in the summary.
- Tests: `v016_voice_hymns_tempo_resequence_ccli`. All 21 ctest suites pass; JUCE syntax check clean; seat-service tests + tsc pass.

## v0.15 - CCLI song-usage log + export (2026-11-04, America/Chicago)
- Clayton (Nov 4, 5:27 PM): "Add log and export ... leave auto reporting." **No CCLI API, no automatic reporting**; nothing leaves the iPad unless the user shares the export.
- Songs: optional CCLI details (CCLI song #, title, authors, public domain). Built-in hymns copied into the library are marked public domain with title and author filled in. Song menu > CCLI details... to edit.
- Log (`core/model/Ccli`, `Library::usageLog`, saved + in backups): automatic during live sets only (never practice mode): "chart" when PLAY runs on the song, "projected" when its lyrics are on the HDMI projector or a projector-only iPad. One row per song per day per setlist; flags OR-ed. Song details are copied when logged; a CCLI # added later is filled in at export.
- Export (Settings > CCLI song usage): date range (YYYY-MM-DD, blank = open) -> `<data>/Exports/CCLI-usage-<from>-to-<to>.csv` (Date, Song Title, CCLI Song Number, Authors, Public Domain, Lyrics Projected, Chart Used, Setlist; Excel-safe quoting, formula injection guarded) + `.txt` summary (uses per song, projected / chart counts, list of songs missing a CCLI #, "enter by hand on CCLI's site"), handed to the share sheet. Public-domain songs left out unless "incl. public-domain hymns" is picked.
- Every device logs its own use; the leader's iPad (which plays every song) is the one to export from.
- Tests: `v015_ccli_usage_log` (24 checks). All 20 ctest suites pass; JUCE syntax check clean; seat service 23/23.
- **Not verified**: not run on a device; the CSV columns follow what CCLI's manual reporting asks for in general but haven't been compared with the current CCLI reporting form; a song shown only on a follower's projector iPad is logged on that iPad, not the leader's.

## v0.14 - multitrack backing tracks (2026-11-04, America/Chicago)
- **Model** `core/model/Multitrack.{h,cpp}` (saved with each song): up to 16 stereo stems with name, colour (guessed from the name), volume, pan, mute, solo and bus (Main L/R or the Out 2 click/cue bus = in-ears only; "click"/"guide" stems default to in-ears). Mutes are remembered per song. Tempo map (offset + tempo changes) and section starts on the track timeline (from WAV cue markers whose names match the chart sections, "Mark section here", or estimated from bars).
- **Sequencing**: the arranged order (song / setlist arrangement) is turned into sample ranges in the files; repeats and deleted sections splice with 10 ms crossfades. Arrangement edits re-sequence while playing. Live jumps land on the next bar line (or at the end of the section: setting `trackJumpAt`) with the usual spoken 1-bar cue; LOOP SECTION vamps until released. Stems start after the normal count-in (same audio block as the count-in clicks).
- **Streaming** `core/audio/MultitrackPlayer.{h,cpp}`: stems stream from float WAV files in `<data>/Multitracks/<song>/` (import converts mp3 / m4a / aiff / caf / other-rate WAV once, at the session rate). A reader thread keeps a 12-chunk cache per stem filled ahead of the playhead and of the next splice target; the audio thread never touches files, locks or allocates. Missing data = silence + underrun count -> "storage can't keep up" warning; rate mismatch and CPU warnings via `mt::loadWarning`, shown on the tracks bar.
- **Live glue** `core/audio/MultitrackLive`: stems drive the chart (the leader's LiveSession lands each section, so the existing sync carries sections, highlights and per-section patches to followers). Footswitch actions: Tracks play/stop, next section, loop (vamp), stop; plain Play/Next/Prev/Jump chorus also drive the stems when tracks are loaded.
- **Sync**: new `mt` event (+ in snapshots): leader's tracks loaded/playing/section/loop/pending. Stems never leave the leader's iPad. Followers: `followLeaderKey` option makes charts follow the leader's key even if the player transposed locally. **Stems are not pitch-shifted** - a key change moves the charts only; change the key on the tracks before the service.
- **UI** (`core/ui/Tracks.{h,cpp}`): Live view TRACKS bar (play/stop, section < >, loop/vamp, current section + bar + queued jump, mixer, mark section here, import), mixer sheet (mute, solo, main / in-ears, volume, pan), song menu (import stems from files or a folder, remove). Locked when the subscription lapses (playback stops, import needs Import access).
- Tests: `v014_multitrack` (81 checks: tempo map, sequencing, arrangement re-sequencing, bar / section-end jumps, vamp, preroll, mix + solo/mute/pan, mute persistence through song/library JSON, streaming reads, WAV + cue-marker reading, live glue, lock, sync messages, warnings). All 19 ctest suites pass; JUCE syntax check clean (incl. store-build pass). Seat service: 23 node tests pass, tsc clean.
- **Not verified**: nothing run on an iPad or Mac. Disk/CPU on the 2017 iPad Pro with 16 stems is unmeasured (budget: 16 x 1.5 MB cache, ~0.75 MB/s per stem at 48 kHz float). Share Sheet import of stems not wired (Files picker + folder only). Tempo-map editing UI not built (constant tempo from the song unless the JSON has tempo points). Import decodes a whole file into RAM once (fine for songs, not for 20-minute files on an old iPad). A section-change arriving from the beat-based auto-advance can briefly disagree with the stems before the stems correct it. Arrangement edit while playing can drop one audio block.

## v0.13 - free trial usable once per person (2026-11-03, America/Chicago)
- Both plans in ONE App Store subscription group, so Apple's 2-week intro offer is once per Apple ID across Individual and Church.
- `WRStoreKit.swift` `introEligibility:` -> `Product.SubscriptionInfo.isEligibleForIntroOffer(for: groupID)`; refuses to report eligible if the products are in different groups. Exposed via `wr::store::introEligibility` (ObjC++ bridge; stubs answer "unknown").
- `core/model/StoreMapping`: `IntroEligibility` (Unknown/Eligible/Ineligible), `introEligibilityFrom`, `paywallButtonLabel`. Paywall shows "Start 2-week free trial, then $X/month" only when Eligible; Unknown/Ineligible show "Subscribe - $X/month" and no trial wording anywhere.
- Seat service: `trial_history` (hashed Apple id, kept on account deletion), `trialEligible` in `/v1/entitlement`, 402 `trial_already_used` for a second trial.
- Tests: `v013_one_trial_per_person` (new); all 18 ctest suites pass; JUCE syntax check clean (incl. store-build pass). Seat service: 23 node tests pass, tsc clean. **Not compiled for iOS/Swift yet.**

## v0.12 - anti-aliasing, cab IR picker, StoreKit 2, read-only locks (2026-11-03, America/Chicago)
- **Oversampling** (`guitar/dsp/Oversampler.h`): polyphase IIR halfband cascade (allpass design, same family as `juce::dsp::Oversampling` IIR mode; written in-house because the DSP library is JUCE-free). Wraps the amp's gain stages + tone stack + power amp/sag, and every drive/fuzz clip stage (clean blend goes through the same filters, so no comb filtering). Setting Off/2x/4x/8x (default 4x) + auto-reduce (down one step after 1 s above 70% audio-thread load; back up after 30 s below 35%). Latency reported to the host: 3.8 / 4.9 / 5.3 samples per oversampled pedal at 2x/4x/8x (worst case: every oversampled pedal in the rig).
  - Aliasing, dB below the harmonics (Off / 2x / 4x / 8x): high-gain amp 17.4 / 21.3 / 30.0 / 38.6; British Crunch + cab 18.6 / 24.2 / 43.5 / 48.9; Distortion 16.7 / 22.1 / 28.7 / 35.7; Fuzz 16.0 / 21.9 / 28.3 / 35.0.
  - CPU, overdrive + amp on one x86 core (Linux): 0.52% / 0.92% / 1.66% / 3.10%. **Not measured on the A10X iPad**; expect several times more. Auto-reduce is there for that.
- **Cab IR picker**: Workshop amp section > Load IR file... (iOS Files, read through the security-scoped URL), validated (RIFF/WAVE, readable, not silent, <= 8 MB; > 1024 taps @48k flagged as shortened), copied into `<data>/IRs` with a safe unique name, shown on the button; Replace / Remove (back to the 2x12 Open Back cab).
- **Live view footswitch panels** show the section colour of the Prev / Next / Chorus targets (tint + bar; "NO CHORUS" when none).
- **StoreKit 2** (`core/ui/store/`): Swift `WRStoreKit.swift` (products, purchase, restore = `AppStore.sync`, `Transaction.currentEntitlements`, `Transaction.updates` listener, finishes transactions), ObjC++ `StoreBridge_ios.mm` (finds the Swift class by name; no bridging header), pure C++ mapping `core/model/StoreMapping` (Church beats Individual; trial, grace, billing retry, expired, revoked, upgraded). Church transaction JWS goes to `SeatClient` team creation. All behind `-DWR_STORE_BUILD=ON`.
- **Read-only locks** (store build): lapsed = library, read charts, hymnal, backup export/restore only. Locked: Live view, Workshop/editing (setlist save, add/reorder, players, arrangement), import/paste/shared charts (queued until unlocked), setlist sharing, projector (also switched off when access lapses), audio, sync. **Paywall**: Individual $4.99 / Church $19.99 (App Store prices when loaded), 14-day free trial wording when eligible, Restore Purchases, "I was invited by my church", Not now.
- Tests: `guitar_v012_antialiasing` (68 checks), `v012_store_locks_sections_ir` (76 checks). All 17 ctest suites pass; JUCE syntax check clean (incl. store-build pass).
- `BUILD_ON_MAC.md` rewritten for a first-time Xcode user with a free Apple ID (iPad Pro 2017 / iPadOS 17 / Lightning Camera Adapter, signing error table, App Store build section).
- **Not verified:** nothing run on a device or Mac. `WRStoreKit.swift` and `StoreBridge_ios.mm` have never been compiled (no Swift/Apple toolchain here). Swift-in-static-library linking may need the `Empty.swift` workaround in BUILD_ON_MAC.md. No App Store Connect products exist; the paywall's legal text has no Terms/Privacy links yet (App Review requires working links in the app before submission). Oversampling CPU and the 70% guard are untuned on the A10X.

## v0.11 - church seat service (2026-11-03, America/Chicago)
- **Service** `seat-service/` (sibling of `app/` in the zip): TypeScript on Cloudflare Workers + D1. Sign in with Apple JWT checked against Apple JWKS; App Store signed data checked through the x5c chain to the pinned Apple Root CA G3, plus Apple leaf/intermediate OIDs; App Store Server API status lookup; Server Notifications v2 (renew, grace, billing retry, expire, refund, refund reversed, revoke; idempotent on UUID; out-of-order safe); teams; single-use 7-day invite codes; join/leave/remove; 15-seat cap incl. leader (guarded insert); Ed25519 entitlement tokens (paid-through incl. grace, 30-day offline cap); rate limits; account deletion; `openapi.yaml`; `DEPLOY.md`. `npm test`: 17 tests (mocked Apple). `npm run dev` + `scripts/smoke.sh`: local end-to-end run.
- **App**: `core/sync/SeatClient.{h,cpp}` (transport-injected, TweetNaCl Ed25519 verify, cache + 30-day offline, persistence), `core/sync/SeatKeys.h` (URL + public key placeholders), `core/third_party/tweetnacl`. `tests/core/test_seats.cpp` (74 checks) verifies tokens signed by the Node service (`seat_fixture.h`, generated by `seat-service/scripts/gen-fixture.ts`). JUCE: `SeatSession` (juce::URL transport, background thread, `<data>/seat.json`), `TeamPanel` (invite entry, leader roster/invites/remove, leave, delete account), Sign in with Apple in `Platform_ios.mm` (no name/email scopes). Settings > Subscription > Church team is shown **only with `WR_STORE_BUILD`**.
- **Not done / unverified**: StoreKit 2 bridge (`platform::churchSubscriptionJws` returns empty, so a leader can't create a team on device yet; it needs a small Swift file); no device, Xcode or real-Apple run; Worker never deployed (wrangler bundling of @peculiar/x509 untested); juce::URL DELETE/headers untested on iOS.

## Built and tested on Linux (core: no JUCE, platform-independent C++17)
`ctest --test-dir build`: 7 test programs, all passing (Linux, Release).
- **Core suite** (links `worship_core` only, no guitar code): 576 unit checks (`core_unit`), 283 sync/projector/roster checks (`core_sync_projector_roster`), 32 click/cue/count-in render checks on the ClickEngine alone (`core_render`), 6 projector PNG checks, and 4 Ableton Link peer checks (`core_link`, when built with `-DWR_LINK_DIR`).
- **Guitar suite** (`worship_guitar_dsp`): 50 rig-build checks (`guitar_unit`) and 71 render/DSP checks (`guitar_render`: every pedal, routing, tails, CPU, click and cues mixed through the rig).
- Same checks as v0.5: the 626 unit checks are now split 576 core + 50 guitar. The renders gained new core-only click tests (output channels, a missing output pair keeps the grid running, Link re-seats once).

| Area | Files | Tested |
|---|---|---|
| Pedal base: params, smoothing, 8 ms click-free enable ramp, spillover tails | `guitar/dsp/Pedal.h` | ✔ click test: max sample step during toggles/scene stays within the signal's natural slope |
| Pedals (23 types) | `guitar/dsp/Pedals.cpp` | ✔ every type renders finite, audible audio → `build/renders/<type>.wav` |
| Rig engine: BEFORE/AFTER AMP lanes, 4-cable + 2-cable, automatic fallback to 2-cable with fewer than 4 outs, output trims, safety limiter, lock-free command queue, chain hot-swap | `guitar/dsp/Rig.h/.cpp`, `RigBuilder.h` | ✔ out1 = mono pre-amp lane, outs 3/4 = stereo post lane, out2 silent; fallback ✔; CPU 4.7 % of one x86 core with 11 pedals on |
| Tuner (YIN pitch tracker) + mute | `core/audio/PitchTracker.h` | ✔ E2…E4 within 3 cents; mute ✔ |
| Chart import: ChordPro, chords-over-lyrics text (what a text PDF gives), `[Verse 1]` / `Chorus:` headers, `x2`, inline ASCII tab, highlight colours | `core/model/Chart.cpp` | ✔ seed chart → V1 V2 PC CH V3(x2) CH V4 V5 CH CH, orange and pink tabs; plain-text sample ✔ |
| Transpose: chords (sharp/flat spelling per key, slash chords), key names, tabs (fret shift, octave jump to stay on the neck, capo advice), whole-chart, per-setlist key override | `core/model/Chart.cpp`, `LiveSession.cpp` | ✔ e.g. A→Bb gives `Bb/D`, `Gm7`; orange riff A→E moves up an octave and stays aligned |
| Data model + JSON persistence (songs, scenes, rigs, setlists by date, controller profiles, settings) | `core/model/Song.h/.cpp`, `Json.h` | ✔ lossless round-trip |
| MIDI: 2× Chocolate Plus (guitar ch 1, sections ch 2), long press while held, MIDI learn, external Cubase (PC / notes → section, CC 102/103, MIDI Clock → BPM), tap tempo | `core/midi/MidiRouter.*` | ✔ all mappings, long-press timing, learn, clock at 72 BPM ±0.2 |
| Live session: setlist, sections incl. repeats, jump to chorus, next/prev song, scenes per section, boost, per-song C/D, full view ↔ section view with auto-return, next-switch cue | `core/model/LiveSession.*` | ✔ |
| Song tempo (v0.2): per-song BPM 30-300 (0.1 resolution), ChordPro `{tempo:}` read + written (`chartToChordPro`, in-place `setChordProTempo`), live -/+ nudge, TAP (on screen + guitar Chocolate A), type-in, Save-to-song / Revert, setlist BPM override cleared on save; MIDI clock overrides while present (MIDI CLOCK badge), falls back to song tempo when the clock stops (no clock for max(1 s, 2.5 beats)); router re-measures after a >250 ms clock gap | `Song.h` (`clampBpm`), `Chart.cpp`, `LiveSession.*`, `MidiRouter.*`, `LiveView.cpp`, `WorkshopView.cpp` | ✔ parse/serialize round trip, clamping, tap averaging/reset, clock override, fallback, restart at new tempo, follow-clock off; render: 1/4 synced delay echo at 72/90/120 BPM within 3 ms |
| v0.3 Click track: sample-accurate, follows the active tempo (song BPM / MIDI clock), accent on beat 1 per `{time:}`, volume, smoothed mute, own output (outs 3/4 default, outs 1/2, or out 2 mono), mixed after the limiter so the tuner mute doesn't silence it; per-song "click on" default; mute remembered | `core/audio/Click.h`, `Rig.*`, `LiveSession.*` | ✔ spacing at 120/90 BPM, accent in 3/4, output isolation, conflict flag, mute/unmute, tuner mute keeps the click |
| v0.3 Tuner: full-screen overlay, rig muted while open, note/octave/cents/Hz, A = 430-450 Hz | `PluginEditor.cpp` (TunerOverlay), `PitchTracker.h` | ✔ reference maths; overlay not seen on device |
| v0.3 Footswitches: guitar A = tap on press + hold 1 s = tuner; guitar D = per-song D on release + hold 0.6 s = click mute; per-switch hold time (`longMs`) | `MidiRouter.cpp`, `Song.cpp` | ✔ |
| v0.3 Key + capo: `{key:}` = sounding key, `{capo:}` read/written (chords stored sounding, written as shapes), capo suggestions, per-setlist capo override, chart shows shapes | `Chart.cpp`, `LiveSession.*` | ✔ |
| v0.3 Autosave + backups: library.json written atomically on every change (0.5 s debounce), rotating snapshots (last 10, edits within 60 s coalesce), lock file for crash detection, damaged file -> newest valid backup, manual restore (undoable) from Setlist/Workshop > Backups | `core/model/Backup.*`, `AppStore.cpp` | ✔ |
| v0.3 Rig change tails: outgoing chain keeps running on silence and fades out (default 3 s, 0-8 s setting) | `Rig.cpp` | ✔ tails ring, fade to silence, no click at swap, 0 s = cut |
| v0.3 Bar auto-advance: sections with `bars=N` advance on bar boundaries at song tempo after PLAY, only without MIDI clock; manual nav re-anchors to the current bar; repeats counted; tempo changes integrate | `LiveSession.cpp` | ✔ |
| v0.3 Setlist sharing: per-player key/capo (Players), band or per-player charts as PDF (own writer) or ChordPro (`{new_song}`), iOS share sheet | `core/model/Export.*`, `Pdf.*`, `Platform_ios.mm` | ✔ export core; PDF checked with pdftotext/pdftoppm; share sheet unverified |
| v0.4 Spoken section cues: next/prev/jump/goto (footswitch, screen, MIDI) wait for the next bar line while the bar grid runs (click on, PLAY, or MIDI clock); the target name is spoken on the click output in the rest of the bar if it fits (else on beat 1 of the next bar, change one bar later), then the remaining beats are counted ("2, 3, 4", any meter). Repeated presses move the target on; repeats say "... again". Cue volume, on/off, "still speak when click muted"; quantise on/off | `core/audio/Click.h`, `Voice.*`, `LiveSession.cpp`, `Rig.*` | ✔ name parsing, fits/defer timing at 120 4/4 and 90 3/4, counts land on beats, mute rules, queue/retarget/due, Cubase goto |
| v0.4 Voice: 49 bundled Piper TTS words (sections, modifiers, one-twelve) in `resources/voice/` (JUCE BinaryData); custom names rendered once with AVSpeechSynthesizer on iOS, else the section kind is spoken | `resources/voice`, `Platform_ios.mm` | ✔ decode + resample; iOS TTS uncompiled |
| v0.4 Count-in: default 8 clicks before PLAY, accent on each bar's 1, ends on a bar line (auto-advance starts after it), audible even when the click is muted, optional spoken count; global + per-song (off / 4 / 8 clicks / 1 / 2 bars) | `Click.h`, `LiveSession.cpp` | ✔ 4/6/8 clicks in 4/4 and 3/4, muted, spoken, codes |
| v0.4 Click presets: Beep hi/lo, Woodblock hi/lo, Cowbell hi/lo, Rimshot/sidestick - beat 1 is a different pitch and timbre in every preset; downbeat level (-6..+12 dB, default +4.4); count-in uses the same sounds | `Click.h`, AUDIO/MIDI > Click track | ✔ spectral peak/centroid differ, spectra uncorrelated even at equal level |
| Seed library: "He who was / He who is" (key A, 72 BPM placeholder), orange + pink tabs, Verse/Pre-Chorus/Chorus scenes, C = Octave, D = Freeze | `resources/he-who-was.cho`, `Song.cpp` | ✔ |

### Pedal status
- **v0.1 priority pedals, working and tested:** Comp, Boost, Overdrive, Fuzz, Octave Down, Chorus, Tremolo (free or synced), Delay (tempo-synced divisions, tap, ping-pong, spillover), Reverb, Shimmer, Freeze, EQ.
- **Working but basic (render-tested only; tune by ear):** Klon-style, Distortion, Poly Octave (granular POG-style), Harmonizer/Whammy, Univibe, Rotary, Phaser, Volume Swell, Key9 piano/organ and Synth9 pad. Key9/Synth9 are **monophonic** in v0.1: pitch tracking drives the worship-keys engine. Polyphonic tracking is TODO.
- **Stub (bypass + TODO):** Looper.

| v0.5 Band sync protocol: JSON-lines over TCP (port 52741), per-session seq numbers, gap -> resync -> snapshot, duplicate drop, late-join snapshot (setlist + songs + position + queued cue + tempo + transport) | `core/sync/Sync.*` | ✔ format/framing, leader diffing, follower apply, gap healing, late join mid-cue-bar and after the change |
| v0.5 Leader/follower: queued one-bar cue sent as Link beats (cueBeat/dueBeat) so every device speaks the cue in the same bar and lands on the same bar line; PLAY/count-in quantised to the next Link bar on all devices; followers keep own key/capo, click mute/volume, chart view; local navigation ignored while following | `Sync.cpp`, `LiveSession` follow* | ✔ same words + same bar on leader and follower, lands at beat 8 together, count-in starts together, own key kept across setlist changes |
| v0.5 Network: non-blocking BSD sockets, 1 s pings, 4 s silence = leader lost (banner, section held), auto-reconnect with backoff, multiple followers | `core/sync/Net.*` | ✔ real 127.0.0.1 TCP: 3 followers join, cue reaches all, leader killed -> banners + hold, leader back on same port -> all rejoin + resync, one leaves, others stay |
| v0.5 Discovery: Bonjour/mDNS `_tabernacle._tcp` (dns_sd: register / browse / resolve / IPv4 addr), works on an offline travel router; join-by-IP and one-tap rejoin fallback | `core/sync/Discovery.*` | compile-checked against Apple's dns_sd.h; not run here (no mDNS daemon) |
| v0.5 Ableton Link: LinkClock (BeatClock), quantum = beats per bar; click grid phase-locks per audio block (`ClickGen::lockTo`, trims drift, re-seats on join) | `core/sync/LinkClock.h`, `Click.h`, `Rig` | ✔ two grids at different start times lock to the same position and count identical beats; ✔ real Link SDK: 2 peers discover, tempo 73 propagates, same bar phase (`wr_link_test`, `-DWR_LINK_DIR=`) |
| v0.5 Leader roster: followers send a heartbeat every second (name, role player/projector, state hash, last seq, Link beat, section); leader pings for round-trip ms. Green = in sync; yellow = different section / behind on updates / Link beat off > 30 ms for > 1.5 s, or heartbeat late > 2.5 s; red = dropped (kept 30 s). Join/drop toasts; REMOVE (kick: follower shows "Removed by the leader" and does not auto-reconnect) | `core/sync/Roster.*`, `core/ui/BandPanel.*`, BAND n indicator on Live | ✔ join toast, ping ms, grace period, out-of-sync section, seq behind, beat -50 ms, whole-bar offset still in phase, late, timeout -> red + toast, expiry, kick vs drop; ✔ over TCP: 2 followers green with names/roles/sections, follow the leader, one forced out of step -> yellow, kick -> removed + no reconnect, app dies -> dropped |
| v0.5 Projector lyrics: lyrics only (chords, chord-only lines, tabs, comments dropped), balanced slides of N lines, title slide, blank/logo between songs, instrumental = blank, bar-spread auto-advance, operator next/prev/blank, ALL CAPS, font S/M/L/XL with auto-shrink + wrap, background image (dimmed) | `core/model/Projector.*`, `core/ui/ProjectorView.*` | ✔ slides/state machine/layout; ✔ projector follower lands on the chorus with the band; ✔ PNG render (`wr_projector_png`) white-on-black, centred, safe area |

## v0.8 (shared core unless noted)
- **Editable section colours** (`core/model/SectionColors.*`): 12-colour stage palette (orange, pink, blue, purple, green, red, yellow, teal, lime, indigo, white, grey) + custom colour wheel (#RRGGBB).
  - Global default per section type: AUDIO/MIDI > Section colours (Verse, Pre-Chorus, Chorus, Bridge, Tag, Intro, Outro, Interlude, Other), each with Custom... and Reset; "Reset all"; "Clear this song's colour overrides".
  - Per song, per section: Arrangement editor > SECTION COLOURS row (tap a swatch; * = override; RESET COLOURS). Saved with SAVE TO SONG / SAVE FOR THIS SETLIST.
  - ChordPro `{x_section_color: Chorus=teal}` (also `section_color`, `@N=` for duplicate names, or `{x_section_color: red}` inside a section). Unknown directives are ignored on import.
  - Shown in chart view (stripe + name), section chips (current filled, next ringed), arrangement editor, NEXT banner, PDF export. Leader sends its defaults + per-song overrides; followers show the leader's colours while following (their library isn't rewritten). Projector slides unchanged.
- **Setlist builder**: already in the app (ordered songs, per-song key/BPM/capo overrides, drag reorder) and synced to followers; v0.8 adds tests for override + reorder sync and a "played N days ago" flag on setlist rows.
- **Auto-scroll**: full-song view glides with the bar position inside sections that have `bars` (AUDIO/MIDI > Stage view). Followers scroll from their own Link-locked position.
- **Footswitch banks/modes**: per Chocolate Plus unit, any number of banks (up to 8) with press + long actions; `bankNext`/`bankPrev` actions (handled in the realtime router). Defaults: guitar bank 2 "Click / transport" (long B = next bank), section bank 2 "Songs" (long C = next bank). Assign / enable / add / rename / remove from the SETLIST screen's switch buttons. Saved in the library.
- **Capo helper**: Live key menu > Capo lists capo 1-7 ranked by open shapes, with the actual shapes in play order ("Capo 3 - G shapes: G C D Em"); the chart shows shapes when chosen.
- **Song stats**: a song is logged (once per day) when PLAY runs on it; AUDIO/MIDI > Song stats (plays, last date, RECENT), repeat window 7-42 days (default 21).
- **Backup / restore everything**: one `.wrbackup.json` file (songs, setlists, rigs, controllers + banks, settings, play log) via share sheet / Files; import as merge (adds missing) or replace. Current library is snapshotted to Backups first.
- **Dark stage mode**: black background, chart drawn 1.35x.
- **Rehearsal recorder** (`core/audio/Recorder.*`, lock-free ring -> WAV 24-bit): Guitar app records the dry input, optionally the click output channel as ch 2; takes in `<data>/Recordings`, play back through outs 1/2, share, delete.
- **Tests**: `core_v08_section_colors` (76 checks), `core_v08_setlist_scroll_banks_capo_stats_backup_recorder` (117 checks); all 11 ctest programs pass; JUCE syntax check of all 20 UI files clean.
- **Unverified in v0.8**: nothing new has been seen on an iPad or Mac (no iOS build here). Unverified: colour picker call-out on iOS, auto-scroll feel, stage mode layout, recorder with a real interface (input level, click-channel choice), Files-app import picker.

## v0.7 (all in the shared core, so every app gets it; no talkback)
- **Arrangement editor:** per song plus a per-setlist-entry override (chips: drag to reorder, tap to duplicate or remove, palette to add; WRITTEN ORDER resets it). Next/Prev, voice cues, bar auto-advance, projector slides and band sync all follow it, because Live builds an "arranged" chart. Repeats cue "Chorus again". ChordPro `{arrangement: V1 C V2 C B C C}` round-trips (`order:` / `x_arrangement:` are read too).
- **Next song card:** shows during the last section and after the song ends: title, the player's own key/capo, tempo, time signature and first chord. Followers get it too.
- **Practice tracks:** attach an mp3/m4a/wav per song (copied to `<data>/practice/`). Practice mode plays it with the chart following editable section marks (auto-estimate from bars/tempo, or "mark section here"). Speed 50-150% without pitch change (WSOLA), loop the current section. Off by default, refused while following a leader, mixed into outs 1/2. The file isn't sent to followers.
- **Service notes:** per setlist item, at start / end / after the song / a named section. Shown as a banner on Live, synced to followers, and optionally on the projector (off by default).
- **Battery / heat:** each device's battery %, charging state and iOS thermal state go in roster heartbeats. The leader's band panel has a POWER column with warnings (battery under 20% and not charging; thermal serious/critical) plus a one-time toast. Each iPad also shows its own warning.
- **Share / paste import:** *Paste chart* (clipboard) and *Import file* now open a preview/confirm screen (editable title, artist, key - marked "guessed" when it came from the chords - capo, the sections found, and what was cleaned up). Charts also come in through iOS "Open in / Copy to" (Documents/Inbox) and the optional Share extension (`WR_SHARE_EXTENSION`).
  - The importer (`core/model/Import`) handles Ultimate-Guitar-style text: `[ch]G[/ch]` and `[tab]` markup, `[Verse 1]`/`[Chorus]` tags, HTML entities, CRLF, "Title Chords by Artist" / "Artist - Title Chords", Key / Capo / Tempo lines. It drops page text around the chart. When the stated key is the capo shapes key, it is corrected to the sounding key.
  - Only text the user shares or pastes is used. Nothing is fetched from ultimate-guitar.com or any URL, and a bare link is refused.
- **Tests:** `core_v07_arrangement_notes_health_practice` (130 checks) and `core_v07_share_import_parser` (39 checks); all 9 ctest programs pass. The JUCE check of every UI file is clean.
- **Unverified in v0.7:** none of the new screens has been seen running (no Mac or iPad here, compile-check only). Also unverified:
  - mp3/m4a decoding on iOS (JUCE CoreAudio formats) and how WSOLA sounds on real music;
  - real battery/thermal readings;
  - the Share extension target (CMake `XCODE_PRODUCT_TYPE` app-extension and embed; a separate `continue-on-error` CI step);
  - the pasteboard hand-off and "Open in" Inbox pickup on a device, including whether JUCE's plist merge keeps `LSSupportsOpeningDocumentsInPlace=false`;
  - the drag gestures in the arrangement editor on touch;
  - Safari page-text cropping on real chord sites (only the selection is reliable).
- **Still to do:** photo/OCR import (Apple Vision).

## v0.6 restructure: one shared core, one app per instrument
Clayton plans four apps (Guitar, Bass, Keys, Chart-only for acoustic players and singers) on one core. v0.6 sets up that structure with the first two apps:

| Layer | Path / CMake target | Contents |
|---|---|---|
| Core library | `core/` → `worship_core` (static, no JUCE) | model (charts/ChordPro, transpose, key/capo, setlists, players + export/PDF, backups, live session, projector layout), MIDI router + controller profiles + MIDI clock, `core/audio` (ClickGen presets, **ClickEngine**: click + count-in + voice cues + bar grid + Link lock, Command queue, VoiceBank, PitchTracker, DspUtil), `core/sync` (protocol, net, Bonjour, roster, LinkClock) |
| Core UI (JUCE) | `core/ui/`, compiled into each app | **CoreProcessor** (store, live session, MIDI, cues/TTS, click hooks, band sync, projector, state), **CoreEditor** (AUDIO/MIDI menu: click/cues/count-in/MIDI/band sync/projector; band panel; toasts; recovery note), **ChartView**, **TopBar** (title, KEY/capo, tempo, BAND n, section chips), SetlistView, BandPanel, ProjectorView, AppStore, Theme, Platform |
| Guitar DSP | `guitar/` → `worship_guitar_dsp` | Pedal base + 23 pedal types, RigEngine (2 lanes, 4/2-cable routing, trims, limiter, tails; owns a `ClickEngine click` and mixes it per sub-block), RigBuilder, wk third-party DSP |
| Guitar app | `apps/guitar/` → **TabernacleGuitar** ("Tabernacle Tools - Electric Guitar", `<prefix>.guitar`, AUv3 + Standalone) | `TabernacleGuitarProcessor : CoreProcessor` (rig, pedals, scenes, tuner, routing/tails settings), editor with LIVE (chart + rig panel + tuner) / SETLIST / WORKSHOP, TunerOverlay. Data folder `TabernacleTools` (was `WorshipRig` before v0.19) |
| Core Test harness | `apps/coretest/` → **TabernacleCoreTest** ("Tabernacle Tools Core Test", `<prefix>.coretest`, Standalone) | `HarnessProcessor : CoreProcessor` with only a ClickEngine on outs 1/2 (no input, no rig). LIVE (top bar + chart + PREV / NEXT / CLICK / MUTE / PLAY strip) and SETLIST. Full AUDIO/MIDI menu incl. band sync + projector. Data folder `TabernacleCoreTest` |

- Both apps advertise and browse the same `_tabernacle._tcp` service and share Link, so either can lead or follow the other.
- The core suite links only `worship_core`. That proves the core builds and runs without any guitar code; no file under `core/` includes a guitar header.
- Kept in core on purpose: MIDI router + Chocolate Plus default profiles (data), PitchTracker + `tunerReading` (instrument-agnostic; a Bass app can reuse them). The tuner UI stays in the Guitar app.
- A new app (e.g. Bass) needs: a processor deriving from `CoreProcessor` (return its ClickEngine, render in `renderAudio`, optional settings hooks), an editor deriving from `CoreEditor` (reuse ChartView / TopBar / SetlistView), and one `juce_add_plugin` + `wr_setup_app(...)` block in CMakeLists.txt.

**Unverified in v0.6:** the Xcode project with the two app targets has not been generated or built (no Mac here; JUCE's CMake can't configure on this Linux box without GUI libs). The `ios.yml` workflow builds both schemes unsigned, so the first push will check it. The new harness screen and the refactored Guitar screens compile (JUCE check) but nobody has seen them running. Two-app band sync over real Wi-Fi is untested; the protocol is unchanged and tested over loopback.

## JUCE app layer (compile-checked on Linux, NOT yet built for iOS)
Every JUCE source (`core/ui/*.cpp`, `apps/guitar/*.cpp`, `apps/coretest/*.cpp`) compiles against JUCE 8.0.4 when cross-compiled for Windows with zig/clang (`tools/check_juce_sources.sh`). The Objective-C++ `core/ui/Platform_ios.mm` (PDFKit text, CoreMIDI network session, AVAudioSession low latency, app-group dir) is **unverified** until the Mac or CI build. **No screen has been visually verified yet**: they're drawn to match the mockups but haven't been seen running.
- `apps/guitar/PluginProcessor.*` (on `core/ui/CoreProcessor.*`): 2 in / 4 out discrete buses, MIDI → router on the audio thread, actions → LiveSession on a 100 Hz message-thread timer, RigSink → rig commands.
- `apps/guitar/PluginEditor.*` (on `core/ui/CoreEditor.*`): LIVE / SETLIST / WORKSHOP tabs, plus the AUDIO / MIDI menu (interface/buffer dialog, routing, MIDI-clock follow, auto-return time, external cue channel, tuner).
- `LiveView.*`: split chart | rig. The top bar has the key (tap to transpose for this set), BPM (tap = tap tempo), section chips (current / next) and tuner. The chart has a NEXT banner with the switch to press; full view scrolls to follow, section view shows current + next. The rig panel has pedal tiles (tap = toggle), both Chocolate units (lit = on, pink dashed = the next switch to press, tap = press), and meters.
- `SetlistView.*`: library (search, + to add, import ChordPro/text/PDF), set by date (drag ≡ to reorder; tap = key, role, or remove), Save Set, saved sets, Go Live, footswitch mappings with MIDI learn.
- Tempo UI (v0.2): Live top bar `[-] BPM [+] TAP` + `MIDI CLOCK` (green, while Cubase clock drives the tempo) or orange `SAVE old→new` when a live change isn't in the song yet; tap the BPM value for Type tempo / Save / Revert. Workshop: `SONG TEMPO (BPM)` field with -/+ (1 BPM steps, hold to repeat) for the loaded song, saved immediately.
- `WorkshopView.*`: pedal library by category (drag), two lanes with drag/drop between them, knob/choice editor, on/bypass, remove/reset, output trims, Save as Scene, Assign to Song (scene → section / every verse… / default; guitar C/D → selected pedal; song rig), Save Rig.

## v0.4 notes
- Click (and cue voice) default output is now **Out 2 (mono)**: in 4-cable, outs 3/4 are the amp loop return. Existing saved settings keep whatever was chosen.
- Section changes are quantised only while the grid runs; with the click off and no PLAY/clock they're immediate, as before. If audio isn't running, a queued change is applied anyway after 2 bars + 0.5 s.
- Cubase's MIDI Start doesn't trigger the count-in (Cubase counts in itself). Bar-length auto-advance doesn't announce sections yet.
- Two v0.3 tests were set to the old behaviour (count-in off, quantise off) because the defaults changed; the new behaviour is covered by `testCues`.

## v0.3 notes
- Click output: in 4-cable, outs 3/4 are the amp loop return (the app flags a click/cue there). Out 2 (v0.4 default) feeds an in-ear pack or mixer; outs 3/4 only when running 2-cable. The M4 headphone jack mirrors outs 3/4.
- The click restarts on a downbeat with PLAY or MIDI Start from Cubase. While following MIDI clock it follows tempo but is not phase-locked to the clock ticks.
- On iOS the app is often killed in the background without a clean exit, so the "didn't close cleanly" note can appear after normal use. Data is current either way because every change is autosaved.
- Guitar D's short press now fires on release (it has a hold action). Tap stays instant.

## Known limitations / next
1. Build on the Mac (see BUILD_ON_MAC.md) and fix any iOS-only compile errors in `Platform_ios.mm`.
2. Real-device check: latency at 64/128, M4 channel mapping (outs 3/4 = indices 2/3), and the OUT 1 level into the Simplifier.
3. (still to do after v0.7) Photo/scanned-PDF import needs OCR (Apple Vision `VNRecognizeTextRequest`). Only text-layer PDFs import in v0.1, and PDFKit's text may misalign chord columns.
4. Polyphonic pitch tracking for Key9/Synth9, and a lower-CPU pitch tracker (FFT-based YIN).
5. Looper.
6. (done in v0.3) Rig changes now let the old rig ring out. CPU roughly doubles for the fade time; check on the A10X.
7. 2x oversampling on the drives (they use pre/post filtering for now).
8. MIDI out to the Chocolate Plus displays is not implemented; switch names show on screen only.
9. Learned controller profiles are copied from the audio thread's router without a lock. This is fine in practice, but should be made a proper handoff.
10. "He who was / He who is" is set to 73 BPM.
11. Tempo drives synced delay/tremolo, the click and bar auto-advance. ChordPro export is in Setlist > Share. The wider tempo controls take room from the section chips in the top bar, so on narrow screens fewer chips show. The Workshop tempo row sits at the bottom of the pedal library and could overlap a very long filtered list.


## v0.4 section-transition rule (Clayton)
The 8-click count-in is only at the start of a song (PLAY). Every section change (footswitch, screen, MIDI) is exactly ONE bar of the song meter: the bar after the press is the cue bar - name spoken on beat 1, remaining beats counted ("Verse two, 2, 3, 4" / "Chorus, 2, 3") - and the change lands on the following bar line. Nothing is spoken in the bar where you pressed; no defer logic. A long name is faded out (8 ms) so "2" still lands on beat 2.


## v0.5 notes - band sync + projector
* AUDIO / MIDI > Band sync: "Lead this session" (advertises over Bonjour), "Join <leader>" (one tap), "Rejoin <last>", "Join by IP", "Leave". Status line shows followers and Link peers. Follower Live screen shows a red banner when the leader is lost (section held, auto-reconnect).
* Followers mirror song, setlist, section changes incl. the queued one-bar cue (their own click speaks it in the same bar), tempo, PLAY/stop and count-in. Each device renders its own click/voice. Local: key/capo, click mute/volume/sound, chart vs tabs view. Footswitch NEXT/PREV/SONG on a follower are ignored.
* PLAY on the leader starts on the next Link bar line (up to one bar later) so everyone's count-in starts together.
* Without Link (option off or SDK not built in) followers still get tempo/sections/cues, but click phase is only as good as message latency.
* Leader Live screen: "BAND n" with a dot per follower (top bar, after tempo). Tap for the roster panel: status dot, device, role, ping, what they show (section, "> next" during a cue), reason when yellow/red, REMOVE. Toasts on join/drop. A projector-only iPad reports role "projector".
* Projector: AUDIO / MIDI > Projector lyrics: external display (HDMI/USB-C, re-attaches on plug-in) or full screen on this iPad (projector-only follower; double-tap closes). Tap right/left = next/previous slide, long-press = blank. Sample: `/workspace/worship-rig/projector-sample.png`.
* Licensing: Ableton Link is dual-licensed GPLv2+ or proprietary. GPL is fine for building/sharing this source (keep the app GPL-compatible, ship source); a closed-source App Store release needs Ableton's Link licence (free on request) or WR_WITH_LINK=OFF. Bonjour/dns_sd is system API. stb_truetype / stb_image_write (tests only) are public domain / MIT.
* Unverified on device: Bonjour browse/resolve, iOS Local Network permission prompt, Link inside the iOS audio callback (host time/latency offset is approximated as 5 ms + one block), external display UIWindowScene attach, real Wi-Fi latency/jitter on a travel router.

## v0.9 (Nov 3, 2026): sound library, amp/cab, packs plumbing, keys stub
- 17 new circuit-modelled pedals (`guitar/dsp/PedalsV09.inc`): germanium/tri-stage/octave fuzz, treble booster, blues drive, amp-in-a-box, OTA comp, noise gate, flanger, vibrato, wah (pedal/auto/env), envelope filter, BBD delay, tape echo, dotted-eighth delay, cloud reverb, spring.
- Amp + Cab (`guitar/dsp/AmpV09.inc`): 6 amps, 5 synthetic cabs + user IR loader (`loadCabImpulse`), output modes Direct to PA / Amp FX Return / Headphones, output limiter. Standalone `cab` pedal.
- Trademark-free UI names enforced by a test. Renamed Klon-style, Harmonizer/Whammy, Key9 Piano, Synth9 and Univibe (type ids unchanged).
- `core/model/Packs`: pack manifest + non-consumable entitlements (no StoreKit yet).
- `keys/dsp/KeysEngine`: stub patches + key-matched pad drone over the wk engine.
- Docs: ../docs/LAUNCH_PLAN.md, ../docs/SOUND_LIBRARY.md.
- Unverified: no UI for the new pedals beyond the generic Workshop param list, no WAV IR picker, no iOS build, nothing heard on real hardware. The Amp has no oversampling yet.

## v0.10 (Nov 3, 2026): subscription model, Workshop amp section, pedal faces, public-domain hymnal
- Pricing (Clayton): Individual $4.99/mo or Church $19.99/mo (up to 15 seats), 14-day full trial, no free tier. After a lapse the app is read-only (library, charts, hymnal, backup export).
  - `core/model/Packs`: plans, StoreKit-mirroring states (trial, active, 16-day grace, expired, revoked), `ChurchTeam` invites/seats, offline seat tokens (30 days), `Feature` gating, cache.
  - `CoreProcessor` silences audio in read-only mode, and band sync menu items are disabled. Sideload builds unlock everything (`WR_STORE_BUILD` turns gating on). No StoreKit calls and no seat server yet.
- Workshop: tap the AMP bar for HEAD / CAB + MIC / OUTPUT, the "Using my own amp" bypass (default in 4-cable), and Load IR (copied into the IRs folder, saved as `PedalDef.file`). Mic + position were added to the amp and cab models.
- Pedal faces for the 17 new pedals plus the cab pedal (colour, tip, footswitch hint, main knobs first).
- Hymnal: 40 public-domain hymns (`resources/hymns`, `tools/gen_hymns.py` -> `core/model/HymnData.cpp`). Search, preview and copy into the library (Setlist screen -> Hymnal).
- Unverified: all new UI is checked for compile errors (JUCE syntax check) but has never run. No iPad or Mac build. Hymn chords are our simple arrangements and haven't been proofread by a musician.
