#pragma once
#include <JuceHeader.h>

struct Sample
{
    juce::AudioBuffer<float> data;      // mono
    double sampleRate = 44100.0;
    bool custom = false;                // loaded from a user wav instead of the built-in synth
};

struct SampleBank
{
    std::vector<Sample> samples;        // indexed by base-bol index
    int customCount = 0;
    juce::File folder;
};

// Describes every bol the user can place in a taal. "Base" bols have one sample;
// compound bols (Dhage, Tirakita...) are played as several base bols spread evenly
// over the duration of the event.
class BolLibrary
{
public:
    struct Def
    {
        juce::String name;
        juce::String description;
        juce::StringArray parts;        // empty for base bols
    };

    static const std::vector<Def>& defs();
    static juce::StringArray playableNames();
    static const Def* find (const juce::String& name);            // case-insensitive
    static juce::String canonicalName (const juce::String& name); // "" if unknown
    static int baseIndex (const juce::String& name);              // -1 for compound / unknown
    static int numBase();

    // Synthesises every base bol, then replaces any bol that has a matching wav file in `folder`.
    static std::shared_ptr<SampleBank> buildBank (const juce::File& folder);
    static juce::File defaultFolder();
};
