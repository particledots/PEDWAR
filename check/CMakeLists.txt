# PedwarCheck and PedwarSynthCheck: console programs that run the plugin layer (parameters, saved state, Init, audio
# through processBlock, side chain, MIDI, the panel) without a host. They compile the plugins' own source files, so
# what they check is what Logic gets. Not part of the plugins; the build workflow runs both on every push.

function(pedwar_check target is_synth plugin_name)
    juce_add_console_app(${target} PRODUCT_NAME "${target}")
    target_sources(${target} PRIVATE
        Check.cpp
        ../plugin/Source/Parameters.cpp
        ../plugin/Source/PluginProcessor.cpp
        ../plugin/Source/PluginEditor.cpp
        ../plugin/Source/Controls.cpp
    )
    target_include_directories(${target} PRIVATE ../plugin/Source)
    target_compile_definitions(${target} PRIVATE
        PEDWAR_SYNTH=${is_synth}
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_ALSA=0
        JUCE_JACK=0
        JUCE_MODAL_LOOPS_PERMITTED=0
        PEDWAR_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden"
        JucePlugin_Name="${plugin_name}"
        JucePlugin_VersionString="${PROJECT_VERSION}"
    )
    target_link_libraries(${target}
        PRIVATE
            pedwar_engine
            juce::juce_audio_processors
            juce::juce_gui_basics
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags
    )
endfunction()

pedwar_check(PedwarCheck 0 "PEDWAR")
pedwar_check(PedwarSynthCheck 1 "PEDWAR Synth")
