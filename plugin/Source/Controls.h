// The panel's own controls, drawn like the approved draft: knobs with a value arc, segmented switches, menus,
// level faders, the strike lights and the LFO bus grid. Each one is bound to its parameter.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Style.h"

class PdLookAndFeel : public juce::LookAndFeel_V4
{
public:
    PdLookAndFeel();
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return PdStyle::font (11.0f); }
    juce::Font getPopupMenuFont() override { return PdStyle::font (13.0f); }
    juce::Font getLabelFont (juce::Label&) override { return PdStyle::font (11.0f); }
};

// A knob: value arc, face, pointer, its name and a readout underneath. Only the knob itself takes the mouse.
// With a modulation source, a dotted ring outside the arc shows how far the LFOs can swing the knob's setting, and a
// dot on it where they have it right now.
class PdKnob : public juce::Slider
{
public:
    using Readout = std::function<juce::String (float)>;
    struct Modulation { float now = 0.0f, depth = 0.0f; };      // in units of the knob's travel (0..1)
    using ModSource = std::function<Modulation()>;

    PdKnob (juce::AudioProcessorValueTreeState&, const char* paramId, juce::Colour accent, float radius,
            const juce::String& label, Readout readout = nullptr);

    void placeAt (float cx, float cy);         // centre of the knob, in panel units
    void paint (juce::Graphics&) override;
    bool hitTest (int x, int y) override;

    void setModulation (ModSource s) { modSource = std::move (s); refreshModulation(); }
    void refreshModulation();                  // called by the panel's timer; repaints when the ring has moved
    Modulation shownModulation() const { return mod; }

private:
    ModSource modSource;
    Modulation mod;
    juce::RangedAudioParameter* param = nullptr;
    juce::Colour accent;
    float r;
    juce::String label;
    Readout readout;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    static constexpr float kW = 100.0f;
};

// A row of segments, one per value (by default the values 0, 1, 2 ... of a choice or switch), with a caption under it.
class PdSegments : public juce::Component
{
public:
    PdSegments (juce::RangedAudioParameter&, juce::StringArray labels, juce::Colour accent,
                const juce::String& caption = {}, juce::Array<float> values = {});

    void place (int x, int y, int w);          // the bar's top-left corner and width, in panel units
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    bool hitTest (int x, int y) override { return y < 18 && x >= 0; }

private:
    juce::RangedAudioParameter& param;
    juce::StringArray labels;
    juce::Array<float> values;
    juce::Colour accent;
    juce::String caption;
    int selected = 0;
    juce::ParameterAttachment attachment;
};

// A drop-down menu bound to a choice parameter; 'shown' replaces the item texts on the panel.
class PdMenu : public juce::ComboBox
{
public:
    PdMenu (juce::AudioProcessorValueTreeState&, const char* paramId, const juce::StringArray& shown = {});
private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
};

// A level fader (drag up and down), with the filter's letter and the level in dB underneath.
class PdFader : public juce::Slider
{
public:
    PdFader (juce::AudioProcessorValueTreeState&, const char* paramId, juce::Colour accent, const juce::String& letter);
    void placeAt (float x);                   // the fader's centre line, in panel units
    void paint (juce::Graphics&) override;
    bool hitTest (int x, int y) override { return y < 100 && x >= 0; }

private:
    juce::RangedAudioParameter* param = nullptr;
    juce::Colour accent;
    juce::String letter;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

// The strike light: flashes on each strike; click it to switch this filter's strike on or off.
class PdStrikeLight : public juce::Component
{
public:
    PdStrikeLight (juce::RangedAudioParameter&, juce::Colour accent);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void flash (float amount) { glow = amount; repaint(); }
    float currentGlow() const { return glow; }
    bool isOn() const { return on; }

private:
    juce::RangedAudioParameter& param;
    juce::Colour accent;
    bool on = true;
    float glow = 0.0f;
    juce::ParameterAttachment attachment;
};

// The LFO bus grid: one column per LFO (alpha .. delta), three rows (width / FM / anim); one choice per column.
class PdBusGrid : public juce::Component
{
public:
    PdBusGrid (juce::AudioProcessorValueTreeState&, const char* const ids[4]);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::RangedAudioParameter* params[4] {};
    int selected[4] {};
    std::vector<std::unique_ptr<juce::ParameterAttachment>> attachments;
};
