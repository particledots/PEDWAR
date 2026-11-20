#include "Controls.h"

using namespace PdStyle;

namespace
{
    // Angle 0 = 12 o'clock, clockwise (as in the draft). Knobs turn from -135 to +135 degrees.
    constexpr float kStart = -135.0f, kEnd = 135.0f;

    void arc (juce::Graphics& g, float cx, float cy, float radius, float a0, float a1, juce::Colour c, float thick,
              bool dotted = false)
    {
        if (a1 <= a0 + 0.01f) return;
        juce::Path p;
        p.addCentredArc (cx, cy, radius, radius, 0.0f, juce::degreesToRadians (a0), juce::degreesToRadians (a1), true);
        g.setColour (c);
        juce::PathStrokeType stroke (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        if (dotted)
        {
            const float dashes[] = { 2.0f, 3.0f };
            juce::Path d;
            stroke.createDashedStroke (d, p, dashes, 2);
            g.fillPath (d);
        }
        else
            g.strokePath (p, stroke);
    }
}

// ------------------------------------------------------------------------------------------------ look

PdLookAndFeel::PdLookAndFeel()
{
    setColour (juce::PopupMenu::backgroundColourId, control());
    setColour (juce::PopupMenu::textColourId, text());
    setColour (juce::PopupMenu::highlightedBackgroundColourId, master().withAlpha (0.3f));
    setColour (juce::PopupMenu::highlightedTextColourId, text());
    setColour (juce::ComboBox::textColourId, text());
    setColour (juce::ComboBox::backgroundColourId, control());
    setColour (juce::ComboBox::outlineColourId, line());
    setColour (juce::ComboBox::arrowColourId, muted());
    setColour (juce::Label::textColourId, text());
}

void PdLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&)
{
    const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (0.5f);
    g.setColour (control());
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (line());
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
    textAt (g, utf8 ("\xe2\x96\xbe"), (float) w - 9.0f, (float) h * 0.5f + 4.0f, muted(), 11.0f);
}

void PdLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBorderSize (juce::BorderSize<int> (0));
    label.setBounds (7, 0, box.getWidth() - 7 - 16, box.getHeight());
    label.setFont (font (11.0f));
    label.setMinimumHorizontalScale (0.7f);
}

// ------------------------------------------------------------------------------------------------ knob

PdKnob::PdKnob (juce::AudioProcessorValueTreeState& state, const char* paramId, juce::Colour c, float radius,
                const juce::String& l, Readout ro)
    : juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox),
      param (state.getParameter (paramId)), accent (c), r (radius), label (l), readout (std::move (ro))
{
    setRotaryParameters (juce::degreesToRadians (kStart), juce::degreesToRadians (kEnd), true);
    setVelocityModeParameters (0.6, 1, 0.0, true, juce::ModifierKeys::shiftModifier);   // hold Shift for fine moves
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramId, *this);
    if (param != nullptr)
        setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));   // double-click: default
}

void PdKnob::placeAt (float cx, float cy)
{
    const float top = cy - r - 10.0f;
    setBounds (juce::roundToInt (cx - kW * 0.5f), juce::roundToInt (top), (int) kW, juce::roundToInt (2.0f * r + 10.0f + 34.0f));
}

bool PdKnob::hitTest (int x, int y)
{
    const float cx = kW * 0.5f, cy = r + 10.0f;
    const float dx = (float) x - cx, dy = (float) y - cy;
    return dx * dx + dy * dy <= (r + 7.0f) * (r + 7.0f);
}

void PdKnob::paint (juce::Graphics& g)
{
    const float cx = kW * 0.5f, cy = r + 10.0f;
    const float pos = param != nullptr ? param->getValue() : 0.0f;
    const float a = kStart + (kEnd - kStart) * pos;

    arc (g, cx, cy, r + 4.0f, kStart, kEnd, track(), 3.0f);
    arc (g, cx, cy, r + 4.0f, kStart, a, accent, 3.0f);
    if (mod.depth > 0.002f)
    {
        // modulation ring (the draft's dotted arc): the swing, and a dot where the LFOs have the setting now
        auto angle = [] (float p) { return kStart + (kEnd - kStart) * juce::jlimit (0.0f, 1.0f, p); };
        const float rr = r + 9.0f;
        arc (g, cx, cy, rr, angle (pos - mod.depth), angle (pos + mod.depth), accent.withAlpha (0.5f), 2.0f, true);
        const float d = juce::degreesToRadians (angle (pos + mod.now));
        g.setColour (accent);
        g.fillEllipse (cx + rr * std::sin (d) - 2.5f, cy - rr * std::cos (d) - 2.5f, 5.0f, 5.0f);
    }
    g.setColour (knobFace());
    g.fillEllipse (cx - r, cy - r, 2.0f * r, 2.0f * r);
    g.setColour (knobEdge());
    g.drawEllipse (cx - r, cy - r, 2.0f * r, 2.0f * r, 1.0f);
    const float rad = juce::degreesToRadians (a);
    g.setColour (text());
    g.drawLine (cx, cy, cx + (r - 3.0f) * std::sin (rad), cy - (r - 3.0f) * std::cos (rad), 2.0f);

    textAt (g, label, cx, cy + r + 17.0f, soft());
    if (param != nullptr)
    {
        const float v = param->convertFrom0to1 (param->getValue());
        const auto s = readout ? readout (v) : param->getCurrentValueAsText();
        textAt (g, s, cx, cy + r + 30.0f, text(), mono (11.0f));
    }
}

void PdKnob::refreshModulation()
{
    if (! modSource) return;
    const auto m = modSource();
    if (std::fabs (m.now - mod.now) > 0.002f || std::fabs (m.depth - mod.depth) > 0.002f)
    {
        mod = m;
        repaint();
    }
}

// ------------------------------------------------------------------------------------------------ segments

PdSegments::PdSegments (juce::RangedAudioParameter& p, juce::StringArray l, juce::Colour c, const juce::String& cap,
                        juce::Array<float> v)
    : param (p), labels (std::move (l)), values (std::move (v)), accent (c), caption (cap),
      attachment (p, [this] (float x)
                  {
                      int best = 0;
                      for (int i = 1; i < values.size(); ++i)
                          if (std::fabs (values[i] - x) < std::fabs (values[best] - x)) best = i;
                      selected = best;
                      repaint();
                  }, nullptr)
{
    if (values.isEmpty())
        for (int i = 0; i < labels.size(); ++i) values.add ((float) i);
    attachment.sendInitialUpdate();
}

void PdSegments::place (int x, int y, int w)
{
    setBounds (x, y, w, caption.isEmpty() ? 18 : 38);
}

void PdSegments::paint (juce::Graphics& g)
{
    const float w = (float) getWidth();
    const auto bar = juce::Rectangle<float> (0.0f, 0.0f, w, 18.0f).reduced (0.5f);
    g.setColour (control());
    g.fillRoundedRectangle (bar, 4.0f);
    g.setColour (line());
    g.drawRoundedRectangle (bar, 4.0f, 1.0f);

    const int n = labels.size();
    const float cw = w / (float) n;
    for (int i = 0; i < n; ++i)
    {
        const bool on = i == selected;
        if (on)
        {
            const auto lit = juce::Rectangle<float> (cw * (float) i + 1.5f, 1.5f, cw - 3.0f, 15.0f);
            g.setColour (accent.withAlpha (0.22f));
            g.fillRoundedRectangle (lit, 3.0f);
            g.setColour (accent);
            g.drawRoundedRectangle (lit, 3.0f, 1.0f);
        }
        textAt (g, labels[i], cw * (float) i + cw * 0.5f, 13.0f, on ? text() : muted());
    }
    if (caption.isNotEmpty())
        textAt (g, caption, w * 0.5f, 32.0f, soft());
}

void PdSegments::mouseDown (const juce::MouseEvent& e)
{
    if (! isEnabled() || e.y >= 18) return;
    const int n = labels.size();
    const int i = juce::jlimit (0, n - 1, (int) ((float) e.x / (float) getWidth() * (float) n));
    if (n == 1) { attachment.setValueAsCompleteGesture (selected == 0 ? 0.0f : values[0]); return; }
    attachment.setValueAsCompleteGesture (values[i]);
}

// ------------------------------------------------------------------------------------------------ menu

PdMenu::PdMenu (juce::AudioProcessorValueTreeState& state, const char* paramId, const juce::StringArray& shown)
{
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (paramId)))
        for (int i = 0; i < p->choices.size(); ++i)
            addItem (i < shown.size() ? shown[i] : p->choices[i], i + 1);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, paramId, *this);
}

// ------------------------------------------------------------------------------------------------ fader

PdFader::PdFader (juce::AudioProcessorValueTreeState& state, const char* paramId, juce::Colour c, const juce::String& l)
    : juce::Slider (juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox),
      param (state.getParameter (paramId)), accent (c), letter (l)
{
    setMouseDragSensitivity (120);               // drag about the fader's own length for the whole range
    setVelocityModeParameters (0.6, 1, 0.0, true, juce::ModifierKeys::shiftModifier);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramId, *this);
    if (param != nullptr)
        setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
}

void PdFader::placeAt (float x)
{
    setBounds (juce::roundToInt (x - 17.0f), 284, 34, 122);
}

void PdFader::paint (juce::Graphics& g)
{
    const float cx = 17.0f, top = 8.0f, len = 78.0f;          // the draft's track: y 292 .. 370
    const float v = param != nullptr ? param->getValue() : 0.0f;
    g.setColour (track());
    g.fillRoundedRectangle (cx - 2.0f, top, 4.0f, len, 2.0f);
    g.setColour (accent);
    g.fillRoundedRectangle (cx - 2.0f, top + len * (1.0f - v), 4.0f, len * v, 2.0f);
    const auto th = juce::Rectangle<float> (cx - 11.0f, top - 6.0f + len * (1.0f - v), 22.0f, 10.0f);
    g.setColour (thumb());
    g.fillRoundedRectangle (th, 2.0f);
    g.setColour (accent);
    g.drawRoundedRectangle (th, 2.0f, 1.0f);

    textAt (g, letter, cx, 102.0f, accent, 12.0f);
    if (param != nullptr)
    {
        const float db = param->convertFrom0to1 (v);
        textAt (g, db <= -59.95f ? juce::String ("-inf") : juce::String (juce::roundToInt (db)), cx, 116.0f, soft(), mono (11.0f));
    }
}

// ------------------------------------------------------------------------------------------------ strike light

PdStrikeLight::PdStrikeLight (juce::RangedAudioParameter& p, juce::Colour c)
    : param (p), accent (c),
      attachment (p, [this] (float v) { on = v > 0.5f; repaint(); }, nullptr)
{
    attachment.sendInitialUpdate();
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void PdStrikeLight::paint (juce::Graphics& g)
{
    // Light at (6, 8), radius 4: lit when strike is on, brighter while it flashes; an empty ring when off.
    const float cx = 6.0f, cy = 8.0f, rr = 4.0f;
    if (on)
    {
        if (glow > 0.01f)
        {
            g.setColour (accent.withAlpha (0.35f * glow));
            g.fillEllipse (cx - rr - 3.0f, cy - rr - 3.0f, 2.0f * rr + 6.0f, 2.0f * rr + 6.0f);
        }
        g.setColour (accent.withAlpha (0.45f + 0.55f * glow));
        g.fillEllipse (cx - rr, cy - rr, 2.0f * rr, 2.0f * rr);
    }
    else
    {
        g.setColour (accent.withAlpha (0.6f));
        g.drawEllipse (cx - rr + 0.5f, cy - rr + 0.5f, 2.0f * rr - 1.0f, 2.0f * rr - 1.0f, 1.0f);
    }
    textAt (g, "strike", 16.0f, 12.0f, on ? soft() : muted(), 11.0f, Align::left);
}

void PdStrikeLight::mouseDown (const juce::MouseEvent&)
{
    if (isEnabled())
        attachment.setValueAsCompleteGesture (on ? 0.0f : 1.0f);
}

// ------------------------------------------------------------------------------------------------ bus grid

// Panel units, relative to the grid's top-left corner (570, 184): columns at x 10 + 18 j, rows at y 26 + 15 k.
// (8 units further left than the draft, so the row names no longer overlap the delta column.)
PdBusGrid::PdBusGrid (juce::AudioProcessorValueTreeState& state, const char* const ids[4])
{
    for (int j = 0; j < 4; ++j)
    {
        params[j] = state.getParameter (ids[j]);
        if (params[j] == nullptr) continue;
        attachments.push_back (std::make_unique<juce::ParameterAttachment> (*params[j],
                               [this, j] (float v) { selected[j] = juce::roundToInt (v); repaint(); }, nullptr));
        attachments.back()->sendInitialUpdate();
    }
}

void PdBusGrid::paint (juce::Graphics& g)
{
    const char* letters[4] = { "\xce\xb1", "\xce\xb2", "\xce\xb3", "\xce\xb4" };
    const char* rows[3] = { "width", "FM", "anim" };
    for (int j = 0; j < 4; ++j)
    {
        const float x = 10.0f + 18.0f * (float) j;
        const auto c = filterColour (j);
        textAt (g, utf8 (letters[j]), x, 14.0f, c);
        for (int k = 0; k < 3; ++k)
        {
            const float y = 26.0f + 15.0f * (float) k;
            const bool on = selected[j] == k;
            if (on) { g.setColour (c); g.fillEllipse (x - 4.5f, y - 4.5f, 9.0f, 9.0f); }
            g.setColour (on ? c : dotOff());
            g.drawEllipse (x - 4.5f, y - 4.5f, 9.0f, 9.0f, 1.0f);
        }
    }
    for (int k = 0; k < 3; ++k)
        textAt (g, rows[k], 96.0f, 30.0f + 15.0f * (float) k, muted(), 11.0f, Align::right);
}

void PdBusGrid::mouseDown (const juce::MouseEvent& e)
{
    if (! isEnabled()) return;
    const int j = juce::roundToInt (((float) e.x - 10.0f) / 18.0f);
    const int k = juce::roundToInt (((float) e.y - 26.0f) / 15.0f);
    if (j < 0 || j > 3 || k < 0 || k > 2 || params[j] == nullptr) return;
    attachments[(size_t) j]->setValueAsCompleteGesture ((float) k);
}
