// The PEDWAR panel, 0.3.2: the gold "sections" look. Laid out at 760 x 726 units and scaled to the window (drag the
// corner to resize; the shape stays the same).
//
// Drawing is split in two so the panel stays light to draw:
//   - the fixed artwork (frames, titles, names, knob arcs, the flow lines) is a child component drawn once into a
//     stored picture (redrawn only when the window opens or changes size);
//   - 30 times a second only what moves is redrawn: the modulation rings on Coarse, Q and Morph, the strike lamps and
//     the "track in / side chain" lines (and a control when its own setting changes).

#pragma once

#include "PluginProcessor.h"
#include "Controls.h"

class PedwarPanel : public juce::Component, private juce::Timer
{
public:
    explicit PedwarPanel (PedwarAudioProcessor&);
    ~PedwarPanel() override;

    void paintOverChildren (juce::Graphics&) override;

    // For the check program: the fixed artwork layer, the areas redrawn 30 times a second (panel units), and the
    // panel's texts with the room each has (panel units), so it can check that every text fits.
    juce::Component& getBackdrop() { return backdrop; }
    juce::Array<juce::Rectangle<int>> liveAreas() const;
    struct FixedText { juce::String text; float width, room; };
    std::vector<FixedText> fixedTexts() const;

    static constexpr int kLiveMilestone = 3;     // controls of this milestone and earlier work (all of them now)

private:
    struct Backdrop : public juce::Component
    {
        explicit Backdrop (PedwarPanel& p) : owner (p) {}
        void paint (juce::Graphics& g) override { owner.paintBackdrop (g); }
        PedwarPanel& owner;
    };

    void timerCallback() override;
    void paintBackdrop (juce::Graphics&);
    PdKnob& knob (const char* pid, float cx, float cy, float r, const juce::String& label, float room,
                  PdKnob::Readout ro = nullptr);
    PdSegments& segs (const char* pid, juce::StringArray labels, PdSegments::Kind, juce::Rectangle<int> r,
                      juce::Array<float> values = {});
    PdMenu& menu (const char* pid, juce::Rectangle<int> r, const juce::StringArray& shown = {});
    void later (juce::Component&, int milestone);

    void updatePresetMenu();
    void updateQualityMenu();
    int snapState() const;                   // changes whenever the Coarse readouts must be redrawn

    PedwarAudioProcessor& proc;
    PdLookAndFeel lnf;
    Backdrop backdrop { *this };
    std::vector<std::unique_ptr<juce::Component>> owned;
    std::vector<PdKnob*> knobs;
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
    PedwarPanel& getPanel() { return panel; }

private:
    PedwarPanel panel;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PedwarAudioProcessorEditor)
};
