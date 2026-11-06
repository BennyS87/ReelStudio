# Reel Studio

A native macOS DAW built with JUCE 8 and Tracktion Engine 3.2. Imported files are never modified: every edit (speed, pitch, reverse, trims, effects) is an instruction stored in the project, and Cmd+Z undoes it.

## Build it on your Mac

You need macOS 12 or newer, an internet connection for the first build, and about 3 GB of free disk space.

1. Install Apple's command line tools (free, no full Xcode needed):

   ```
   xcode-select --install
   ```

2. Install CMake 3.22 or newer. With Homebrew: `brew install cmake`. Without Homebrew, use the installer from cmake.org and run `sudo "/Applications/CMake.app/Contents/bin/cmake-gui" --install` to put `cmake` on your path.

3. In Terminal, go to this folder and run:

   ```
   ./build.sh
   ```

   The first build downloads JUCE and Tracktion Engine and compiles them, which takes about 10 minutes on an Apple Silicon Mac. Later builds only recompile what changed.

4. Open the app:

   ```
   open "Reel Studio.app"
   ```

   Drag it into Applications if you like. On first launch it opens a demo song.

### Building from VS Code

Open this folder in VS Code and install the recommended extensions (CMake Tools, clangd). Pick a kit (Clang), then use **CMake: Build** and **CMake: Run Without Debugging**. The `Run self-test` launch configuration runs the automated checks described below.

### Building manually

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
open "build/ReelStudio_artefacts/Release/Reel Studio.app"
```

## Using it

| To | Do this |
|---|---|
| Add tracks | **Add track**: audio from a file, synth, sampler, or drum machine. Or drop audio and MIDI files on the timeline. |
| Make a clip | Double-click an empty spot on a track. Synth and sampler tracks get a note clip, drum tracks get a step clip. |
| Change speed and pitch | Select an audio clip, open the **Editor** tab. Speed and pitch are independent while **Keep pitch** is ticked; untick it for tape-style speed. |
| Cut, trim, reverse | Put the playhead on a clip and press **B** to split. Drag clip edges to trim. Right-click a clip to reverse it. |
| Mix | Track headers have mute, solo and a volume bar. The **Mixer** tab has faders, pan and meters. |
| Add effects | **Effects** tab, **Add effect**: EQ (8 bands, live spectrum, drag the points), compressor, saturation, distortion, phaser / flanger / chorus, echo, reverb, vocoder. Effects run in order; use Earlier and Later to reorder, the switch to bypass, Remove to delete. You can add the same effect more than once. |
| Use an effect only at certain points | Press **Automate...** on an effect and pick a setting, then click points in the lane that opens under the track. Drag points to move them; right-click removes one. A point at zero turns the effect off at that moment. |
| Turn a sound into an instrument | Right-click an audio clip, choose **Make a playable instrument**. Reel renders that clip to a new file, finds its musical note, and creates a sampler track that plays it in tune on every key. |
| Play notes | A MIDI keyboard connected by USB, the on-screen keyboard, or your computer keys A W S E D F T G Y H U J K (Z and X change octave). Notes go to the selected synth, sampler or drum track. |
| Record | Select an **audio track** and press **Record** (or R) to record your microphone or audio interface. Choose the input, hear-input and count-in in that track's **Sound** tab. Select a **synth, sampler or drum track** to record your MIDI keyboard instead. Recordings are saved as new files next to your project. |
| Draw notes as lines | Open the **Lines** tab on a synth, sampler or drum track and press **Add Lines**. Press and drag in the grid to draw a line: **Glide** makes smooth slides between pitches, **Step** makes jumps. Drag points to reshape, click a line to add a point, double-click a segment to switch glide and step, right-click to delete. The lane below edits **pressure**, **slide** and **velocity**; **Vibrato**, **Transpose**, **Quantize**, **Length** (1 to 16 bars) and **Rec** (record your keyboard as lines) are in the toolbar. Each line plays as its own MPE note, so every note bends on its own. Clips and your keyboard still play on the same synth. |
| Arrange the workspace | Every section (Editor, Sound, Effects, Mixer, Lines) can sit at the bottom, on the left or right, or in its own window: use the **...** button on its tabs, or the **View** button. Drag the edges between areas to resize them. **Fill the window** gives one area, or the timeline, the whole window (Esc brings the rest back). **Cmd+Ctrl+F** or the green window button makes Reel full screen; a section in its own window can go full screen too. Closing a section's window puts it back where it was docked. Reel remembers your layout; **View, Reset layout** goes back to the original. |
| Engine (sound playground) | **Add track, Engine** makes an instrument with four sound layers, each a synth sound, one of your instruments or a sound file, played through one of six engines (choose in the **Sound** tab). **Layers** stacks them, with **Blend** bringing them in one by one and **Drift** making them breathe. **Orbits** puts them in the corners of an X/Y pad: drag the ring, and let the orbit (circle, figure 8, spiral) move through them. **Threads** gives three sequencer lanes, each on its own layer with its own steps, rate, order and drawn velocities, plus shift, humanize, gate and divisi. **Chords** plays a whole chord from one key, with inversion, spread and strum. **Disperse** fans one note out into voices that glide apart. **Lattice** turns one held note into a rhythmic part with octave jumps. Inspired by FFOSSO's engines; the sounds are your own. |
| Arpeggiator | On a synth track or a track playing one of your instruments, open the **Sound** tab and press **Add arpeggiator**. Hold a chord (from a clip, your keyboard or Lines) and it plays the notes in turn, in time with the song, or on its own clock when the song is stopped. **Pattern**: Up, Down, Up and down, Down and up, As played, Random, Chords. **Rate** 1/4 to 1/32 with triplets, **Octaves** 1 to 4, **Gate**, **Swing**, **Velocity** (as played or fixed) and **Hold** (keeps playing after you let go). It stays in front of the instrument when you switch sounds. |
| Lines on recordings | Select an audio track and open the **Lines** tab. **Turn into lines** follows the recording's pitch, slides and vibrato and draws them as lines on a new synth track, lined up with the recording (also in the clip's right-click menu under **Turn into MIDI notes**). **Play lines with this sound** makes a playable instrument from the recording and adds Lines, so the lines you draw glide the recording itself. |
| Save sounds as instruments | Right-click an audio clip and choose **Save this sound as an instrument**, or press **Save as instrument** on a sampler track. Your instruments are listed in the **Sound** tab of every synth and sampler track (switching keeps the track's notes and lines) and under **Add track**, **My instrument**. They're kept in `~/Library/Application Support/Reel Studio/Instruments`. |
| Turn audio into MIDI | Right-click an audio clip (or use the **Editor** tab) and choose **Turn into MIDI notes**: **Melody** for one note at a time (voice, bass, lead), **Chords** for several at once, **Drums** for kick, snare and hi-hat. The notes land on a new track you can edit and re-voice. Clean, single-instrument recordings give the best results. |
| Import MIDI | Drop a .mid file or use **Add track**. Each part becomes a track with a sound matched to its MIDI instrument; channel 10 becomes a drum track. Edit notes in the piano roll, change the sound in the **Sound** tab. |
| Design sounds | **Sound** tab on a synth track opens the instrument designer: 4 oscillators with unison, a 12 or 24 dB filter with its own envelope, volume envelope, 2 LFOs, 2 modulation envelopes, a modulation matrix (LFOs, envelopes, velocity, key, mod wheel or pressure to any setting), voice mode with glide, drive and chorus. Start from one of 12 sounds; **Save as my sound** keeps your version. The drum kit lets you tune, shorten, balance or replace each drum. |
| Program drums | Select a drum clip; the **Editor** tab shows the step grid. Click a step for on, again for accent, again to clear. |
| Save and export | **Project** menu: save, open, export a WAV mixdown, audio and MIDI settings. Work is also autosaved every 15 seconds. |

Keyboard: Space play or pause, Home back to start, B split, Cmd+D duplicate, Delete remove, Cmd+Z undo, Shift+Cmd+Z redo. Pinch or Cmd-scroll zooms the timeline and piano roll.

## Where things live

- Projects are `.tracktionedit` files. They reference your audio files where they are, so keep those files in place (or re-save the project after moving them).
- `~/Library/Application Support/Reel Studio/` holds the autosave, the generated drum kit, sounds you saved, and samples made with **Make a playable instrument**.

## Self-test

`"Reel Studio.app/Contents/MacOS/Reel Studio" --selftest results` builds a song through the same code the interface uses, renders it offline and checks it: independent speed and pitch (261.6 Hz expected and measured), tape mode, reverse, split and undo, all synth sounds, the drum machine, all four effects with automation, the vocoder, sound-to-instrument note finding, MIDI import, save and reopen, the demo mixdown, and that the original audio file is byte-for-byte unchanged. It prints PASS or FAIL for each.

## Source layout

| File | What it does |
|---|---|
| `Source/Session.*` | Everything the app does to the Tracktion edit: tracks, clips, sounds, effects, automation, transport, live MIDI, import and export. |
| `Source/ReelPlugins.*` | All effects: EQ, compressor, saturation, distortion, phaser / flanger / chorus, echo, reverb, vocoder. |
| `Source/EffectEditors.*` | EQ display with spectrum, compressor curve and meter, saturation curve, envelope pictures. |
| `Source/Dsp.*` | Drum kit synthesis and pitch detection. |
| `Source/ArrangeView.*` | The timeline: tracks, clips, ruler, loop range, automation lanes. |
| `Source/Editors.*` | Piano roll, step grid and the audio clip editor. |
| `Source/Engines.*`, `Source/EngineWidgets.h` | The Engine instrument: four layers, six engines, the X/Y pad and lane editors. |
| `Source/Dock.*` | The workspace layout: dock areas, section windows, resizing, fill the window, saved layout. |
| `Source/Arpeggiator.*` | The arpeggiator: takes held notes and plays them as patterns in time with the song. |
| `Source/ReelSampler.*` | Reel's sampler: plays a recorded sound on every key, with per-note pitch bend (so Lines can glide it), pressure, slide, envelope and looping. |
| `Source/LinesPlugin.*` | The Lines sequencer: line data, the MPE note engine, recording. |
| `Source/LinesEditor.*` | The Lines tab: drawing canvas, expression lane, toolbar. |
| `Source/Panels.*` | Instrument designer, effect chain and mixer tabs. |
| `Source/MainComponent.*`, `Source/Main.cpp` | Window, toolbar, keyboard shortcuts and app start-up. |
| `Source/SelfTest.cpp` | The automated checks. |

## Limits

- This version doesn't load third-party AU or VST3 plugins. JUCE supports that if you want it later; it needs `JUCE_PLUGINHOST_AU=1` in `CMakeLists.txt` and a plugin browser.
- Time-stretching uses SoundTouch, which is good for most material. Tracktion also supports Elastique and RubberBand, which sound better on extreme changes but need separate licences or GPL builds.

## Licences

JUCE is dual-licensed under AGPLv3 and the JUCE licence. Tracktion Engine is dual-licensed under GPLv3 and a commercial licence. Building and using this app yourself is fine under those terms. If you ever distribute it, either release your source under a compatible licence or buy the commercial licences.
