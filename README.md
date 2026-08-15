# GGrid

A modular distortion VST3/Standalone plugin built with JUCE: build a signal chain out of up to 8
rack slots, drop in whichever modules you want (in whatever order), and mangle the sound.

## Features

- **Node-based patch bay** -- right-click blank canvas space to add a node (Waveshaper/Filter/
  Delay/Dynamics/Convolution/Utility/Ring Mod/LFO/Lossy/EQ 8/Chorus-Flanger/EQ 3), drag its title
  bar to reposition, drag a cable from a node's output to another node's input to connect them. Up
  to 8 nodes, including duplicates of the same type. Each audio node has 2 output nubs and 2 input
  dots -- enough to split a signal into two parallel chains and sum them back together, not just a
  single-file chain. A freshly added node auto-chains after whichever node you added right before
  it, so simple serial patches still "just work" without wiring every node by hand -- explicit
  rewiring is how you branch off into something parallel. Grab an *existing* cable (not just a
  node's output) to rewire it elsewhere, or drop it on blank space to genuinely disconnect it --
  a node that's had every connection removed goes silent (out of the signal path entirely), not a
  free-running unpatched copy of the dry signal still bleeding into the mix. A brand-new node
  that's never been wired at all is the one exception: it still works standalone as a
  chain-of-one, the same convenience that's always let a freshly dropped single module "just work."
  Left-click-drag blank canvas to pan (a quick grab-and-go); holding briefly before you drag
  selects instead, opening a rubber band to catch several nodes at once (shift-drag does the same
  instantly, skipping the hold) -- drag any selected node to move the whole selection together,
  and Delete/Backspace removes every selected node. Two-finger/wheel scroll pans in whichever
  direction(s) it reports (no visible scrollbars, so it reads as an open sandbox rather than a
  bounded scrollable area) -- zoom (20%-200%) is a separate gesture: pinch on a trackpad,
  Ctrl+scroll on a plain mouse wheel, or the +/-/reset buttons, so a touchpad's natural two-finger
  scroll doesn't get misread as pinch-to-zoom. Click a node's title bar to select it, shift-click
  to add/remove it from the selection.
- **Modulation cables** -- LFO nodes have no audio ports at all (they're modulation sources, not
  audio processors) -- just a single violet output nub, dragged onto a matching violet
  modulation-destination dot on **any** continuous knob any other node exposes (essentially every
  knob in the rack, not a fixed handful -- e.g. every Waveshaper knob, not just Drive). A
  genuinely separate cable graph from the orange audio connections, rendered in violet so the two
  are never ambiguous at a glance; grab an existing mod cable to rewire/disconnect it exactly like
  an audio one. A glowing pulse rides along each mod cable, positioned by the source LFO's live
  output rather than a free-running clock, so its back-and-forth motion visibly tracks that LFO's
  actual Rate and Shape (a Square LFO's pulse snaps between the two ends; a Sine's eases) as it
  travels toward the module it's modulating. Modulation is additive on top of the knob position,
  never overwrites it. This generic per-knob system is separate from (and additive with) the fixed
  6-destination MIDI Mod Matrix below -- a knob that's a MIDI Mod Matrix destination can be nudged
  by both at once.
- **Waveshaper/Wavefolder** -- Hard Clip, Soft Clip (tanh/cubic), Foldback Wavefolder, Sine Fold,
  Rectify/Asymmetric, with Drive, Symmetry, Fold Amount, 2x/4x oversampling, Mix, Output, and a
  live transfer-curve preview showing the current shape/symmetry/fold as an actual bent line
  rather than just numbers on the knobs.
- **Filter** -- one module, 7 algorithms via a Type dropdown: Low Pass / High Pass / Band Pass /
  Notch (resonant biquads), Comb (Feedback), Comb (Feedforward), and an Allpass Diffusor
  (Schroeder allpass -- the classic reverb-diffusion building block).
- **Delay** -- feedback delay (1-2000ms, or Sync'd to a host-tempo note division from 1/1 to
  1/16T) with a tanh saturator in the feedback path, so pushing feedback hard adds grit and
  self-limits instead of spiraling; Ping-Pong cross-feeds L/R so echoes alternate channels; Low
  Cut/Hi Cut filters also sit in the feedback path, so repeats get progressively darker/thinner
  like a real tape delay instead of every echo sounding identically full-band. Rate changes are
  smoothed (~15ms ramp) rather than jumping, so turning the Time knob glides instead of clicking;
  the delay line itself uses cubic (Lagrange 3rd-order) interpolation rather than JUCE's default
  linear, for less high-frequency smearing on the constantly-fractional reads that smoothing produces.
- **Dynamics** -- standard downward compressor (Threshold/Ratio/Attack/Release/Makeup) with a
  wet/dry Mix for parallel compression.
- **Convolution** -- the same curated 95-IR factory library as
  [MultibandConvolver](../MultibandConvolver) (real rooms/halls/plates/springs, ported wholesale
  -- see `Resources/IRs/CREDITS.md`), picked from a dropdown grouped by category, with a live
  waveform display of the currently-loaded IR, Tone (tilt EQ), Fade In, Fade Out (a decay/length
  control that actually shortens the tail, not just a taper), Stretch (density/length multiplier,
  0.25x-4x), Mix, Output. Prev/Next arrows next to the dropdown step through the catalog by one
  without opening the menu. Drop your own files into the library's Custom folder (the "..."
  button opens it) to have them show up in the picker too. IR-affecting changes are debounced and
  the actual reshape (disk read, resample, fade envelope) runs on a background thread, so
  dragging Stretch or switching IRs never spikes the audio thread.
- **Utility** -- Gain, Pan (balance, for already-stereo material), Width (mid/side stereo width,
  0-200%), Mono, and independent Phase Invert per channel. Mirrors Ableton's Utility device: pure
  gain-staging/imaging, no coloration of its own.
- **Ring Mod / Freq Shift** -- one module, two Modes. Ring Mod multiplies the signal by a sine
  carrier (Frequency/Fine). Freq Shift is a true single-sideband frequency shift, not a pitch
  shift -- it moves every partial by the same Hz amount rather than the same ratio, so harmonic
  content becomes inharmonic (the classic Bode/Moog "shifter" sound), via a zero-latency
  cascaded-allpass Hilbert transformer rather than an FFT/convolution approach.
- **LFO** -- Sine/Triangle/Square/Saw/Sample & Hold shapes, Free (Hz) or host-tempo-Synced rate
  (same note-division table as Delay's sync), and a Depth knob that scales its output across every
  cable it drives. Not an audio node -- see Modulation cables above.
- **Lossy** -- a spectral, "codec-style" lo-fi degradation effect ported from
  [SPANDEX](../RepitchDeck)'s Lossy processor (itself modeled after Goodhertz's Lossy), not a
  time-domain bitcrusher: a streaming STFT quantizes the magnitude spectrum to Bits discrete
  levels and randomizes per-bin phase by Jitter, refreshing that quantized/jittered frame at Rate
  times/second -- low Rate holds old spectral content for a smeared, underwater texture, high Rate
  refreshes almost every hop for a garbled, glitchy one. Mix blends in the spectral domain (not a
  sample-level dry path -- see the module's own comment for why), plus Output.
- **EQ 8** -- a classic 8-band graphic EQ, one octave apart (100Hz-12.8kHz), each a fixed-
  frequency peaking band with just a gain knob (no per-band frequency/Q, unlike a parametric EQ),
  plus Mix/Output.
- **Chorus/Flanger** -- one module, two related characters via a Mode dropdown: Chorus (lush,
  doubling, no feedback) or Flanger (resonant, "jet plane" comb sweep, feedback path active) --
  a real difference in the signal path, not just a label. Rate/Depth/Delay drive a modulated delay
  line per channel (each channel's LFO starts at a different phase for stereo width, no extra
  knob needed); Depth scales as a fraction of Delay itself so the same knob works proportionally
  whether Delay is dialed short (flange) or long (chorus). Feedback (Flanger mode only), Mix,
  Output.
- **EQ 3** -- a simple 3-band tone EQ, EQ 8's lighter sibling: Low Shelf (~150Hz), Mid Bell
  (~1kHz), High Shelf (~4kHz), each just a gain knob, plus Mix/Output.
- **MIDI Mod Matrix** -- its own tab (next to Rack), separate from the canvas: 6 routes, each
  Source (Note Pitch/Velocity/Mod Wheel/2 CC lanes) -> Destination (any slot's Filter Frequency/
  Feedback, Delay Time/Feedback, Waveshaper Drive, or Convolution Mix) -> bipolar Depth.
  Modulation is additive on top of the knob position, never overwrites it. The Safety Limiter
  controls live at the bottom of this tab too (see below) -- it's an on-and-forget setting for
  most users, so it's tucked away here rather than eating space in the canvas header.
- **Safety Limiter** -- a brickwall limiter that always runs last, after the full rack graph,
  regardless of routing. The Waveshaper itself has no ceiling (push it as hard as you want); this
  exists purely to keep an aggressively-driven chain from reaching your speakers/headphones at a
  dangerous level. On by default, adjustable ceiling, can be switched off. Lives at the bottom of
  the Mod Matrix tab.
- **Live output scope** -- a waveform display fed from the true final output (post-chain,
  post-limiter), in the header.
- **Update checker** -- on launch, a background check against this repo's GitHub Releases (fail
  silent -- no network/GitHub-down/rate-limit ever shows an error, it just quietly finds nothing).
  If a newer version is out, the "..." menu button in the header recolors and its menu gets an
  "Update available: vX" item that opens the release page.

Styled to match [SPANDEX](../RepitchDeck) (flat, hairline-bordered, no gradients or shadows).
Not visually polished beyond that yet -- see Known limitations.

## Installing

**Windows**: run `Install GGrid.exe` from the Releases page (or build it yourself, see below),
choose Standalone and/or VST3, confirm the VST3 install location if you use a non-default plugin
folder.

**Mac**: the universal (arm64 + x86_64) build runs via GitHub Actions on a hosted macOS runner --
see `.github/workflows/macos-build.yml`. Trigger it manually from the Actions tab (or
`gh workflow run macos-build.yml`); the resulting zip (VST3 + Standalone + an `IRs/` folder) is
uploaded as a downloadable artifact on the run. Not yet code-signed/notarized, so Gatekeeper will
block it by default -- right-click and choose "Open," or approve it under System Settings >
Privacy & Security after the first blocked attempt. There's no installer yet, so also copy the
zip's `IRs/` folder to `~/Documents/GGrid/IRs` manually -- Convolution's factory library won't
resolve otherwise (see `IRLibrary::resolveIRRoot()`).

## Building from source

Requires CMake 3.22+ and a C++20 compiler (Visual Studio 2022 Build Tools on Windows). JUCE is
fetched automatically via CMake's `FetchContent` on first configure.

```
cmake -B build
cmake --build build --config Release --target GGrid_VST3 GGrid_Standalone
```

Built artifacts land in `build/GGrid_artefacts/Release/`. To build the Windows installer, install
[Inno Setup](https://jrsoftware.org/isinfo.php) and run:

```
ISCC.exe Installer\GGrid.iss
```

There's also an offline DSP verification harness (`Tests/OfflineDspTest.cpp`, target
`OfflineDspTest`) that checks every module's DSP -- boundedness at extreme settings, filter
frequency response, delay timing, compressor gain reduction, convolution IR loading, and the mod
matrix's MIDI-to-parameter routing -- with no audio device or DAW host required to run it:

```
cmake --build build --config Release --target OfflineDspTest
build\Release\OfflineDspTest.exe
```

## Project layout

```
Source/
  PluginProcessor.*, PluginEditor.*   top-level plugin/editor
  Rack/                                RackModule interface, RackSlot (owns/swaps module instances),
                                        ConnectionGraph (2-in/2-out routing graph + topological
                                        order), SharedServices (convolution queue + IR reshape worker)
  Modules/                             WaveshaperModule, FilterModule, DelayModule, DynamicsModule,
                                        ConvolutionModule -- one file pair per rack module type
  IR/                                   IRLibrary (factory catalog + Custom folder), IRProcessor
                                        (stretch/fade shaping), IRReshapeWorker (background thread)
  Modulation/                          ModulationMatrix -- the MIDI mod matrix
  Params/                              parameter ID scheme, APVTS layout builder
  GUI/                                 NodeGraphEditor (the patch-bay canvas), NodeComponent,
                                        ModuleControlPanels (per-module-type controls, reused by
                                        every node), MasterControlsPanel, ModMatrixPanel,
                                        IRWaveformComponent, GGridLookAndFeel
Resources/IRs/                         factory impulse response library (95 IRs + CREDITS.md)
Tests/                                 offline DSP verification harness
Installer/                             Inno Setup installer script
.github/workflows/                     macOS CI build
```

## Architecture notes

- **Routing = flexible rack with a separate connection graph.** Each slot has stable APVTS
  parameter IDs (`slot{n}_moduleType`, `slot{n}_bypass`, `slot{n}_{moduleType}_{param}`) that
  never move. What the canvas edits is a separate, non-automated list of directed edges
  (`GGridAudioProcessor::connections`, `Source/Rack/ConnectionGraph.h`) persisted alongside the
  APVTS state -- this is what lets slots be freely wired/duplicated/branched without fighting
  JUCE's static-parameter-layout requirement or breaking automation/preset recall. Each slot
  allows at most 2 outgoing and 2 incoming edges (matching each node's 2 output nubs / 2 input
  dots) -- enough for parallel chains and merges without a general N-port model. A slot with no
  active predecessor is a root and gets the plugin's raw dry input; a slot with no active
  successor is a sink and its output sums into the final mix; `processBlock` computes a
  topological order (`buildProcessingOrder`, Kahn's algorithm) each block and processes every
  active slot exactly once in dependency order, pulling the sum of each slot's predecessor
  outputs before running it. New connections are rejected at add-time if they'd exceed capacity,
  duplicate an existing edge, or create a cycle (`GGridAudioProcessor::canAddConnection`) --
  block-based processing has no notion of a same-block feedback loop, so the graph must stay
  acyclic.
- **A slot with zero connections is only a root+sink pass-through if it's never been connected.**
  `active[i]` in `processBlock` also checks `GGridAudioProcessor::everConnected[i]` -- a node that
  WAS wired and is now fully disconnected is excluded from the graph entirely (silent) rather than
  defaulting back to root+sink, which would otherwise make it a free-running unpatched parallel
  path invisibly summing its processed output into the mix, indistinguishable at a glance from
  actually being removed from the signal path. `everConnected` is set in `addConnection` and
  cleared via `resetEverConnected()` on any type change (`NodeGraphEditor::
  pruneStaleConnectionsForSlot`), persisted alongside `connections`/`nodePositions`.
- **Modulation is additive**, applied at the point of use inside each destination module, never
  by overwriting the APVTS parameter -- the knob position is always the base value.
- **Two parallel modulation mechanisms coexist by design.** The original enum-keyed
  `ModDestinationParam`/`getOffsetForDestination()` path powers only the fixed 6-route MIDI Mod
  Matrix (its original 6 destinations: Filter Frequency/Feedback, Delay Time/Feedback, Waveshaper
  Drive, Convolution Mix) and was deliberately left untouched when cables were generalized, to
  avoid risking already-shipped, tested behavior. The generic `ModulationMatrix::getOffsetForParam
  (paramId, range)` path is string-APVTS-ID-keyed and powers the cable system's "any knob"
  destinations -- every continuous knob on every module type builds a `ModTarget` list
  (`paramId`/label/`juce::Slider*`) in its control panel's constructor (see `ModTarget` in
  `ModuleControlPanels.h`), and `NodeComponent` exposes it as `getModTargetCount()`/
  `getModTargetParamId(i)`/`getModTargetPosition(i)` so the canvas can place/hit-test one
  destination nub per knob rather than a single fixed one. Modules that had old-mechanism support
  sum both paths (e.g. Filter Frequency reads `getOffsetForDestination(...) +
  getOffsetForParam(...)`) so a knob that's both a MIDI Mod Matrix destination and a cable target
  responds to either.
- **Modulation cables are a second, separate graph** (`ModulationMatrix::modConnections`) from the
  audio `connections` above -- each entry is `{ fromSlot, toSlot, destinationParamId }`, an LFO
  slot's depth-scaled output paired with the exact APVTS parameter ID it targets. Capacity is
  enforced per-destination (one source per knob, keyed on `destinationParamId` alone) rather than
  per-source, since a modulation source usefully fans out to several destinations at once. LFO
  slots are excluded from the audio `connections` graph entirely and ticked in their own pass each
  block, before the audio graph runs, so a destination always reads this block's LFO value
  regardless of where the LFO node happens to fall in audio topological order (which has nothing
  to do with modulation routing) -- see `LFOModule` and `GGridAudioProcessor::processBlock`.
  Persisted state uses `"|"` as the field separator (not `"-"`) since `destinationParamId` is an
  arbitrary parameter-ID string rather than a fixed enum.
- **The safety limiter is not a rack slot** -- it's pinned as the literal last stage in
  `PluginProcessor::processBlock`, specifically so it can't be reordered away from "last."
- **Node canvas is a view onto `connections`, not a separate routing model.** Node existence
  mirrors each slot's type parameter (visible iff type != None); dragging a cable from node A's
  output to node B's input calls `GGridAudioProcessor::addConnection(A, B)`, which validates
  capacity/duplicates/cycles before adding the edge. Node canvas *position* is separate,
  cosmetic-only state (`nodePositions`, persisted alongside `connections`). Both are reconciled
  from processor state on a 20Hz timer in `NodeGraphEditor` rather than fine-grained callbacks,
  since several different things can change them (canvas interaction, automation, undo, project
  load) and polling 8 slots is cheap. Grabbing an existing cable removes its edge immediately (at
  mouseDown, not mouseUp) -- both "rewire" and "drag fresh from an output" then look identical
  from mouseUp onward: add the new edge if a valid target was found, otherwise there's nothing
  left to do.
- **"Unplugging" a cable now genuinely disconnects it** -- with a real graph underneath instead
  of a single ordered list, dropping a grabbed cable on blank space just removes that edge; a
  node with no connections at all still processes (it becomes its own root and sink, taking the
  dry input straight to the mix), it just isn't chained to anything else. Use a node's own Bypass
  toggle if you want it silent instead.
- **Pan/zoom is a Component transform on `NodeGraphEditor` inside a Viewport with scrollbars
  hidden**, not a from-scratch camera system. Zoom (`setTransform(scale(zoom))`) pivots around a
  chosen point by solving for the Viewport scroll position that keeps that canvas point fixed on
  screen; panning drives `Viewport::setViewPosition` directly from click-drag deltas. JUCE
  auto-inverse-transforms incoming mouse events for a transformed component, so none of the node
  drag/cable/add-node math needs to know zoom exists -- it all stays in plain canvas coordinates.

## Known limitations

- GUI aesthetics are still fairly plain (flat SPANDEX styling, no custom skin/textures) -- the
  node-based interaction model is done, but it hasn't had a visual polish pass.
- Each node caps out at 2 outgoing and 2 incoming connections, and the graph must stay acyclic --
  no true feedback loop *across* nodes (a node's own internal feedback, like Delay's, is
  unaffected). This covers standard parallel-chain/group patching but not an arbitrary N-port
  modular grid.
- **Upgrading from a build before this routing rewrite will lose an existing project's wiring.**
  The old `chainOrder` save format was replaced with the new `connections` graph rather than
  migrated (this is still unreleased/actively-iterated software, so there are no real users'
  presets to preserve) -- a project saved with an older build will reopen with its nodes in place
  but disconnected, and need to be rewired.
- The 6000x4000 canvas is generously large but not literally infinite, and a Viewport's scroll
  range doesn't grow with a child's zoom transform -- dragging a node very close to that edge
  and then zooming in past 100% could in principle make a corner hard to reach by panning. Not
  a realistic issue for normal use (nodes cluster near wherever you're actually working).
- The IR library is resolved at runtime from (in order): beside the plugin binary, the dev source
  tree's `Resources/IRs`, or `Documents\GGrid\IRs` (what the Windows installer populates, and what
  the macOS zip's `IRs/` folder needs to be manually copied to until there's a real installer) --
  see `IRLibrary::resolveIRRoot()`.
