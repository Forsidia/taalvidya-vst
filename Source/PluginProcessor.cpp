#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    constexpr int kFirstMidiNote = 36;   // C2 -> first bol in the palette
}

TablaProcessor::TablaProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    pTempo    = apvts.getRawParameterValue ("tempo");
    pPitch    = apvts.getRawParameterValue ("pitch");
    pVolume   = apvts.getRawParameterValue ("volume");
    pLoop     = apvts.getRawParameterValue ("loop");
    pHostSync = apvts.getRawParameterValue ("hostSync");

    for (auto& d : BolLibrary::defs())
    {
        std::vector<int> parts;
        if (d.parts.isEmpty()) parts.push_back (BolLibrary::baseIndex (d.name));
        else for (auto& p : d.parts) parts.push_back (BolLibrary::baseIndex (p));
        defPartSamples.push_back (std::move (parts));
    }

    sampleFolder = BolLibrary::defaultFolder();
    sampleFolder.createDirectory();
    setSampleFolder (sampleFolder);

    auto taals = BuiltinTaals::create();
    currentTaal = taals.front();
    setTaal (currentTaal);
    apvts.getParameter ("tempo")->setValueNotifyingHost (
        apvts.getParameter ("tempo")->convertTo0to1 ((float) currentTaal.defaultTempo));
}

TablaProcessor::~TablaProcessor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout TablaProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "tempo", 1 }, "Tempo",
                                                  NormalisableRange<float> (20.0f, 400.0f, 0.01f), 100.0f,
                                                  AudioParameterFloatAttributes().withLabel ("bpm")));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "pitch", 1 }, "Pitch",
                                                  NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 0.0f,
                                                  AudioParameterFloatAttributes().withLabel ("st")));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "volume", 1 }, "Volume",
                                                  NormalisableRange<float> (-36.0f, 6.0f, 0.1f), 0.0f,
                                                  AudioParameterFloatAttributes().withLabel ("dB")));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "loop", 1 }, "Loop", true));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "hostSync", 1 }, "Host Sync", false));
    return l;
}

std::shared_ptr<const CompiledTaal> TablaProcessor::compile (const Taal& t)
{
    auto c = std::make_shared<CompiledTaal>();
    double start = 0.0;
    for (auto& e : t.events)
    {
        const float gain = std::pow (10.0f, e.emphasisDb / 20.0f);
        if (! e.bol.equalsIgnoreCase ("Rest"))
        {
            if (auto* def = BolLibrary::find (e.bol))
            {
                if (def->parts.isEmpty())
                    c->hits.push_back ({ start, BolLibrary::baseIndex (def->name), gain });
                else
                    for (int i = 0; i < def->parts.size(); ++i)
                        c->hits.push_back ({ start + e.duration * i / def->parts.size(),
                                             BolLibrary::baseIndex (def->parts[i]), gain });
            }
        }
        start += e.duration;
    }
    c->length = start;
    std::stable_sort (c->hits.begin(), c->hits.end(), [] (auto& a, auto& b) { return a.start < b.start; });
    return c;
}

void TablaProcessor::setTaal (const Taal& t, bool notify)
{
    currentTaal = t;
    auto c = compile (t);
    {
        const juce::SpinLock::ScopedLockType sl (handoverLock);
        pendingTaal = std::move (c);   // an older, unconsumed pending taal is freed here, off the audio thread
        hasNewTaal = true;
    }
    if (notify) sendChangeMessage();
}

void TablaProcessor::setSampleFolder (const juce::File& folder)
{
    sampleFolder = folder;
    auto bank = BolLibrary::buildBank (folder);
    customSampleCount = bank->customCount;
    const juce::SpinLock::ScopedLockType sl (handoverLock);
    pendingBank = std::move (bank);
    hasNewBank = true;
}

void TablaProcessor::setPlaying (bool shouldPlay)
{
    if (shouldPlay) resetRequested = true;
    playing = shouldPlay;
}

void TablaProcessor::previewBol (const juce::String& bol, float emphasisDb)
{
    int idx = 0;
    for (auto& d : BolLibrary::defs())
    {
        if (d.name.equalsIgnoreCase (bol))
        {
            const auto scope = previewFifo.write (1);
            if (scope.blockSize1 > 0) previewQueue[(size_t) scope.startIndex1] = { idx, emphasisDb };
            return;
        }
        ++idx;
    }
}

// ---------------------------------------------------------------------------------------------
void TablaProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    for (auto& v : voices) v = {};
    numScheduled = 0;
    resetRequested = true;
}

bool TablaProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void TablaProcessor::adoptPending()
{
    const juce::SpinLock::ScopedTryLockType sl (handoverLock);
    if (! sl.isLocked()) return;

    if (hasNewBank)
    {
        std::swap (activeBank, pendingBank);   // old bank ends up in pendingBank -> freed by message thread later
        hasNewBank = false;
        for (auto& v : voices) v = {};
        numScheduled = 0;
    }
    if (hasNewTaal)
    {
        std::swap (activeTaal, pendingTaal);
        hasNewTaal = false;
    }
}

void TablaProcessor::startVoice (int sampleIndex, float gain)
{
    if (activeBank == nullptr || sampleIndex < 0 || sampleIndex >= (int) activeBank->samples.size()) return;
    auto& s = activeBank->samples[(size_t) sampleIndex];

    // prefer a free voice, otherwise steal round-robin
    Voice* slot = nullptr;
    for (auto& v : voices) if (v.sample == nullptr) { slot = &v; break; }
    if (slot == nullptr) { slot = &voices[nextVoice]; nextVoice = (nextVoice + 1) % voices.size(); }

    slot->sample = &s;
    slot->pos = 0.0;
    slot->ratio = s.sampleRate / sr * currentPitchRatio;
    slot->gain = gain;
}

void TablaProcessor::schedule (int sampleIndex, float gain, int delaySamples)
{
    if (numScheduled >= (int) scheduled.size()) return;
    scheduled[(size_t) numScheduled++] = { delaySamples, sampleIndex, gain };
}

void TablaProcessor::triggerDef (int defIndex, float gain, double samplesPerBeat)
{
    if (defIndex < 0 || defIndex >= (int) defPartSamples.size()) return;
    auto& parts = defPartSamples[(size_t) defIndex];
    for (size_t i = 0; i < parts.size(); ++i)
        schedule (parts[i], gain, (int) (samplesPerBeat * (double) i / (double) parts.size()));
}

void TablaProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    adoptPending();

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    const bool hostSync = pHostSync->load() > 0.5f;
    const bool loop = pLoop->load() > 0.5f;
    const float masterGain = 0.5f * std::pow (10.0f, pVolume->load() / 20.0f);
    currentPitchRatio = std::pow (2.0f, pPitch->load() / 12.0f);

    const double len = activeTaal != nullptr ? activeTaal->length : 0.0;
    double bpm = pTempo->load();
    bool running = false;
    bool needReset = resetRequested.exchange (false);

    if (hostSync)
    {
        bool hostPlaying = false;
        if (auto* ph = getPlayHead())
            if (auto info = ph->getPosition())
            {
                hostPlaying = info->getIsPlaying();
                if (hostPlaying && len > 0.0)
                {
                    if (auto hb = info->getBpm()) bpm = *hb;
                    if (auto ppq = info->getPpqPosition())
                    {
                        const double p = std::fmod (std::max (0.0, *ppq), len);
                        double d = std::abs (p - beatPos);
                        d = std::min (d, len - d);
                        if (! hostWasPlaying || d > 0.05) beatPos = p;
                    }
                    running = true;
                }
            }
        hostWasPlaying = hostPlaying;
    }
    else
    {
        running = playing.load() && len > 0.0;
        if (needReset) beatPos = 0.0;
    }
    if (! running && wasRunning) { /* let tails ring out */ }
    wasRunning = running;

    const double samplesPerBeat = sr * 60.0 / std::max (1.0, bpm);
    const double inc = 1.0 / samplesPerBeat;

    // UI preview requests + MIDI notes become scheduled triggers
    {
        const auto scope = previewFifo.read (previewFifo.getNumReady());
        auto handle = [&] (int idx) { auto& r = previewQueue[(size_t) idx]; triggerDef (r.defIndex, std::pow (10.0f, r.db / 20.0f), samplesPerBeat); };
        for (int i = 0; i < scope.blockSize1; ++i) handle (scope.startIndex1 + i);
        for (int i = 0; i < scope.blockSize2; ++i) handle (scope.startIndex2 + i);
    }

    auto midiIt = midi.begin();

    auto fire = [&] (double a, double b)
    {
        for (auto& h : activeTaal->hits)
        {
            if (h.start >= b) break;
            if (h.start >= a) startVoice (h.sample, h.gain);
        }
    };

    auto* left = buffer.getWritePointer (0);
    auto* right = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        while (midiIt != midi.end() && (*midiIt).samplePosition <= i)
        {
            const auto msg = (*midiIt).getMessage();
            if (msg.isNoteOn())
            {
                const int idx = msg.getNoteNumber() - kFirstMidiNote;
                if (idx >= 0 && idx < (int) defPartSamples.size())
                    triggerDef (idx, msg.getFloatVelocity(), samplesPerBeat);
            }
            ++midiIt;
        }

        if (running)
        {
            double next = beatPos + inc;
            if (next >= len)
            {
                fire (beatPos, len);
                if (loop || hostSync) { next = std::fmod (next, len); fire (-1.0e-9, next); }
                else { next = len; playing = false; running = false; }
            }
            else
                fire (beatPos, next);
            beatPos = next;
        }

        if (numScheduled > 0)
        {
            for (int s = 0; s < numScheduled;)
            {
                if (scheduled[(size_t) s].delay <= 0)
                {
                    startVoice (scheduled[(size_t) s].sample, scheduled[(size_t) s].gain);
                    scheduled[(size_t) s] = scheduled[(size_t) --numScheduled];
                }
                else { --scheduled[(size_t) s].delay; ++s; }
            }
        }

        float mix = 0.0f;
        for (auto& v : voices)
        {
            if (v.sample == nullptr) continue;
            const auto* d = v.sample->data.getReadPointer (0);
            const int n = v.sample->data.getNumSamples();
            const int i0 = (int) v.pos;
            if (i0 >= n - 1) { v.sample = nullptr; continue; }
            const float frac = (float) (v.pos - i0);
            mix += v.gain * (d[i0] + frac * (d[i0 + 1] - d[i0]));
            v.pos += v.ratio;
        }
        mix *= masterGain;
        left[i] = mix;
        if (right != nullptr) right[i] = mix;
    }

    playheadBeat.store (running || (! hostSync && playing.load()) ? beatPos : -1.0);
}

// ---------------------------------------------------------------------------------------------
void TablaProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("taal", currentTaal.toJson(), nullptr);
    state.setProperty ("sampleFolder", sampleFolder.getFullPathName(), nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void TablaProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
    {
        auto state = juce::ValueTree::fromXml (*xml);
        if (! state.isValid()) return;

        const auto folder = state.getProperty ("sampleFolder").toString();
        const auto taalJson = state.getProperty ("taal").toString();
        apvts.replaceState (state);

        if (folder.isNotEmpty() && juce::File (folder).isDirectory() && juce::File (folder) != sampleFolder)
            setSampleFolder (juce::File (folder));

        Taal t;
        if (Taal::fromJson (taalJson, t)) setTaal (t, true);
    }
}

juce::AudioProcessorEditor* TablaProcessor::createEditor() { return new TablaEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TablaProcessor(); }
