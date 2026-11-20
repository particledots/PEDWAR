// The PEDWAR panel. Laid out at 680 x 612 units exactly like the approved draft (pedwar-gui-draft.html, 2026-11-19)
// and scaled to the window (drag the corner to resize; the shape stays the same).
// Every control is live (milestone 3: all controls of the brief).

#pragma once

#include "PluginProcessor.h"
#include "Controls.h"

class PedwarPanel : public juce::Component, private juce::Timer
{
public:
    explicit PedwarPanel (PedwarAudioProcessor&);
    ~PedwarPanel() override;

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    PdKnob& knob (int filter, int slot, float cx, float cy, float r, const juce::String& label, int milestone,
                  PdKnob::Readout ro = nullptr);
    PdKnob& knob (const char* pid, juce::Colour c, float cx, float cy, float r, const juce::String& label, int milestone);
    PdSegments& segs (const char* pid, juce::StringArray labels, juce::Colour c, int x, int y, int w,
                      const juce::String& caption, int milestone, juce::Array<float> values = {});
    PdMenu& menu (const char* pid, int x, int y, int w, int milestone, const juce::StringArray& shown = {});
    void later (juce::Component&, int milestone);

    void updatePresetMenu();
    void updateQualityMenu();
    int snapState() const;                   // changes whenever the Coarse readouts must be redrawn

    PedwarAudioProcessor& proc;
    PdLookAndFeel lnf;
    std::vector<std::unique_ptr<juce::Component>> owned;
    PdStrikeLight* lights[4] {};
    std::vector<PdKnob*> modKnobs;           // Coarse, Q and Morph of each filter: they show modulation rings
    int lastStrikes[4] {};
    bool shownSidechain[4] { false, false, false, false };
    std::vector<PdKnob*> coarseKnobs;        // their readout shows the snapped note while Pitch Snap or Scale is on
    juce::ComboBox presetMenu;
    PdMenu* qualityMenu = nullptr;
    int shownPresetId = -2, shownQuality = -1, shownSnap = -1;
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
