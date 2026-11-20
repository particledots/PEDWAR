#include "PluginEditor.h"

using namespace PdStyle;
using namespace PdParams;

namespace
{
    constexpr float kDefaultScale = 1.15f;         // opens at about 780 x 700
    constexpr int kLiveMilestone = 2;              // controls of this milestone and earlier work; later ones are dimmed
    constexpr int kMasterX = 504, kMasterW = 168;
    int columnX (int f) { return 8 + f * 124; }

    const char* const kLetters[4] = { "\xce\xb1", "\xce\xb2", "\xce\xb3", "\xce\xb4" };
    const char* const kNames[4] = { "alpha", "beta", "gamma", "delta" };

    void tag (juce::Graphics& g, float x, float y, int m)
    {
        g.setColour (m == 2 ? tagM2() : tagM3());
        g.fillRoundedRectangle (x, y, 26.0f, 15.0f, 3.0f);
        textAt (g, m == 2 ? "M2" : "M3", x + 13.0f, y + 11.0f, juce::Colours::white, 11.0f);
    }

    void dashedLine (juce::Graphics& g, float x0, float x1, float y, juce::Colour c)
    {
        const float dashes[] = { 3.0f, 3.0f };
        g.setColour (c);
        g.drawDashedLine (juce::Line<float> (x0, y, x1, y), dashes, 2, 1.0f);
    }
}

// ================================================================================================ panel

PedwarPanel::PedwarPanel (PedwarAudioProcessor& p) : proc (p)
{
    setLookAndFeel (&lnf);
    auto& st = proc.getValueTreeState();

    // Top bar (M3): the Preset menu and the Quality menu.
    presetMenu.addItem ("Init", 1);
    presetMenu.setSelectedId (1, juce::dontSendNotification);
    presetMenu.setBounds (318, 15, 140, 20);
    addAndMakeVisible (presetMenu);
    later (presetMenu, 3);
    menu (quality, 466, 15, 70, 3);

    // The four filters
    for (int f = 0; f < numFilters; ++f)
    {
        const int x0 = columnX (f);
        const float cx = (float) x0 + 59.0f;
        const auto c = filterColour (f);

        segs (id (f, Mode), { "Filter", "Res", "Osc" }, c, x0 + 8, 101, 102, {}, 1);
        knob (f, Gain, cx - 30, 158, 11, "gain", 1);
        segs (id (f, InLimit), { "off", "tanh" }, c, (int) cx + 6, 149, 48, "in limit", 1);
        auto& coarse = knob (f, Coarse, cx - 17, 232, 20, "coarse", 1, [] (float v) { return hzText (v); });
        knob (f, Fine, cx + 36, 222, 9, "fine", 1);
        auto& q = knob (f, Q, cx - 28, 312, 14, "Q", 1);

        knob (f, FmAmt, cx + 32, 308, 9, "FM amt", 2);
        auto* ends = st.getParameter (id (f, MorphEnds));
        auto* morphParam = st.getParameter (id (f, Morph));
        auto& morph = knob (f, Morph, cx - 28, 383, 14, "morph", 2, [ends, morphParam] (float v)
        {
            // the panel names the right-hand end by the Morph ends switch (morph's range is 0..1, so v is normalised)
            if (pedwar::morphMultiple (v) < 0.0f) return juce::String (ends != nullptr && ends->getValue() > 0.5f ? "BP" : "HP");
            return morphParam != nullptr ? morphParam->getText (v, 20) : juce::String();
        });
        knob (f, Width, cx + 32, 379, 10, "width", 2);
        // 3 units lower than the draft (and the Morph and Width knobs 3 higher), so the Morph readout is not hidden
        segs (id (f, MorphEnds), { utf8 ("LP\xc2\xb7HP"), "BP" }, c, x0 + 8, 431, 102, "morph ends", 2);

        // Modulation rings: where the LFOs move Coarse (octaves over the knob's 10.3), Q (over its 6.6) and Morph.
        const auto& eng = proc.getEngine();
        auto ring = [&eng, f] (int target, float span)
        {
            return [&eng, f, target, span]
            {
                return PdKnob::Modulation { eng.modNow (f, target) / span, eng.modDepth (f, target) / span };
            };
        };
        coarse.setModulation (ring (pedwar::Engine::ModCoarse, std::log2 (20000.0f / 16.0f)));
        q.setModulation (ring (pedwar::Engine::ModQ, std::log2 (50.0f / 0.5f)));
        morph.setModulation (ring (pedwar::Engine::ModMorph, 1.0f));
        modKnobs.push_back (&coarse);
        modKnobs.push_back (&q);
        modKnobs.push_back (&morph);

        segs (id (f, OutLimit), { "off", "on" }, c, x0 + 8, 470, 48, "out limit", 1);
        segs (id (f, KeyTrack), { "off", "on" }, c, x0 + 62, 470, 48, "key trk", 1);
        menu (id (f, Tap), x0 + 8, 508, 102, 1, { "stereo morph", "LP", "BP", "HP", "notch" });

        auto light = std::make_unique<PdStrikeLight> (*st.getParameter (id (f, Strike)), c);
        light->setBounds (x0 + 8, 540, 70, 18);
        addAndMakeVisible (*light);
        lights[f] = light.get();
        lastStrikes[f] = proc.getEngine().strikeCount (f);
        owned.push_back (std::move (light));
    }

    // Links: alpha leads beta, delta leads gamma.
    for (int i : { 0, 2 })
    {
        const float m = ((float) columnX (i) + 59.0f + (float) columnX (i + 1) + 59.0f) * 0.5f;
        segs (i == 0 ? linkAB : linkGD, { "link", "off" }, filterColour (i), juce::roundToInt (m - 36.0f), 571, 72, {}, 2, { 1.0f, 0.0f });
    }

    // Master strip
    const int mx = kMasterX;
    knob (offset, master(), (float) mx + 38, 124, 14, "offset", 2);
    segs (pitchSplit, { "all", "split" }, master(), mx + 80, 112, 78, utf8 ("pitch \xce\xb3\xce\xb4"), 2);
    knob (lfoRate, master(), (float) mx + 34, 222, 13, "rate", 2);
    {
        auto grid = std::make_unique<PdBusGrid> (st, busIds);
        grid->setBounds (570, 184, 102, 66);
        addAndMakeVisible (*grid);
        later (*grid, 2);
        owned.push_back (std::move (grid));
    }
    for (int f = 0; f < numFilters; ++f)
    {
        auto fader = std::make_unique<PdFader> (st, id (f, Level), filterColour (f), utf8 (kLetters[f]));
        fader->placeAt ((float) (mx + 30 + f * 36));
        addAndMakeVisible (*fader);
        owned.push_back (std::move (fader));
    }
    segs (stereoMode, { "sum", utf8 ("\xc3\xbc" "ber") }, master(), mx + 10, 414, 70, "stereo", 3);
    segs (masterLimit, { "off", "on" }, master(), mx + 88, 414, 70, "master lim", 1);
    segs (pitchSnap, { "Off", "Semis", "Steps" }, master(), mx + 10, 478, 148, {}, 3);
    menu (pitchScale, mx + 10, 506, 96, 3);
    menu (pitchKey, mx + 112, 506, 46, 3);

    startTimerHz (30);
}

PedwarPanel::~PedwarPanel()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void PedwarPanel::later (juce::Component& c, int milestone)
{
    if (milestone <= kLiveMilestone) return;
    c.setEnabled (false);
    c.setAlpha (dimAlpha);
}

PdKnob& PedwarPanel::knob (int filter, int slot, float cx, float cy, float r, const juce::String& label, int milestone,
                           PdKnob::Readout ro)
{
    auto k = std::make_unique<PdKnob> (proc.getValueTreeState(), id (filter, slot), filterColour (filter), r, label, std::move (ro));
    k->placeAt (cx, cy);
    addAndMakeVisible (*k);
    later (*k, milestone);
    auto* raw = k.get();
    owned.push_back (std::move (k));
    return *raw;
}

PdKnob& PedwarPanel::knob (const char* pid, juce::Colour c, float cx, float cy, float r, const juce::String& label, int milestone)
{
    auto k = std::make_unique<PdKnob> (proc.getValueTreeState(), pid, c, r, label);
    k->placeAt (cx, cy);
    addAndMakeVisible (*k);
    later (*k, milestone);
    auto* raw = k.get();
    owned.push_back (std::move (k));
    return *raw;
}

PdSegments& PedwarPanel::segs (const char* pid, juce::StringArray labels, juce::Colour c, int x, int y, int w,
                               const juce::String& caption, int milestone, juce::Array<float> values)
{
    auto* p = proc.getValueTreeState().getParameter (pid);
    jassert (p != nullptr);
    auto s = std::make_unique<PdSegments> (*p, std::move (labels), c, caption, std::move (values));
    s->place (x, y, w);
    addAndMakeVisible (*s);
    later (*s, milestone);
    auto* raw = s.get();
    owned.push_back (std::move (s));
    return *raw;
}

PdMenu& PedwarPanel::menu (const char* pid, int x, int y, int w, int milestone, const juce::StringArray& shown)
{
    auto m = std::make_unique<PdMenu> (proc.getValueTreeState(), pid, shown);
    m->setBounds (x, y, w, 20);
    addAndMakeVisible (*m);
    later (*m, milestone);
    auto* raw = m.get();
    owned.push_back (std::move (m));
    return *raw;
}

void PedwarPanel::timerCallback()
{
    // Strike lights: a flash on every strike, fading out over about 0.15 s.
    for (int f = 0; f < numFilters; ++f)
    {
        const int n = proc.getEngine().strikeCount (f);
        auto* l = lights[f];
        if (n != lastStrikes[f]) { lastStrikes[f] = n; l->flash (1.0f); }
        else if (l->currentGlow() > 0.01f) l->flash (l->currentGlow() * 0.72f);
        else if (l->currentGlow() > 0.0f) l->flash (0.0f);
    }
    // Modulation rings on Coarse, Q and Morph.
    for (auto* k : modKnobs) k->refreshModulation();
    // Each filter's input label: "side chain" in the filter's colour while that filter is taking the side chain.
    bool changed = false;
    for (int f = 0; f < numFilters; ++f)
    {
        const bool sc = proc.getEngine().filterUsesSidechain (f);
        if (sc != shownSidechain[f]) { shownSidechain[f] = sc; changed = true; }
    }
    if (changed)
        repaint (0, 80, kMasterX, 16);
}

void PedwarPanel::paint (juce::Graphics& g)
{
    g.fillAll (window());
    g.setColour (panel());
    g.fillRoundedRectangle (0.0f, 0.0f, (float) panelW, (float) panelH, 10.0f);

    // Header
    textAt (g, "PEDWAR", 16, 29, text(), 16.0f, Align::left);
    textAt (g, "quad resonant filter", 92, 29, muted(), 11.0f, Align::left);
    textAt (g, "particledots", (float) panelW - 16.0f, 29, muted(), 11.0f, Align::right);

    // Filter columns
    for (int f = 0; f < numFilters; ++f)
    {
        const float x0 = (float) columnX (f);
        const auto c = filterColour (f);
        g.setColour (column());
        g.fillRoundedRectangle (x0, 50.0f, 118.0f, 512.0f, 8.0f);
        g.setColour (line());
        g.drawRoundedRectangle (x0 + 0.5f, 50.5f, 117.0f, 511.0f, 8.0f, 1.0f);
        g.setColour (c);
        g.fillRoundedRectangle (x0, 50.0f, 118.0f, 4.0f, 2.0f);

        textAt (g, utf8 (kLetters[f]), x0 + 12.0f, 76.0f, c, 17.0f, Align::left);
        textAt (g, kNames[f], x0 + 30.0f, 75.0f, text(), 13.0f, Align::left);
        // alpha / beta: "track in" (grey) unless they have switched to the side chain; gamma / delta (and every filter
        // of PEDWAR Synth): "side chain", dark while there is none. In use: the filter's colour, with a dot.
        const bool onSide = shownSidechain[f];
        const bool sideLabel = onSide || PedwarAudioProcessor::isSynth || f >= pedwar::Gamma;
        const auto labelColour = onSide ? c : (sideLabel ? muted() : soft());
        if (onSide)
        {
            g.setColour (c);
            g.fillEllipse (x0 + 12.0f, 84.0f, 6.0f, 6.0f);
        }
        textAt (g, sideLabel ? "side chain" : "track in", x0 + (onSide ? 22.0f : 12.0f), 91.0f, labelColour, 11.0f, Align::left);
    }

    // Links
    for (int i : { 0, 2 })
    {
        const float a = (float) columnX (i) + 59.0f, b = (float) columnX (i + 1) + 59.0f;
        dashedLine (g, a, b, 580.0f, filterColour (i).withAlpha (0.6f));
        textAt (g, utf8 (i == 0 ? "\xce\xb1 leads \xce\xb2" : "\xce\xb4 leads \xce\xb3"), (a + b) * 0.5f, 604.0f, muted(), 11.0f);
    }

    // Master strip
    const float mx = (float) kMasterX;
    g.setColour (column());
    g.fillRoundedRectangle (mx, 50.0f, (float) kMasterW, 512.0f, 8.0f);
    g.setColour (line());
    g.drawRoundedRectangle (mx + 0.5f, 50.5f, (float) kMasterW - 1.0f, 511.0f, 8.0f, 1.0f);
    textAt (g, "master", mx + 12.0f, 76.0f, text(), 13.0f, Align::left);
    textAt (g, "modulation", mx + 12.0f, 184.0f, text(), 11.0f, Align::left);
    textAt (g, "levels", mx + 12.0f, 282.0f, text(), 11.0f, Align::left);
    textAt (g, "pitch snap", mx + 12.0f, 470.0f, text().withAlpha (dimAlpha), 11.0f, Align::left);
    textAt (g, "MIDI in: strike + key track", mx + (float) kMasterW * 0.5f, 550.0f, muted(), 11.0f);
}

void PedwarPanel::paintOverChildren (juce::Graphics& g)
{
    // Milestone tags (not dimmed), where the draft has them. Milestone 2's controls work now, so only M3 is left.
    tag (g, 318, 1, 3);
    tag (g, (float) (kMasterX + 54), 402, 3);
    tag (g, (float) (kMasterX + kMasterW - 34), 458, 3);
}

// ================================================================================================ editor

PedwarAudioProcessorEditor::PedwarAudioProcessorEditor (PedwarAudioProcessor& p)
    : juce::AudioProcessorEditor (p), panel (p)
{
    addAndMakeVisible (panel);
    panel.setBounds (0, 0, panelW, panelH);

    setResizable (true, true);
    setResizeLimits ((int) (panelW * 0.9f), (int) (panelH * 0.9f), (int) (panelW * 2.0f), (int) (panelH * 2.0f));
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) panelW / (double) panelH);
    setSize (juce::roundToInt (panelW * kDefaultScale), juce::roundToInt (panelH * kDefaultScale));
}

void PedwarAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (window());
}

void PedwarAudioProcessorEditor::resized()
{
    const float s = (float) getWidth() / (float) panelW;
    panel.setTransform (juce::AffineTransform::scale (s));
}
