# MelodyMagic

MelodyMagic is a MIDI phrase arpeggiator for Windows, built as a VST3 with JUCE 8. Hold a chord and it plays a **phrase** over it. A phrase is a pattern whose notes pick chord tones (rows) in octave lanes, so the same phrase follows any chord you play.

Its arpeggiator reproduces the MIDI behaviour of a legacy phrase-arpeggiator plugin note for note, so the two can be A/B tested. On top of that it adds:

* **Scale Lock**, using the 16 ArpSampler scales plus Persian, Chromatic and Custom.
* **Three phrase generators.**
* **Drag MIDI**, which drops the arpeggiated pattern straight onto your DAW timeline.

It shares its look with **Virus Delay** and **ArpSampler**: the same palette, procedural knobs, panels and LCDs. Everything is drawn as vectors, with no bitmaps.

![screenshot](docs/screenshot.png)

---

## Installing

The plugin is built for **Windows x64** (VST3). Every push to `main` builds it on GitHub Actions, runs the unit tests and pluginval (strictness 10), and commits a zipped build to the **`builds-windows`** branch.

1. Download `MelodyMagic-VST3-Windows-x64.zip` from the `builds-windows` branch, or from the run's artifacts under **Actions**.
2. Unzip `MelodyMagic.vst3` into `C:\Program Files\Common Files\VST3\`.
3. Rescan plugins in your host.

### Where it goes in your DAW

MelodyMagic takes MIDI in and sends MIDI out; it makes no sound itself. It registers as an **instrument** with a silent output, because that is the slot every major host lets send MIDI on to other tracks.

* **Ableton Live:** put MelodyMagic on a MIDI track. On a second MIDI track holding your synth, set *MIDI From* to the MelodyMagic track, with *MelodyMagic* as the source.
* **FL Studio:** in MelodyMagic's wrapper, set an output port (*Settings > MIDI output port*), and set the same port as the synth's input port.
* **Reaper, Cubase, Bitwig, Studio One:** route the plugin's MIDI output to the synth track in the usual way.
* **Any host:** **Drag MIDI** renders the pattern as a clip, so you can use it without any MIDI routing.

### The preset library

The plugin ships with a set of original **Factory** phrases. To browse the original plugin's expansion content (487 phrases, 312 grooves, 1,464 key maps, 1,388 chord clips), put these four files in your library folder:

```
nora_fas_library.json   nora_qas_library.json   nora_has_library.json   nora_mid_library.json
```

The default library folder is `Documents\MelodyMagic\Library`; the plugin creates it the first time it runs. **Library > Folder...** picks a different folder and **Rescan** reads it again.

The folder can also hold raw `.fas` / `.qas` / `.has` / `.mid` files in any subfolders.

These libraries are **not** in this repository. The expansion's readme asks that its clips not be redistributed, and `.gitignore` keeps them out.

---

## Using MelodyMagic

### Phrase editor (top left)

Each row is a **chord tone**: R1 is the lowest note you hold, R2 the next, and so on. Each **lane** is an octave of the chord: `OCT 0` plays it as held, and `OCT +1` / `OCT -1` play it an octave up or down. A row the chord doesn't have, such as R4 under a 3-note chord, stays silent.

| Action | Result |
|---|---|
| Click empty space | add a note (drag to set its length) |
| Drag a note | move it in time and pitch |
| Drag a note's right edge | change its length |
| Alt while dragging | no snap |
| Right-click or double-click a note | delete it |
| Mouse wheel over a note, or **- / +** | **Note_Offset**: a diatonic shift |
| Drag in the lower strip | set note velocities |

**Note_Offset** moves the chord's root by that many semitones, snapped into the Scale Lock scale, and rebuilds the chord shape diatonically. For example, in C major with a C-E-G chord, R2 with +3 plays G.

The toolbar sets the **Lanes** (1, 3 or 5), the **Rows** per lane (1-10), the **Length** (Auto, or 1-8 bars) and the editing **Snap**. **Undo**, **Redo**, **Load** and **Save** are in the top bar; Save writes the original `.fas` phrase format.

### Arp

* **Mode**:
  * **Free:** when the chord changes, notes that are still sounding switch to the new chord at once.
  * **Static:** a chord change applies from the next phrase note. Note-offs always match their note-ons.
  * Whenever the host transport is stopped, the engine runs in Static mode, as the original does.
* **Speed** sets the phrase rate against the host tempo, from x1/4 to x4.
* **Int Sync** (Force Internal Sync) makes the phrase restart on the first key you press after releasing all keys, like a classic arp. It also runs while the host is stopped. Without it, the phrase follows the host's song position and only plays while the transport runs.
* **Fixed Vel** (Ignore Host Velocities) uses the phrase's own velocities only. Without it, velocity = phrase velocity × how hard you played / 127.
* **Rnd Vel** adds a random ± velocity amount (0-127).

### Scale Lock

**Key**, **Scale** and **Scale Lock** work like ArpSampler's. With Scale Lock on, every note MelodyMagic sends, both live and through Drag MIDI, moves to the nearest note of the key and scale (ties go down). Generated patterns are locked to your scale this way, whatever chord you hold.

The scale list has ArpSampler's 16 scales, in the same order:

> Major, Natural Minor, Harmonic Minor, Melodic Minor, Dorian, Phrygian, Lydian, Mixolydian, Locrian, Major Pentatonic, Minor Pentatonic, Blues, Phrygian Dominant, Hungarian Minor, Whole Tone, Japanese (In Sen)

After those come **Persian** and **Chromatic** (which complete the original plugin's set) and **Custom**. For a Custom scale, click the 12 note buttons to build it; custom scales ignore the key.

The same scale is the one **Note_Offset** moves in, as in the original.

### Generators (tabs under the phrase)

| Generator | What it does |
|---|---|
| **303 Random** | A monophonic line. Knobs: Density, Height (rows used), Express(ion), Oct Mut(ate), Slide, Pitch Chg, Shuffle, Gate, Accent, Acc Vel, Reg Vel and Rotate. Its **mutator** knobs (Show/Hide, Pitch Oct, Octave, Swap, Acc Flip, Slide Flip) apply when you press **Mutate**. |
| **Chord / Bass** | Chord hits plus a bass line. Knobs: Density, Movement, Length, Vel Var, Ch Alter, Balance (chord vs bass), Juice (bass octave pops), Len Var, Jitter, Bs Alter, Bs Atten, **Macro** (1/2/4/8-bar motif repetition) and Size. **Bass Row** reserves the lowest row for the bass. |
| **Mutate** | Changes the **snapshot**: the probability and amount of change for the note number, start, length, velocity and pitch. Pitch changes are whole scale steps. **Grouped** moves notes that share a start together; **Join** merges overlapping notes. |

How the controls work:

* **Turning a knob** re-runs the generator with the same random draw, so you can shape the result.
* **Regen** makes a new draw.
* **Randomize** gives new values to every unlocked knob. **Right-click a knob to lock it**; a padlock appears. Bars is locked by default.
* **Reset** returns to the defaults.
* **Snapshot** remembers the current phrase as the starting point for both mutators. If there is no snapshot when you mutate, one is taken first.

### Groove / Quantize

Load a groove from **Library > Grooves**, such as the MPC, SP1200 or swing grids. **Q Start** and **Q End** pull note starts and note ends toward the nearest groove line, from 0 to 100 %. **Link** uses Q Start for both.

### MIDI

* **In** and **Out** are channel filters. In: All, Off or 1-16. Out: All (the channel of the chord note), Off or 1-16.
* **Key Map**: load a map from **Library > Key Maps** (ChordScale, Scale/Forced and Scale/Restricted sets). Each key you play then becomes a chord before it reaches the arpeggiator.

### Drag MIDI

Grab the blue **Drag MIDI** grip in the top bar and drop it on a DAW track. The pattern is rendered offline by the same engine the plugin plays live, so it includes every current setting: Scale Lock, groove, velocities and speed. Right-click the grip to save the `.mid` file instead.

**Drag MIDI Export** sets what the phrase plays over:

* **Last Played Chord:** the last chord you held. Before you've played anything, it uses the key chord.
* **Chord Clip:** a clip from **Library > Chord Clips**, looped to the length.
* **A progression** in your key and scale, one chord per bar: I · I-V-vi-IV · vi-IV-I-V · I-vi-IV-V · ii-V-I-I · I-IV-V-IV · i-VII-VI-VII · i-iv-v-i · i-VI-III-VII. Chords are stacked in thirds of the scale, with as many notes as the phrase has rows.

The length can be the phrase's own length or 1, 2, 4, 8 or 16 bars. The file name includes the phrase, key, scale, length and tempo.

### Knobs and window

* Every knob moves in **whole steps of 1**, by drag or **mouse wheel** (one notch = 1).
* Shift makes the drag slower, but it still moves in whole steps.
* Double-click a knob to reset it.
* The window resizes from 60 % to 200 % from the bottom-right corner, and remembers its size.
* **?** opens the quick help.

### Saved state

All settings are automatable parameters and are saved with your project. The parameter IDs are the original property names: `Static_Mode`, `Ignore_Host_Velocities`, `Force_Internal_Sync`, `Speed`, `Bypassed`, `Random_Vel`, `Quantize_Start`, `Quantize_End`, `Quantize_Link`, `Midi_Input_Channel_Filter`, `Midi_Output_Channel_Filter`, plus `Scale_Key`, `Scale_Index`, `Scale_Lock` and `Key_Map_Enabled`.

The phrase, groove and key map are saved as `SECUENCIA`, `QUANTIZER` and `HARMONIZER` trees, so a saved state can be inspected and compared directly.

---

## A/B testing against the original

`phrasearp-render` renders a phrase over a MIDI file offline, using exactly the plugin's processing. `tools/diff_midi.py` reports per-note differences in pitch, start, end and velocity. The Windows build publishes both to `builds-windows`.

1. In your host, put the original plugin and MelodyMagic on two MIDI tracks fed by the same MIDI clip, and record both outputs as MIDI.
2. Start with a fixed phrase (for example `Phrases/kick101/MonoLines/MonoLine150.fas`), Random Vel 0, quantizer strengths 0 and Scale Lock off. Note-for-note equality is expected in both Free and Static mode.
3. Repeat with chord changes that land in the middle of phrase notes, to exercise the Free/Static difference.
4. Try phrases with Note_Offset (for example `Pluck_Stab/133.fas`) with the scale set as in the original (e.g. Major, key C).
5. Sweep Q Start and Q End (0, 0.5, 1) to calibrate the quantizer. Its rule is inferred; it lives in one place, `quantizeTick()` in `core/src/ClipPlayer.cpp`.

Offline render and comparison:

```bash
phrasearp-render "Phrases/kick101/MonoLines/MonoLine150.fas" input.mid --library <library folder> -o replica.mid
```

```bash
phrasearp-render phrase.fas input.mid --static --scale Major --key C -o replica_static.mid
```

```bash
python tools/diff_midi.py recorded_from_original.mid replica.mid --tolerance 1
```

`phrasearp-render --help` lists every option: scale lock, random velocity with a seed, speed, groove and strengths, key map, channel filters, tempo and length.

### How faithful is it?

| Area | Fidelity |
|---|---|
| Preset formats (.fas / .qas / .has / .mid) | Exact: byte-level ValueTree codec that reads and writes |
| Row/lane mapping, Free/Static engines, note-off matching, zero-length cleanup | Exact (from the decompiled code) |
| Diatonic Note_Offset, scale tables, snap clamps | Exact |
| Velocity scaling (round half to even), Ignore Host Velocities | Exact |
| Random velocity | Exact formula on a JUCE-compatible 48-bit LCG (seedable for tests) |
| Clip timing (speed, loop, sync) | High |
| Quantizer strength | Inferred (calibrate with step 5) |
| Generators | Behavioural (not bit-exact by design) |
| Key-map stage | Format exact; its place in the chain is inferred (input stage) |

Where MelodyMagic deliberately differs:

* A note-on whose velocity works out to 0 is not sent, since MIDI would treat it as a note-off.
* Repeating a phrase Number that is still sounding in Static mode first ends the earlier note.
* Free mode remembers the pitch each note started on, so changing the scale mid-note can never leave a note hanging.

None of these changes the output in normal use.

---

## Building

You need CMake 3.22 or newer and Visual Studio 2022. JUCE 8.0.4 and Catch2 3.7.1 are downloaded automatically. To use a local JUCE checkout instead, pass `-DJUCE_DIR=...`.

```bash
cmake -B build -G "Visual Studio 17 2022" -A x64
```

```bash
cmake --build build --config Release --target MelodyMagic_VST3 MelodyMagicTests phrasearp-render
```

```bash
build\Release\MelodyMagicTests.exe
```

The plugin is written to `build/MelodyMagic_artefacts/Release/VST3/MelodyMagic.vst3`.

| CMake option | Default | Meaning |
|---|---|---|
| `MELODYMAGIC_BUILD_PLUGIN` | ON | Build the VST3 (downloads JUCE) |
| `MELODYMAGIC_BUILD_STANDALONE` | OFF | Also build a standalone app |
| `MELODYMAGIC_BUILD_TESTS` | ON | Build the Catch2 unit tests |
| `MELODYMAGIC_BUILD_TOOLS` | ON | Build `phrasearp-render` |
| `MELODYMAGIC_BUILD_SCREENSHOT` | OFF | Build the tool that renders `docs/screenshot.png` |
| `MELODYMAGIC_COPY_AFTER_BUILD` | OFF | Copy the plugin into the VST3 folder after building |

The core library and the tests have no JUCE dependency, so they also build on Linux and macOS (`-DMELODYMAGIC_BUILD_PLUGIN=OFF`).

### Unit tests

There are 56 test cases. They cover the golden cases (row/lane mapping, Note_Offset, velocity, Free vs Static, zero-length cleanup and a seeded random-velocity snapshot), scales and Scale Lock, the codec, the clip player, the quantizer, the key map, channel filters, bypass, internal sync, offline rendering, MIDI files and the generators.

Two corpus tests run when their data is available:

* `MM_NORA_DIR=<expansion folder>` round-trips every original `.fas`, `.qas` and `.has` file byte for byte.
* `MM_LIBRARY_DIR=<folder with the four JSON files>` loads every library entry (487 / 312 / 1,464 / 1,388), checks the totals (9,672 notes, 6,971 grid lines), round-trips each one, and checks the A Major ChordScale key map.

## Project layout

```
core/                     all musical logic, no JUCE
  include/mm/ValueTreeCodec.h   JUCE ValueTree binary format reader/writer
  include/mm/Formats.h          phrase (.fas), groove (.qas), key map (.has)
  include/mm/Scales.h           scale tables, snap, scale state, diatonic shift, Scale Lock
  include/mm/ArpEngine.h        Free / Static arpeggiator
  include/mm/ClipPlayer.h       looping phrase player + quantizer
  include/mm/PhraseProcessor.h  per-block processing (filters, key map, sync, bypass) + offline render
  include/mm/Generators.h       303 Random, Chord / Bass, Mutate
  include/mm/Progressions.h     diatonic chords for Drag MIDI
  include/mm/Library.h, Json.h, MidiFile.h, Rng.h
tests/                    Catch2 unit tests
tools/                    phrasearp-render, diff_midi.py, screenshot tool
plugin/Source/            JUCE plugin: processor, editor, phrase grid, generator / library panels,
                          MIDI monitor, Drag MIDI grip, shared Virus look-and-feel
```

## Notes

* MelodyMagic contains no licensing or copy-protection code. It does not use the original plugin's name, skins or images.
* "Virus" in Virus Delay is used only as a descriptive name. No Access Music logos or artwork are included.
