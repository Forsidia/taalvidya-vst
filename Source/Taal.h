#pragma once
#include <JuceHeader.h>

// One bol in a taal. A bol named "Rest" is silent.
struct BolEvent
{
    juce::String bol { "Dha" };
    double duration = 1.0;      // in beats
    float emphasisDb = 0.0f;
    int breaksBefore = 0;       // line breaks typed before this bol in the text box (layout only, no sound)
};

struct Taal
{
    juce::String name { "Untitled" };
    double defaultTempo = 100.0;
    std::vector<BolEvent> events;

    double length() const;
    double startOf (int index) const;
    int indexAtBeat (double beat) const;

    // "Dha, Dhin[0.5], -, Ge{+3}"  (| is also accepted)  <->  events
    juce::String toText() const;
    static bool parseText (const juce::String& text, std::vector<BolEvent>& out, juce::String& error);

    // Durations are shown and saved as fractions ("1/4", "1/3", "3/2"), never as decimals.
    static juce::String formatDuration (double beats);
    static double parseDuration (const juce::String& text);   // "1/4", "0.25", "3/2"; <= 0 if invalid
    static double snapDuration (double beats);                // nearest simple fraction, e.g. 0.3333 -> 1/3

    juce::var toVar() const;
    static bool fromVar (const juce::var& v, Taal& out);
    juce::String toJson() const;
    static bool fromJson (const juce::String& json, Taal& out);
};

namespace BuiltinTaals { std::vector<Taal> create(); }
