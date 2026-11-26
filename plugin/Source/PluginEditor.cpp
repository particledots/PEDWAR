#include "PluginEditor.h"

using namespace PdStyle;
using namespace PdParams;
using Kind = PdSegments::Kind;

namespace
{
    constexpr float kDefaultScale = 1.0f;          // opens at 760 x 726

    // Filter columns and the master column (panel units)
    constexpr int kColW = 134, kTop = 58, kColBottom = 662;
    int colX (int f) { return 10 + f * 140; }
    float colCentre (int f) { return (float) colX (f) + (float) kColW * 0.5f; }
    constexpr int kMasterX = 572, kMasterW = 178;

    // Row titles inside each filter frame: the y of their hairline
    struct Row { float y; const char* title; };
    const Row kFilterRows[] = { { 124.0f, "DRIVE" }, { 210.0f, "TUNING" }, { 340.0f, "RESONANCE \xc2\xb7 FM" },
                                { 446.0f, "STEREO MORPH" }, { 580.0f, "OUTPUT" } };

    // Frames, each with its title set into its top line
    struct Frame { float x, y, w, h; juce::String title; };
    const char* const kFilterTitles[4] = { "\xce\xb1  ALPHA", "\xce\xb2  BETA", "\xce\xb3  GAMMA", "\xce\xb4  DELTA" };
    std::vector<Frame> frames()
    {
        std::vector<Frame> v;
        for (int f = 0; f < 4; ++f)
            v.push_back ({ (float) colX (f), (float) kTop, (float) kColW, (float) (kColBottom - kTop), utf8 (kFilterTitles[f]) });
        v.push_back ({ (float) kMasterX, 58.0f, (float) kMasterW, 72.0f, "PRESET" });
        v.push_back ({ (float) kMasterX, 140.0f, (float) kMasterW, 204.0f, "PITCH" });
        v.push_back ({ (float) kMasterX, 354.0f, (float) kMasterW, 90.0f, "MODULATION" });
        v.push_back ({ (float) kMasterX, 454.0f, (float) kMasterW, (float) kColBottom - 454.0f, "MIX" });
        return v;
    }
    constexpr float kFrameTitleInset = 16.0f;

    // The flow lines
    constexpr float kOutLane = 676.0f;             // the four outputs run along here to the mix
    constexpr float kOutRiseX = 568.0f, kMixArriveY = 516.0f;
    constexpr float kLinkLane = 698.0f;            // the link lines (leader -> follower)
    float linkLeaderX (int pair)   { return pair == 0 ? (float) colX (0) + 24.0f : (float) colX (3) + 110.0f; }
    float linkFollowerX (int pair) { return pair == 0 ? (float) colX (1) + 110.0f : (float) colX (2) + 24.0f; }
    float linkMid (int pair) { return (linkLeaderX (pair) + linkFollowerX (pair)) * 0.5f; }

    // The "track in / side chain" line under each filter's title (redrawn while it changes)
    juce::Rectangle<int> inputRow (int f) { return { colX (f) + 8, 69, kColW - 16, 16 }; }

    // A path with softly rounded corners through the given points.
    juce::Path run (std::initializer_list<juce::Point<float>> pts)
    {
        juce::Path p;
        std::vector<juce::Point<float>> v (pts);
        p.startNewSubPath (v[0]);
        for (size_t i = 1; i < v.size(); ++i) p.lineTo (v[i]);
        return p.createPathWithRoundedCorners (4.0f);
    }
    void stroke (juce::Graphics& g, const juce::Path& p, float w = 0.9f)
    {
        g.setColour (gold());
        g.strokePath (p, juce::PathStrokeType (w));
    }
    // A node: where a flow line meets a frame (a small hollow ring)
    void node (juce::Graphics& g, float x, float y)
    {
        g.setColour (panel());
        g.fillEllipse (x - 2.6f, y - 2.6f, 5.2f, 5.2f);
        g.setColour (gold());
        g.drawEllipse (x - 2.6f, y - 2.6f, 5.2f, 5.2f, 0.9f);
    }
    // A joint: where one flow line joins another (a small filled dot)
    void joint (juce::Graphics& g, float x, float y)
    {
        g.setColour (gold());
        g.fillEllipse (x - 2.0f, y - 2.0f, 4.0f, 4.0f);
    }
}

// ================================================================================================ panel

PedwarPanel::PedwarPanel (PedwarAudioProcessor& p) : proc (p)
{
    setLookAndFeel (&lnf);
    auto& st = proc.getValueTreeState();

    // The fixed artwork, drawn once into a stored picture
    backdrop.setBounds (0, 0, panelW, panelH);
    backdrop.setInterceptsMouseClicks (false, false);
    backdrop.setBufferedToImage (true);
    backdrop.setComponentID ("backdrop");
    addAndMakeVisible (backdrop);

    // The four filters
    for (int f = 0; f < numFilters; ++f)
    {
        const int x0 = colX (f);
        const float fx = (float) x0;

        segs (id (f, Mode), { "FILTER", "RES", "OSC" }, Kind::Pill, { x0 + 10, 89, kColW - 20, 18 });

        // Drive
        knob (id (f, Gain), fx + 34.0f, 154.0f, 11.0f, "GAIN", 56.0f);
        segs (id (f, InLimit), { "OFF", "TANH" }, Kind::Pill, { x0 + 62, 146, 62, 18 });

        // Tuning. Coarse readout: the frequency; while Pitch Snap or Scale is on, the note the knobs really play
        // (Coarse + Fine, snapped; before key tracking, Offset and Link).
        auto* fineP = st.getRawParameterValue (id (f, Fine));
        auto* snapP = st.getRawParameterValue (pitchSnap);
        auto* scaleP = st.getRawParameterValue (pitchScale);
        auto* keyP = st.getRawParameterValue (pitchKey);
        auto& coarse = knob (id (f, Coarse), fx + 42.0f, 252.0f, 20.0f, "COARSE", 80.0f, [fineP, snapP, scaleP, keyP] (float v)
        {
            const int sn = juce::roundToInt (snapP->load()), sc = juce::roundToInt (scaleP->load()), k = juce::roundToInt (keyP->load());
            if (! pedwar::pitch::active (sn, sc)) return hzText (v);
            pedwar::FilterParams fp;
            fp.coarseHz = v;
            fp.fineCents = fineP->load();
            return hzText (pedwar::Engine::knobHz (fp, sn, sc, k));
        });
        coarseKnobs.push_back (&coarse);
        knob (id (f, Fine), fx + 102.0f, 238.0f, 9.0f, "FINE", 54.0f);
        segs (id (f, KeyTrack), { "KEY TRACK" }, Kind::Option, { x0 + 10, 312, juce::roundToInt (PdSegments::optionWidth ("KEY TRACK")) + 1, 16 });

        // Resonance and FM
        auto& q = knob (id (f, Q), fx + 38.0f, 380.0f, 14.0f, "Q", 60.0f);
        knob (id (f, FmAmt), fx + 100.0f, 372.0f, 9.0f, "FM AMT", 56.0f);

        // Stereo morph
        auto* ends = st.getParameter (id (f, MorphEnds));
        auto* morphParam = st.getParameter (id (f, Morph));
        auto& morph = knob (id (f, Morph), fx + 38.0f, 486.0f, 14.0f, "MORPH", 60.0f, [ends, morphParam] (float v)
        {
            // the panel names the right-hand end by the Morph ends switch (morph's range is 0..1, so v is normalised)
            if (pedwar::morphMultiple (v) < 0.0f) return juce::String (ends != nullptr && ends->getValue() > 0.5f ? "BP" : "HP");
            return morphParam != nullptr ? morphParam->getText (v, 20) : juce::String();
        });
        knob (id (f, Width), fx + 100.0f, 478.0f, 10.0f, "WIDTH", 56.0f);
        segs (id (f, MorphEnds), { utf8 ("LP\xc2\xb7HP"), "BP" }, Kind::Pill, { x0 + 46, 544, 78, 18 });

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

        // Output
        menu (id (f, Tap), { x0 + 10, 590, kColW - 20, 20 }, { "STEREO MORPH", "LP", "BP", "HP", "NOTCH" });
        segs (id (f, OutLimit), { "OUT LIMIT" }, Kind::Option, { x0 + 10, 618, juce::roundToInt (PdSegments::optionWidth ("OUT LIMIT")) + 1, 16 });
        auto light = std::make_unique<PdStrikeLight> (*st.getParameter (id (f, Strike)));
        light->setBounds (x0 + 10, 638, juce::roundToInt (PdSegments::optionWidth ("STRIKE")) + 1, 16);
        addAndMakeVisible (*light);
        lights[f] = light.get();
        lastStrikes[f] = proc.getEngine().strikeCount (f);
        owned.push_back (std::move (light));
    }

    // Links: alpha leads beta, delta leads gamma (a lamp on each link line)
    for (int i : { 0, 1 })
    {
        const int w = juce::roundToInt (PdSegments::optionWidth ("LINK")) + 1;
        segs (i == 0 ? linkAB : linkGD, { "LINK" }, Kind::Option, { juce::roundToInt (linkMid (i) - (float) w * 0.5f), (int) kLinkLane - 8, w, 16 });
    }

    // Master column. Preset: the Preset menu (Init + the factory presets) and the Quality menu.
    const int mx = kMasterX;
    presetMenu.setName ("Preset");
    presetMenu.setComponentID ("preset");
    for (int i = 0; i < proc.getNumPrograms(); ++i)
        presetMenu.addItem (proc.getProgramName (i), i + 1);
    presetMenu.setBounds (mx + 12, 72, kMasterW - 24, 20);
    addAndMakeVisible (presetMenu);
    presetMenu.onChange = [this]
    {
        const int sel = presetMenu.getSelectedId();
        if (sel > 0)
        {
            proc.setCurrentProgram (sel - 1);
            updatePresetMenu();
        }
    };
    updatePresetMenu();
    qualityMenu = &menu (quality, { mx + 84, 102, 82, 20 });
    qualityMenu->changeItemText (3, "8x heavy");          // 8x runs everything at 8 times the project rate: CPU-hungry
    updateQualityMenu();

    // Pitch: Offset, pitch gamma-delta, and Pitch snap
    knob (offset, (float) mx + 42.0f, 186.0f, 14.0f, "OFFSET", 64.0f);
    segs (pitchSplit, { "ALL", "SPLIT" }, Kind::Pill, { mx + 84, 170, 82, 18 });
    segs (pitchSnap, { "OFF", "SEMIS", "STEPS" }, Kind::Pill, { mx + 12, 262, kMasterW - 24, 18 });
    {
        // The Scale menu shows the scales' names, one shortened to fit ("Double Harmonic (Byzantine)": the menu shows
        // "Double Harmonic"; Logic and saved projects keep the full name).
        juce::StringArray shown;
        if (auto* sc = dynamic_cast<juce::AudioParameterChoice*> (st.getParameter (pitchScale)))
            for (auto n : sc->choices)
                shown.add (n == "Double Harmonic (Byzantine)" ? juce::String ("Double Harmonic") : n);
        menu (pitchScale, { mx + 12, 288, kMasterW - 24, 20 }, shown);
    }
    menu (pitchKey, { mx + 84, 314, 82, 20 });

    // Modulation: Rate and the bus grid
    knob (lfoRate, (float) mx + 40.0f, 392.0f, 13.0f, "RATE", 56.0f);
    {
        auto grid = std::make_unique<PdBusGrid> (st, busIds);
        grid->setBounds (mx + 68, 364, 100, 70);
        addAndMakeVisible (*grid);
        owned.push_back (std::move (grid));
    }

    // Mix: the four levels, Stereo and Master limit
    const char* const letters[4] = { "\xce\xb1", "\xce\xb2", "\xce\xb3", "\xce\xb4" };
    for (int f = 0; f < numFilters; ++f)
    {
        auto fader = std::make_unique<PdFader> (st, id (f, Level), utf8 (letters[f]));
        fader->placeAt ((float) (mx + 32 + f * 38), 474.0f);
        addAndMakeVisible (*fader);
        owned.push_back (std::move (fader));
    }
    segs (stereoMode, { "SUM", utf8 ("\xc3\x9c" "BER") }, Kind::Pill, { mx + 68, 600, 98, 18 });
    segs (masterLimit, { "MASTER LIMIT" }, Kind::Option, { mx + 12, 630, juce::roundToInt (PdSegments::optionWidth ("MASTER LIMIT")) + 1, 16 });

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

PdKnob& PedwarPanel::knob (const char* pid, float cx, float cy, float r, const juce::String& label, float room,
                           PdKnob::Readout ro)
{
    auto k = std::make_unique<PdKnob> (proc.getValueTreeState(), pid, r, label, room, std::move (ro));
    k->placeAt (cx, cy);
    addAndMakeVisible (*k);
    auto* raw = k.get();
    knobs.push_back (raw);
    owned.push_back (std::move (k));
    return *raw;
}

PdSegments& PedwarPanel::segs (const char* pid, juce::StringArray labels, Kind kind, juce::Rectangle<int> r,
                               juce::Array<float> values)
{
    auto* p = proc.getValueTreeState().getParameter (pid);
    jassert (p != nullptr);
    auto s = std::make_unique<PdSegments> (*p, std::move (labels), kind, std::move (values));
    s->setBounds (r);
    addAndMakeVisible (*s);
    auto* raw = s.get();
    owned.push_back (std::move (s));
    return *raw;
}

PdMenu& PedwarPanel::menu (const char* pid, juce::Rectangle<int> r, const juce::StringArray& shown)
{
    auto m = std::make_unique<PdMenu> (proc.getValueTreeState(), pid, shown);
    m->setBounds (r);
    addAndMakeVisible (*m);
    auto* raw = m.get();
    owned.push_back (std::move (m));
    return *raw;
}

juce::Array<juce::Rectangle<int>> PedwarPanel::liveAreas() const
{
    juce::Array<juce::Rectangle<int>> a;
    for (auto* k : modKnobs) a.add (k->getBounds());
    for (auto* l : lights) a.add (l->getBounds());
    for (int f = 0; f < numFilters; ++f) a.add (inputRow (f));
    return a;
}

void PedwarPanel::timerCallback()
{
    // Strike lamps: a flash on every strike, fading out over about 0.15 s.
    for (int f = 0; f < numFilters; ++f)
    {
        const int n = proc.getEngine().strikeCount (f);
        auto* l = lights[f];
        if (n != lastStrikes[f]) { lastStrikes[f] = n; l->flash (1.0f); }
        else if (l->currentGlow() > 0.01f) l->flash (l->currentGlow() * 0.72f);
        else if (l->currentGlow() > 0.0f) l->flash (0.0f);
    }
    // Modulation rings on Coarse, Q and Morph (each knob repaints only when its ring has moved).
    for (auto* k : modKnobs) k->refreshModulation();
    // Each filter's input line: a lit lamp and "SIDE CHAIN" while that filter is taking the side chain.
    for (int f = 0; f < numFilters; ++f)
    {
        const bool sc = proc.getEngine().filterUsesSidechain (f);
        if (sc != shownSidechain[f]) { shownSidechain[f] = sc; repaint (inputRow (f)); }
    }
    // Menus and readouts that follow other controls
    if (proc.shownPreset() != shownPresetId) updatePresetMenu();
    if (qualityMenu != nullptr && juce::roundToInt (proc.getValueTreeState().getRawParameterValue (quality)->load()) != shownQuality)
        updateQualityMenu();
    const int sn = snapState();
    if (sn != shownSnap)
    {
        shownSnap = sn;
        for (auto* k : coarseKnobs) k->repaint();
    }
}

void PedwarPanel::updatePresetMenu()
{
    const int shown = proc.shownPreset();
    if (shown >= 0)
        presetMenu.setSelectedId (shown + 1, juce::dontSendNotification);
    else
    {
        presetMenu.setSelectedId (0, juce::dontSendNotification);
        presetMenu.setText ("Custom", juce::dontSendNotification);   // settings from a project saved before 0.3.0
    }
    shownPresetId = shown;
}

// 8x is marked "heavy" in the list and on the menu while it is chosen, in gold.
void PedwarPanel::updateQualityMenu()
{
    const int q = juce::roundToInt (proc.getValueTreeState().getRawParameterValue (quality)->load());
    qualityMenu->setColour (juce::ComboBox::textColourId, q == 2 ? goldText() : text());
    qualityMenu->setText (qualityMenu->getItemText (juce::jlimit (0, 2, q)), juce::dontSendNotification);
    qualityMenu->repaint();
    shownQuality = q;
}

int PedwarPanel::snapState() const
{
    auto& st = proc.getValueTreeState();
    auto i = [&st] (const char* pid) { return juce::roundToInt (st.getRawParameterValue (pid)->load()); };
    int h = i (pitchSnap) * 4096 + i (pitchScale) * 32 + i (pitchKey);
    if (pedwar::pitch::active (i (pitchSnap), i (pitchScale)))
        for (int f = 0; f < numFilters; ++f)
            h = h * 31 + juce::roundToInt (st.getRawParameterValue (id (f, Fine))->load() * 10.0f);
    return h;
}

// ------------------------------------------------------------------------------------------------ fixed artwork

void PedwarPanel::paintBackdrop (juce::Graphics& g)
{
    g.setColour (panel());
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 12.0f);

    // Header: the title between two ornament lines
    textIn (g, "PEDWAR", titleFont(), titleCentre, titleBaseline, goldText(), juce::Justification::centred);
    g.setColour (gold());
    g.fillRect (ornamentOuterLeft, 24.6f, ornamentLeftEnd - ornamentOuterLeft, 0.8f);
    g.fillRect (ornamentRightStart, 24.6f, ornamentOuterRight - ornamentRightStart, 0.8f);
    g.fillEllipse (ornamentOuterLeft - 1.8f, 23.2f, 3.6f, 3.6f);
    g.fillEllipse (ornamentOuterRight - 1.8f, 23.2f, 3.6f, 3.6f);
    textAt (g, "PARTICLEDOTS", 22.0f, 29.0f, muted(), 11.0f, juce::Justification::centredLeft, false, 2.5f);
    textAt (g, "QUAD FILTER", (float) panelW - 22.0f, 29.0f, muted(), 11.0f, juce::Justification::centredRight, false, 2.5f);

    // Section frames, each with its title set into the top line
    for (const auto& f : frames())
    {
        g.setColour (gold());
        g.drawRoundedRectangle (f.x + 0.5f, f.y + 0.5f, f.w - 1.0f, f.h - 1.0f, 10.0f, 0.9f);
        const float tw = widthOf (f.title, capsFont());
        const float tx = f.x + kFrameTitleInset;
        g.setColour (panel());
        g.fillRect (tx - 6.0f, f.y - 5.0f, tw + 12.0f, 11.0f);
        textIn (g, f.title, capsFont(), tx, f.y + 4.5f, goldText(), juce::Justification::centredLeft);
    }

    // Row titles: gold capitals with a hairline running on to the frame's right edge
    auto rowTitle = [&g] (const juce::String& t, float x, float y, float right)
    {
        textIn (g, t, rowFont(), x, y + 3.5f, goldText(), juce::Justification::centredLeft);
        const float end = x + widthOf (t, rowFont()) + 6.0f;
        g.setColour (gold().withAlpha (0.45f));
        g.fillRect (end, y - 0.3f, right - end, 0.6f);
    };
    for (int f = 0; f < numFilters; ++f)
        for (const auto& r : kFilterRows)
            rowTitle (utf8 (r.title), (float) colX (f) + 12.0f, r.y, (float) (colX (f) + kColW) - 12.0f);
    rowTitle ("SNAP", (float) kMasterX + 14.0f, 250.0f, (float) (kMasterX + kMasterW) - 12.0f);

    // Names next to and over switches
    for (int f = 0; f < numFilters; ++f)
    {
        const float fx = (float) colX (f);
        textIn (g, "IN LIMIT", labelFont(), fx + 93.0f, 141.0f, text(), juce::Justification::centred);
        textIn (g, "ENDS", labelFont(), fx + 12.0f, 556.5f, text(), juce::Justification::centredLeft);
    }
    const float mx = (float) kMasterX;
    textIn (g, "QUALITY", labelFont(), mx + 14.0f, 115.5f, text(), juce::Justification::centredLeft);
    textIn (g, utf8 ("PITCH \xce\xb3\xce\xb4"), labelFont(), mx + 125.0f, 202.0f, text(), juce::Justification::centred);
    textIn (g, "STEREO", labelFont(), mx + 14.0f, 612.5f, text(), juce::Justification::centredLeft);
    textIn (g, "KEY", labelFont(), mx + 14.0f, 327.5f, text(), juce::Justification::centredLeft);
    textIn (g, "MIDI IN", labelFont(), mx + kMasterW * 0.5f, 688.0f, muted(), juce::Justification::centred);
    textIn (g, "STRIKE + KEY TRACK", labelFont(), mx + kMasterW * 0.5f, 702.0f, muted(), juce::Justification::centred);

    // The knobs' gold range arcs (7:30 round to 4:30) with a dot at each end, and their names
    for (auto* k : knobs)
    {
        const float cx = k->centreX(), cy = k->centreY(), ar = k->arcRadius();
        juce::Path arc;
        arc.addCentredArc (cx, cy, ar, ar, 0.0f, juce::degreesToRadians (-135.0f), juce::degreesToRadians (135.0f), true);
        g.setColour (gold());
        g.strokePath (arc, juce::PathStrokeType (0.9f));
        const float d = ar * 0.70710678f, dr = juce::jmin (1.8f, 0.9f + ar * 0.04f);
        g.fillEllipse (cx - d - dr, cy + d - dr, dr * 2.0f, dr * 2.0f);
        g.fillEllipse (cx + d - dr, cy + d - dr, dr * 2.0f, dr * 2.0f);
        textIn (g, k->getLabel(), labelFont(), cx, k->labelBaseline(), text(), juce::Justification::centred);
    }

    // Flow lines. The four outputs: each leaves its filter's frame, they join on one lane and run to the mix.
    const float bottom = (float) kColBottom - 0.5f;
    const float hopXs[] = { linkFollowerX (0), linkFollowerX (1), linkLeaderX (1) };   // where link lines cross the lane
    {
        juce::Path lane;
        lane.startNewSubPath (colCentre (0), bottom);
        lane.lineTo (colCentre (0), kOutLane);
        float x = colCentre (0);
        for (float hx : hopXs)
        {
            // the output lane hops over each link line it crosses
            lane.lineTo (hx - 6.0f, kOutLane);
            lane.quadraticTo (hx, kOutLane - 13.0f, hx + 6.0f, kOutLane);
            x = hx + 6.0f;
        }
        juce::ignoreUnused (x);
        lane.lineTo (kOutRiseX - 4.0f, kOutLane);
        lane.quadraticTo (kOutRiseX, kOutLane, kOutRiseX, kOutLane - 4.0f);
        lane.lineTo (kOutRiseX, kMixArriveY + 4.0f);
        lane.quadraticTo (kOutRiseX, kMixArriveY, kOutRiseX + 4.0f, kMixArriveY);
        lane.lineTo ((float) kMasterX + 0.5f, kMixArriveY);
        stroke (g, lane);
        for (int f = 1; f < numFilters; ++f)
        {
            stroke (g, run ({ { colCentre (f), bottom }, { colCentre (f), kOutLane } }));
            joint (g, colCentre (f), kOutLane);
        }
        for (int f = 0; f < numFilters; ++f) node (g, colCentre (f), bottom);
        node (g, (float) kMasterX + 0.5f, kMixArriveY);
    }
    // The links: from the leader's frame to the follower's, through the Link lamp, with "alpha leads beta" under it.
    for (int i : { 0, 1 })
    {
        const float lead = linkLeaderX (i), follow = linkFollowerX (i), mid = linkMid (i);
        const float half = PdSegments::optionWidth ("LINK") * 0.5f + 5.0f;
        const float a = juce::jmin (lead, follow), b = juce::jmax (lead, follow);
        stroke (g, run ({ { a, bottom }, { a, kLinkLane }, { mid - half, kLinkLane } }));
        stroke (g, run ({ { mid + half, kLinkLane }, { b, kLinkLane }, { b, bottom } }));
        node (g, lead, bottom);
        node (g, follow, bottom);
        textIn (g, utf8 (i == 0 ? "\xce\xb1 LEADS \xce\xb2" : "\xce\xb4 LEADS \xce\xb3"), labelFont(), mid, 718.0f, muted(),
                juce::Justification::centred);
    }
}

std::vector<PedwarPanel::FixedText> PedwarPanel::fixedTexts() const
{
    std::vector<FixedText> v;
    auto add = [&v] (const juce::String& t, const juce::Font& f, float room) { v.push_back ({ t, widthOf (t, f), room }); };
    for (const auto& f : frames())
        add (f.title, capsFont(), f.w - 2.0f * kFrameTitleInset);
    for (const auto& r : kFilterRows)
        add (utf8 (r.title), rowFont(), (float) kColW - 24.0f - 8.0f);      // leaves at least 8 units of hairline
    add ("SNAP", rowFont(), (float) kMasterW - 26.0f - 12.0f);
    add ("IN LIMIT", labelFont(), 60.0f);                                   // over its switch
    add ("ENDS", labelFont(), 46.0f - 12.0f - 2.0f);                        // left of its switch
    add ("QUALITY", labelFont(), 84.0f - 14.0f - 4.0f);
    add (utf8 ("PITCH \xce\xb3\xce\xb4"), labelFont(), 82.0f);
    add ("STEREO", labelFont(), 68.0f - 14.0f - 4.0f);
    add ("KEY", labelFont(), 84.0f - 14.0f - 4.0f);
    add ("STRIKE + KEY TRACK", labelFont(), (float) kMasterW - 8.0f);
    add (utf8 ("\xce\xb1 LEADS \xce\xb2"), labelFont(), 120.0f);
    add ("PARTICLEDOTS", font (11.0f, false, 2.5f), ornamentOuterLeft - 8.0f - 22.0f);
    add ("QUAD FILTER", font (11.0f, false, 2.5f), (float) panelW - 22.0f - ornamentOuterRight - 8.0f);
    for (auto* k : knobs)
        add (k->getLabel(), labelFont(), k->readoutRoom());
    return v;
}

void PedwarPanel::paintOverChildren (juce::Graphics& g)
{
    // Each filter's input: "TRACK IN", or "SIDE CHAIN" (gamma / delta, and every filter of PEDWAR Synth); while a
    // filter is taking the side chain its lamp is lit and the words turn blue.
    for (int f = 0; f < numFilters; ++f)
    {
        const auto r = inputRow (f).toFloat();
        if (! g.clipRegionIntersects (r.getSmallestIntegerContainer())) continue;
        const bool onSide = shownSidechain[f];
        const bool sideLabel = onSide || PedwarAudioProcessor::isSynth || f >= pedwar::Gamma;
        drawLamp (g, r.getX() + 6.0f, r.getCentreY(), onSide, onSide ? 3.2f : 3.0f);
        textIn (g, sideLabel ? "SIDE CHAIN" : "TRACK IN", labelFont(), r.getX() + 16.0f, r.getCentreY() + 3.5f,
                onSide ? lampText() : (sideLabel ? muted() : soft()), juce::Justification::centredLeft);
    }
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
