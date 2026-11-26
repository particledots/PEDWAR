#include "Controls.h"
#include <PdFontData.h>

using namespace PdStyle;

// ------------------------------------------------------------------------------------------------ fonts and text

namespace
{
    juce::Typeface::Ptr load (const char* data, int size)
    {
        return juce::Typeface::createSystemTypefaceFor (data, (size_t) size);
    }
    struct Faces
    {
        juce::Typeface::Ptr regular = load (PdFontData::SpaceGroteskRegular_ttf, PdFontData::SpaceGroteskRegular_ttfSize);
        juce::Typeface::Ptr medium  = load (PdFontData::SpaceGroteskMedium_ttf, PdFontData::SpaceGroteskMedium_ttfSize);
        juce::Typeface::Ptr title   = load (PdFontData::MarcellusRegular_ttf, PdFontData::MarcellusRegular_ttfSize);
        juce::Typeface::Ptr greek   = load (PdFontData::GFSDidotGreek_ttf, PdFontData::GFSDidotGreek_ttfSize);
    };
    Faces& faces() { static Faces f; return f; }

    juce::Font make (juce::Typeface::Ptr face, float size, float spacing)
    {
        // If a font could not be read for any reason, fall back to the system font rather than draw nothing.
        juce::FontOptions o = face != nullptr ? juce::FontOptions (face) : juce::FontOptions();
        juce::Font f (o.withPointHeight (size));
        if (spacing > 0.0f)
            f = f.withExtraKerningFactor (spacing / f.getHeight());
        return f;
    }

    bool isGreek (juce::juce_wchar c) { return c >= 0x370 && c <= 0x3FF; }

    // The string cut into runs: Greek letters in GFS Didot (a little larger, so they sit with the capitals), the rest
    // in the font given.
    struct Run { juce::String s; juce::Font f; };
    std::vector<Run> runsOf (const juce::String& s, const juce::Font& f)
    {
        std::vector<Run> runs;
        const juce::Font g = greekFont (f.getHeight() * 1.12f).withExtraKerningFactor (f.getExtraKerningFactor());
        for (auto p = s.getCharPointer(); ! p.isEmpty();)
        {
            const bool gk = isGreek (*p);
            juce::String part;
            while (! p.isEmpty() && isGreek (*p) == gk) part += p.getAndAdvance();
            runs.push_back ({ part, gk ? g : f });
        }
        return runs;
    }
    float runWidth (const Run& r)
    {
        juce::GlyphArrangement ga;
        ga.addLineOfText (r.f, r.s, 0.0f, 0.0f);
        return ga.getBoundingBox (0, -1, true).getWidth();
    }
}

juce::Font PdStyle::font (float size, bool medium, float spacing)
{
    return make (medium ? faces().medium : faces().regular, size, spacing);
}

juce::Font PdStyle::titleFont() { return make (faces().title, titleSize, 8.0f); }

juce::Font PdStyle::greekFont (float size)
{
    juce::FontOptions o = faces().greek != nullptr ? juce::FontOptions (faces().greek) : juce::FontOptions();
    return juce::Font (o.withHeight (size));
}

float PdStyle::widthOf (const juce::String& s, const juce::Font& f)
{
    float w = 0.0f;
    for (const auto& r : runsOf (s, f)) w += runWidth (r);
    return w;
}

float PdStyle::titleWidth()
{
    juce::GlyphArrangement ga;
    ga.addLineOfText (titleFont(), "PEDWAR", 0.0f, 0.0f);
    return ga.getBoundingBox (0, -1, false).getWidth();      // the ink, without the spacing after the last letter
}

void PdStyle::textIn (juce::Graphics& g, const juce::String& s, const juce::Font& f, float x, float y, juce::Colour c,
                      juce::Justification just)
{
    const auto runs = runsOf (s, f);
    float total = 0.0f;
    std::vector<float> widths;
    for (const auto& r : runs) { widths.push_back (runWidth (r)); total += widths.back(); }
    float left = x;
    if (just == juce::Justification::centred)           left = x - total * 0.5f;
    else if (just == juce::Justification::centredRight) left = x - total;
    juce::GlyphArrangement ga;
    for (size_t i = 0; i < runs.size(); ++i)
    {
        ga.addLineOfText (runs[i].f, runs[i].s, left, y);
        left += widths[i];
    }
    g.setColour (c);
    ga.draw (g);
}

// ------------------------------------------------------------------------------------------------ look

PdLookAndFeel::PdLookAndFeel()
{
    setColour (juce::PopupMenu::backgroundColourId, menu());
    setColour (juce::PopupMenu::textColourId, text());
    setColour (juce::PopupMenu::highlightedBackgroundColourId, lampFill());
    setColour (juce::PopupMenu::highlightedTextColourId, text());
    setColour (juce::ComboBox::textColourId, text());
    setColour (juce::ComboBox::backgroundColourId, panel());
    setColour (juce::ComboBox::outlineColourId, gold());
    setColour (juce::ComboBox::arrowColourId, gold());
    setColour (juce::Label::textColourId, text());
}

void PdLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&)
{
    const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (0.5f);
    g.setColour (gold());
    g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 0.8f);
    juce::Path chevron;
    const float cx = (float) w - 11.0f, cy = (float) h * 0.5f;
    chevron.startNewSubPath (cx - 3.5f, cy - 2.0f);
    chevron.lineTo (cx, cy + 2.0f);
    chevron.lineTo (cx + 3.5f, cy - 2.0f);
    g.strokePath (chevron, juce::PathStrokeType (0.9f));
}

void PdLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBorderSize (juce::BorderSize<int> (0));
    label.setBounds (8, 0, box.getWidth() - 8 - 18, box.getHeight());
    label.setJustificationType (juce::Justification::centredLeft);
    label.setFont (menuFont());
    label.setMinimumHorizontalScale (1.0f);      // never squeezed: the fit check makes sure every entry fits
}

// ------------------------------------------------------------------------------------------------ knob

namespace
{
    // Angle 0 = 12 o'clock, clockwise. Knobs turn from -135 to +135 degrees.
    constexpr float kStart = -135.0f, kEnd = 135.0f;
}

PdKnob::PdKnob (juce::AudioProcessorValueTreeState& state, const char* paramId, float radius, const juce::String& l,
                float readoutRoomUnits, Readout ro)
    : juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox),
      param (state.getParameter (paramId)), r (radius), room (readoutRoomUnits), label (l), readout (std::move (ro))
{
    setRotaryParameters (juce::degreesToRadians (kStart), juce::degreesToRadians (kEnd), true);
    setVelocityModeParameters (0.6, 1, 0.0, true, juce::ModifierKeys::shiftModifier);   // hold Shift for fine moves
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramId, *this);
    if (param != nullptr)
        setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));   // double-click: default
    setComponentID (paramId);
}

void PdKnob::placeAt (float x, float y)
{
    cx = x;
    cy = y;
    const float w = juce::jmax (room, 2.0f * ringRadius() + 6.0f);
    const float top = cy - ringRadius() - 4.0f;
    // The knob's own area: the ring, and the value under it (the name between them is fixed artwork).
    setBounds (juce::roundToInt (cx - w * 0.5f), juce::roundToInt (top), juce::roundToInt (w),
               juce::roundToInt (readoutBaseline() + 5.0f - top));
}

bool PdKnob::hitTest (int x, int y)
{
    const float dx = (float) x + (float) getX() - cx, dy = (float) y + (float) getY() - cy;
    const float reach = arcRadius() + 3.0f;
    return dx * dx + dy * dy <= reach * reach;
}

void PdKnob::paint (juce::Graphics& g)
{
    const float x = cx - (float) getX(), y = cy - (float) getY();
    const float pos = param != nullptr ? param->getValue() : 0.0f;
    const float a = kStart + (kEnd - kStart) * pos;

    if (mod.depth > 0.002f)
    {
        // the LFOs' swing: a dotted cream ring outside the gold arc, and a dot where they have the setting now
        auto angle = [] (float p) { return kStart + (kEnd - kStart) * juce::jlimit (0.0f, 1.0f, p); };
        const float rr = ringRadius();
        const float a0 = angle (pos - mod.depth), a1 = angle (pos + mod.depth);
        if (a1 > a0 + 0.01f)
        {
            juce::Path p, d;
            p.addCentredArc (x, y, rr, rr, 0.0f, juce::degreesToRadians (a0), juce::degreesToRadians (a1), true);
            const float dashes[] = { 1.6f, 2.4f };
            juce::PathStrokeType (1.1f).createDashedStroke (d, p, dashes, 2);
            g.setColour (modRing());
            g.fillPath (d);
        }
        const float dr = juce::degreesToRadians (angle (pos + mod.now));
        g.setColour (cap());
        g.fillEllipse (x + rr * std::sin (dr) - 2.2f, y - rr * std::cos (dr) - 2.2f, 4.4f, 4.4f);
    }

    auto disc = [&] (float radius, juce::Colour fill, juce::Colour line, float lw)
    {
        g.setColour (fill);
        g.fillEllipse (x - radius, y - radius, radius * 2.0f, radius * 2.0f);
        g.setColour (line);
        g.drawEllipse (x - radius, y - radius, radius * 2.0f, radius * 2.0f, lw);
    };
    disc (r, skirt(), skirtLine(), 1.1f);
    const float capR = r * 0.73f;
    disc (capR, cap(), capLine(), 0.8f);
    const float rad = juce::degreesToRadians (a), sx = std::sin (rad), sy = -std::cos (rad);
    juce::Path p;
    p.startNewSubPath (x + capR * 0.3f * sx, y + capR * 0.3f * sy);
    p.lineTo (x + capR * 0.94f * sx, y + capR * 0.94f * sy);
    g.setColour (pointer());
    g.strokePath (p, juce::PathStrokeType (juce::jmax (1.4f, r * 0.09f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (param != nullptr)
        textIn (g, readoutText(), valueFont(), x, readoutBaseline() - (float) getY(), muted(), juce::Justification::centred);
}

juce::String PdKnob::readoutAt (float normalised) const
{
    if (param == nullptr) return {};
    const float v = param->convertFrom0to1 (normalised);
    return readout ? readout (v) : param->getText (normalised, 20);
}

juce::String PdKnob::readoutText() const
{
    if (param == nullptr) return {};
    const float v = param->convertFrom0to1 (param->getValue());
    return readout ? readout (v) : param->getCurrentValueAsText();
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

// ------------------------------------------------------------------------------------------------ switches

namespace { constexpr float kPartPad = 8.0f; }   // room around a pill choice's label

PdSegments::PdSegments (juce::RangedAudioParameter& p, juce::StringArray l, Kind k, juce::Array<float> v)
    : param (p), labels (std::move (l)), values (std::move (v)), kind (k),
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
        for (int i = 0; i < (kind == Kind::Option ? 2 : labels.size()); ++i) values.add ((float) i);
    attachment.sendInitialUpdate();
    setComponentID (p.getParameterID());
}

float PdSegments::optionWidth (const juce::String& label) { return 17.0f + widthOf (label, labelFont()) + 2.0f; }

float PdSegments::neededWidth (int i) const
{
    if (kind == Kind::Option) return optionWidth (labels[0]);
    return widthOf (labels[i], switchFont()) + 4.0f;
}

float PdSegments::partX (int i) const
{
    // Each part is as wide as its label needs, and the spare room is shared out evenly.
    const int n = labels.size();
    float need = 0.0f;
    for (int k = 0; k < n; ++k) need += widthOf (labels[k], switchFont()) + kPartPad;
    const float spare = ((float) getWidth() - need) / (float) n;
    float x = 0.0f;
    for (int k = 0; k < i; ++k) x += widthOf (labels[k], switchFont()) + kPartPad + spare;
    return x;
}

float PdSegments::roomFor (int i) const
{
    if (kind == Kind::Option) return (float) getWidth();
    return partX (i + 1) - partX (i);
}

void PdSegments::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    if (kind == Kind::Option)
    {
        const bool on = selected == 1;
        drawLamp (g, b.getX() + 7.0f, b.getCentreY(), on, on ? 3.2f : 3.0f);
        textIn (g, labels[0], labelFont(), b.getX() + 17.0f, b.getCentreY() + 3.5f, on ? text() : soft(),
                juce::Justification::centredLeft);
        return;
    }

    const auto r = b.reduced (0.5f);
    const int n = labels.size();
    const float rad = r.getHeight() * 0.5f;
    // the chosen part, lit: a blue fill with a blue edge, clipped to the pill's shape
    {
        const float x0 = r.getX() + partX (selected), x1 = r.getX() + partX (selected + 1);
        juce::Path pill;
        pill.addRoundedRectangle (r, rad);
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (pill);
        g.setColour (lampFill());
        g.fillRect (juce::Rectangle<float> (x0, r.getY(), x1 - x0, r.getHeight()));
        g.setColour (lamp());
        juce::Path lit;
        lit.addRoundedRectangle (x0 + 0.6f, r.getY() + 0.6f, x1 - x0 - 1.2f, r.getHeight() - 1.2f, rad - 0.6f,
                                 rad - 0.6f, selected == 0, selected == n - 1, selected == 0, selected == n - 1);
        g.strokePath (lit, juce::PathStrokeType (1.0f));
    }
    g.setColour (gold());
    g.drawRoundedRectangle (r, rad, 0.7f);
    for (int i = 1; i < n; ++i)
        if (i != selected && i != selected + 1)
            g.fillRect (r.getX() + partX (i) - 0.35f, r.getY() + 0.5f, 0.7f, r.getHeight() - 1.0f);
    for (int i = 0; i < n; ++i)
    {
        const float cx = r.getX() + (partX (i) + partX (i + 1)) * 0.5f;
        textIn (g, labels[i], switchFont(), cx, r.getCentreY() + 3.5f, i == selected ? text() : muted(),
                juce::Justification::centred);
    }
}

void PdSegments::mouseDown (const juce::MouseEvent& e)
{
    if (! isEnabled()) return;
    if (kind == Kind::Option)
    {
        attachment.setValueAsCompleteGesture (values[selected == 1 ? 0 : 1]);
        return;
    }
    const int n = labels.size();
    int i = n - 1;
    for (int k = 1; k < n; ++k)
        if ((float) e.x < partX (k)) { i = k - 1; break; }
    attachment.setValueAsCompleteGesture (values[i]);
}

// ------------------------------------------------------------------------------------------------ menu

PdMenu::PdMenu (juce::AudioProcessorValueTreeState& state, const char* paramId, const juce::StringArray& shown)
{
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (paramId)))
        for (int i = 0; i < p->choices.size(); ++i)
            addItem (i < shown.size() ? shown[i] : p->choices[i], i + 1);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, paramId, *this);
    setComponentID (paramId);
}

// ------------------------------------------------------------------------------------------------ fader

PdFader::PdFader (juce::AudioProcessorValueTreeState& state, const char* paramId, const juce::String& l)
    : juce::Slider (juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox),
      param (state.getParameter (paramId)), letter (l)
{
    setMouseDragSensitivity (120);               // drag about the fader's own length for the whole range
    setVelocityModeParameters (0.6, 1, 0.0, true, juce::ModifierKeys::shiftModifier);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramId, *this);
    if (param != nullptr)
        setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
    setComponentID (paramId);
}

void PdFader::placeAt (float x, float top)
{
    setBounds (juce::roundToInt (x - kW * 0.5f), juce::roundToInt (top - 8.0f), (int) kW, juce::roundToInt (kLen + 46.0f));
}

juce::String PdFader::readoutText() const
{
    if (param == nullptr) return {};
    const float db = param->convertFrom0to1 (param->getValue());
    return db <= -59.95f ? juce::String ("-inf") : juce::String (juce::roundToInt (db));
}

void PdFader::paint (juce::Graphics& g)
{
    const float cx = kW * 0.5f, top = 8.0f, len = kLen;
    const float v = param != nullptr ? param->getValue() : 0.0f;
    // the slot: a gold hairline outline, gold below the thumb (the level that passes)
    g.setColour (gold());
    g.drawRoundedRectangle (cx - 2.5f, top, 5.0f, len, 2.5f, 0.7f);
    const float ty = top + len * (1.0f - v);
    g.setColour (gold().withAlpha (0.55f));
    g.fillRoundedRectangle (cx - 1.0f, ty, 2.0f, top + len - ty, 1.0f);
    // the thumb: a cream cap with a dark line across
    const auto th = juce::Rectangle<float> (cx - 10.0f, ty - 4.5f, 20.0f, 9.0f);
    g.setColour (cap());
    g.fillRoundedRectangle (th, 2.0f);
    g.setColour (capLine());
    g.drawRoundedRectangle (th, 2.0f, 0.8f);
    g.setColour (pointer());
    g.fillRect (th.getX() + 3.0f, ty - 0.6f, th.getWidth() - 6.0f, 1.2f);

    textIn (g, letter, font (12.0f), cx, top + len + 20.0f, goldText(), juce::Justification::centred);
    textIn (g, readoutText(), valueFont(), cx, top + len + 33.0f, muted(), juce::Justification::centred);
}

// ------------------------------------------------------------------------------------------------ strike lamp

PdStrikeLight::PdStrikeLight (juce::RangedAudioParameter& p)
    : param (p),
      attachment (p, [this] (float v) { on = v > 0.5f; repaint(); }, nullptr)
{
    attachment.sendInitialUpdate();
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setComponentID (p.getParameterID());
}

void PdStrikeLight::paint (juce::Graphics& g)
{
    const float cy = (float) getHeight() * 0.5f;
    drawLamp (g, 7.0f, cy, on, on ? 3.2f : 3.0f, on ? glow : 0.0f);
    textIn (g, "STRIKE", labelFont(), 17.0f, cy + 3.5f, on ? text() : soft(), juce::Justification::centredLeft);
}

void PdStrikeLight::mouseDown (const juce::MouseEvent&)
{
    if (isEnabled())
        attachment.setValueAsCompleteGesture (on ? 0.0f : 1.0f);
}

// ------------------------------------------------------------------------------------------------ bus grid

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
    setComponentID ("busGrid");
}

void PdBusGrid::paint (juce::Graphics& g)
{
    const char* letters[4] = { "\xce\xb1", "\xce\xb2", "\xce\xb3", "\xce\xb4" };
    const char* rows[3] = { "WIDTH", "FM", "ANIM" };
    for (int j = 0; j < 4; ++j)
    {
        const float x = colX0 + colStep * (float) j;
        textIn (g, utf8 (letters[j]), font (11.0f), x, 13.0f, goldText(), juce::Justification::centred);
        for (int k = 0; k < 3; ++k)
            drawLamp (g, x, rowY0 + rowStep * (float) k, selected[j] == k, selected[j] == k ? 3.2f : 3.0f);
    }
    for (int k = 0; k < 3; ++k)
        textIn (g, rows[k], labelFont(), (float) getWidth(), rowY0 + rowStep * (float) k + 3.5f, muted(),
                juce::Justification::centredRight);
}

void PdBusGrid::mouseDown (const juce::MouseEvent& e)
{
    if (! isEnabled()) return;
    const int j = juce::roundToInt (((float) e.x - colX0) / colStep);
    const int k = juce::roundToInt (((float) e.y - rowY0) / rowStep);
    if (j < 0 || j > 3 || k < 0 || k > 2 || params[j] == nullptr) return;
    attachments[(size_t) j]->setValueAsCompleteGesture ((float) k);
}
