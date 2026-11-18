# MelodyMagic

MelodyMagic is a pattern arpeggiator for Windows (VST3, JUCE 8). Hold a key or a chord and it plays a pattern with it:

* **One key** plays the pattern as a melody in that key.
* **A chord** is arpeggiated (Up, Down, Up & Down or Down & Up), and the pattern sets the rhythm.

Its Melody Maker writes classic trance leads over a chord progression, or learns from a MIDI file you import, always in your key and scale. **Drag MIDI** drops the result straight onto your DAW's timeline.

It shares its look with **Virus Delay** and **ArpSampler**: the same palette, knobs, panels and LCDs, all drawn as vectors.

![screenshot](docs/screenshot.png)

## Installing

Every push to `main` builds the plugin on GitHub Actions and runs the unit tests and pluginval (strictness 10). It then commits a zipped build to the **`builds-windows`** branch.

1. Download `MelodyMagic-VST3-Windows-x64.zip` from that branch, or from the run's artifacts.
2. Unzip `MelodyMagic.vst3` into `C:\Program Files\Common Files\VST3\`.
3. Rescan plugins in your DAW.

MelodyMagic takes MIDI in and sends MIDI out; it makes no sound itself. It loads as an instrument, because that is the slot DAWs let send MIDI on to another track.

* **Ableton Live:** set the synth track's *MIDI From* to the MelodyMagic track.
* **FL Studio:** set a MIDI output port in MelodyMagic's wrapper, and the same port on the synth.
* **Any DAW:** you can skip the routing and use **Drag MIDI**.

## Using it

**Pattern (top left).** The pattern is a small piano roll. The rows are shown as notes of the Scale Lock **Key**, as if you were holding the key's root; holding another key moves the whole pattern with it.

| Action | Result |
|---|---|
| Click empty space | add a note (drag to set its length) |
| Drag a note | move it |
| Drag a note's right edge | change its length |
| Alt while dragging | no snap |
| **Mouse wheel over a note** | up / down a semitone (with Scale Lock on, to the next note of the scale) |
| Right-click or double-click a note | delete it |
| Drag in the lower strip | set velocities |
| **Select All** button or Ctrl+A | select every note |
| Shift+drag on empty space / Shift+click | select a group / add or remove a note |
| **Up / Down** buttons, Up/Down keys or mouse wheel | move the whole selection to the next scale note (Alt = a semitone, Ctrl = an octave). With nothing selected, the buttons move every note |
| Drag a selected note | moves the whole selection |
| Delete / right-click | deletes the selection |
| Esc | clears the selection |

Drawing is **locked to the Key + Scale**: new or dragged notes snap to the nearest scale note, and the mouse wheel and Up/Down move to the next scale note. Hold **Alt** to place or move by a free semitone.

The display always uses the same zoom: two octaves of rows, centred on the notes.

**Length** (1, 2, 4 or 8 bars) copies the pattern into the new bars (1 → 2 bars plays it twice). **Snap**, **Clear**, **Undo** and **Redo** are around the pattern.

**Arp.**

* **Mode** sets how the pattern plays:
  * **Up, Down, Up & Down, Down & Up** play the held keys in that order. Each pattern note plays the next key, plus its own offset.
  * **Melody** works like ArpSampler's Step mode. It plays the pattern's own notes from the Key's root, and the key you hold only starts it and picks the octave: C3–B3 plays it as written, C2–B2 an octave down.
* **Speed** runs from x1/4 to x4 of the host tempo.
* **Rnd Vel** adds a random ± velocity.
* **Auto Play** is the "ghost note". While the DAW plays, MelodyMagic plays with no MIDI note on its track, as if the Key's root (octave 3) were held. Route its output to a synth and press play. A key you play takes over while you hold it.

While the DAW plays, the pattern follows the song position. While it is stopped, the pattern starts when you press a key.

**Scale Lock.** **Key** and **Scale** use ArpSampler's 16 scales, plus Persian, Chromatic and Custom. For Custom, click the 12 note buttons to build the scale; it ignores the key. With **Scale Lock** on, every note sent, live or through Drag MIDI, moves to the nearest note of the scale.

**Melody Maker.** The melodies follow how classic trance leads are written, and they are always in the Key and Scale:

* **Chords** sets the progression the melody is written over, one chord per bar. The panel shows the actual chords in your key (e.g. Am F C G, with flats in flat keys: Gm Eb Bb F). **Auto** uses the classic trance progression for the scale. The list has:
  * classic progressions;
  * a **Dark** set (e.g. 1-6-4-5 = Am F Dm Em, 1-7-6-5, 1-2-1-7, 1-4-1-6);
  * an **Uplifting** set;
  * 8-bar progressions.
* The melodies are written the way classic trance leads are:
  * notes on the beat are chord tones, and the notes in between move by scale step (passing and neighbour notes), with the odd leap;
  * a one-bar hook repeats over each chord, moved to fit it, and an answer bar ends the phrase (4 bars = A A A B, 8 bars = A A A B A A A C);
  * rhythms come from classic trance lead rhythms (offbeat, 3-3-2, rolling, gallop and others).
  * **Density** sets how busy the rhythm is, and **Range** how far the melody spreads.
* **Gate** shortens every note as it plays, live and in Drag MIDI (100 % = as drawn). The pattern keeps its notes; the faint end of each note in the display is the part the Gate cuts off.
* **New Melody** writes new notes. The pattern (rhythm) stays exactly as it is, and the melody stays in the pattern's register, from its lowest note up.
* **Top Notes Only** makes New Melody keep the bottom notes (the pattern's lowest note and the lower notes of chords) and change only the notes above them. On a pattern that is all one note, the notes on the beat stay as a pedal and the ones in between become the melody.
* **New Pattern** writes a new rhythm. The notes stay, in the same order.
* On an empty pattern (after **Clear**), either button writes both.
* Both buttons switch the Arp to **Melody** mode, so you hear the melody as written.
* **Randomise Notes** (Library) keeps the rhythm and writes new notes.
* **Learn From MIDI File** is on once you've loaded a MIDI file. New Melody and Randomise Notes then learn from that file: its rhythm (its first bar is the hook, its last bar the answer), its intervals, its favourite notes and its range.
* **Shuffle Notes** (Library) keeps the rhythm and uses exactly the imported file's notes, in new places.

**Library.** It has three tabs; click an entry to load it, and with the list focused the arrow keys browse. It reopens on the tab and folder you used last.

* **Factory:** the built-in patterns.
* **Phrases:** the library folder (**Folder...**). A phrase's rows become chord notes of your key (1st, 3rd, 5th of the scale), and loading one switches the Arp to Melody mode. It holds `nora_fas_library.json` or `.fas` files; these are not in this repository, because their pack asks that they not be redistributed.
* **My MIDI:** your own MIDI files, kept in `Documents\MelodyMagic\My MIDI`. **Import...** copies `.mid` files in and loads them. Importing reads **notes only**: controllers (CC), pitch bend, aftertouch, program changes and SysEx are ignored. Right-click an entry to delete it or show it in Explorer.
* **Save...** (any tab) saves the current pattern as MIDI into **My MIDI** or any **Factory** or **Phrases** folder (Arps, Bass, MonoLines ...). It then appears in that folder. Patterns saved into Factory and Phrases folders are kept in `Documents\MelodyMagic\User Patterns`. A saved pattern remembers the key it was written in, so loading it into another key moves it correctly.
* Phrases folders show their short name (Bass, MonoLines ...). Dropping a `.mid` on the pattern imports it into My MIDI too.

**Groove.** Pick a preset from the Groove dropdown (they come from `nora_qas_library.json` in the library folder). **Q Start** and **Q End** pull note starts and ends onto it; **Link** uses one strength for both.

**Keyboard.** Click the pattern and it takes the keyboard:

* **Up / Down** move the selected note.
* **Left / Right** select the previous / next note.
* **Delete** removes the selected note.
* **Ctrl+Z / Ctrl+Y** undo and redo.

Click the Library to browse with the arrow keys again. Buttons and knobs never take the keyboard away from the notes.

**Drag MIDI.** Drag the blue grip onto a DAW track. It renders exactly what you hear, over the last chord you played (or the key's root if you haven't played yet). The clip is at least 4 bars long. Right-click the grip to save the `.mid` file instead.

**Tips** in the top bar turns the tooltips on and off.

**Knobs** move in whole steps of 1, by drag or mouse wheel; double-click resets a knob. The window resizes from 60 % to 200 % from the corner.

## Building

You need CMake 3.22 or newer and Visual Studio 2022. JUCE 8.0.4 and Catch2 are downloaded automatically.

```bash
cmake -B build -G "Visual Studio 17 2022" -A x64
```

```bash
cmake --build build --config Release --target MelodyMagic_VST3 MelodyMagicTests
```

```bash
build\Release\MelodyMagicTests.exe
```

The plugin is written to `build/MelodyMagic_artefacts/Release/VST3/MelodyMagic.vst3`. The musical code in `core/` has no JUCE dependency, so the tests also build on Linux and macOS (`-DMELODYMAGIC_BUILD_PLUGIN=OFF`).

```
core/            pattern model, arpeggiator, Melody Maker, shuffle, MIDI import,
                 scales, groove quantize, library (.fas / .qas / JSON), MIDI files
tests/           Catch2 unit tests
plugin/Source/   JUCE plugin: processor, editor, pattern grid, library, monitor, Drag MIDI
tools/           screenshot tool (docs/screenshot.png)
```
