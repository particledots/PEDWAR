// The PEDWAR panel. Laid out at 680 x 612 units exactly like the approved draft (pedwar-gui-draft.html, 2026-11-19)
// and scaled to the window (drag the corner to resize; the shape stays the same).
// Controls that arrive in a later milestone are in place, dimmed and tagged "M2" or "M3".

#pragma once

#include "PluginProcessor.h"
#include "Controls.h"

class PedwarPanel : public juce::Component, private juce::Timer
{
public:
    explicit PedwarPanel (PedwarAudioProcessor&);
    ~PedwarPanel() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;

private:
    void timerCallback() override;
    PdKnob& knob (int filter, int slot, float cx, float cy, float r, const juce::String& label, int milestone,
                  PdKnob::Readout ro = nullptr);
    PdKnob& knob (const char* pid, juce::Colour c, float cx, float cy, float r, const juce::String& label, int milestone);
    PdSegments& segs (const char* pid, juce::StringArray labels, juce::Colour c, int x, int y, int w,
                      const juce::String& caption, int milestone, juce::Array<float> values = {});
    PdMenu& menu (const char* pid, int x, int y, int w, int milestone, const juce::StringArray& shown = {});
    void later (juce::Component&, int milestone);

    PedwarAudioProcessor& proc;
    PdLookAndFeel lnf;
    std::vector<std::unique_ptr<juce::Component>> owned;
    PdStrikeLight* lights[4] {};
    int lastStrikes[4] {};
    bool shownSidechain[4] { false, false, false, false };
    juce::ComboBox presetMenu;
};

class PedwarAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit PedwarAudioProcessorEditor (PedwarAudioProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    PedwarPanel panel;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PedwarAudioProcessorEditor)
};
