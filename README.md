# GGrid

A node-based patch bay -- VST3/Standalone plugin built with JUCE. Drop modules onto a free-form
canvas, wire them together however you like (serial, parallel, branching), and modulate almost any
knob with an LFO/Envelope/ADSR cable.

## Modules

**Distortion**
- Waveshaper/Wavefolder -- 6 shapes, live transfer-curve preview
- Lossy -- spectral STFT lo-fi/bitcrush degradation

**Filter & EQ**
- Filter -- 10 types: biquads, comb/allpass, saturating Ladder Low/High Pass, vowel-sweep Formant
- EQ 8 -- draggable graphic EQ (Ableton EQ Eight style), 8 bands x 7 types, live input spectrum
- EQ 3 -- simple 3-band tone shaping

**Dynamics**
- Dynamics -- compressor with parallel Mix

**Time & Space**
- Delay -- tempo sync, ping-pong, saturating feedback with its own filtering
- Convolution -- 95-IR factory library, Tone/Fade/Stretch shaping, live waveform display
- Multiband Convolution -- 3-band crossover, full Convolution engine per band, recombines to one output
- Multipass -- 3-band crossover *splitter*: no per-band effect and no recombining, each band exits
  its own output port for independent downstream processing

**Modulation**
- Chorus/Flanger -- one module, two characters via a Mode switch
- Ring Mod / Freq Shift -- ring modulation or true single-sideband frequency shift
- LFO, Envelope (freeform hand-drawn shape), ADSR (classic sustain/release) -- modulation sources,
  no audio ports, drag their violet output nub onto any knob's destination dot

**Utility**
- Utility -- gain/pan/width/mono/phase, no coloration of its own

**Generators**
- 3xOsc -- 16-voice polyphonic synth, 3 FM-linked oscillators per voice, Mono/Legato + Glide

**I/O**
- Input / Output -- any number of each; nothing reaches the speakers unless wired all the way
  through from an Input to an Output

Multiband Convolution/Multipass's crossover strip and EQ 8's curve both show a live spectrum of
the incoming signal, so you can see what you're shaping.

## How it works

- **Patch bay** -- right-click to add a node, drag cables between output nubs and input dots. Up
  to 24 modules, 4 ports per side per node (all identical except Multipass's, which are genuinely
  independent per band). Left-drag to pan; hold-then-drag or shift-drag to rubber-band select;
  pinch/Ctrl+scroll/buttons to zoom.
- **Modulation cables** -- a separate, additive system from audio cables: drag an LFO/Envelope/
  ADSR's single output nub onto any knob's destination dot.
- **MIDI Mod Matrix** -- its own tab: 6 fixed routes (Note Pitch/Velocity/Mod Wheel/2 CC lanes) to
  a handful of destinations, plus the always-on Safety Limiter (brickwall, protects your ears/gear
  from an aggressively-driven chain).
- **File/Edit menus** -- Init Patch, Save/Load Patch (`.ggridpatch`), Copy/Paste/Duplicate.
- **Update checker** -- checks this repo's GitHub Releases in the background on launch.

Styled to match [SPANDEX](../RepitchDeck) (flat, hairline-bordered, no gradients/shadows) -- not
visually polished beyond that yet.

## Installing

**Windows**: run `Install GGrid.exe` from the Releases page, choose Standalone and/or VST3.

**Mac**: built via GitHub Actions (`.github/workflows/macos-build.yml`) as a universal binary --
trigger it manually and download the artifact zip. Unsigned/not notarized, so right-click ->
Open to get past Gatekeeper, and copy the zip's `IRs/` folder to `~/Documents/GGrid/IRs` (no
installer yet, so this step is manual).

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
- **Modulation cables and the MIDI Mod Matrix are two separate, additive systems** -- see
  `ModulationMatrix`.
- Design reasoning for specific choices lives in code comments near the relevant code, not
  duplicated here.

## Known limitations

- GUI is functional, not visually polished (flat SPANDEX styling, no custom skin).
- Each node caps at 4 in / 4 out connections, and the graph must stay acyclic -- no feedback loop
  *across* nodes (a module's own internal feedback, like Delay's, is unaffected).
- macOS build has no installer yet -- IRs need manual copying.
