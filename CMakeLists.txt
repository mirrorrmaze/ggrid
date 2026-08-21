cmake_minimum_required(VERSION 3.22)

project(GGrid VERSION 1.0.9)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.6
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(GGrid
    COMPANY_NAME "Mirror Maze"
    BUNDLE_ID "com.mirrormaze.ggrid"
    PLUGIN_MANUFACTURER_CODE Izak
    PLUGIN_CODE GGrd
    FORMATS VST3 Standalone
    PRODUCT_NAME "GGrid"
    VST3_CATEGORIES Fx
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_SYNTH FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
)

target_sources(GGrid
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/UpdateChecker.cpp
        Source/Params/ParameterLayout.cpp
        Source/Rack/RackSlot.cpp
        Source/Modules/WaveshaperModule.cpp
        Source/Modules/FilterModule.cpp
        Source/Modules/NonlinearFilterModule.cpp
        Source/Modules/MackityModule.cpp
        Source/Modules/ShimmerReverbModule.cpp
        Source/Modules/DelayModule.cpp
        Source/Modules/CompressorModule.cpp
        Source/Modules/LimiterModule.cpp
        Source/Modules/GranularModule.cpp
        Source/Modules/ConvolutionModule.cpp
        Source/Modules/UtilityModule.cpp
        Source/Modules/RingModModule.cpp
        Source/Modules/LFOModule.cpp
        Source/Modules/LossyModule.cpp
        Source/Modules/SpectralClipperModule.cpp
        Source/Modules/Eq8Module.cpp
        Source/Modules/ChorusModule.cpp
        Source/Modules/Eq3Module.cpp
        Source/Modules/MultibandConvolutionModule.cpp
        Source/Modules/ThreeOscModule.cpp
        Source/Modules/WavetableSynthModule.cpp
        Source/Modules/AdsrModule.cpp
        Source/Modules/EnvelopeModule.cpp
        Source/Modules/MultipassModule.cpp
        Source/Modules/LfoTableModule.cpp
        Source/Modules/SamplerModule.cpp
        Source/Modulation/ModulationMatrix.cpp
        Source/IR/IRLibrary.cpp
        Source/IR/IRProcessor.cpp
        Source/IR/IRReshapeWorker.cpp
        Source/Wavetable/WavetableLibrary.cpp
        Source/Sampler/SamplerSampleLibrary.cpp
        Source/GUI/ModuleControlPanels.cpp
        Source/GUI/CrossoverSplitBar.cpp
        Source/GUI/Eq8CurveEditor.cpp
        Source/GUI/EnvelopeBreakpointEditor.cpp
        Source/GUI/NodeComponent.cpp
        Source/GUI/NodeGraphEditor.cpp
        Source/GUI/AddModuleSearchPopup.cpp
        Source/GUI/GGridLookAndFeel.cpp
        Source/GUI/IRWaveformComponent.cpp
)

target_include_directories(GGrid
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Source
)

target_compile_definitions(GGrid
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        GGRID_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(GGrid
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Lightweight offline DSP verification harness -- deliberately excludes GUI/plugin-client modules
# so it builds fast and can run in any environment, no audio device or DAW host required.
add_executable(OfflineDspTest
    Tests/OfflineDspTest.cpp
    Source/Params/ParameterLayout.cpp
    Source/Rack/RackSlot.cpp
    Source/Modules/WaveshaperModule.cpp
    Source/Modules/FilterModule.cpp
    Source/Modules/NonlinearFilterModule.cpp
    Source/Modules/MackityModule.cpp
    Source/Modules/ShimmerReverbModule.cpp
    Source/Modules/DelayModule.cpp
    Source/Modules/CompressorModule.cpp
    Source/Modules/LimiterModule.cpp
    Source/Modules/GranularModule.cpp
    Source/Modules/ConvolutionModule.cpp
    Source/Modules/UtilityModule.cpp
    Source/Modules/RingModModule.cpp
    Source/Modules/LFOModule.cpp
    Source/Modules/LossyModule.cpp
    Source/Modules/SpectralClipperModule.cpp
    Source/Modules/Eq8Module.cpp
    Source/Modules/ChorusModule.cpp
    Source/Modules/Eq3Module.cpp
    Source/Modules/MultibandConvolutionModule.cpp
    Source/Modules/ThreeOscModule.cpp
    Source/Modules/WavetableSynthModule.cpp
    Source/Modules/AdsrModule.cpp
    Source/Modules/EnvelopeModule.cpp
    Source/Modules/MultipassModule.cpp
    Source/Modules/LfoTableModule.cpp
    Source/Modules/SamplerModule.cpp
    Source/Modulation/ModulationMatrix.cpp
    Source/IR/IRLibrary.cpp
    Source/IR/IRProcessor.cpp
    Source/IR/IRReshapeWorker.cpp
    Source/Wavetable/WavetableLibrary.cpp
    Source/Sampler/SamplerSampleLibrary.cpp
)

target_include_directories(OfflineDspTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Source)

target_link_libraries(OfflineDspTest
    PRIVATE
        juce::juce_dsp
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)
