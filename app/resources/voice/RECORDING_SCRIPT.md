# Voice cue recording script (49 files)

These replace the placeholder TTS voice (its Lessac dataset licence doesn't clearly allow commercial distribution). Record each word once, in the same voice, at the same distance and level.

**Format:** WAV, mono, 44.1 or 48 kHz, 16- or 24-bit. The app accepts any rate and resamples.
**Delivery:** steady and confident, like calling a cue to a band over in-ears. Not sung. Speak each word on its own, with no lead-in breath. Leave about 0.3 s of silence before and after; trimming and normalising happen in `tools/gen_voice.py`, or trim by hand to a 5 ms start and a 30 ms tail and peak at -1 dBFS.
**Numbers** are used both for the spoken count-in ("one, two, three, four") and after section words ("Verse *two*"). Keep them short (under 0.45 s at 72 BPM) so they fit between clicks.
**Combinations** are built by the app ("Verse" + "two", "Pre-chorus", "Last" + "chorus", "Half" + "time", "All" + "in"). Don't record phrases.

**Swapping in:** name the files exactly as listed below.
- To test without rebuilding: copy the files into the iPad's Files app, under On My iPad > Tabernacle Tools > Voice. Matching names override the bundled ones the next time the app launches.
- To bundle them: replace the files in `app/resources/voice/` and rebuild.

The test `v016_voice_manifest` checks that every word in `core/audio/VoiceManifest.h` has a file.

| File | Say | Notes |
|---|---|---|
| `acapella.wav` | A cappella | section word |
| `again.wav` | Again | section word |
| `all.wav` | All | section word |
| `break.wav` | Break | section word |
| `breakdown.wav` | Breakdown | section word |
| `bridge.wav` | Bridge | section word |
| `build.wav` | Build | section word |
| `channel.wav` | Channel | section word |
| `chorus.wav` | Chorus | section word |
| `coda.wav` | Coda | section word |
| `down.wav` | Down | section word |
| `eight.wav` | Eight | count-in number: crisp, on the beat, no trailing breath |
| `eleven.wav` | Eleven | count-in number: crisp, on the beat, no trailing breath |
| `ending.wav` | Ending | section word |
| `final.wav` | Final | section word |
| `five.wav` | Five | count-in number: crisp, on the beat, no trailing breath |
| `four.wav` | Four | count-in number: crisp, on the beat, no trailing breath |
| `half.wav` | Half | section word |
| `hold.wav` | Hold | section word |
| `hook.wav` | Hook | section word |
| `in.wav` | In | section word |
| `instrumental.wav` | Instrumental | section word |
| `interlude.wav` | Interlude | section word |
| `intro.wav` | Intro | section word |
| `last.wav` | Last | section word |
| `next.wav` | Next | section word |
| `nine.wav` | Nine | count-in number: crisp, on the beat, no trailing breath |
| `one.wav` | One | count-in number: crisp, on the beat, no trailing breath |
| `out.wav` | Out | section word |
| `outro.wav` | Outro | section word |
| `post-chorus.wav` | Post-chorus | section word |
| `pre-chorus.wav` | Pre-chorus | section word |
| `refrain.wav` | Refrain | section word |
| `seven.wav` | Seven | count-in number: crisp, on the beat, no trailing breath |
| `six.wav` | Six | count-in number: crisp, on the beat, no trailing breath |
| `solo.wav` | Solo | section word |
| `song.wav` | Song | section word |
| `spontaneous.wav` | Spontaneous | section word |
| `stop.wav` | Stop | section word |
| `tag.wav` | Tag | section word |
| `ten.wav` | Ten | count-in number: crisp, on the beat, no trailing breath |
| `three.wav` | Three | count-in number: crisp, on the beat, no trailing breath |
| `time.wav` | Time | section word |
| `turnaround.wav` | Turnaround | section word |
| `twelve.wav` | Twelve | count-in number: crisp, on the beat, no trailing breath |
| `two.wav` | Two | count-in number: crisp, on the beat, no trailing breath |
| `vamp.wav` | Vamp | section word |
| `verse.wav` | Verse | section word |
| `worship.wav` | Worship | section word |
