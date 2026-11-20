// Colours, sizes and text helpers of the PEDWAR panel (the approved draft, pedwar-gui-draft.html, 2026-11-19).
// Original look: dark panel, one colour per filter (alpha green, beta violet, gamma rose, delta amber), master in stone.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace PdStyle
{
    inline juce::Colour window()     { return juce::Colour (0xFF121214); }   // behind the rounded panel corners
    inline juce::Colour panel()      { return juce::Colour (0xFF1B1B1E); }
    inline juce::Colour column()     { return juce::Colour (0xFF232327); }
    inline juce::Colour line()       { return juce::Colour (0xFF34343A); }
    inline juce::Colour control()    { return juce::Colour (0xFF2A2A2F); }   // switch and menu backgrounds
    inline juce::Colour knobFace()   { return juce::Colour (0xFF2C2C32); }
    inline juce::Colour knobEdge()   { return juce::Colour (0xFF46464D); }
    inline juce::Colour track()      { return juce::Colour (0xFF3A3A41); }
    inline juce::Colour thumb()      { return juce::Colour (0xFF3B3B42); }
    inline juce::Colour dotOff()     { return juce::Colour (0xFF55555C); }
    inline juce::Colour text()       { return juce::Colour (0xFFECE9E1); }   // primary
    inline juce::Colour soft()       { return juce::Colour (0xFF9D9B93); }   // secondary
    inline juce::Colour muted()      { return juce::Colour (0xFF6E6D67); }
    inline juce::Colour master()     { return juce::Colour (0xFFB4B2A9); }

    inline juce::Colour filterColour (int f)
    {
        static const juce::uint32 c[4] = { 0xFF1D9E75, 0xFF7F77DD, 0xFFD4537E, 0xFFEF9F27 };
        return juce::Colour (c[f & 3]);
    }

    inline constexpr float dimAlpha = 0.28f;        // a control that is switched off (none since milestone 3)

    // The panel is designed at 680 x 612 units (the draft's size) and scaled to the window.
    inline constexpr int panelW = 680;
    inline constexpr int panelH = 612;

    inline juce::Font font (float size, bool bold = false)
    {
        return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
    }
    inline juce::Font mono (float size)
    {
        return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), size, juce::Font::plain));
    }

    // Text with y as its baseline (like the draft's SVG text), placed left of / centred on / right of x.
    enum class Align { left, centre, right };

    inline void textAt (juce::Graphics& g, const juce::String& s, float x, float y, juce::Colour c,
                        const juce::Font& f, Align a = Align::centre)
    {
        g.setColour (c);
        g.setFont (f);
        const float w = 400.0f, asc = f.getAscent(), h = f.getHeight();
        float left = x - w * 0.5f;
        auto just = juce::Justification::centred;
        if (a == Align::left)  { left = x;     just = juce::Justification::centredLeft; }
        if (a == Align::right) { left = x - w; just = juce::Justification::centredRight; }
        g.drawText (s, juce::Rectangle<float> (left, y - asc, w, h), just, false);
    }
    inline void textAt (juce::Graphics& g, const juce::String& s, float x, float y, juce::Colour c, float size = 11.0f,
                        Align a = Align::centre)
    {
        textAt (g, s, x, y, c, font (size), a);
    }

    inline juce::String utf8 (const char* s) { return juce::String::fromUTF8 (s); }
}
