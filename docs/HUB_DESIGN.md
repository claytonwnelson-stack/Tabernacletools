# Tabernacle Tools - Hub: design (draft)
*Design only, no code. Drafted Nov 3, 2026 (America/Chicago) against app v0.11; updated the same day with Clayton's decisions (§19). Prices are rough planning ranges, not quotes.*

## 1. What it is (and isn't)
**Tabernacle Tools - Hub** is an optional desktop app for Mac and Windows, built in JUCE on the shared `worship_core` plus the guitar and keys DSP. In Hub mode, one computer with a multi-input audio interface does all the sound for the band. Each player's iPad becomes that player's **remote and in-ear receiver**.

- **Standalone mode is unchanged.** iPads over Wi-Fi still each make their own sound and use leader/follower band sync (v0.6+). Hub is an extra mode, not a replacement.
- **The hub does:** every player's rig DSP (pedals, amp/cab/IR, keys patches), vocal channel strips, timing (click, count-ins, spoken cues, section changes, MIDI clock out), setlist and live session, projector lyrics, the FOH mix, a separate livestream mix, every player's in-ear mix, and multitrack plus stereo recording.
- **Each iPad does:** shows that player's pedals and knobs for fine-tuning, takes that player's M-Vave/Chocolate footswitch MIDI and forwards it, plays that player's in-ear mix (streamed from the hub) into earbuds plugged into the iPad, and shows the chart.
- **Excluded by decision:** rehearsal mode and visitor/guest players. Every connected player is a church-team member with a seat.
- **Projector lyrics:** HDMI from the hub only (decision).
- **Not used:** Dante/AVB, per-player stage boxes, Bluetooth earbuds (100–200 ms latency; the app refuses them for in-ears and warns).

## 2. Signal flow
```
 guitars / bass / keys / acoustic / vocal mics
        │  (cables to the stage snake or straight to the interface)
        ▼
 [ church audio interface, 8–18 inputs ] ──USB/TB──▶ [ HUB computer ]
        ▲                                             │ per-player rig DSP, vocal strips,
        │ outs: FOH L/R, livestream L/R,              │ click/cues, mixes, recorder
        │ (optional) drummer wired click              │
        └─────────────────────────────────────────────┘
                                                      │ wired Ethernet (gigabit switch)
                         ┌────────────────┬───────────┴────────┬──────────────────┐
                      iPad (guitar)    iPad (bass)        iPad (vocal 1)   Sound-tech device
                      remote + IEM     remote + IEM       remote + IEM     FOH/levels/record
                        │ earbuds        │ earbuds          │ earbuds
                        └ M-Vave (BT MIDI or USB)
 Projector: HDMI from the hub only.
```

## 3. Network and physical connection
- **Topology:** a dedicated, isolated wired LAN. A gigabit unmanaged switch (8 or 16 ports) with the hub on one port and every iPad and the sound-tech device on others. A cheap router (or the hub itself) serves DHCP; link-local addressing (169.254/16, Bonjour) works with no router at all. **Do not** share the church's guest Wi-Fi or office LAN for audio. An optional uplink to the church network (for the hub's internet: licence checks, updates, livestream) goes on a separate NIC or a VLAN port.
- **iPad wiring (USB-C iPads):** a USB-C hub/adapter with **Ethernet + USB-C PD charge pass-through + 3.5 mm headphone out** (or a separate USB-C DAC; for the 3 ms profile, use a DAC from the certified list). One cable carries network, and the charger keeps the iPad topped up for a 2-hour service. A powered adapter is recommended because Ethernet plus a DAC drains the battery.
- **PoE option:** run a PoE switch (802.3af/at) to each position plus a **PoE-to-USB-C splitter/adapter** (Ethernet + power on one Cat6 run). That means one cable per player and no wall outlet on stage. Check that each adapter really delivers data and charging to iPads (some only charge). The headphone DAC then goes on the adapter's spare USB-C/USB-A port or a combo adapter.
- **Lightning iPads:** Apple Lightning-to-USB 3 Camera Adapter (power pass-through) plus a USB Ethernet adapter, with earbuds on a Lightning DAC. Supported, but USB-C is recommended.
- **Bandwidth:** each in-ear stream is 48 kHz stereo 24-bit ≈ 2.3 Mbit/s plus overhead. 15 players ≈ 40 Mbit/s, trivial on gigabit. Control traffic is negligible.

## 4. In-ear audio streaming (hub → iPad)
**Stated target: 3 ms** from the string to the player's ear (decision). **Acceptable fallback: 5–6 ms**, which the system drops to automatically when the 3 ms profile isn't stable on a given rig.

| Stage | 3 ms profile (target) | 5–6 ms fallback profile |
|---|---|---|
| Interface A/D + D/A converter delay | ~0.5 ms (certified interface) | ~0.5–1 ms |
| Driver input buffer | 32 frames = 0.67 ms | 64 frames = 1.33 ms |
| Hub DSP (one block; rigs in parallel) | 32 frames = 0.67 ms | 64 frames = 1.33 ms |
| Packetise, send, switch, receive (16–32-frame packets) | ~0.15 ms | ~0.3 ms |
| iPad jitter buffer | 1 packet minimum (0.33–0.67 ms) | 2–3 packets (~1.3–2 ms) |
| iPad output (iOS IO buffer) | 32 frames ≈ 0.67 ms, with a low-latency USB DAC | 64 frames ≈ 1.33 ms |
| **Total** | **≈ 3 ms** | **≈ 5–6 ms** |

**What 3 ms requires (all of these):**
- **32-sample buffers end to end:** interface driver at 32 frames, the hub engine processing 32-frame blocks, and the iOS session asking for a 32-frame IO buffer (iOS may grant 32–64 depending on the device and DAC, so certify iPad + DAC combinations too).
- **Low-latency interface certification:** a published list of interfaces measured for round-trip latency at 32 frames (target ≤ 1.5 ms A/D+D/A+driver), stable for 2 hours with no xruns, and with a working ASIO driver on Windows or a Core Audio driver on macOS. Interfaces that can't do 32 frames run in the fallback profile only. **The certified list is still open** (see §19).
- **Wired, isolated network:** a gigabit switch dedicated to the band, with no Wi-Fi in the in-ear path, no shared church LAN traffic, energy-efficient Ethernet (EEE) off where the switch allows, and Cat6 runs.
- **Minimum jitter buffer:** 1 packet (16–32 frames), only possible on a clean wire. The buffer still adapts upward if late packets appear (see below), which is how the system falls back.
- **Tuning:**
  - Hub: real-time audio threads (macOS Audio Workgroups, Windows MMCSS "Pro Audio"), CPU frequency scaling off or a performance power plan, Wi-Fi/Bluetooth radios off, background apps closed.
  - DSP: total DSP kept under 60% of the 32-frame deadline, which may mean shorter cab IRs or zero-latency partitioned convolution only.
  - iPad: Low Power Mode off, the app in the foreground, and a wired DAC.
- **Measurement:** the hub shows live per-player latency, combining round-trip pings with known buffer sizes, plus a loopback test (a mic at the earbud) during soundcheck.

**Automatic fallback:** if a player's stream shows late or lost packets above 0.5% over 10 s at the 1-packet buffer, that player's buffer grows (up to 6 packets). If the hub misses deadlines at 32 frames, the whole engine switches to 64 frames between songs, never mid-song. Players see "3 ms" or "6 ms" on their screen.

**Protocol: "WRStream", RTP-style over UDP unicast** (one stream per player; multicast isn't needed and is fragile on cheap switches).
- **Packet:** header {magic, version, stream id, 32-bit sequence, 64-bit sample timestamp in hub-clock frames, frame count = 32, format}, then interleaved stereo PCM. 24-bit by default (float32 optional); no codec, since compression adds latency for no benefit on a LAN.
- **Integrity:** each packet carries a truncated HMAC-SHA256 or AES-GCM tag (session key from pairing, §9). At 1,500 packets/s per stream this costs about 1% of a core.
- **Redundancy:** "send twice" (packet n also carries n−1), **on by default** (recommendation; Clayton didn't decide). It doubles bandwidth (still tiny) and hides single losses without adding buffer, which matters at 3 ms.
- **Loss concealment:** a missing packet is filled by the redundant copy, then by a short crossfaded repeat of the last packet, then by a fade to silence after 3 consecutive losses. There are never clicks; ramps are 1 ms minimum.
- **Clock:** the hub's audio interface is the **master clock**, and the iPad's audio clock drifts relative to it (±50 ppm is typical). The iPad runs an **adaptive resampler** steered by a PI controller that holds the jitter-buffer fill at its target (the approach used by zita-njbridge/JackTrip). Correction stays within ±200 ppm and is inaudible. A wall-clock timestamp exchange (NTP-style ping every second over the control channel) measures round-trip and offset for diagnostics and for aligning footswitch events.
- **Adaptive jitter buffer:** it starts at the **1-packet minimum** (3 ms profile) and grows (to a max of 6 packets) when late packets exceed 0.5% over 10 s, then shrinks slowly after 60 s clean. The current latency is shown on the player's screen and in the sound-tech view.
- **Fallback (stream unhealthy, i.e. loss above 2% or link down):**
  1. Over Wi-Fi, if the iPad is on the same network: the stream continues with a 10–20 ms buffer and a "WI-FI IEM - higher latency" badge.
  2. If there's no network at all, the iPad plays **its own local click + cues** from the setlist it already holds (v0.3 click engine) so the band keeps time, with a "HUB LOST" banner.
  3. See failover (§11) for the instrument sound itself.
- **Control channel:** TCP (TLS-PSK, §9) carrying length-prefixed JSON, reusing the v0.6 BandSync message set extended with rig, mix and transport messages. Discovery uses Bonjour `_wrhub._tcp`.

## 5. MIDI footswitches through the iPad
The M-Vave (BLE MIDI or USB) pairs with the player's iPad, exactly as today. The iPad's MidiRouter maps presses to actions (section, scene, boost, tap, tuner) and sends **action messages** (not raw MIDI) with an iPad timestamp to the hub over the control channel. The hub applies them on the next block (one-way delay < 1 ms wired). Tap tempo uses the timestamps, so network jitter doesn't skew the BPM. Only the leader's (or the MD-role) switch can change sections for everyone. Tone/scene presses affect only that player's rig.

## 6. Per-player data: tones vs. in-ear mix
- **Tones:** per song and per section (scenes), plus pedal and amp settings, as today. These change during the set.
- **In-ear mix:** **per player, set once, constant across all songs.** It holds levels/pans for "me", each other player, vocals, click, cues and the playback track, plus a master and a limiter. There's no per-song in-ear automation. Click and cue volume live in the same mix.
- **Player profile** = {tones library, rig chain, in-ear mix, input assignment hint, preferences}. It is stored on the player's iPad (the owner copy) and cached on the hub.

### Sync on connect, live tweaks, conflicts
- **Model:** every syncable item (a pedal parameter, a scene, a mix fader) carries `{value, rev, editedBy, editedAt}`, where `rev` is a per-profile counter. Each side keeps the last revision it saw from the other.
- **On connect:** the iPad sends its profile digest, and both sides compute what changed since the last sync.
  - Changes on only one side are applied silently.
  - A field changed on **both** sides is resolved by rule: **tones → the player's iPad wins**; **in-ear mix → the player wins** (the player is the one who hears it). A sound-tech change made while the iPad was away is kept only if it is a *safety limit* (below).
  - Losing values are kept in a "changes not applied" list the player can restore with one tap.
- **During the service:** edits stream both ways with < 50 ms of UI echo. The hub is the arbiter of order: last-writer-wins per parameter by hub receive order, and the owner's device always shows the hub's resulting value.
- **Edit rights:**

| Role | Own tones | Own IEM | Others' tones | Others' IEM | FOH / livestream / recording | Section changes |
|---|---|---|---|---|---|---|
| Player (own iPad) | ✔ | ✔ | ✖ | ✖ | ✖ | ✖ (unless MD) |
| Worship leader / MD | ✔ | ✔ | view | ✖ | ✖ | ✔ |
| Sound tech | ✖ (view only) | **cap only**: max level + limiter | ✖ | cap only | ✔ | ✖ |
| Hub console (local) | ✔ (all, for setup) | ✔ | ✔ | ✔ | ✔ | ✔ |

- Changes **during a song** never alter the rig chain structure (no adding or removing pedals mid-song; parameters only), to avoid glitches. Structure changes queue until the song ends.

## 7. Sound-tech view
A separate device: a laptop or tablet running the Hub app in **Tech** role (or the hub's own screen). It provides:
- Input meters (pre/post), gain trims (where the interface exposes remote preamp control; otherwise on-screen instructions), mute/solo-in-headphones, and FOH faders, pans, EQ and compressor per channel/group.
- **FOH bus**, **livestream bus** (its own mix: more vocals and keys, room-mic blend, a gentle bus compressor and limiter, with optional "send click to stream" off by default), and an optional **drummer wired click** output.
- Recording arm, start/stop, markers, and disk space and time remaining.
- Per-player in-ear **status**: connected, latency, loss, battery, and the level cap. **The tech cannot listen to any player's in-ear mix** (decision) and can't alter its balance beyond the cap. Troubleshooting is done from the status numbers and by the player.
- **Not available here:** tone editing. Tones are read-only in this role by design.

## 8. Vocal channel strips
Each vocal input gets: HPF → gate/expander → EQ (4-band + de-esser) → compressor → saturation (light) → sends (reverb/delay shared FX buses, tempo-synced delay) → FOH/livestream/IEM.
- **Per-singer presets:** saved to the singer's profile and following the singer, not the channel. If Sarah sings on input 7 this week and input 9 next week, her preset moves with her.
- Starter presets: Lead male, Lead female, Harmony, Spoken word / pastor, plus Acoustic guitar DI and Choir pair.
- An optional **lead/harmony auto-balance** ducker for harmonies under the lead. Off by default.

## 9. Security and pairing
- **Isolated LAN** is the first line of defence. The hub only listens on the interface the user picks.
- **Pairing:** the hub shows a QR code / 6-digit code. The iPad (already signed in to its church seat, v0.11) scans it, and both derive a **per-device key** (SPAKE2 or a Noise XX handshake, with the code as the out-of-band check). Paired devices are remembered, and the hub admin can revoke one.
- The control channel runs over TLS-PSK (or Noise) with that key. Audio packets are authenticated per packet, and encrypting them is optional; on an isolated wire, authentication is what matters, so a stranger can't inject audio or control.
- **Roles** (player / MD / tech) are granted by the hub admin and tied to the seat member id, so the iPad's seat token identifies who it is.

## 10. Auto input assignment and soundcheck
- **Discovery:** the hub opens all interface inputs and listens. In "Assign inputs", each connected player taps **"Find me"** and plays or sings. The hub picks the channel with the strongest correlated onset and classifies it (Hi-Z guitar vs. DI vs. mic by spectrum and level). It proposes the mapping, the tech confirms, and phantom power stays off until a channel is confirmed as a condenser mic.
- **Remembered weekly:** the assignment is saved as a **Service template** (e.g. "Sunday 9:00", "Wednesday night") with the date. Next week the hub pre-loads it and only asks to re-find players whose input shows no signal or the wrong class.
- **One-click soundcheck** (about 3–5 min, guided on every iPad):
  1. Line check: each input in turn, "play/sing now", with auto trim toward −18 dBFS RMS, peaks under −6. If the interface has no remote gain, it shows "turn knob 3 up a little".
  2. In-ear check: a short tone/click on each iPad, measuring round-trip latency and packet loss, then green or red.
  3. Full band: 30 s playing together. Levels into FOH are balanced to the template targets, a 30 s reference is recorded, and the report is saved.
- No feedback-hunting or room EQ in v1 (open question).

## 11. Failover: what is realistically possible
The instruments are physically plugged into the **hub's interface**, so if the hub computer dies, **no iPad can hear that guitar** unless the signal also reaches the iPad. Options, best first:
1. **Prevent it:** run the hub on a dedicated computer with sleep and updates off, a UPS, and an audio-thread watchdog. If the audio engine crashes, it restarts in under 2 s and restores state (v0.3 autosave). This covers most real failures.
2. **Interface-level safety:** many interfaces (RME TotalMix, MOTU CueMix, Focusrite Control, Behringer/Midas with onboard mixing) keep their **internal mixer routing alive when the computer stops**. The setup wizard stores a "dry safety mix" in the interface (vocals and DI'd instruments to FOH) so the room still hears something, dry and without amps, if the hub disappears.
3. **Per-player takeover (needs extra hardware):** a player who wants full independence uses a **splitter/DI with two outputs**: one to the hub interface, one to a small USB interface on their iPad. If the hub is lost, the iPad switches to **local mode** using the same per-song tones (it already holds them, §6), plays its sound on its own output to FOH or an amp, and runs its own click. This is v0.6 standalone mode with the profile already synced. It costs one splitter and one small interface per player, so it's offered as an option for lead guitar and keys, not as the default.
4. **Timing:** every iPad already has the setlist and tempo map, so click/cues and section navigation continue locally (one iPad becomes the leader over Wi-Fi via v0.6 band sync).

What the design does **not** promise: seamless, inaudible failover for every instrument. Without the splitter, a hub loss means amp-sim'd instruments go silent (or go dry through option 2) until the hub restarts.

## 12. Recording
- **Per service:** a folder `Services/YYYY-MM-DD <template>/` containing:
  - multitrack WAV, one per input, **pre-DSP (raw)** so it can be re-amped later through the same rigs;
  - optional post-rig stems per player;
  - **stereo FOH** and **stereo livestream** mixes;
  - a markers file (song starts and section changes from the live session), so songs can be split automatically.
- **Disk use:** 48 kHz/24-bit = 0.52 GB per channel per hour. 16 inputs plus 2 stereo mixes ≈ 10 GB/hour. Requires an SSD; the hub warns when space for under 3 hours remains.
- Writing happens on a background thread with a 10 s RAM buffer and never blocks audio (the v0.8 Recorder design scaled up).

## 13. CPU budget
**Measured baseline (v0.5–0.10, Linux x86, Release, 48 kHz):** an 11-pedal guitar rig uses about 4.7% of one core. Estimates from that baseline:

| Per unit | Approx. % of one modern core (64-frame blocks) |
|---|---|
| Guitar rig (pedals + amp + cab IR, 2048-tap partitioned conv) | 7–10% |
| Bass rig | 5–7% |
| Keys patch (wk engine + pad) | 8–15% |
| Vocal strip (+ shared FX sends) | 1–2% |
| In-ear mix per player (sum + limiter + resample/packetise) | ~1% |
| FOH + livestream buses + FX returns | 5% |
| Recorder (I/O on another thread) | ~1% |

A medium band (2 guitars, bass, keys, 4 vocals, 8 in-ear mixes) comes to about **50–70% of one core**. Since there are 8–12 cores available, the problem isn't total CPU; it is **finishing within one 1.33 ms block**.
- The hub processes rigs **in parallel on a worker pool** joined into the audio thread's deadline: macOS Audio Workgroups (`os_workgroup`) and Windows MMCSS "Pro Audio" threads. A dependency graph runs inputs → rigs → buses → mixes → outputs.
- **N rigs:** with 4 workers, about 30 rigs fit in theory at 64 frames. The practical limit is the interface's input count, not the CPU. The app shows a per-rig DSP meter and refuses to arm more than 70% of the deadline. A 128-frame mode (+2.7 ms) is the escape hatch on weak machines.
- **Minimum hub:** Apple-silicon Mac mini (M2 or newer, 16 GB) or a Windows PC with a 6-core 2020+ CPU, 16 GB RAM and an SSD. **Windows requires an ASIO driver** (most interfaces ship one); WASAPI shared mode is rejected because its latency is too high.

## 14. Interface I/O requirements
- **Inputs:** one per instrument and mic. Electric guitars need **Hi-Z** inputs or a DI (most interfaces have only 2 Hi-Z inputs, so DI boxes go on the rest). Keys are stereo line (2 channels), and condenser mics need phantom power.
- **Outputs:** FOH L/R (2), livestream L/R (2), and the "safety mix" routing. All in-ears, **including the drummer's** (decision: the drummer uses an iPad by default), go over the network, so **4–6 outputs is enough**. A wired click output to the drummer stays available as a manual option.
- **Latency:** certified for 32-frame operation for the 3 ms profile (§4).
- **Clock:** the interface's internal clock (or word clock if ADAT-expanded); the hub follows it.
- **Expansion:** ADAT optical (+8 inputs via an 8-channel preamp) is the cheap way past 8 mic preamps.
- **Driver must be:** class-compliant or ASIO (Windows), stable at 64 frames, ideally with remote preamp gain and an onboard mixer that keeps routing without the computer (§11).

## 15. Starter gear bundles (planning ranges; verify before recommending)
| | **Small** (3–5 players) | **Medium** (6–9) | **Large** (10–15) |
|---|---|---|---|
| Interface | 8-in desktop interface with 4 preamps and 2 Hi-Z (e.g. Focusrite Scarlett 18i8 class) | 18-in, 8 preamps + ADAT (Scarlett 18i20 / Behringer UMC1820 class) | 18i20 class + 8-ch ADAT preamp, or an RME/MOTU-class interface with an onboard mixer |
| Hub computer | Mac mini M-series 16 GB or an equivalent PC | same | Mac mini Pro-tier or an 8+ core PC, plus a UPS |
| Network | 8-port gigabit switch | 16-port gigabit (PoE optional) | 16–24-port PoE+ switch |
| Per player | USB-C Ethernet + PD + headphone adapter, 3–10 m Cat6, wired earbuds | same, or a PoE-to-USB-C adapter | PoE-to-USB-C adapter per position |
| DIs | 2 passive DI | 4 DI | 6–8 DI, 2 active |
| Rough total (excl. iPads, mics, speakers) | ~$1.2–1.8k | ~$2–3k | ~$3.5–6k |

## 16. Licensing (Church plan)
- The Hub is **included in the Church plan** ($19.99/mo, 15 seats); there's no separate hub fee in v1.
- The hub signs in as the **team leader**, or as a member with the admin role. It is a **team device and does not use a seat**. Each connected iPad is a member using its own seat token (v0.11). Connecting requires that the iPad's seat token belongs to the same team.
- The hub verifies the team's Ed25519 entitlement token like the iPads do, with the same 30-day offline cap, so a church without internet on Sunday is fine.
- **Church plan only** (decision). There's no Individual-plan or single-player solo hub.
- If the subscription lapses, the hub goes read-only like the iPads (setlists, charts, export), and rig/amp DSP, the in-ear streams and recording stop. **The dry safety mix keeps passing audio** (decision: never silent): inputs go to FOH with their saved trims and basic vocal HPF/EQ, and a banner shows on the hub and the tech view.

## 17. Reuse of the shared core
`worship_core`: LiveSession, setlists, charts, click/cues/count-in, MidiRouter, BandSync messages, Projector, Recorder, Backup, Entitlements/SeatClient. `worship_guitar_dsp`: the Rig is instantiated **per player**, and AmpSection and IRs carry over. `worship_keys_dsp`: per keys player.
New modules:
- `hub/engine`: multi-rig graph, worker pool, buses, vocal strips;
- `hub/stream`: WRStream sender, plus the iPad receiver with ASRC in `core/sync`;
- `hub/session`: profiles, sync/merge, roles;
- `hub/io`: input discovery, templates, soundcheck;
- `hub/ui`: hub console and the tech view.

## 18. Phased build plan
| Phase | Scope | Exit test |
|---|---|---|
| H0 (3 wks) | Spike: WRStream over wired Ethernet to one iPad with ASRC, 32-frame buffers end to end, 1-packet jitter buffer; measure latency/loss with a loopback (mic at the earbud) on 2–3 candidate interfaces and DACs; tune | **Aim: 3 ms** measured, 0 audible dropouts in 2 h; record which hardware hits it. Acceptable minimum to continue: ≤ 6 ms |
| H1 (4 wks) | Hub engine: N guitar/bass rigs + click from interface inputs, parallel worker pool, FOH out | 4 rigs @ 32 frames (fallback 64), no xruns over 2 h on a Mac mini and a mid-range PC |
| H2 (4 wks) | iPad remote mode: pairing, per-player in-ear mix, tone remote, footswitch forwarding, profile sync + conflict rules | Player-switch and reconnect tests; conflict suite |
| H3 (3 wks) | Vocal strips + per-singer presets, livestream bus, tech view & roles | The tech can mix without touching tones (permission tests) |
| H4 (3 wks) | Input discovery + weekly templates, one-click soundcheck, per-service recording with markers | A cold start to soundcheck-complete in < 10 min with a 6-piece band |
| H5 (2 wks) | Failover (incl. the lapsed-subscription safety mix): audio watchdog/restart, safety-mix wizard per interface, iPad local takeover with splitter, local click on hub loss | Pull the hub's power mid-song: click continues; splitter players continue |
| H6 (2 wks) | Licensing/team-device integration, gear guide, beta with 2–3 churches | Two real Sunday services |

## 19. Decisions and open questions
**Decided by Clayton (Nov 3, 2026):**
1. **Latency:** 3 ms is the stated target (§4); 5–6 ms is the acceptable fallback; the phase H0 test must aim for 3 ms.
2. **Lapsed subscription:** the dry safety mix keeps passing audio. Never silent (§16).
3. **Drummer:** in-ears through an iPad by default, like everyone else (§14).
4. **Hub is Church plan only:** no Individual solo hub (§16).
5. **Projector lyrics:** HDMI from the hub only (§1).
6. **Sound tech can't listen to players' in-ear mixes** (§7).

**Recommendations, not decided by Clayton:**
7. Dual-send in-ear packets **on by default** (§4).
8. The per-player failover splitter stays **optional**, not in the bundles (§11).

**Still open:**
9. The certified interface (and iPad DAC) list for the 3 ms profile. It needs measurements in H0, and at least one certified interface must have an onboard mixer that keeps working without the computer (§11).
