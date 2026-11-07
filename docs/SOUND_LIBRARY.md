# Tabernacle Tools — Sound Library (Guitar + Keys)

*v0.9 · Status key: ✅ built (in repo, tested) · 🟡 built in v0.9 (new, offline-tested only) · ⬜ planned*

## 0. Naming and legal rules (read before adding anything)
- **Modelling a circuit is fine.** A schematic (the topology plus part values) is functional information, and publicly published schematics and tracings may be studied and modelled.
- **Names are not fine.** Product names, brand names, and artist names are trademarks. Don't use them in the UI, presets, pack descriptions, or App Store copy. This includes things like Tube Screamer, Klon/Centaur, Big Muff, Fuzz Face, RAT, Uni-Vibe, Echoplex, Space Echo, Memory Man, Leslie, Rhodes, Wurlitzer, Hammond, Marshall, Fender, Vox, Mesa, Dumble, POG, Whammy, Timeline, Big Sky, and so on.
- Use **descriptive names**: "Green Screamer-style" becomes **"Green Mid Drive"**, "Klon" becomes **"Transparent Boost-Drive"**, and a Fender Twin becomes **"Clean American"**.
- A developer comment in the code may name the circuit family so people know what was studied. Keep these short and factual.
- Don't copy anyone's graphics, knob layouts, or colour schemes closely enough to suggest a connection (trade dress).
- Don't sample or capture other companies' IRs or presets. User IRs are loaded by the user.
- `tests/guitar/test_v09.cpp::testNames` fails the build if a catalogue name contains a listed trademark. In v0.9 this caught and renamed 5 old names: Klon-style, Harmonizer/Whammy, Key9 Piano, Synth9, and Univibe. Internal type IDs stayed the same so saved rigs still load.

## 1. Guitar — effects
Every model is pure C++ DSP in `guitar/dsp/` (`Pedals.cpp`, `PedalsV09.inc`, `AmpV09.inc`).

### 1.1 Dynamics
| Model (UI name) | Circuit studied | Status |
|---|---|---|
| Comp | Generic VCA feed-forward comp with blend | ✅ `comp` |
| OTA Squeeze Comp | CA3080 OTA compressor (Ross/Dyna family) | 🟡 `otacomp` |
| Optical Studio Comp | LA-2A-style opto cell, program-dependent release | ⬜ |
| Noise Gate | Hysteresis gate | 🟡 `gate` |
| Volume Swell (auto) | Envelope-triggered fade-in | ✅ `swell` |
| Expression Volume | Pedal-controlled volume, minimum-volume knob | ⬜ |

### 1.2 Boost, overdrive, distortion, fuzz
| Model | Circuit studied | Status |
|---|---|---|
| Boost | Clean op-amp/FET booster with EQ | ✅ `boost` |
| Treble Booster | 1-transistor germanium treble booster (1965 Brit) | 🟡 `treblebst` |
| Green Mid Drive (current UI name "Overdrive") | Op-amp mid-hump, clipping diodes in the feedback loop (TS808/TS9 family) | ✅ `overdrive` |
| Transparent Boost-Drive | Charge-pumped dual-path clean + clipped (Centaur family) | ✅ `klon` |
| Blues Drive | Low-gain op-amp/discrete, asymmetric (BD-2 family) | 🟡 `bluesdrive` |
| Transparent Amp-in-a-Box | Dual op-amp "breaker" drive (Bluesbreaker pedal family) | 🟡 `ampinbox` |
| Plexi-in-a-Box | JFET cascade drive (Marshall-in-a-box family) | ⬜ |
| Dumble-in-a-Box Smooth OD | JFET/op-amp "ODS" topology | ⬜ |
| Heavy Distortion | Op-amp + LM308 hard clip with filter control (RAT family) | ✅ `distortion` |
| Grunge Metal | Op-amp + transistor high gain, 3-band EQ (DS-1/HM-2 families) | ⬜ |
| Fuzz | 2-transistor positive-ground fuzz with bias (Fuzz Face family) | ✅ `fuzz` |
| Germanium Bender Fuzz | 3-transistor MkII germanium bender | 🟡 `gefuzz` |
| Tri-Stage Sustain Fuzz | 4-transistor fuzz/sustainer with mid-scoop tone (Big Muff family) | 🟡 `tristage` |
| Octave-Up Fuzz | Transformer/diode full-wave rectifier octave (Octavia family) | 🟡 `octfuzz` |
| Gated Synth Fuzz | Square-wave hard gate fuzz | ⬜ |

### 1.3 Modulation and filter
| Model | Circuit studied | Status |
|---|---|---|
| Chorus | BBD chorus (CE-2 family) | ✅ `chorus` |
| Dimension Chorus | Dual-BBD stereo "dimension" | ⬜ |
| Phaser | 4/6-stage OTA phaser (Phase 90 family) | ✅ `phaser` |
| Photocell Vibe | 4-stage LDR/lamp phase-vibe (Uni-Vibe family) | ✅ `univibe` (renamed) |
| Jet Flanger | BBD flanger with regen (Electric Mistress/BF-2 family) | 🟡 `flanger` |
| Pitch Vibrato | BBD vibrato, 100% wet | 🟡 `vibrato` |
| Tremolo (sine/square, tempo sync, stereo pan) | Opto/bias tremolo | ✅ `tremolo` |
| Harmonic Tremolo | Split-band tremolo (brownface amp) | ⬜ |
| Rotary | Horn plus drum rotating speaker | ✅ `rotary` |
| Classic Wah (pedal / auto / envelope) | Inductor wah, ~350 Hz–2.2 kHz (Cry Baby family) | 🟡 `wah` |
| Envelope Filter | SVF envelope filter (Mu-Tron/Q-Tron family) | 🟡 `envfilter` |
| Ring Mod | Diode ring modulator | ⬜ |

### 1.4 Pitch and synth
| Model | Status |
|---|---|
| Octave Down (analog flip-flop octave divider) | ✅ `octdown` |
| Poly Octave (-1/+1/+2) | ✅ `polyoct` |
| Harmonizer / Pitch Bend (fixed interval or expression sweep) | ✅ `harmonizer` (renamed) |
| Guitar-to-Piano (pitch-tracked keys) | ✅ `key9` (renamed) |
| Guitar Synth (tracked mono synth) | ✅ `synth9` (renamed) |
| Detune / Doubler (±cents micro-shift) | ⬜ |
| Key-aware Harmonizer (follows the song `{key:}`) | ⬜ (a strong worship differentiator) |

### 1.5 Delay
| Model | Status |
|---|---|
| Delay (digital, tempo divisions, ping-pong, mod) | ✅ `delay` |
| **Dotted-Eighth Delay** (locked to the song BPM, optional 1/4 tap, ducking) | 🟡 `dotted8` |
| Bucket-Brigade Delay (MN3005-family BBD, dark, self-oscillates) | 🟡 `analogdly` |
| Space Tape Echo (multi-head tape: wow/flutter, wear, 1–3 heads) | 🟡 `tapeecho` |
| Reverse Delay | ⬜ |
| Swell Delay (auto-swell into delay) | ⬜ |
| Looper | ⬜ `looper` stub (TODO since v0.2) |

### 1.6 Reverb and ambient (modern worship staples)
| Model | Status |
|---|---|
| Reverb (FDN hall/room) | ✅ `reverb` |
| Shimmer (octave-up feedback) | ✅ `shimmer` |
| **Cloud Ambient Reverb** (30 s modulated FDN, shimmer, built-in swell) | 🟡 `cloud` |
| Spring Tank (dispersive allpass spring) | 🟡 `spring` |
| Plate | ⬜ |
| Freeze (capture and sustain) | ✅ `freeze` |
| Swell (auto volume) | ✅ `swell` |
| Infinite Pad (freeze → reverb, key-change crossfade) | ⬜ |
| Granular Cloud | ⬜ |

### 1.7 Utility
EQ ✅ · Noise Gate 🟡 · Tuner ✅ (core) · Splitter / A-B lanes ✅ (Rig) · Stereo Widener ⬜ · Output limiter 🟡 (inside Amp).

## 2. Guitar — amps, cabs, direct-to-PA
`amp` pedal (category **Amp & Cab**), `guitar/dsp/AmpV09.inc`.

### 2.1 Amp models
| UI name | Inspired by (dev only) | Stages | Status |
|---|---|---|---|
| Clean American | Blackface 2x12 clean | 1 | 🟡 |
| Tweed Breakup | Cathode-biased tweed combo | 2 | 🟡 |
| British Chime | Top-boost class-A EL84 | 2 | 🟡 |
| British Crunch | 4-input plexi | 2 | 🟡 |
| Boutique Lead | ODS-style smooth lead | 3 | 🟡 |
| Modern High Gain | Rectified high-gain | 3 | 🟡 |
| Brownface Warm | 60s brownface | 2 | ⬜ |
| Small Tweed Studio | 5 W single-ended | 1 | ⬜ |
| Black Country Clean+ | Class-A/B cleanish combo | 2 | ⬜ |
| Jazz Solid-State | 2x12 solid-state clean (great for worship cleans) | 0–1 | ⬜ |
| Acoustic DI Sim | Piezo to "mic'd body" EQ for acoustics | — | ⬜ (Chart-only app add-on) |

Each model has: an input HP filter → 1–3 biased triode-style stages with inter-stage RC filters → a tone stack with model-specific corners → a push-pull power stage with **sag** → presence → cab.

Controls: Gain, Bass, Mid, Treble, Presence, Master, Level.

**Not yet:** oversampling (some aliasing is likely at high gain), a real FMV tone-stack solve, and per-model power-tube types. These are planned for v1.0.

### 2.2 Output modes (for people without an amp)
| Mode | Signal path | Use |
|---|---|---|
| **Direct to PA** *(default)* | Amp + power + cab + PA safety EQ (80 Hz–9 kHz) + output limiter | **No amp at all.** Run the iPad interface straight to the board or in-ears. |
| Amp FX Return | Preamp only, no power stage or cab | Into a real amp's FX return (4-cable setup) or a powered flat cab |
| Headphones | Amp + cab + a small room EQ | Practice |

The limiter keeps peaks under −0.4 dBFS, which protects the PA and ears (tested on every model and mode).

### 2.3 Cabs and IR loader
- **Synthetic cabs 🟡:** 1x12 Open Back, 2x12 Open Back, 2x12 Blue Alnico, 4x12 Closed Back, 4x10 Tweed.
- **User IR loader 🟡:** `loadCabImpulse()` resamples to the session rate, normalises energy, fades the tail, and keeps the first 1024 taps (about 21 ms at 48 kHz).
  - It uses direct FIR for now: about 5.7% of one desktop core per instance.
  - TODO: partitioned FFT convolution, longer IRs, and a WAV file picker in the UI.
  - There's also a standalone `cab` pedal for players running a real amp's line-out or a load box.
- **Shipped IRs:** we must capture our own, through a load box plus a mic on owned cabs, or license them, or use CC0 ones. Don't bundle third-party IR packs.

## 3. Keys — library design (Keys app, v0.9 = stub)
`keys/dsp/KeysEngine.{h,cpp}` wraps the existing `wk` synth engine (tonewheel/pipe organ, pads, grand/EP, delay, FDN reverb).
- Starter patches load and render (tested): Grand, Tine EP, Tonewheel Organ, Warm Pad, Key-Matched Pad.
- `setPadKey(songKey)` holds root + fifth + octave for the song's `{key:}`.
- Still to build: the Keys app UI and MIDI keyboard input.

### 3.1 Catalogue
| Family | Patches | Engine | Sample source if sampled |
|---|---|---|---|
| Acoustic piano | Concert Grand, Bright Pop Grand, Felt/Soft Piano, Upright, Hymn Piano | **Sample-based** (multi-velocity, release samples, sympathetic resonance), plus a light modal synth fallback (current `wk` piano) | Our own recordings of a church grand (a rental hall or the church's piano): 88 keys × 8 velocities × RR2, about 1–2 GB, compressed for packs. Free alternatives: **Salamander Grand (CC-BY 3.0, attribution)** and **University of Iowa MIS (free use)**. Check each licence before shipping. |
| Tine EP | Classic Tine, Suitcase w/ trem, Bell EP, Dyno-style bright | **Physical/FM hybrid synthesis** (tine + tonebar model, no samples needed) | — |
| Reed EP | Reed EP clean / driven | Synthesis (reed + pickup + preamp drive) | — |
| CP/Electric grand | Electric Grand | Sample (own recording) or synth | Own |
| Organs | Tonewheel (9 drawbars, perc, click, scanner vibrato, rotary), Gospel Organ, Pipe/Church Organ (principals, flutes, reeds, full), Combo Organ | **Additive synthesis** (`wk::Organ` ✅ core exists) + rotary | — |
| Synth pads | Warm Pad, Air Pad, Glass Pad, Analog Brass Pad, Choir Pad, String Pad, Evolving Texture | **Subtractive / wavetable synthesis** (`wk::Pads` ✅ core exists) | — |
| **Ambient worship pads, key-matched** ★ | Pad drone per key (12 keys × major/minor), swell on section change, auto crossfade on key change, follow-song mode, fifths/octaves/sus2 voicing, "shimmer" & "grit" layers | **Synthesis first** (exact pitch, any key, tiny download). Optional **sampled pad sets**: we render our own long pads (DAW + our synths → 12 keys × 60 s loops) and own them | Own renders only |
| Strings | Ensemble Strings, Cello swell, Pizz, String Machine | Ensemble = sample (own quartet session or a licensed library) · String Machine = synthesis | **VSCO 2 Community Edition (CC0)**, **Sonatina Symphonic Orchestra (CC Sampling+; check terms)**, or a commercial redistribution licence |
| Brass / Winds | Synth Brass (synth), Brass Section (sample), Flute, Solo Horn | Synth / sample | VSCO 2 CE (CC0) for winds; brass needs a licensed library or our own recordings |
| Choir | Ooh/Aah choir, Air choir pad | Sample + formant synth layer | Own recordings (church choir session, signed releases) |
| Leads | Saw Lead, Square Lead, Soft Sine Lead, Portamento Lead | Synthesis | — |
| Bass | Sub Bass, Moog-style Bass (named "Ladder Bass"), Synth Pluck Bass, Pedal-board bass (left hand / footpedals) | Synthesis | — |
| Arps & pulses | Tempo-locked arp (up/down/random, 1/8 ./1/16), Pulse pad, Rhythmic gate | Synthesis + core click tempo | — |
| Bells & plucks | Bell, Celesta, Music Box, Mallets | FM/modal synthesis (+ sample celesta from VSCO CE) | CC0 |
| FX | Risers, Sub drops, Reverse swells, Ambient textures | Synth + own renders | Own |

**Sampling rules:**
- Only use CC0, our own recordings, or libraries with a written redistribution licence.
- CC-BY is allowed, but needs credits on the About screen.
- No "NC" (non-commercial) licences, and nothing ripped from other products.
- Keep every licence in `resources/licenses/`.

## 4. Pack plan (manifest: `core/model/Packs.cpp`)
v0.10: packs are **content groupings only**. Any active Individual ($4.99/mo) or Church ($19.99/mo) plan unlocks every pack; there are no per-pack purchases (see LAUNCH_PLAN §4).
| Pack | Rig | Price | Contents |
|---|---|---|---|
| **Guitar Starter** | Guitar | Included | Comp, gate, boost, Green Mid Drive, Heavy Distortion, Fuzz, Octave Down, chorus, phaser, tremolo, delay, **dotted-eighth**, reverb, shimmer, freeze, swell, EQ, **Amp + Cab** (Clean American, British Chime, 2 cabs, user IR), Direct-to-PA |
| Ambient Worship | Guitar | Included | Cloud reverb, poly octave, harmonizer, Guitar-to-Piano, Guitar Synth, worship presets |
| Vintage Pedals | Guitar | Included | Germanium/tri-stage/octave fuzz, treble booster, blues drive, amp-in-a-box, transparent boost-drive, OTA comp, flanger, vibrato, vibe, rotary, wah, envelope filter, BBD, tape echo, spring |
| Amp Collection | Guitar | Included | Tweed, British Crunch, Boutique Lead, Modern High Gain + 3 cabs (+ planned amps) |
| Keys Starter | Keys | Included | Grand, Tine EP, Tonewheel, Warm Pad, Key-Matched Pad |
| Ambient Pads | Keys | Included | Shimmer, Air, Choir, Strings and Texture pads (all key-matched) |
| Vintage Keys | Keys | Included | Reed EP, Pipe Organ, String Machine, Poly Synth |
| *(planned)* Sampled Grand | Keys | Included | Our multi-sampled church grand |
| *(planned)* Bass Starter / Bass Amps | Bass | Included | — |
| *(dropped)* All Packs bundle | — | n/a (subscription) | — |

## 5. Preset patches: popular worship tones (generic descriptions)
| Preset | Chain |
|---|---|
| **Sunday Clean** | Comp → Clean American (gain 0.2) → 2x12 Open → plate-ish reverb 25% |
| **Dotted-Eighth Lead** | Comp → Transparent Boost-Drive → British Chime (edge of breakup) → Dotted-Eighth (fb 40%, 1/4 tap 50%) → Cloud 30% |
| **Swell Pad** | Volume Swell → Pitch Vibrato (subtle) → Dotted-Eighth → Cloud Ambient (decay 20 s, shimmer 40%) |
| **Shimmer Wash** | Swell → Shimmer 60% → Freeze on footswitch |
| **Cathedral Lead** | Blues Drive → British Crunch → Tape Echo (2 heads) → Cloud |
| **Big Chorus Drive** | Green Mid Drive + Transparent Boost-Drive stacked → British Crunch → Dotted-Eighth → Reverb |
| **Bridge Build** | Tri-Stage Fuzz (low sustain) → Modern High Gain (gain 0.4) → 4x12 → Bucket-Brigade Delay |
| **Ambient Bed** | Freeze + Poly Octave (+1) → Cloud 100% wet on Out 2 lane |
| **Acoustic Direct** | Comp → EQ (low-cut 100, mid −3 @ 400) → Spring 10% (Chart-only/acoustic) |
| **Keys: Pad Under Prayer** | Key-Matched Pad in the song key, fade in over 4 bars |
| **Keys: Gospel Organ** | Tonewheel 888000000 + perc + fast rotary on the mod wheel |

## 6. Counts (v0.9)
| | Built | Planned (total incl. built) |
|---|---|---|
| Guitar pedals | 22 pre-v0.9 implemented (+1 looper stub) + **17 new** = **39** | ~55 |
| Amp models | **6** | ~11 |
| Cab voicings | **5** + user IR | 8 + shipped own IRs |
| Output modes | **3** (Direct to PA, FX Return, Headphones) | 3 |
| Keys patches | **5** stub patches | ~60 across 12 families |
| Packs in manifest | **7** | ~11 |

## 7. v0.10 Workshop amp section
- Tap the **AMP bar** between the lanes. It opens HEAD (6 heads, Gain/Bass/Mid/Treble/Presence/Master/Level), CAB + MIC (5 cabs or User IR, plus Dynamic/Ribbon/Condenser mic and centre→edge position; mic/position shape the built-in cabs only, because IRs already contain their mic), and OUTPUT (Direct to PA / Amp FX Return / Headphones).
- **"Using my own amp"** bypasses the sim. That's the default for 4-cable rigs, so Clayton's Simplifier setup is unchanged.
- **Load IR file...** copies the WAV into the app's IRs folder and remembers it in the rig (`PedalDef.file`).
- The 17 v0.9 pedals plus the cab pedal get a **pedal face**: colour strip, one-line tip, footswitch hint, and their main knobs first with the rest under MORE (`guitar/dsp/PedalFaces.cpp`).
