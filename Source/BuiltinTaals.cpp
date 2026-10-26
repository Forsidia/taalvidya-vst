#include "Taal.h"

namespace
{
    Taal make (const char* name, double tempo, const char* text)
    {
        Taal t;
        t.name = name;
        t.defaultTempo = tempo;
        juce::String err;
        const bool ok = Taal::parseText (text, t.events, err);
        jassert (ok && err.isEmpty());
        juce::ignoreUnused (ok);
        return t;
    }
}

std::vector<Taal> BuiltinTaals::create()
{
    std::vector<Taal> v;
    v.push_back (make ("Ardh Trital", 200, "Dha | Dhin | Dhin | Dha | Dha | Tin | Tin | Na"));
    v.push_back (make ("Chautal", 100, "Dha | Dha | Dhin | Ta | Kat | Dha | Dhin | Ta | Tit | Kat | Ge | Na"));
    v.push_back (make ("Dadra", 120, "Dha | Dhi | Na | Dha | Tu | Na"));
    v.push_back (make ("Dhamar", 90, "Ka | Dhi | Te | Dhi | Te | Dha | - | Ge | Ti | Te | Ti | Te | Ta | -"));
    v.push_back (make ("Dipchandi", 100, "Dha | Dhin | - | Dha | Ge | Tin | - | Ta | Tin | - | Dha | Ge | Dhin | -"));
    v.push_back (make ("Ektal", 90, "Dhin | Dhin | Dhage | Tirakita | Tu | Na | Ka | Ta | Dhage | Tirakita | Dhin | Na"));
    v.push_back (make ("Jhaptal", 100, "Dhi | Na | Dhi | Dhi | Na | Ti | Na | Dhi | Dhi | Na"));
    v.push_back (make ("Keherwa", 120, "Dha | Ge | Na | Ti | Na | Ka | Dhi | Na"));
    v.push_back (make ("Rupak", 100, "Tin | Tin | Na | Dhi | Na | Dhi | Na"));
    v.push_back (make ("Teental", 100, "Dha | Dhin | Dhin | Dha | Dha | Dhin | Dhin | Dha | Dha | Tin | Tin | Ta | Ta | Dhin | Dhin | Dha"));
    v.push_back (make ("Tilwada", 80, "Dha | Tirakita | Dhin | Dhin | Dha | Dha | Tin | Tin | Ta | Tirakita | Dhin | Dhin | Dha | Dha | Dhin | Dhin"));
    return v;
}
