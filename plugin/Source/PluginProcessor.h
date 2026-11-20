// The plugin wrapper, shared by both plugins. It talks to Logic, reads the control values, the MIDI notes and the audio,
// and passes them to the engine. It contains no sound-making code.
//
//   PEDWAR        (PEDWAR_SYNTH = 0): MIDI-controlled effect (AU aumf). Track audio in; side-chain audio in. In Logic it
//                  gets MIDI only in a Software Instrument track's instrument slot (AU MIDI-controlled Effects), where
//                  the track input is silent and the audio arrives on the side chain.
//   PEDWAR Synth  (PEDWAR_SYNTH = 1): instrument (AU aumu) with an optional side-chain audio input.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <pedwar/Engine.h>
#include <pedwar/Presets.h>
#include "Parameters.h"

#ifndef PEDWAR_SYNTH
 #define PEDWAR_SYNTH 0
#endif

class PedwarAudioProcessor : public juce::AudioProcessor
{
public:
    static constexpr bool isSynth = PEDWAR_SYNTH != 0;

    PedwarAudioProcessor();
    ~PedwarAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 5.0; }   // a high-Q ring at the bottom of the range

    // Programs = the Preset menu: 0 is "Init" (every control at its default), then the factory presets (engine
    // Presets.h). Neither ever touches Pitch Snap, Scale, Key or Quality. The choice is stored with the project.
    int getNumPrograms() override { return 1 + pedwar::presets::kNumFactory; }
    int getCurrentProgram() override { return programIndex.load(); }
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override {}
    // For the panel: the program last chosen (0 = Init), or -1 ("Custom") for a project saved before 0.3.0, when the
    // Preset menu had Init only and did not say what the settings were.
    int shownPreset() const { return presetShown.load(); }

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getValueTreeState() { return apvts; }
    pedwar::Params readParams() const;
    const pedwar::Engine& getEngine() const { return engine; }

private:
    static BusesProperties makeBuses();
    void handleMidi (const juce::MidiMessage&);
    void applyParams (const pedwar::Params&);      // every control except Pitch Snap, Scale, Key and Quality

    std::atomic<int> programIndex { 0 }, presetShown { 0 };
    juce::AudioProcessorValueTreeState apvts;
    pedwar::Engine engine;
    std::vector<float> trackMono, sideMono, outL, outR;

    std::atomic<float>* fp[PdParams::numFilters][PdParams::numSlots] {};
    std::atomic<float> *masterLimit {}, *linkAB {}, *linkGD {}, *offset {}, *pitchSplit {}, *lfoRate {};
    std::atomic<float> *bus[PdParams::numFilters] {};
    std::atomic<float> *stereoMode {}, *snap {}, *scale {}, *key {}, *quality {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PedwarAudioProcessor)
};
