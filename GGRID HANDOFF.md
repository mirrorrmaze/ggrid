# GGrid — Project Handoff

This document summarizes the GGrid project as of 2026-08-16: what it is, what's been built, the
technical conventions in place, and the workflow rules that have been established for working on
it with an AI coding assistant (Claude Code). It's meant to get a new collaborator up to speed
quickly, whether or not they're using Claude Code themselves.

---

## What GGrid is

A modular processing rack — a VST3/Standalone audio plugin built with JUCE (C++20). Instead of a
fixed effects chain, you get a free-form node-based patch bay (Bitwig Grid style): drop in up to
24 effect modules spanning distortion, filtering, dynamics, modulation, and convolution, wire them
together with cables however you want (serial, parallel, branching), and modulate almost any knob
with an LFO via a separate cable type.

- **Source:** `D:\Claude Projects\GGrid`
- **GitHub repo:** [`mirrorrmaze/ggrid`](https://github.com/mirrorrmaze/ggrid) (public)
- **Ships as:** VST3 plugin + standalone app, Windows and macOS
- **Distribution:** loose files in `VST PROJECT ALPHA INSTALLERS\GGrid` on the shared Dropbox
  (`Install GGrid.exe`, `GGrid-macOS.zip`, `README.txt`) — not zipped together, not
  version-baked-into-filenames (each of the team's plugin projects uses a different convention
  here; this one hasn't been unified with the others on purpose)

## Effect modules (12 so far)

| Module | What it does |
|---|---|
| Waveshaper | Hard Clip / Soft Clip (tanh, cubic) / Foldback Wavefolder / Sine Fold / Rectify-Asymmetric, with a live transfer-curve preview |
| Filter | LP/HP/BP/Notch (biquad) + Comb Feedback/Feedforward + Allpass Diffusor |
| Delay | Feedback delay w/ saturation in the feedback path, tempo sync, ping-pong, Low Cut/Hi Cut |
| Dynamics | Compressor (threshold/ratio/attack/release/makeup/mix) |
| Convolution | IR loader, 95-entry factory catalog + custom folder, Stretch/Fade In/Fade Out/Tone shaping, live waveform display |
| Utility | Gain/Pan/Width/Mono/Phase invert (mirrors Ableton's Utility device) |
| Ring Mod | Ring modulation or true single-sideband Frequency Shift |
| LFO | Modulation source only — no audio ports, drives the mod-cable system |
| Lossy | Spectral STFT lo-fi/bitcrush-style degradation (ported from a sibling project, SPANDEX) |
| EQ 8 | 8-band fixed-frequency graphic EQ (100 Hz–12.8 kHz, one octave apart) |
| Chorus/Flanger | One modulated delay line, mode-switched (Flanger adds a feedback path) |
| EQ 3 | Simple Low Shelf / Mid Bell / High Shelf tone EQ |

## Core architecture

**Parameter ID scheme:** every rack slot pre-declares parameters for every module type it could
ever become, regardless of its current type — `slot{n}_type`, `slot{n}_bypass`,
`slot{n}_{moduleType}_{paramName}`. This means a slot's parameter identity (and therefore DAW
automation/preset recall) never moves when you change what module occupies it. See
`Source/Params/Identifiers.h`.

**Module type registry:** `ModuleType` enum in `Identifiers.h` is **append-only** — new module
types are always added at the end with the next integer value, never inserted or reordered, so a
saved project's type index keeps meaning the same thing across versions.

**Routing graph (the biggest architectural piece, reworked 2026-08-16):** the canvas isn't a
single ordered chain, it's a real directed graph (`GGridAudioProcessor::connections`,
`Source/Rack/ConnectionGraph.h`). Two fixed pseudo-nodes, **Input** and **Output**, are always
present on the canvas and can't be deleted or retyped — Input is the plugin's raw dry signal
(output ports only), Output is the final mix (input ports only). A module only actually
processes audio if it has an explicit path from Input to Output; there's no implicit
per-node dry-through the way earlier versions had. Each node (module or pseudo-node) allows up to
4 outgoing and 4 incoming connections, enough for real parallel-processing patches (e.g. splitting
one signal into three separately-EQ'd bands). `processBlock` computes a topological order each
block (Kahn's algorithm anchored at Input) and processes every reachable node exactly once,
summing predecessor outputs before running each module. New connections are rejected at add-time
if they'd exceed capacity, duplicate an edge, or create a cycle — block-based processing has no
notion of a same-block feedback loop.

**Modulation cables:** a second, separate graph (`ModulationMatrix::modConnections`) — LFO nodes
have no audio ports, just a single output nub you drag onto a destination dot on essentially any
knob on any other module. Rendered in violet, distinct from the orange audio cables, with a
glowing pulse riding the cable positioned by the LFO's live output. This coexists with an older,
smaller **MIDI Mod Matrix** (6 fixed routes: Note Pitch/Velocity/Mod Wheel/2 CC lanes → a handful
of fixed destinations) that was deliberately left in place rather than unified, so a knob that's
targeted by both just gets both offsets summed.

**Save/Load patches:** reuses the same `getStateInformation`/`setStateInformation` pair a DAW
host calls for its own presets — `.ggridpatch` files (binary-encoded XML), default location
`Documents/GGrid/Patches`. A save from before the Input/Output redesign migrates automatically on
load (old implicit-root/implicit-sink wiring gets converted into explicit Input/Output edges).

**GUI stack:** `NodeComponent` (one draggable box per module, wraps whichever control panel
matches its current type — see `ModuleControlPanels.h`), `IONodeComponent` (the two fixed
Input/Output boxes — no type dropdown, no delete button), `NodeGraphEditor` (the canvas itself:
pan/zoom, cable drawing/hit-testing, multi-select, add/delete). Custom `GGridLookAndFeel` matches
a sibling project's (SPANDEX) flat, hairline-bordered, two-tone aesthetic — no gradients or
rounded corners.

## Testing

`OfflineDspTest` is a second CMake build target that links only the DSP/graph-logic source files
(no GUI, no plugin-client dependencies) and asserts on signal properties directly — finite/bounded
output, expected frequency response, expected sample-accurate timing, graph topology correctness,
etc. Over 60 checks as of this writing. This is the primary way DSP and routing-graph changes get
verified without needing to open a DAW:

```bash
cmake --build build --config Release --target OfflineDspTest
./build/Release/OfflineDspTest.exe
```

GUI/interactive changes are **not** covered by this harness — those get verified by building the
Standalone app and testing by hand (see workflow rules below).

## Build & release pipeline

- **Local build:** CMake + JUCE (FetchContent), C++20, `cmake --build build --config Release
  --target GGrid_VST3 --target GGrid_Standalone`
- **Windows installer:** Inno Setup script at `Installer/GGrid.iss`, built via
  `ISCC.exe GGrid.iss`, output at `Installer/Output/Install GGrid.exe`
- **macOS build:** GitHub Actions workflow (`.github/workflows/macos-build.yml`),
  `workflow_dispatch`-triggered (not automatic on push) — universal binary, VST3 + Standalone.
  Triggered via `gh workflow run macos-build.yml --ref main`, artifact pulled down via
  `gh run download <run-id> --name GGrid-macOS`
- **Local VST3 install (Windows):** copy the built `.vst3` bundle to
  `C:\Program Files\Common Files\VST3\`

---

## Workflow rules established for this project

These are standing conventions that have been explicitly confirmed over the course of this
project's development. If you're working on GGrid with an AI assistant, these are worth telling
it up front (or codifying in a `CLAUDE.md` in the repo, which doesn't exist yet as of this
writing).

1. **Proactively refresh the Dropbox installer folder whenever a version is verified and ready
   for testers** — don't wait to be asked. "Verified" means: it builds clean, the offline test
   suite passes, and (for GUI changes) it's been functionally confirmed. This applies across this
   user's plugin projects generally (GGrid, plus two sibling JUCE projects), each of which uses
   its own distribution convention in its own Dropbox subfolder — don't try to unify them.

2. **Always keep the macOS build in Dropbox in sync with Windows, not just Windows.** Every time
   the Windows installer gets refreshed for testers, also trigger the macOS GitHub Actions
   workflow and pull down the resulting artifact to replace `GGrid-macOS.zip`. Never leave a
   "macOS is a few versions behind" caveat in the README as a substitute for actually rebuilding
   it.

3. **Don't attempt synthetic UI-click automation or screenshot-based verification for this
   native desktop app.** Coordinate/handle-based window capture and synthetic input have proven
   unreliable in this environment (grabbed unrelated windows, didn't register clicks on real
   controls) even with careful title/handle checks. Instead: build the change, verify everything
   that *can* be verified non-interactively (code review, the offline test suite, layout/bounds
   math), then directly hand it to the human to click through and confirm. This is specifically
   about GGrid/JUCE-style native apps — it doesn't apply to browser-based tooling, which has a
   reliable accessibility tree to work with.

4. **Only commit/push when explicitly working toward a verified checkpoint**, with descriptive
   commit messages explaining *why* a change was made, not just what changed. Never force-push,
   never amend published commits, never skip build verification before pushing.

5. **`ModuleType` is append-only.** Never renumber or reorder existing entries — always add new
   module types at the end. Renaming a module's *display name* or internal C++ identifiers is
   fine (has happened twice — Graphic EQ → EQ 8, and Chorus stayed Chorus), but changing which
   integer a *type* maps to breaks every existing saved project's slot-type parameter.

6. **When a rearchitecture would break old saved state, prefer a cheap automatic migration over
   either a silent break or complex compatibility shims.** E.g. the Input/Output redesign
   detects old-format saves via a missing version marker and synthesizes equivalent explicit
   wiring on load, rather than leaving old patches silent or maintaining two parallel routing
   models indefinitely.

---

## Suggestion for the collaborator

If you're also going to use an AI coding assistant (Claude Code or otherwise) on this repo, it's
worth adding a `CLAUDE.md` (or equivalent) at the project root capturing the architecture notes
and workflow rules above — GGrid doesn't have one yet, unlike its sibling projects, which has
meant re-deriving some of these conventions from the code each time a new session picks it up.
