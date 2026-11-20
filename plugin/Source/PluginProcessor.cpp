#include "PluginProcessor.h"
#include "PluginEditor.h"

juce::AudioProcessor::BusesProperties PedwarAudioProcessor::makeBuses()
{
   #if PEDWAR_SYNTH
        // Instrument: no track audio. The side chain is optional (off unless chosen in Logic) and feeds all four filters.
        return BusesProperties()
                   .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)
                   .withOutput ("Output",    juce::AudioChannelSet::stereo(), true);
   #else
        // Effect: the track audio, and the side chain (feeds gamma and delta while it carries audio).
        return BusesProperties()
                   .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                   .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), true)
                   .withOutput ("Output",    juce::AudioChannelSet::stereo(), true);
   #endif
}

namespace
{
    // The four controls Init (and later the presets) leave alone: the player's own pitch and quality set-up.
    bool isPlayersSetting (const juce::String& pid)
    {
        return pid == PdParams::pitchSnap || pid == PdParams::pitchScale || pid == PdParams::pitchKey || pid == PdParams::quality;
    }
}

PedwarAudioProcessor::PedwarAudioProcessor()
    : AudioProcessor (makeBuses()),
      apvts (*this, nullptr, "Parameters", PdParams::createLayout())
{
    auto p = [this] (const char* pid) { return apvts.getRawParameterValue (pid); };
    for (int f = 0; f < PdParams::numFilters; ++f)
        for (int s = 0; s < PdParams::numSlots; ++s)
            fp[f][s] = p (PdParams::id (f, s));
    masterLimit = p (PdParams::masterLimit);
    linkAB = p (PdParams::linkAB);  linkGD = p (PdParams::linkGD);
    offset = p (PdParams::offset);  pitchSplit = p (PdParams::pitchSplit);  lfoRate = p (PdParams::lfoRate);
    for (int f = 0; f < PdParams::numFilters; ++f) bus[f] = p (PdParams::busIds[f]);
    stereoMode = p (PdParams::stereoMode);
    snap = p (PdParams::pitchSnap);  scale = p (PdParams::pitchScale);  key = p (PdParams::pitchKey);
    quality = p (PdParams::quality);
    engine.setSidechainFeedsAll (isSynth);
}

pedwar::Params PedwarAudioProcessor::readParams() const
{
    auto v = [] (const std::atomic<float>* a) { return a->load (std::memory_order_relaxed); };
    auto i = [] (const std::atomic<float>* a) { return juce::roundToInt (a->load (std::memory_order_relaxed)); };
    auto b = [] (const std::atomic<float>* a) { return a->load (std::memory_order_relaxed) > 0.5f; };

    using namespace PdParams;
    pedwar::Params P;
    for (int f = 0; f < numFilters; ++f)
    {
        auto& o = P.f[f];
        auto* const* s = fp[f];
        o.mode = i (s[Mode]);       o.gainDb = v (s[Gain]);    o.inLimit = b (s[InLimit]);
        o.coarseHz = v (s[Coarse]); o.fineCents = v (s[Fine]); o.q = v (s[Q]);
        o.outLimit = b (s[OutLimit]); o.keyTrack = b (s[KeyTrack]); o.tap = i (s[Tap]); o.strike = b (s[Strike]);
        o.fmAmt = v (s[FmAmt]);     o.morph = v (s[Morph]);    o.morphEnds = i (s[MorphEnds]); o.width = v (s[Width]);
        o.levelDb = v (s[Level]);
    }
    P.masterLimit = b (masterLimit);
    P.linkAB = b (linkAB); P.linkGD = b (linkGD);
    P.offset = v (offset); P.pitchSplit = i (pitchSplit); P.lfoRate = v (lfoRate);
    for (int f = 0; f < numFilters; ++f) P.bus[f] = i (bus[f]);
    P.stereoMode = i (stereoMode);
    P.snap = i (snap); P.scale = i (scale); P.key = i (key);
    P.quality = i (quality);
    return P;
}

void PedwarAudioProcessor::setCurrentProgram (int index)
{
    // Init: every control back to its default, except the player's Pitch Snap, Scale, Key and Quality.
    for (auto* base : getParameters())
        if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (base))
            if (! isPlayersSetting (p->paramID))
                p->setValueNotifyingHost (p->getDefaultValue());
    programIndex = juce::jlimit (0, getNumPrograms() - 1, index);
}

void PedwarAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.setParams (readParams());
    engine.prepare (sampleRate, samplesPerBlock);
    setLatencySamples (juce::roundToInt (engine.latencySamples()));
    const size_t n = (size_t) juce::jmax (samplesPerBlock, 16384);
    trackMono.assign (n, 0.0f);
    sideMono.assign (n, 0.0f);
    outL.assign (n, 0.0f);
    outR.assign (n, 0.0f);
}

bool PedwarAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    auto okInput = [] (const juce::AudioChannelSet& s, bool mayBeOff)
    {
        return (mayBeOff && s.isDisabled()) || s == juce::AudioChannelSet::mono() || s == juce::AudioChannelSet::stereo();
    };
   #if PEDWAR_SYNTH
    return layouts.inputBuses.size() == 1 && okInput (layouts.getChannelSet (true, 0), true);
   #else
    return layouts.inputBuses.size() == 2
        && okInput (layouts.getChannelSet (true, 0), false)
        && okInput (layouts.getChannelSet (true, 1), true);
   #endif
}

void PedwarAudioProcessor::handleMidi (const juce::MidiMessage& m)
{
    if (m.isNoteOn())                                engine.noteOn (m.getNoteNumber(), m.getFloatVelocity());
    else if (m.isNoteOff())                          engine.noteOff (m.getNoteNumber());
    else if (m.isAllNotesOff() || m.isAllSoundOff()) engine.allNotesOff();
}

void PedwarAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    engine.setParams (readParams());

    const int n = buffer.getNumSamples();
    auto out = getBusBuffer (buffer, false, 0);
    const int numOut = out.getNumChannels();
    if (numOut == 0 || n > (int) outL.size()) { midi.clear(); return; }

    // The inputs and the output share memory, so the inputs are summed to mono and copied out first.
    auto toMono = [n] (const juce::AudioBuffer<float>& in, std::vector<float>& dst) -> const float*
    {
        if (in.getNumChannels() == 0) return nullptr;
        const float* l = in.getReadPointer (0);
        const float* r = in.getReadPointer (in.getNumChannels() > 1 ? 1 : 0);
        for (int i = 0; i < n; ++i) dst[(size_t) i] = 0.5f * (l[i] + r[i]);
        return dst.data();
    };
    const float* track = nullptr;
    const float* side = nullptr;
   #if PEDWAR_SYNTH
    if (auto* sc = getBus (true, 0); sc != nullptr && sc->isEnabled())
        side = toMono (getBusBuffer (buffer, true, 0), sideMono);
   #else
    track = toMono (getBusBuffer (buffer, true, 0), trackMono);
    if (auto* sc = getBus (true, 1); sc != nullptr && sc->isEnabled())
        side = toMono (getBusBuffer (buffer, true, 1), sideMono);
   #endif

    // Render up to each MIDI event, then apply it, so strikes land exactly where they are in the bar.
    int pos = 0;
    auto renderTo = [&] (int end)
    {
        if (end <= pos) return;
        engine.process (track != nullptr ? track + pos : nullptr, side != nullptr ? side + pos : nullptr,
                        outL.data() + pos, outR.data() + pos, end - pos);
        pos = end;
    };
    for (const auto meta : midi)
    {
        renderTo (juce::jlimit (0, n, meta.samplePosition));
        handleMidi (meta.getMessage());
    }
    renderTo (n);

    if (numOut == 1)
        for (int i = 0; i < n; ++i) out.getWritePointer (0)[i] = 0.5f * (outL[(size_t) i] + outR[(size_t) i]);
    else
    {
        juce::FloatVectorOperations::copy (out.getWritePointer (0), outL.data(), n);
        juce::FloatVectorOperations::copy (out.getWritePointer (1), outR.data(), n);
        for (int c = 2; c < numOut; ++c) out.clear (c, 0, n);
    }
    midi.clear();
}

juce::AudioProcessorEditor* PedwarAudioProcessor::createEditor()
{
    return new PedwarAudioProcessorEditor (*this);
}

void PedwarAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("preset", programIndex.load(), nullptr);       // the Preset menu's entry (0 = Init)
    if (auto xml = state.createXml())
    {
        xml->setAttribute ("pluginVersion", JucePlugin_VersionString);
        copyXmlToBinary (*xml, destData);
    }
}

void PedwarAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            const auto tree = juce::ValueTree::fromXml (*xml);
            apvts.replaceState (tree);
            programIndex = tree.hasProperty ("preset") ? juce::jlimit (0, getNumPrograms() - 1, (int) tree["preset"]) : 0;
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PedwarAudioProcessor();
}
