// The panel's own controls in the gold "sections" look (0.3.2): knobs (black skirt, cream cap, dark pointer; the gold
// range arc and the name are part of the panel's fixed artwork), switches (a gold pill whose chosen part is lit blue,
// or one on/off lamp with its label), menus, level faders, the strike lamps and the LFO bus grid. Each one is bound to
// its parameter.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Style.h"

class PdLookAndFeel : public juce::LookAndFeel_V4
{
public:
    PdLookAndFeel();
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return PdStyle::menuFont(); }
    juce::Font getPopupMenuFont() override { return PdStyle::font (13.0f); }
    juce::Font getLabelFont (juce::Label&) override { return PdStyle::menuFont(); }
};

// A knob: black skirt, cream cap, dark pointer and the value underneath. Only the knob itself takes the mouse.
// With a modulation source, a dotted cream ring outside the gold arc shows how far the LFOs can swing the knob's
// setting, and a dot on it where they have it right now.
class PdKnob : public juce::Slider
{
public:
    using Readout = std::function<juce::String (float)>;
    struct Modulation { float now = 0.0f, depth = 0.0f; };      // in units of the knob's travel (0..1)
    using ModSource = std::function<Modulation()>;

    PdKnob (juce::AudioProcessorValueTreeState&, const char* paramId, float radius, const juce::String& label,
            float readoutRoom, Readout readout = nullptr);

    void placeAt (float cx, float cy);         // centre of the knob, in panel units
    void paint (juce::Graphics&) override;
    bool hitTest (int x, int y) override;

    void setModulation (ModSource s) { modSource = std::move (s); refreshModulation(); }
    bool hasModulation() const { return (bool) modSource; }
    void refreshModulation();                  // called by the panel's timer; repaints when the ring has moved
    Modulation shownModulation() const { return mod; }
    juce::String readoutText() const;          // the value shown under the knob
    juce::String readoutAt (float normalised) const;   // what it would show at another setting (for the fit check)

    // Geometry, in panel units (the fixed artwork draws the arc and the name from these)
    float centreX() const { return cx; }
    float centreY() const { return cy; }
    float radius() const { return r; }
    float arcRadius() const { return r * 1.36f + 1.5f; }
    float ringRadius() const { return arcRadius() + 4.0f; }
    float labelBaseline() const { return cy + arcRadius() + 12.0f; }
    float readoutBaseline() const { return labelBaseline() + 12.0f; }
    float readoutRoom() const { return room; }
    const juce::String& getLabel() const { return label; }

private:
    ModSource modSource;
    Modulation mod;
    juce::RangedAudioParameter* param = nullptr;
    float r, room, cx = 0.0f, cy = 0.0f;
    juce::String label;
    Readout readout;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

// A switch bound to one parameter, drawn in one of two ways:
//   Pill:   a row of choices in a gold pill (each part as wide as its label needs); the chosen one is lit blue.
//   Option: one on/off lamp with its label (lit blue = on).
// values = the parameter value of each choice (by default 0, 1, 2 ...); an Option's two values are { off, on }.
class PdSegments : public juce::Component
{
public:
    enum class Kind { Pill, Option };
    PdSegments (juce::RangedAudioParameter&, juce::StringArray labels, Kind, juce::Array<float> values = {});

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    int selectedIndex() const { return selected; }
    const juce::StringArray& getLabels() const { return labels; }
    Kind getKind() const { return kind; }

    // The width each label needs (for the fit check), and the width the switch has for it, in panel units.
    float neededWidth (int i) const;
    float roomFor (int i) const;
    static float optionWidth (const juce::String& label);      // lamp, gap, label

private:
    float partX (int i) const;                 // where part i starts (Pill)
    juce::RangedAudioParameter& param;
    juce::StringArray labels;
    juce::Array<float> values;
    Kind kind;
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

// A level fader (drag up and down): a gold slot with a cream thumb, the filter's letter and the level in dB underneath.
class PdFader : public juce::Slider
{
public:
    PdFader (juce::AudioProcessorValueTreeState&, const char* paramId, const juce::String& letter);
    void placeAt (float x, float top);         // the fader's centre line and the top of its slot, in panel units
    void paint (juce::Graphics&) override;
    bool hitTest (int x, int y) override { return y < (int) (kLen + 16.0f) && x >= 0; }
    juce::String readoutText() const;
    static constexpr float kLen = 80.0f, kW = 34.0f;

private:
    juce::RangedAudioParameter* param = nullptr;
    juce::String letter;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

// The strike lamp: lit while strike is on, flashing brighter on each strike; click it to switch strike on or off.
class PdStrikeLight : public juce::Component
{
public:
    explicit PdStrikeLight (juce::RangedAudioParameter&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void flash (float amount) { glow = amount; repaint(); }
    float currentGlow() const { return glow; }
    bool isOn() const { return on; }

private:
    juce::RangedAudioParameter& param;
    bool on = true;
    float glow = 0.0f;
    juce::ParameterAttachment attachment;
};

// The LFO bus grid: one column per LFO (alpha .. delta), three rows (width / FM / anim); one lamp lit per column.
class PdBusGrid : public juce::Component
{
public:
    PdBusGrid (juce::AudioProcessorValueTreeState&, const char* const ids[4]);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    static constexpr float colX0 = 8.0f, colStep = 14.0f, rowY0 = 26.0f, rowStep = 15.0f;
    static float rowLabelRoom (float gridWidth) { return gridWidth - (colX0 + 3.0f * colStep + 9.0f); }

private:
    juce::RangedAudioParameter* params[4] {};
    int selected[4] {};
    std::vector<std::unique_ptr<juce::ParameterAttachment>> attachments;
};
