# GGrid

A node-based patch bay -- VST3/Standalone plugin built with JUCE. Drop modules onto a free-form
canvas, wire them together however you like (serial, parallel, branching), and modulate almost any
knob with an LFO/Envelope/ADSR cable.

## Modules

**Distortion**
- Waveshaper/Wavefolder -- 6 shapes, live transfer-curve preview
- Lossy -- spectral STFT lo-fi/bitcrush degradation
- Spectral Clipper -- clips per-bin magnitude in the frequency domain rather than the time-domain
  waveform (Hard/Soft/Foldback/Sine Fold shapes), a cleaner-sounding alternative to a normal clipper
- Mackity -- vintage small-console input-stage saturation with a much hotter input drive curve,
  spongy slam/edge overload, output pad, mix, and gain

**Filter & EQ**
- Filter -- 10 types: biquads, comb/allpass, saturating Ladder Low/High Pass, vowel-sweep Formant
- Nonlinear Filter -- visual driven filter with cutoff/resonance/drive/morph, multiple modes, and
  Soft Clip/Hard Clip/Sine Fold/Foldback/Asymmetric/Warm/Saturated/Bias/Clipped distortion
- EQ 8 -- draggable graphic EQ (Ableton EQ Eight style), 8 bands x 7 types, live input spectrum;
  drag nodes for frequency/gain, hold Alt or Command while dragging to narrow/widen Q
- EQ 3 -- simple 3-band tone shaping

**Dynamics**
- Compressor -- feed-forward downward compressor modeled on Ableton Live's: Threshold/Ratio/
  Attack/Release, a soft Knee, a Peak/RMS Detection switch, Makeup gain, and parallel Mix
- Limiter -- brickwall peak limiter: Gain (input trim), Ceiling, Release

**Time & Space**
- Delay -- tempo sync, ping-pong, saturating feedback with its own filtering
- Shimmer Reverb -- pitch-shifted feedback reverb with Size/Feedback/Diffusion, semitone Shift,
  Single/Dual/Reverse pitch modes, Bright/Dark color, modulation, low/high cuts, Mix, and Output
- Convolution -- 95-IR factory library, Tone/Fade/Stretch shaping, live waveform display
- Multiband Convolution -- 3-band crossover, full Convolution engine per band, recombines to one output
- Multipass -- 3-band crossover *splitter*: no per-band effect and no recombining, each band exits
  its own output port for independent downstream processing; the crossover strip shows fixed
  frequency markers and precise split frequencies while dragging

**Modulation**
- Chorus/Flanger -- one module, two characters via a Mode switch
- Ring Mod / Freq Shift -- ring modulation or true single-sideband frequency shift
- LFO, LFO Table, Envelope (freeform hand-drawn shape), ADSR (classic sustain/release) --
  modulation sources, no audio ports, drag their violet output nub onto any knob's destination dot
- LFO's built-in shapes can be edited in-place: edited presets stay selected and show an asterisk,
  Alt or Command highlights/curves a segment, and Control-drag draws dense point runs

**Utility**
- Utility -- gain/pan/width/mono/phase, no coloration of its own

**Generators**
- 3xOsc -- 16-voice polyphonic synth, 3 FM-linked oscillators per voice, Mono/Legato + Glide
- WT Synth -- Phase Plant-inspired wavetable generator with built-in Sine/Triangle/Saw/Square
  waves, searchable bundled factory wavetable browser, live wavetable preview, external FM by
  patching one WT Synth into another, parallel stacking through normal graph routing, Polyphony,
  Master Pitch, Bend Range, Mono/Legato + Glide, plus Unison/Spread algorithms: Hard, Smooth,
  Synthetic, Freq Stack, Pitch Stack, Shepard, Pentatonic Major/Minor, Octaves, Fifths, Minor,
  Major, Sus2/Sus4, Dim, Harmonics, and 1x-8x multipliers

**I/O**
- Input / Output -- any number of each; nothing reaches the speakers unless wired all the way
  through from an Input to an Output

Multiband Convolution/Multipass's crossover strip and EQ 8's curve both show a live spectrum of
the incoming signal, so you can see what you're shaping.

## How it works

- **Patch bay** -- right-click for a Spotlight-style search popup to add a node (type to filter,
  arrow keys + Enter, or click), drag cables between output nubs and input dots. Up
  to 24 modules, 4 ports per side per node (Multipass and WT Synth expose genuinely independent
  output buses). Click-hold anywhere that is not a parameter to move a module; drag the lower-right
  resize handle to resize it; use the fold button to minimize. Left-drag empty canvas to pan;
  hold-then-drag or shift-drag to rubber-band select; pinch/Ctrl+scroll/buttons to zoom.
- **Modulation cables** -- a separate, additive system from audio cables: drag an LFO/Envelope/
  ADSR/LFO Table's single output nub onto any knob's destination dot. A destination can take more
  than one cable at once -- e.g. two LFOs, or an LFO and an Envelope, both stacking onto the same
  knob -- their contributions sum rather than one replacing the other.
- **LFO Table / WT Synth tables** -- GGrid ships its own copy of a Kilohearts-style factory
  wavetable set (`Resources/Wavetables/Kilohearts`, installed to `Documents/GGrid/Wavetables`).
  It is self-contained: nothing reads from your local Kilohearts install path, so teammates and
  CI builds get the same catalog from the repo/installer.
- **WT Synth routing** -- FM is graph-first: route the audio output from one WT Synth into another
  WT Synth's `FM In`. For parallel layers, route multiple WT Synth modules to the same downstream
  module or Output.
- **File/Edit menus** -- Init Patch, Save/Load Patch (`.ggridpatch`), Copy/Paste/Duplicate.
- **Update checker** -- checks this repo's GitHub Releases in the background on launch.

## Installing

**Windows**: run `Install GGrid.exe` from the Releases page, choose Standalone and/or VST3.

**Mac**: built via GitHub Actions (`.github/workflows/macos-build.yml`) as a universal binary --
trigger it manually and download the artifact zip. Unsigned/not notarized, so right-click ->
Open to get past Gatekeeper, then copy the zip's `IRs/` folder to `~/Documents/GGrid/IRs` and
`Wavetables/` to `~/Documents/GGrid/Wavetables` (no installer yet, so this step is manual).

## Building from source

Requires CMake 3.22+ and a C++20 compiler (Visual Studio 2022 Build Tools on Windows). JUCE is
fetched automatically via `FetchContent`.

```
cmake -B build
cmake --build build --config Release --target GGrid_VST3 GGrid_Standalone
```

Built artifacts land in `build/GGrid_artefacts/Release/`. Windows installer: install
[Inno Setup](https://jrsoftware.org/isinfo.php), then `ISCC.exe Installer\GGrid.iss`.

Offline DSP test harness (checks every module's DSP, no audio device or DAW host required):

```
cmake --build build --config Release --target OfflineDspTest
build\Release\OfflineDspTest.exe
```

## Project layout

```
Source/
  PluginProcessor.*, PluginEditor.*   top-level plugin/editor
  Rack/                                RackModule interface, RackSlot, ConnectionGraph, SharedServices
  Modules/                             one file pair per rack module type
  IR/                                   IR library, processing, background reshape worker
  Modulation/                          MIDI mod matrix
  Params/                              parameter ID scheme, APVTS layout builder
  GUI/                                 canvas, node components, per-module control panels
Resources/IRs/                         factory impulse response library (95 IRs + CREDITS.md)
Resources/Wavetables/                  bundled wavetable fallback library
Tests/                                 offline DSP verification harness
Installer/                             Inno Setup installer script
.github/workflows/                     macOS CI build
```

## Architecture

- **Routing is a directed graph, not a fixed chain** -- `GGridAudioProcessor::connections`
  (`Source/Rack/ConnectionGraph.h`). Input-type slots are roots, Output-type slots are sinks,
  `processBlock` runs a topological order each block. Up to 4 connections per side per node.
- **Every slot pre-declares parameters for every module type it could become**
  (`slot{n}_{moduleType}_{param}`), so a slot's automation/preset identity never moves when you
  change what's in it.
- **Modulation cables are additive** -- a destination can take more than one cable at once, see
  `ModulationMatrix`.
- **Modules with multiple outputs publish per-output audio buses from the module itself** --
  Multipass exposes Low/Mid/High bands. WT Synth is intentionally graph-first with a single audio
  output and a single `FM In`; build FM/parallel stacks by patching multiple WT modules together.
- Design reasoning for specific choices lives in code comments near the relevant code, not
  duplicated here.

## Known limitations

- GUI is still evolving toward a custom skin, but the main module workflow now supports direct
  drag-anywhere movement, resizing, folding, copy/paste, and duplication.
- Each node caps at 4 in / 4 out connections, and the graph must stay acyclic -- no feedback loop
  *across* nodes (a module's own internal feedback, like Delay's, is unaffected).
- macOS build has no installer yet -- IRs and Wavetables need manual copying from the artifact zip.
