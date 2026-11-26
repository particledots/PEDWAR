// Colours, fonts and text helpers of the PEDWAR panel.
// 0.3.2 look, the gold "sections" look of the particledots plugins (from Anamnesis 0.3.1): black panel, gold hairline
// frames and flow lines, cream knob caps on black skirts inside gold range arcs, blue lamps as the only colour (they
// mean "on" or "chosen"), spaced capitals, the title in Marcellus between two ornament lines.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace PdStyle
{
    inline juce::Colour panel()       { return juce::Colour (0xFF0B0B0C); }
    inline juce::Colour window()      { return juce::Colour (0xFF0B0B0C); }   // behind the rounded panel corners
    inline juce::Colour gold()        { return juce::Colour (0xFFB08D57); }   // frames, lines, outlines
    inline juce::Colour goldText()    { return juce::Colour (0xFFD4B27A); }   // the title, section and row titles
    inline juce::Colour text()        { return juce::Colour (0xFFE8E1D0); }   // labels, chosen settings
    inline juce::Colour soft()        { return juce::Colour (0xFFB5AC98); }   // labels of switches that are off
    inline juce::Colour muted()       { return juce::Colour (0xFFA39886); }   // values, choices not chosen
    inline juce::Colour faint()       { return juce::Colour (0xFF5E574B); }
    inline juce::Colour lamp()        { return juce::Colour (0xFF4C9BFF); }   // lit lamps
    inline juce::Colour lampText()    { return juce::Colour (0xFF7DB6FF); }
    inline juce::Colour lampFill()    { return juce::Colour (0xFF1B3A66); }
    inline juce::Colour skirt()       { return juce::Colour (0xFF121212); }   // knob skirts
    inline juce::Colour skirtLine()   { return juce::Colour (0xFF2E2A22); }
    inline juce::Colour cap()         { return juce::Colour (0xFFE9E2D2); }   // knob caps, fader thumbs
    inline juce::Colour capLine()     { return juce::Colour (0xFFBFB6A2); }
    inline juce::Colour pointer()     { return juce::Colour (0xFF161514); }
    inline juce::Colour menu()        { return juce::Colour (0xFF16161A); }   // pop-up menus
    inline juce::Colour modRing()     { return juce::Colour (0xFFE8E1D0).withAlpha (0.55f); }   // the LFOs' swing

    inline constexpr float dimAlpha = 0.38f;        // a control that is switched off (none since milestone 3)

    // The panel is designed at 760 x 726 units and scaled to the window (drag the corner; the shape stays the same).
    inline constexpr int panelW = 760;
    inline constexpr int panelH = 726;

    // The title "PEDWAR" is centred at x = 380 and must stay between these two x positions at every window size
    // (inside the two gold ornament lines; checked by PedwarCheck, also in the drawn pixels).
    inline constexpr float titleCentre = 380.0f, titleLeft = 300.0f, titleRight = 460.0f;
    inline constexpr float titleSize = 24.0f, titleBaseline = 33.0f;
    inline constexpr float ornamentLeftEnd = 290.0f, ornamentRightStart = 470.0f;
    inline constexpr float ornamentOuterLeft = 170.0f, ornamentOuterRight = 590.0f;

    // Space Grotesk (built in, so every Mac and the check measure the same); spacing = extra letter spacing in units.
    juce::Font font (float size, bool medium = false, float spacing = 0.0f);
    juce::Font titleFont();                          // Marcellus
    juce::Font greekFont (float size);               // GFS Didot: the Greek letters alpha .. delta

    // Text with y as its baseline (like the draft's SVG text), placed left of / centred on / right of x. The Greek
    // letters in it are drawn in GFS Didot (Space Grotesk has none), everything else in the font given.
    void textIn (juce::Graphics&, const juce::String&, const juce::Font&, float x, float y, juce::Colour,
                 juce::Justification);
    float widthOf (const juce::String&, const juce::Font&);

    inline void textAt (juce::Graphics& g, const juce::String& s, float x, float y, juce::Colour c, float size = 11.0f,
                        juce::Justification just = juce::Justification::centred, bool medium = false, float spacing = 0.0f)
    {
        textIn (g, s, font (size, medium, spacing), x, y, c, just);
    }
    inline float textWidth (const juce::String& s, float size, bool medium = false, float spacing = 0.0f)
    {
        return widthOf (s, font (size, medium, spacing));
    }
    float titleWidth();

    // The fonts of the panel's kinds of text (sizes in panel units)
    inline juce::Font capsFont()    { return font (11.0f, false, 2.0f); }    // section titles
    inline juce::Font rowFont()     { return font (10.0f, false, 1.5f); }    // row titles inside a section
    inline juce::Font labelFont()   { return font (10.0f, false, 1.2f); }    // knob names, switch labels
    inline juce::Font valueFont()   { return font (10.5f); }                 // values under the knobs
    inline juce::Font switchFont()  { return font (9.5f, false, 0.8f); }    // the choices in a pill
    inline juce::Font menuFont()    { return font (10.5f, false, 0.3f); }    // the text in a menu

    // A lamp: lit = blue with a soft halo; off = a thin ring. glow (0..1) brightens the halo (the strike flash).
    inline void drawLamp (juce::Graphics& g, float cx, float cy, bool lit, float r = 3.2f, float glow = 0.0f)
    {
        if (lit)
        {
            const float h = 2.2f + 1.2f * glow;
            g.setColour (lamp().withAlpha (0.22f + 0.4f * glow));
            g.fillEllipse (cx - r * h, cy - r * h, r * 2.0f * h, r * 2.0f * h);
            g.setColour (glow > 0.01f ? lamp().interpolatedWith (juce::Colours::white, 0.45f * glow) : lamp());
            g.fillEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f);
        }
        else
        {
            g.setColour (muted());
            g.drawEllipse (cx - r + 0.3f, cy - r + 0.3f, r * 2.0f - 0.6f, r * 2.0f - 0.6f, 0.9f);
        }
    }

    inline juce::String utf8 (const char* s) { return juce::String::fromUTF8 (s); }
}
