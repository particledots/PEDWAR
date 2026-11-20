# The two PEDWAR plugin wrappers: what Logic sees (AU / VST3), the controls and the panel.
# Both are built from the same source files and the same parameter list; the sound itself lives in ../engine.
#
#   PEDWAR        MIDI-controlled audio effect (AU type aumf): Audio FX slot, MIDI from Logic's side-chain MIDI menu.
#   PEDWAR Synth  instrument (AU type aumu) with an optional side-chain audio input.
#
# The identifiers below were chosen once, for the first build, and must never change
# (Logic finds saved projects' plugins by them).

set(PEDWAR_SOURCES
    Source/Parameters.h
    Source/Parameters.cpp
    Source/PluginProcessor.h
    Source/PluginProcessor.cpp
    Source/PluginEditor.h
    Source/PluginEditor.cpp
    Source/Controls.h
    Source/Controls.cpp
    Source/Style.h
)

function(pedwar_finish target is_synth)
    target_sources(${target} PRIVATE ${PEDWAR_SOURCES})
    target_compile_definitions(${target} PUBLIC
        PEDWAR_SYNTH=${is_synth}
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
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

juce_add_plugin(Pedwar
    COMPANY_NAME                "particledots"
    BUNDLE_ID                   com.particledots.pedwar
    PLUGIN_MANUFACTURER_CODE    Pdot
    PLUGIN_CODE                 Pdwr
    FORMATS                     AU VST3
    PRODUCT_NAME                "PEDWAR"
    PLUGIN_NAME                 "PEDWAR"
    DESCRIPTION                 "Quad resonant filter (inspired by the Instruo ceithir; not affiliated)"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Fx Filter
    AU_MAIN_TYPE                kAudioUnitType_MusicEffect
)
pedwar_finish(Pedwar 0)

juce_add_plugin(PedwarSynth
    COMPANY_NAME                "particledots"
    BUNDLE_ID                   com.particledots.pedwarsynth
    PLUGIN_MANUFACTURER_CODE    Pdot
    PLUGIN_CODE                 PdwS
    FORMATS                     AU VST3
    PRODUCT_NAME                "PEDWAR Synth"
    PLUGIN_NAME                 "PEDWAR Synth"
    DESCRIPTION                 "Quad resonator instrument (inspired by the Instruo ceithir; not affiliated)"
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Instrument Synth
    AU_MAIN_TYPE                kAudioUnitType_MusicDevice
)
pedwar_finish(PedwarSynth 1)
