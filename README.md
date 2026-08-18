# GGrid

A node-based patch bay -- VST3/Standalone plugin built with JUCE: build a signal chain out
of up to 24 rack slots, drop in whichever modules you want (distortion, filtering, dynamics,
modulation, convolution, and more), wire them together however you like, and shape the sound.

## Features

- **Node-based patch bay** -- right-click blank canvas space to add a node (Waveshaper/Filter/
  Delay/Dynamics/Convolution/Multiband Convolution/Multipass/Utility/Ring Mod/LFO/Envelope/ADSR/
  Lossy/EQ 8/Chorus-Flanger/EQ 3/3xOsc, or Input/Output under their own "I/O" category), drag its title bar to reposition, drag a cable from a node's
  output to another node's input to connect them. Up to 24 modules at once, including duplicates
  of the same type. Input and Output are ordinary addable/deletable node types like any other --
  a small compact box (rather than the wider boxes every other module gets) showing a live
  oscilloscope of the signal passing through it instead of a control panel, since there's nothing
  to configure. Any number of Input/Output nodes can exist at once: every Input provides the same
  raw dry signal, every Output's incoming signal sums into the final mix, so several independent
  racks or parallel signal paths can share one canvas. Each audio node has 4 output nubs and 4
  input dots -- enough to split a signal into several parallel chains and sum them back together,
  not just a single-file chain. For every module except Multipass below, all 4 output nubs carry
  the exact same signal (purely a wiring convenience, so cables fan out to visually distinct dots
  instead of stacking); Multipass is the one exception, where each of its 3 active nubs genuinely
  carries different content (its own frequency band) -- GGrid's first module with real independent
  outputs rather than one signal duplicated across ports.
  A fresh project starts with one Input and one Output already in place, and only the very first
  regular module you add is auto-wired straight through between them so it's audible immediately
  -- every module added after that (including any extra Input/Output nodes) arrives fully
  unpatched, since past the first node there's no single "obvious" default worth guessing (serial?
  parallel? feeding something else entirely?); you wire it in yourself, however you want. Grab an
  *existing* cable (not just a node's output) to rewire it elsewhere, or drop it on blank space to
  genuinely disconnect it. Nothing reaches the speakers unless it's patched all the way from an
  Input node to an Output node -- a node with no path between them (including one that's had every
  connection removed) does nothing, full stop; there's no implicit per-node dry-through. Left-
  click-drag blank canvas to pan (a quick grab-and-go); holding briefly before you drag selects
  instead, opening a rubber band to catch several nodes at once (shift-drag does the same
  instantly, skipping the hold) -- drag any selected node to move the whole selection together,
  and Delete/Backspace (or the Edit menu) removes every selected node. Two-finger/wheel scroll
  pans in whichever direction(s) it reports (no visible scrollbars, so it reads as an open sandbox
  rather than a bounded scrollable area) -- zoom (20%-200%) is a separate gesture: pinch on a
  trackpad, Ctrl+scroll on a plain mouse wheel, or the +/-/reset buttons, so a touchpad's natural
  two-finger scroll doesn't get misread as pinch-to-zoom. Click a node's title bar to select it,
  shift-click to add/remove it from the selection.
- **File/Edit menus** -- File: Init Patch (wipes the rack back to genuinely empty -- every slot,
  connection, and mod cable gone), Save Patch/Load Patch (`.ggridpatch` files, independent of
  whatever preset mechanism the host provides), version/update info. Edit: Copy/Paste/Duplicate
  (Ctrl+C/Ctrl+V/Ctrl+D also work directly on the canvas) and Delete, each grayed out when there's
  nothing to act on. Copying a multi-node selection preserves the audio and modulation cables
  *between* the copied nodes (not connections to anything outside the selection), so duplicating a
  whole wired sub-chain keeps it intact -- pasted/duplicated nodes land unpatched to everything
  else, offset from the originals, ready to wire in wherever you want.
- **Modulation cables** -- LFO/Envelope/ADSR nodes have no audio ports at all (they're modulation
  sources, not audio processors) -- just a single violet output nub, dragged onto a matching
  violet modulation-destination dot on **any** continuous knob any other node exposes (essentially
  every knob in the rack, not a fixed handful -- e.g. every Waveshaper knob, not just Drive). A
  genuinely separate cable graph from the orange audio connections, rendered in violet so the two
  are never ambiguous at a glance; grab an existing mod cable to rewire/disconnect it exactly like
  an audio one. A glowing pulse rides along each mod cable, positioned by the source's live output
  rather than a free-running clock, so its motion visibly tracks what that source is actually
  doing (an LFO's Rate and Shape; an Envelope/ADSR's current playback position) as it travels
  toward the module it's modulating. Modulation is additive on top of the knob position, never
  overwrites it. This generic per-knob system is separate from (and additive with) the fixed
  6-destination MIDI Mod Matrix below -- a knob that's a MIDI Mod Matrix destination can be nudged
  by both at once.
- **Waveshaper/Wavefolder** -- Hard Clip, Soft Clip (tanh/cubic), Foldback Wavefolder, Sine Fold,
  Rectify/Asymmetric, with Drive, Symmetry, Fold Amount, 2x/4x oversampling, Mix, Output, and a
  live transfer-curve preview showing the current shape/symmetry/fold as an actual bent line
  rather than just numbers on the knobs.
- **Filter** -- one module, 10 algorithms via a Type dropdown: Low Pass / High Pass / Band Pass /
  Notch (resonant biquads), Comb (Feedback), Comb (Feedforward), Allpass Diffusor (Schroeder
  allpass -- the classic reverb-diffusion building block), Ladder Low Pass / Ladder High Pass
  (a saturating 4-stage nonlinear-feedback ladder -- the warm, self-oscillating character a clean
  biquad can't produce, Resonance driving how hard it pushes into distortion rather than a clean
  Q), and Formant (three parallel resonant peaks blended by vowel position -- Frequency is
  repurposed as a sweep through A/E/I/O/U rather than a literal cutoff).
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
- **Multiband Convolution** -- ported from [MultibandConvolver](../MultibandConvolver), scoped to
  a fixed 3 bands (Low/Mid/High) so it fits the rack's node format: an LR4 (24dB/oct) crossover
  splits the signal at 2 split points, shown as draggable markers on a frequency strip across the
  top of the node (drag them directly rather than turning a knob -- the strip is log-frequency-
  mapped 20Hz-20kHz, same range the split parameters themselves cover). That same strip doubles as
  a Low/Mid/High tab switcher -- click a band to select it (the lit-up region shows which is
  active) and the single IR picker + Tone/Fade In/Fade Out/Stretch/Mix/Output knob set below
  retargets to that band's own parameters, matching the original MultibandConvolver desktop app's
  tabbed layout rather than showing all 3 bands' controls at once. Each band still runs its own
  full independent copy of Convolution's engine internally and they sum back together at the
  output -- only the on-screen controls are shared, not the underlying processing.
- **Multipass** -- a 3-band (Low/Mid/High) crossover splitter with no per-band effect of its own
  and, unlike Multiband Convolution above, no recombining at the end: each band exits through its
  own dedicated output nub instead, so you can route Low/Mid/High to three completely different
  downstream chains and process each individually -- the whole point of the module. Same
  draggable-split-point frequency strip as Multiband Convolution (drag the 2 markers directly,
  log-frequency-mapped), plus one Mix knob per band blending that band's isolated crossover-
  filtered content against the original pre-split full-spectrum signal. Only 3 of its 4 output
  nubs are shown/active, one per band -- see the Node-based patch bay note above on why this is
  the one module whose ports genuinely differ rather than all carrying the same signal.
- **Utility** -- Gain, Pan (balance, for already-stereo material), Width (mid/side stereo width,
  0-200%), Mono, and independent Phase Invert per channel. Mirrors Ableton's Utility device: pure
  gain-staging/imaging, no coloration of its own.
- **Ring Mod / Freq Shift** -- one module, two Modes. Ring Mod multiplies the signal by a sine
  carrier (Frequency/Fine). Freq Shift is a true single-sideband frequency shift, not a pitch
  shift -- it moves every partial by the same Hz amount rather than the same ratio, so harmonic
  content becomes inharmonic (the classic Bode/Moog "shifter" sound), via a zero-latency
  cascaded-allpass Hilbert transformer rather than an FFT/convolution approach.
- **LFO** -- Sine/Triangle/Square/Saw/Sample & Hold/Ramp Up/Ramp Down shapes, Free (Hz) or
  host-tempo-Synced rate (same note-division table as Delay's sync), and a Depth knob that scales
  its output across every cable it drives. Not an audio node -- see Modulation cables above.
- **Envelope** -- a freeform-breakpoint modulation source: click empty canvas space on the editor
  to add a point, drag a point to move it, right-click to delete it (the first/last points are
  pinned at the start/end and can't be deleted, so the drawn shape always spans the full Length).
  Plays the shape once per MIDI note-on, linearly interpolating between points over the Length
  knob's duration, then holds at the final point's value regardless of how long the note's held or
  released -- note-off is ignored entirely, unlike ADSR below. Depth scales the output. Not an
  audio node -- see Modulation cables above.
- **ADSR** -- a standalone Attack/Decay/Sustain/Release envelope, usable as a modulation source
  for any knob in the rack (not just baked into an instrument) -- sustains while a note is held
  and releases on note-off, the classic synth-envelope behaviour Envelope above deliberately
  doesn't have. Mono: releasing one note out of a held chord doesn't cut it short as long as
  another note is still held. Not an audio node -- see Modulation cables above.
- **Lossy** -- a spectral, "codec-style" lo-fi degradation effect ported from
  [SPANDEX](../RepitchDeck)'s Lossy processor (itself modeled after Goodhertz's Lossy), not a
  time-domain bitcrusher: a streaming STFT quantizes the magnitude spectrum to Bits discrete
  levels and randomizes per-bin phase by Jitter, refreshing that quantized/jittered frame at Rate
  times/second -- low Rate holds old spectral content for a smeared, underwater texture, high Rate
  refreshes almost every hop for a garbled, glitchy one. Mix blends in the spectral domain (not a
  sample-level dry path -- see the module's own comment for why), plus Output.
- **EQ 8** -- a draggable graphic EQ (Ableton EQ Eight / SPANDEX EQ style): 8 always-on band nodes
  plotted on a log-frequency x / linear-dB y curve, each independently Enabled/Type (Bell, Low
  Shelf, High Shelf, High Pass, Low Pass, Notch, Band Pass)/Frequency/Gain/Q. Drag a node to move
  its Frequency (always) and Gain (only for the 3 gain-shaped types -- Bell/Low Shelf/High Shelf;
  the other 4 have no meaningful gain, so dragging those only moves Frequency), scroll while
  hovering a node to adjust its Q, click a node to select it -- a knob strip below shows the
  selected band's exact Enable/Type/Freq/Gain/Q values and retargets whenever you pick a different
  band, the numeric fallback to the curve's drag gestures. A stroked curve shows the combined
  response of every enabled band. Plus Mix/Output.
- **Chorus/Flanger** -- one module, two related characters via a Mode dropdown: Chorus (lush,
  doubling, no feedback) or Flanger (resonant, "jet plane" comb sweep, feedback path active) --
  a real difference in the signal path, not just a label. Rate/Depth/Delay drive a modulated delay
  line per channel (each channel's LFO starts at a different phase for stereo width, no extra
  knob needed); Depth scales as a fraction of Delay itself so the same knob works proportionally
  whether Delay is dialed short (flange) or long (chorus). Feedback (Flanger mode only), Mix,
  Output.
- **EQ 3** -- a simple 3-band tone EQ, EQ 8's lighter sibling: Low Shelf (~150Hz), Mid Bell
  (~1kHz), High Shelf (~4kHz), each just a gain knob, plus Mix/Output.
- **3xOsc** -- a 16-voice polyphonic MIDI synth inspired by FL Studio's 3xOsc, GGrid's first
  generator module. Unlike every other node, it doesn't process incoming audio at all -- it's a
  graph *source* like Input (no input ports, ignores anything patched into it), generating its own
  sound from the MIDI notes reaching the plugin. 3 independently tuned oscillators per voice
  (Waveform, Coarse/Fine tune, Pan, Level), each phase-modulating the next (FM 1>2, FM 2>3) for
  DX7-ish timbres, shaped by a shared Attack/Decay/Sustain/Release amp envelope, plus Output. Saw/
  Square are PolyBLEP-corrected against audio-rate aliasing. Voice-stealing kicks in past 16
  simultaneous notes (oldest triggered first). Mono/Legato collapses it down to a single voice --
  a new note while another's held retargets that one voice's pitch instead of triggering fresh (no
  envelope retrigger, no phase reset), last-note priority when you release one note out of an
  overlapping run. Glide (with an ms Glide Time knob underneath) makes that retargeting a smooth
  portamento slide in semitone space instead of an instant snap; it's off by default and only
  affects Mono/Legato's retargeting -- polyphonic voices always start directly at their own note's
  pitch. Lives in its own "Generators" category in the add-node menu; wire it straight to an
  Output node (or anywhere else) -- no Input node needed upstream, since it doesn't use one.
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
- **Save/Load Patch** -- in the "..." header menu: writes/reads the full plugin state (every
  module's params, the connection graph, node positions) to/from a `.ggridpatch` file, defaulting
  to `Documents/GGrid/Patches`. Independent of whatever preset mechanism the host provides --
  Standalone mode has none at all -- since both just call the same get/setStateInformation pair a
  host already uses for its own preset save/recall.
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

- **Routing = flexible rack with a separate connection graph, where Input/Output are just two more
  addable module types.** Each slot has stable APVTS parameter IDs (`slot{n}_moduleType`,
  `slot{n}_bypass`, `slot{n}_{moduleType}_{param}`) that never move -- this now includes
  `ModuleType::input`/`output`, appended to the same type enum as every DSP module, rather than
  living as fixed pseudo-nodes outside the 24-slot array the way an earlier iteration of this
  worked. What the canvas edits is a separate, non-automated list of directed edges
  (`GGridAudioProcessor::connections`, `Source/Rack/ConnectionGraph.h`) persisted alongside the
  APVTS state -- this is what lets slots be freely wired/duplicated/branched without fighting
  JUCE's static-parameter-layout requirement or breaking automation/preset recall. Each slot
  allows at most `kMaxPortsPerSide` (4) outgoing and 4 incoming edges (matching each node's 4
  output nubs / 4 input dots) -- enough for real parallel-processing patches without a general
  N-port model. Every slot currently typed Input is a root (seeded with the plugin's raw dry input
  every block, regardless of what's patched into it) and every slot typed Output is a sink (its
  summed input contributes to the final mix) -- there can be any number of each at once. A regular
  module is never an implicit root or sink; if it has no path from any Input it simply doesn't
  run, matching Bitwig Grid's fully-explicit patching model. `processBlock` computes a topological
  order (`buildProcessingOrder`, Kahn's algorithm seeded from every Input-role slot at once, with
  a separate reachability pass so a node genuinely unpatched from any Input is excluded even by
  the defensive cycle fallback) each block and processes every reachable node exactly once in
  dependency order, pulling the sum of each node's predecessor outputs before running it. New
  connections are rejected at add-time if they'd exceed capacity, duplicate an existing edge,
  create a cycle, point *into* an Input-type slot, or point *out of* an Output-type slot
  (`GGridAudioProcessor::canAddConnection`) -- block-based processing has no notion of a
  same-block feedback loop, so the graph must stay acyclic. A fresh instance seeds slot 0 as Input
  and slot 1 as Output by default (see the constructor) so a first real module still auto-wires
  immediately; loading a project overwrites this via `apvts.replaceState()` like any other default
  parameter value. Saves from before Input/Output became addable module types aren't migrated
  (this is still actively-iterated pre-release software) -- their connections reference slot
  indices that predate the current model and are silently dropped on load, so such a save reopens
  with its modules in place but disconnected.
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
  load) and polling this many slots is cheap. Grabbing an existing cable removes its edge immediately (at
  mouseDown, not mouseUp) -- both "rewire" and "drag fresh from an output" then look identical
  from mouseUp onward: add the new edge if a valid target was found, otherwise there's nothing
  left to do.
- **"Unplugging" a cable now genuinely disconnects it** -- dropping a grabbed cable on blank space
  just removes that edge; a node with no path from Input simply doesn't run at all (see above),
  it isn't an implicit standalone pass-through the way a slot used to be. Use a node's own Bypass
  toggle instead if you want a *patched* node silent without removing it from the chain.
- **Pan/zoom is a Component transform on `NodeGraphEditor` inside a Viewport with scrollbars
  hidden**, not a from-scratch camera system. Zoom (`setTransform(scale(zoom))`) pivots around a
  chosen point by solving for the Viewport scroll position that keeps that canvas point fixed on
  screen; panning drives `Viewport::setViewPosition` directly from click-drag deltas. JUCE
  auto-inverse-transforms incoming mouse events for a transformed component, so none of the node
  drag/cable/add-node math needs to know zoom exists -- it all stays in plain canvas coordinates.

## Known limitations

- GUI aesthetics are still fairly plain (flat SPANDEX styling, no custom skin/textures) -- the
  node-based interaction model is done, but it hasn't had a visual polish pass.
- Each node caps out at 4 outgoing and 4 incoming connections, and the graph must stay acyclic --
  no true feedback loop *across* nodes (a node's own internal feedback, like Delay's, is
  unaffected). This covers real parallel-chain/group patching but not an arbitrary N-port modular
  grid.
- The 6000x4000 canvas is generously large but not literally infinite, and a Viewport's scroll
  range doesn't grow with a child's zoom transform -- dragging a node very close to that edge
  and then zooming in past 100% could in principle make a corner hard to reach by panning. Not
  a realistic issue for normal use (nodes cluster near wherever you're actually working).
- The IR library is resolved at runtime from (in order): beside the plugin binary, the dev source
  tree's `Resources/IRs`, or `Documents\GGrid\IRs` (what the Windows installer populates, and what
  the macOS zip's `IRs/` folder needs to be manually copied to until there's a real installer) --
  see `IRLibrary::resolveIRRoot()`.
