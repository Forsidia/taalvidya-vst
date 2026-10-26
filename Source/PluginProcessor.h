#pragma once
#include <JuceHeader.h>
#include "Taal.h"
#include "BolLibrary.h"

// A taal flattened into individual sample triggers (compound bols already expanded).
struct CompiledTaal
{
    struct Hit { double start; int sample; float gain; };
    double length = 0.0;
    std::vector<Hit> hits;     // sorted by start beat
};

class TablaProcessor : public juce::AudioProcessor,
                       public juce::ChangeBroadcaster
{
public:
    TablaProcessor();
    ~TablaProcessor() override;

    // ---- AudioProcessor ----
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- message-thread API used by the editor ----
    const Taal& getTaal() const { return currentTaal; }
    void setTaal (const Taal& t, bool notify = false);

    void setPlaying (bool shouldPlay);
    bool isPlaying() const { return playing.load(); }
    double getPlayheadBeat() const { return playheadBeat.load(); }   // < 0 when stopped

    void previewBol (const juce::String& bol, float emphasisDb = 0.0f);

    void setSampleFolder (const juce::File& folder);
    juce::File getSampleFolder() const { return sampleFolder; }
    int getCustomSampleCount() const { return customSampleCount; }

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static std::shared_ptr<const CompiledTaal> compile (const Taal&);

    struct Voice
    {
        const Sample* sample = nullptr;
        double pos = 0.0;
        double ratio = 1.0;
        float gain = 0.0f;
    };
    struct Scheduled { int delay = 0; int sample = 0; float gain = 0.0f; };
    struct PreviewRequest { int defIndex = 0; float db = 0.0f; };

    void startVoice (int sampleIndex, float gain);
    void schedule (int sampleIndex, float gain, int delaySamples);
    void triggerDef (int defIndex, float gain, double samplesPerBeat);
    void adoptPending();

    // message thread state
    Taal currentTaal;
    juce::File sampleFolder;
    int customSampleCount = 0;

    // hand-over to audio thread (swap trick: audio thread swaps, message thread frees the old one)
    juce::SpinLock handoverLock;
    std::shared_ptr<const CompiledTaal> pendingTaal;
    std::shared_ptr<SampleBank> pendingBank;
    bool hasNewTaal = false, hasNewBank = false;

    // audio thread state
    std::shared_ptr<const CompiledTaal> activeTaal;
    std::shared_ptr<SampleBank> activeBank;
    std::array<Voice, 48> voices;
    std::array<Scheduled, 64> scheduled;
    int numScheduled = 0;
    size_t nextVoice = 0;
    double sr = 44100.0;
    double beatPos = 0.0;
    bool wasRunning = false;
    bool hostWasPlaying = false;
    float currentPitchRatio = 1.0f;
    std::vector<std::vector<int>> defPartSamples;   // per playable bol: base-bol indices

    juce::AbstractFifo previewFifo { 32 };
    std::array<PreviewRequest, 32> previewQueue;

    std::atomic<bool> playing { false };
    std::atomic<bool> resetRequested { true };
    std::atomic<double> playheadBeat { -1.0 };

    std::atomic<float>* pTempo = nullptr;
    std::atomic<float>* pPitch = nullptr;
    std::atomic<float>* pVolume = nullptr;
    std::atomic<float>* pLoop = nullptr;
    std::atomic<float>* pHostSync = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TablaProcessor)
};
