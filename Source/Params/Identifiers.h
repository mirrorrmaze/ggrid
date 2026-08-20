#pragma once

#include <juce_core/juce_core.h>
#include <array>

// Parameter ID scheme: "slot{n}_type" / "slot{n}_bypass" pick and bypass whichever module
// occupies rack slot n; "slot{n}_{moduleType}_{paramName}" are that module type's own params,
// pre-declared for every slot regardless of the slot's current type so a slot's parameter
// identity (and therefore DAW automation/preset recall) never moves when its type changes.
// Chain order (which slot runs first/second/...) is NOT a parameter -- it's structural state,
// see PluginProcessor::getStateInformation.
namespace GGrid
{
    constexpr int kMaxSlots = 24;

    // Appended-only: never insert or reorder entries, only add new ones at the end, so a
    // saved slot-type choice index keeps meaning the same thing across plugin versions.
    enum class ModuleType
    {
        none = 0,
        waveshaper = 1,
        filter = 2,
        delay = 3,
        dynamics = 4,
        convolution = 5,
        utility = 6,
        ringMod = 7,
        lfo = 8,
        lossy = 9,
        eq8 = 10,
        chorus = 11,
        eq3 = 12,
        input = 13,
        output = 14,
        multibandConvolution = 15,
        threeOsc = 16,
        envelope = 17,
        adsr = 18,
        multipass = 19,
        lfoTable = 20,
        wavetableSynth = 21,
        nonlinearFilter = 22,
        mackity = 23,
        shimmerReverb = 24,
        spectralClipper = 25,
    };

    // Modulation SOURCE types -- LFO, Envelope, and ADSR alike have no audio ports at all and
    // aren't part of the audio ConnectionGraph (see GGridAudioProcessor::processBlock's active[]
    // exclusion); each produces a per-block scalar fed into ModulationMatrix via
    // RackModule::getCurrentModulationValue(), and shows a single mod-output nub in the GUI
    // instead of the normal 4 audio-output nubs (see NodeComponent::isModulationSourceType()).
    inline bool isModulationSourceType (ModuleType type)
    {
        return type == ModuleType::lfo || type == ModuleType::envelope || type == ModuleType::adsr
               || type == ModuleType::lfoTable;
    }

    inline juce::StringArray getModuleTypeChoices()
    {
        return { "None", "Waveshaper", "Filter", "Delay", "Dynamics", "Convolution", "Utility", "Ring Mod", "LFO",
                 "Lossy", "EQ 8", "Chorus/Flanger", "EQ 3", "Input", "Output", "Multiband Convolution", "3xOsc",
                 "Envelope", "ADSR", "Multipass", "LFO Table", "WT Synth", "Nonlinear Filter", "Mackity", "Shimmer Reverb",
                 "Spectral Clipper" };
    }

    inline juce::String slotTypeParamId (int slotIndex)   { return "slot" + juce::String (slotIndex) + "_type"; }
    inline juce::String slotBypassParamId (int slotIndex) { return "slot" + juce::String (slotIndex) + "_bypass"; }

    inline juce::String waveshaperParamId (int slotIndex, const juce::String& paramName)
    {
        return "slot" + juce::String (slotIndex) + "_waveshaper_" + paramName;
    }

    namespace WaveshaperParam
    {
        static const juce::String drive      = "drive";
        static const juce::String shape      = "shape";
        static const juce::String symmetry   = "symmetry";
        static const juce::String foldAmount = "fold";
        static const juce::String oversample = "oversample";
        static const juce::String mix        = "mix";
        static const juce::String output     = "output";
    }

    inline juce::StringArray getWaveshaperShapeChoices()
    {
        return { "Hard Clip", "Soft Clip (tanh)", "Soft Clip (cubic)", "Foldback Wavefolder", "Sine Fold", "Rectify/Asymmetric" };
    }

    inline juce::String filterParamId (int slotIndex, const juce::String& paramName)
    {
        return "slot" + juce::String (slotIndex) + "_filter_" + paramName;
    }

    namespace FilterParam
    {
        static const juce::String frequency  = "freq";
        static const juce::String type       = "type";
        static const juce::String resonance  = "resonance";
        static const juce::String drive      = "drive";
        static const juce::String feedback   = "feedback";
        static const juce::String mix        = "mix";
        static const juce::String output     = "output";
    }

    // First 4 are biquad SVF-style modes (Frequency/Resonance); next 3 are delay-line based
    // (Frequency maps to pitch via sampleRate/freq, Feedback controls their character). The
    // Allpass Diffusor is a Schroeder allpass -- the classic building block of algorithmic
    // reverb diffusion, standing in for a dedicated "reverb filter" until the convolution
    // module lands. Last 3 (Serum-2-inspired) are their own DSP category: Ladder Low/High Pass
    // are a saturating 4-stage nonlinear-feedback ladder (Frequency/Resonance, Resonance driving
    // how hard the feedback path saturates rather than a clean Q), Formant repurposes Frequency
    // as a vowel-sweep position (A->E->I->O->U) through a bank of resonant peaks rather than a
    // literal cutoff -- see FilterModule for all three.
    inline juce::StringArray getFilterTypeChoices()
    {
        return { "Low Pass", "High Pass", "Band Pass", "Notch", "Comb (Feedback)", "Comb (Feedforward)", "Allpass (Diffusor)",
                 "Ladder Low Pass", "Ladder High Pass", "Formant" };
    }

    inline juce::String delayParamId (int slotIndex, const juce::String& paramName)
    {
        return "slot" + juce::String (slotIndex) + "_delay_" + paramName;
    }

    inline juce::String nonlinearFilterParamId (int slotIndex, const juce::String& paramName)
    {
        return "slot" + juce::String (slotIndex) + "_nonlinear_filter_" + paramName;
    }

    namespace NonlinearFilterParam
    {
        static const juce::String frequency = "freq";
        static const juce::String resonance = "resonance";
        static const juce::String drive     = "drive";
        static const juce::String morph     = "morph";
        static const juce::String mode      = "mode";
        static const juce::String distortion = "distortion";
        static const juce::String mix       = "mix";
        static const juce::String output    = "output";
    }

    inline juce::StringArray getNonlinearFilterModeChoices()
    {
        return { "Low Pass", "High Pass", "Band Pass", "Notch" };
    }

    inline juce::StringArray getNonlinearFilterDistortionChoices()
    {
        return { "Soft Clip", "Hard Clip", "Sine Fold", "Foldback", "Asymmetric", "Warm", "Saturated", "Bias", "Clipped" };
    }

    inline juce::String mackityParamId (int slotIndex, const juce::String& paramName)
    {
        return "slot" + juce::String (slotIndex) + "_mackity_" + paramName;
    }

    namespace MackityParam
    {
        static const juce::String input  = "input";
        static const juce::String pad    = "pad";
        static const juce::String mix    = "mix";
        static const juce::String output = "output";
    }

    inline juce::String shimmerReverbParamId (int slotIndex, const juce::String& paramName)
    {
        return "slot" + juce::String (slotIndex) + "_shimmer_reverb_" + paramName;
    }

    namespace ShimmerReverbParam
    {
        static const juce::String size      = "size";
        static const juce::String feedback  = "feedback";
        static const juce::String diffusion = "diffusion";
        static const juce::String shift     = "shift";
        static const juce::String pitchMode = "pitchMode";
        static const juce::String color     = "color";
        static const juce::String modRate   = "modRate";
        static const juce::String modDepth  = "modDepth";
        static const juce::String lowCut    = "lowCut";
        static const juce::String highCut   = "highCut";
        static const juce::String mix       = "mix";
        static const juce::String output    = "output";
    }

    inline juce::StringArray getShimmerReverbPitchModeChoices()
    {
        return { "Bypass", "Single", "Dual", "Reverse", "Dual Reverse" };
    }

    inline juce::StringArray getShimmerReverbColorChoices()
    {
        return { "Bright", "Dark" };
    }

    namespace DelayParam
    {
        static const juce::String time       = "time";
        static const juce::String sync       = "sync";
        static const juce::String division   = "division";
        static const juce::String feedback   = "feedback";
        static const juce::String saturation = "saturation";
        static const juce::String lowCut     = "lowCut";
        static const juce::String hiCut      = "hiCut";
        static const juce::String pingPong   = "pingPong";
        static const juce::String mix        = "mix";
        static const juce::String output     = "output";
    }

    // Multiplier applied to a quarter-note length (60000/bpm ms) to get the synced delay time.
    // Dotted = *1.5, triplet = *2/3, matching standard DAW delay-sync conventions.
    inline juce::StringArray getDelayDivisionChoices()
    {
        return { "1/1", "1/2", "1/4", "1/8", "1/16", "1/32", "1/4.", "1/8.", "1/16.", "1/4T", "1/8T", "1/16T" };
    }

    inline float getDelayDivisionMultiplier (int index)
    {
        static const float multipliers[] = { 4.0f, 2.0f, 1.0f, 0.5f, 0.25f, 0.125f,
                                              1.5f, 0.75f, 0.375f,
                                              (2.0f / 3.0f), (1.0f / 3.0f), (1.0f / 6.0f) };
        return multipliers[(size_t) juce::jlimit (0, 11, index)];
    }

    inline juce::String dynamicsParamId (int slotIndex, const juce::String& paramName)
    {
        return "slot" + juce::String (slotIndex) + "_dynamics_" + paramName;
    }

    namespace DynamicsParam
    {
        static const juce::String threshold = "threshold";
        static const juce::String ratio     = "ratio";
        static const juce::String attack    = "attack";
        static const juce::String release   = "release";
        static const juce::String makeup    = "makeup";
        static const juce::String mix       = "mix";
    }

    inline juce::String convolutionParamId (int slotIndex, const juce::String& paramName)
    {
        return "slot" + juce::String (slotIndex) + "_convolution_" + paramName;
    }

    // irIndex picks from IRLibrary::getCatalog() (factory library + anything dropped into the
    // Custom folder) -- see IR/IRLibrary.h. No "IR file path" parameter, since a catalog index
    // is stable/automatable the same way any other choice parameter is.
    namespace ConvolutionParam
    {
        static const juce::String irIndex  = "irIndex";
        static const juce::String tone     = "tone";
        static const juce::String fadeIn   = "fadeIn";
        static const juce::String fadeOut  = "fadeOut";
        static const juce::String stretch  = "stretch";
        static const juce::String mix      = "mix";
        static const juce::String output   = "output";
    }

    inline juce::String utilityParamId (int slotIndex, const juce::String& paramName)
    {
        return "slot" + juce::String (slotIndex) + "_utility_" + paramName;
    }

    // Mirrors Ableton's Utility device: pure gain-staging/imaging, no coloration.
    namespace UtilityParam
    {
        static const juce::String gain        = "gain";
        static const juce::String pan         = "pan";
        static const juce::String width       = "width";
        static const juce::String mono        = "mono";
        static const juce::String phaseInvertL = "phaseInvertL";
        static const juce::String phaseInvertR = "phaseInvertR";
    }

    inline juce::String ringModParamId (int slotIndex, const juce::String& paramName)
    {
        return "slot" + juce::String (slotIndex) + "_ringMod_" + paramName;
    }

    namespace RingModParam
    {
        static const juce::String mode      = "mode";
        static const juce::String frequency = "frequency";
        static const juce::String fine      = "fine";
        static const juce::String mix       = "mix";
        static const juce::String output    = "output";
    }

    // Ring Mod: plain multiply by a sine carrier. Freq Shift: true single-sideband shift (via a
    // Hilbert transform), not a pitch shift -- moves every partial by the same Hz amount rather
    // than the same ratio, so harmonic content becomes inharmonic (the classic "shifter" sound).
    inline juce::StringArray getRingModModeChoices()
    {
        return { "Ring Mod", "Freq Shift" };
    }

    inline juce::String lfoParamId (int slotIndex, const juce::String& paramName)
    {
        return "slot" + juce::String (slotIndex) + "_lfo_" + paramName;
    }

    namespace LfoParam
    {
        static const juce::String shape    = "shape";
        static const juce::String rateMode = "rateMode";
        static const juce::String rateHz   = "rateHz";
        static const juce::String division = "division";
        static const juce::String depth    = "depth";
    }

    // Append-only, like ModuleType -- a saved shape choice index must keep meaning the same thing
    // across versions. Ramp Up/Ramp Down are separate entries from Saw (mathematically identical
    // to Ramp Up) rather than a rename, so existing automation pointing at Saw's index 3 isn't
    // disturbed.
    inline juce::StringArray getLfoShapeChoices()
    {
        return { "Sine", "Triangle", "Square", "Saw", "Sample & Hold", "Ramp Up", "Ramp Down", "Custom" };
    }

    inline juce::String lossyParamId (int slotIndex, const juce::String& paramName)
    {
        return "slot" + juce::String (slotIndex) + "_lossy_" + paramName;
    }

    // A spectral "codec-style" lo-fi degradation effect (ported from SPANDEX's LossyProcessor) --
    // see LossyModule's class comment for the full algorithm.
    namespace LossyParam
    {
        static const juce::String bits    = "bits";
        static const juce::String rate    = "rate";
        static const juce::String jitter  = "jitter";
        static const juce::String mix     = "mix";
        static const juce::String output  = "output";
    }

    inline juce::String spectralClipperParamId (int slotIndex, const juce::String& paramName)
    {
        return "slot" + juce::String (slotIndex) + "_spectralClipper_" + paramName;
    }

    // Clips in the frequency domain rather than the time domain: an STFT (same window/hop as
    // Lossy) transforms each block to its magnitude/phase spectrum, Ceiling caps each bin's
    // magnitude (Shape decides how the overshoot above that ceiling gets handled), then an
    // inverse transform reconstructs the signal. Distinct in character from Waveshaper's
    // time-domain clipping -- the nonlinearity acts per-frequency-bin instead of on the combined
    // waveform, so it doesn't generate the same broadband harmonic series a sample-domain clip
    // does. See SpectralClipperModule.
    namespace SpectralClipperParam
    {
        static const juce::String drive   = "drive";
        static const juce::String ceiling = "ceiling";
        static const juce::String shape   = "shape";
        static const juce::String mix     = "mix";
        static const juce::String output  = "output";
    }

    inline juce::StringArray getSpectralClipperShapeChoices()
    {
        return { "Hard", "Soft (tanh)", "Foldback", "Sine Fold" };
    }

    inline juce::String eq8ParamId (int slotIndex, const juce::String& paramName)
    {
        return "slot" + juce::String (slotIndex) + "_eq8_" + paramName;
    }

    // A draggable graphic EQ, Ableton EQ Eight / SPANDEX-style: 8 always-present bands, each with
    // its own Frequency/Q/Type/Enabled alongside the original Gain -- click-and-drag a band's dot
    // on Eq8CurveEditor to move Frequency (always) and Gain (only for gain-shaped types --
    // Bell/Low Shelf/High Shelf, see bandTypeHasGain in Eq8CurveEditor.cpp), scroll while
    // hovering to adjust Q. Frequency/Q/Type/Enabled are all NEW parameters added alongside the
    // original fixed-frequency-ladder gain-only scheme (band1..band8, kept exactly as before for
    // backward compatibility -- an old save with no value for the new params just gets their
    // defaults, see addEq8Params, which reproduce the old fixed-ladder/always-on/Bell-shaped
    // behaviour exactly).
    namespace Eq8Param
    {
        static const juce::String band1  = "band1";  // Gain, dB
        static const juce::String band2  = "band2";
        static const juce::String band3  = "band3";
        static const juce::String band4  = "band4";
        static const juce::String band5  = "band5";
        static const juce::String band6  = "band6";
        static const juce::String band7  = "band7";
        static const juce::String band8  = "band8";
        static const juce::String mix    = "mix";
        static const juce::String output = "output";
    }

    constexpr int kNumEq8Bands = 8;
    // Only used as each band's DEFAULT frequency now (old saves' bands land exactly where the
    // fixed ladder always put them) -- freely draggable afterward, unlike before this existed.
    constexpr std::array<float, kNumEq8Bands> kEq8BandFrequencies {
        100.0f, 200.0f, 400.0f, 800.0f, 1600.0f, 3200.0f, 6400.0f, 12800.0f
    };
    inline juce::StringArray getEq8BandLabels()
    {
        return { "100", "200", "400", "800", "1.6k", "3.2k", "6.4k", "12.8k" };
    }
    inline const juce::String& eq8BandParam (int bandIndex)
    {
        static const juce::String bandParams[kNumEq8Bands] = {
            Eq8Param::band1, Eq8Param::band2, Eq8Param::band3, Eq8Param::band4,
            Eq8Param::band5, Eq8Param::band6, Eq8Param::band7, Eq8Param::band8
        };
        return bandParams[(size_t) juce::jlimit (0, kNumEq8Bands - 1, bandIndex)];
    }

    // Bell is index 0 (the default Type for a band with no saved value) since a peaking filter is
    // exactly what every band always was before Type existed -- an old save's bands sound
    // identical to before, just now also draggable/re-typeable. High Pass/Low Pass/Notch/Band
    // Pass ignore Gain entirely (see bandTypeHasGain).
    inline juce::StringArray getEq8FilterTypeChoices()
    {
        return { "Bell", "Low Shelf", "High Shelf", "High Pass", "Low Pass", "Notch", "Band Pass" };
    }

    inline juce::String eq8BandFreqParamId (int slotIndex, int bandIndex)
    {
        return "slot" + juce::String (slotIndex) + "_eq8_band" + juce::String (bandIndex) + "Freq";
    }
    inline juce::String eq8BandQParamId (int slotIndex, int bandIndex)
    {
        return "slot" + juce::String (slotIndex) + "_eq8_band" + juce::String (bandIndex) + "Q";
    }
    inline juce::String eq8BandTypeParamId (int slotIndex, int bandIndex)
    {
        return "slot" + juce::String (slotIndex) + "_eq8_band" + juce::String (bandIndex) + "Type";
    }
    inline juce::String eq8BandEnabledParamId (int slotIndex, int bandIndex)
    {
        return "slot" + juce::String (slotIndex) + "_eq8_band" + juce::String (bandIndex) + "Enabled";
    }

    inline juce::String chorusParamId (int slotIndex, const juce::String& paramName)
    {
        return "slot" + juce::String (slotIndex) + "_chorus_" + paramName;
    }

    // One modulated delay line per channel; Mode picks Chorus (no feedback path -- lush,
    // doubling character) or Flanger (feedback path active -- resonant, "jet plane" comb sweep).
    // Depth swings the delay time as a fraction of Delay itself (see ChorusModule::process), so
    // the same knob works proportionally whether Delay is dialed short (flange) or long (chorus).
    namespace ChorusParam
    {
        static const juce::String mode     = "mode";
        static const juce::String rate     = "rate";
        static const juce::String depth    = "depth";
        static const juce::String delay    = "delay";
        static const juce::String feedback = "feedback";
        static const juce::String mix      = "mix";
        static const juce::String output   = "output";
    }

    inline juce::StringArray getChorusModeChoices()
    {
        return { "Chorus", "Flanger" };
    }

    inline juce::String eq3ParamId (int slotIndex, const juce::String& paramName)
    {
        return "slot" + juce::String (slotIndex) + "_eq3_" + paramName;
    }

    // A simple 3-band tone EQ (Low Shelf / Mid Bell / High Shelf), each just a gain knob --
    // EQ 8's lighter sibling. See Eq3Module for the exact filter frequencies/Q.
    namespace Eq3Param
    {
        static const juce::String low    = "low";
        static const juce::String mid    = "mid";
        static const juce::String high   = "high";
        static const juce::String mix    = "mix";
        static const juce::String output = "output";
    }

    // A fixed 3-band (Low/Mid/High) multiband convolution: an LR4 crossover splits the signal at
    // 2 draggable split points, each band running its own independent copy of the Convolution
    // module's engine (IR select -> Tone -> Fade In/Fade Out/Stretch reshape -> Mix -> Output),
    // recombined at the output -- ported from MultibandConvolver (D:\Claude Projects\
    // MultibandConvolver\Source\DSP\CrossoverSplitter.{h,cpp}/BandChain.{h,cpp}), scoped down
    // from that project's up-to-8-band/12-macro-per-band desktop UI to a fixed 3 bands so it fits
    // GGrid's compact node-panel format. See MultibandConvolutionModule.
    constexpr int kNumConvolutionBands = 3;
    inline juce::StringArray getConvolutionBandLabels() { return { "Low", "Mid", "High" }; }

    inline juce::String multibandConvolutionParamId (int slotIndex, const juce::String& paramName)
    {
        return "slot" + juce::String (slotIndex) + "_multibandConvolution_" + paramName;
    }

    // The 2 crossover split points (band 0/1 boundary, band 1/2 boundary) -- shared across all 3
    // bands, not per-band, since they define where bands begin/end rather than belonging to any
    // one of them. Dragged directly on-canvas via CrossoverSplitBar rather than a knob.
    namespace MultibandConvolutionParam
    {
        static const juce::String splitHz1 = "splitHz1"; // Low/Mid boundary
        static const juce::String splitHz2 = "splitHz2"; // Mid/High boundary
    }

    inline juce::String multibandConvolutionBandParamId (int slotIndex, int bandIndex, const juce::String& paramName)
    {
        return "slot" + juce::String (slotIndex) + "_multibandConvolution_band" + juce::String (bandIndex) + "_" + paramName;
    }

    // Mirrors ConvolutionParam exactly, one full set per band.
    namespace MultibandConvolutionBandParam
    {
        static const juce::String irIndex = "irIndex";
        static const juce::String tone    = "tone";
        static const juce::String fadeIn  = "fadeIn";
        static const juce::String fadeOut = "fadeOut";
        static const juce::String stretch = "stretch";
        static const juce::String mix     = "mix";
        static const juce::String output  = "output";
    }

    // A 16-voice polyphonic MIDI synth (inspired by FL Studio's 3xOsc): 3 oscillators per voice,
    // each independently tuned/panned/leveled, phase-modulated by the previous oscillator (FM
    // 1->2, FM 2->3), shaped by a shared per-voice ADSR amp envelope. Unlike every other module
    // type, a ThreeOsc slot is a graph SOURCE (like Input) rather than a signal processor -- it
    // ignores whatever audio reaches it and generates its own from the MIDI notes in the same
    // MidiBuffer every RackModule already receives (see ThreeOscModule). See
    // GGridAudioProcessor::processBlock's isSourceRole handling and RackSlot::createModuleForType.
    constexpr int kNumThreeOscOscillators = 3;
    constexpr int kMaxThreeOscVoices = 16;
    inline juce::StringArray getThreeOscWaveformChoices() { return { "Sine", "Triangle", "Saw", "Square" }; }
    inline juce::StringArray getThreeOscOscLabels() { return { "Osc 1", "Osc 2", "Osc 3" }; }

    inline juce::String threeOscParamId (int slotIndex, const juce::String& paramName)
    {
        return "slot" + juce::String (slotIndex) + "_threeOsc_" + paramName;
    }

    // Shared per-voice ADSR amp envelope, the two inter-oscillator FM depths, and the overall
    // output trim -- not per-oscillator, these shape the voice as a whole.
    namespace ThreeOscParam
    {
        static const juce::String attack  = "attack";
        static const juce::String decay   = "decay";
        static const juce::String sustain = "sustain";
        static const juce::String release = "release";
        static const juce::String fm1to2  = "fm1to2"; // osc 1's output phase-modulates osc 2
        static const juce::String fm2to3  = "fm2to3"; // osc 2's output phase-modulates osc 3
        static const juce::String output  = "output";

        // Mono/Legato collapses all 16 voices into one: a new note-on while another is still
        // held retargets that single voice's pitch instead of triggering a new one (no envelope
        // retrigger, no phase reset) -- classic monosynth behaviour, last-held-note priority via
        // a small note stack (releasing one note of an overlapping run falls back to whichever
        // other held note was pressed most recently, not silence, as long as one remains held).
        static const juce::String monoLegato = "monoLegato";
        // Glide only affects Mono/Legato's pitch retargeting (poly voices always start directly
        // at their own note's pitch) -- when on, the retarget ramps over Glide Time instead of
        // snapping immediately.
        static const juce::String glide       = "glide";
        static const juce::String glideTimeMs = "glideTimeMs";
    }

    inline juce::String threeOscOscParamId (int slotIndex, int oscIndex, const juce::String& paramName)
    {
        return "slot" + juce::String (slotIndex) + "_threeOsc_osc" + juce::String (oscIndex) + "_" + paramName;
    }

    namespace ThreeOscOscParam
    {
        static const juce::String waveform = "waveform";
        static const juce::String coarse   = "coarse"; // semitones
        static const juce::String fine     = "fine";   // cents
        static const juce::String pan      = "pan";
        static const juce::String level    = "level";
    }

    // A Phase Plant-inspired wavetable generator. GGrid's graph has 4 physical output ports per
    // node, so this module exposes 4 routable output buses and a bank of internal generators that
    // can each be assigned to any bus. The fixed parameter bank keeps DAW automation stable while
    // still feeling expandable in use: turn on more generator rows as needed, choose built-in
    // sine/triangle/saw/square or the bundled Kilohearts tables, then patch output ports 1-4 to
    // different lanes/effects. FM routing is picked with an Operator-style algorithm selector.
    constexpr int kNumWavetableSynthGenerators = 8;
    constexpr int kNumWavetableSynthOutputs = 4;
    constexpr int kMaxWavetableSynthVoices = 16;

    inline juce::StringArray getWavetableSynthGeneratorLabels()
    {
        return { "Gen 1", "Gen 2", "Gen 3", "Gen 4", "Gen 5", "Gen 6", "Gen 7", "Gen 8" };
    }

    inline juce::StringArray getWavetableSynthOutputChoices()
    {
        return { "Out 1", "Out 2", "Out 3", "Out 4" };
    }

    inline juce::StringArray getWavetableSynthAlgorithmChoices()
    {
        return { "Series", "Pairs", "Two Stacks", "Carriers" };
    }

    inline juce::String wavetableSynthParamId (int slotIndex, const juce::String& paramName)
    {
        return "slot" + juce::String (slotIndex) + "_wavetableSynth_" + paramName;
    }

    namespace WavetableSynthParam
    {
        static const juce::String attack      = "attack";
        static const juce::String decay       = "decay";
        static const juce::String sustain     = "sustain";
        static const juce::String release     = "release";
        static const juce::String output      = "output";
        static const juce::String algorithm   = "algorithm";
        static const juce::String monoLegato  = "monoLegato";
        static const juce::String glide       = "glide";
        static const juce::String glideTimeMs = "glideTimeMs";
    }

    inline juce::String wavetableSynthGenParamId (int slotIndex, int genIndex, const juce::String& paramName)
    {
        return "slot" + juce::String (slotIndex) + "_wavetableSynth_gen" + juce::String (genIndex) + "_" + paramName;
    }

    namespace WavetableSynthGenParam
    {
        static const juce::String enabled = "enabled";
        static const juce::String table   = "table";
        static const juce::String frame   = "frame";
        static const juce::String smooth  = "smooth";
        static const juce::String coarse  = "coarse";
        static const juce::String fine    = "fine";
        static const juce::String pan     = "pan";
        static const juce::String level   = "level";
        static const juce::String fm      = "fm";
        static const juce::String output  = "output";
    }

    // A standalone ADSR modulation source: sustains while a note is held, releases on note-off --
    // the classic synth-envelope behaviour, as opposed to Envelope's fixed-length one-shot (see
    // that module's own comment for why the two are split rather than one configurable module).
    // Mono (last-note-wins, matching ModulationMatrix's own existing note-tracking convention),
    // reports a unipolar 0-1 value via RackModule::getCurrentModulationValue().
    inline juce::String adsrParamId (int slotIndex, const juce::String& paramName)
    {
        return "slot" + juce::String (slotIndex) + "_adsr_" + paramName;
    }

    namespace AdsrParam
    {
        static const juce::String attack  = "attack";
        static const juce::String decay   = "decay";
        static const juce::String sustain = "sustain";
        static const juce::String release = "release";
    }

    // A freeform-breakpoint modulation source: draw an arbitrary shape (click empty space to add
    // a point, drag to move one, right-click to delete -- see EnvelopeBreakpointEditor), which
    // plays once per MIDI note-on over the Length knob's fixed duration and then holds at the
    // final point's value, regardless of how long the note is held or when it's released --
    // deliberately NOT sustain/release like Adsr (see AdsrParam's own comment); the two modules
    // are kept distinct rather than folding sustain-hold behaviour into one configurable module.
    // Breakpoints are NOT APVTS parameters (a variable-length point list can't be represented as
    // a fixed set of automatable scalars) -- see RackModule::writeExtraState/readExtraState and
    // EnvelopeModule's own comment for how they're persisted instead.
    constexpr int kMaxEnvelopePoints = 64;

    inline juce::String envelopeParamId (int slotIndex, const juce::String& paramName)
    {
        return "slot" + juce::String (slotIndex) + "_envelope_" + paramName;
    }

    namespace EnvelopeParam
    {
        static const juce::String length = "length"; // total playback duration, note-on to finish
        static const juce::String depth  = "depth";  // scales the drawn shape's 0-1 output
    }

    // A wavetable-backed modulation source inspired by Kilohearts' LFO Table: select a table
    // from Kilohearts' installed factory_wavetables folder (or GGrid's own Wavetables folders),
    // choose a frame, then cycle through that frame as an LFO. Kilohearts factory tables are
    // 256 frames x 2048 samples; the loader infers frame count from file length so compatible
    // user tables still work if they use a smaller/larger frame count. Like LFO/Envelope/ADSR,
    // LFO Table is a modulation source only: no audio ports, one violet output nub.
    inline juce::String lfoTableParamId (int slotIndex, const juce::String& paramName)
    {
        return "slot" + juce::String (slotIndex) + "_lfoTable_" + paramName;
    }

    namespace LfoTableParam
    {
        static const juce::String tableIndex = "table";
        static const juce::String frame      = "frame";
        static const juce::String smooth     = "smooth";
        static const juce::String phase      = "phase";
        static const juce::String rateMode   = "rateMode";
        static const juce::String rateHz     = "rateHz";
        static const juce::String division   = "division";
        static const juce::String depth      = "depth";
        static const juce::String retrigger  = "retrigger";
    }

    // A 3-band (Low/Mid/High) LR4 crossover splitter -- unlike Multiband Convolution, it has NO
    // per-band effect of its own and does NOT recombine the bands back into one output. Instead
    // it's GGrid's first (and so far only) multi-output-bus module: each band exits through its
    // own dedicated output port (see RackModule::getNumOutputBuses/getOutputBusBuffer and
    // ConnectionGraph.h's Connection::fromPort), so each can be routed to a completely different
    // downstream chain and processed individually -- the entire point of the module. Same 2
    // draggable split points as Multiband Convolution (reuses CrossoverSplitBar), plus one Gain
    // knob per band -- a plain dB trim on that band's isolated crossover-filtered content, for
    // re-levelling Low/Mid/High relative to each other, not a Mix/blend knob (there's no dry
    // signal worth blending back in here -- each band already IS the entire signal reaching its
    // own downstream chain). See MultipassModule.
    inline juce::String multipassParamId (int slotIndex, const juce::String& paramName)
    {
        return "slot" + juce::String (slotIndex) + "_multipass_" + paramName;
    }

    namespace MultipassParam
    {
        static const juce::String splitHz1 = "splitHz1"; // Low/Mid boundary
        static const juce::String splitHz2 = "splitHz2"; // Mid/High boundary
    }

    constexpr int kNumMultipassBands = 3;
    inline juce::StringArray getMultipassBandLabels() { return { "Low", "Mid", "High" }; }

    inline juce::String multipassBandParamId (int slotIndex, int bandIndex, const juce::String& paramName)
    {
        return "slot" + juce::String (slotIndex) + "_multipass_band" + juce::String (bandIndex) + "_" + paramName;
    }

    namespace MultipassBandParam
    {
        static const juce::String gain = "gain";
    }

    // Master safety limiter -- always the last stage after the rack chain, not a rack slot
    // itself (it must never be reorderable away from "last", or it stops protecting anything).
    inline juce::String masterLimiterEnabledParamId() { return "master_limiterEnabled"; }
    inline juce::String masterLimiterCeilingParamId() { return "master_limiterCeiling"; }
}
