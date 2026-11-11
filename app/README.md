# Bearing Live (v0.6)
An iPad guitar rig for worship: charts with chords and tabs, setlists, two-lane (before/after amp) pedal chains for the 4-cable method with a MOTU M4 and a Simplifier, scenes per song section, and two M-Vave Chocolate Plus footswitches. It's built as a JUCE iOS app plus an AUv3.

Since v0.6 there is one shared core and one app per instrument (Guitar now; Bass, Keys, and Chart-only later):
- `core/` (`worship_core` library, no JUCE): charts/ChordPro, transpose, key/capo, setlists, sharing/export, backups, MIDI + clock, live session, click/cues/count-in (`core/audio`), band sync + Link + roster (`core/sync`), projector layout
- `core/ui/`: shared JUCE layer compiled into every app: CoreProcessor, CoreEditor (settings menu, band panel, toasts), ChartView, TopBar, SetlistView, ProjectorView, AppStore, Platform
- `guitar/` (`worship_guitar_dsp` library): pedals, two-lane rig, 4-cable routing, tails, wk third-party DSP
- `apps/guitar/`: **Bearing Live Guitar** (Live with rig panel and tuner, Workshop pedal editor)
- `apps/coretest/`: **Bearing Live Core Test**, a harness with no rig (setlist, chart, click, cues, band sync, projector)
- `tests/core/`, `tests/guitar/`: the two test suites (`cmake -S . -B build && cmake --build build && ctest --test-dir build`)
- `resources/he-who-was.cho`: the seed chart
- `BUILD_ON_MAC.md`: how to build and sideload with a free Apple ID; `STATUS.md`: what works and what's next
