#include "Taal.h"
#include "BolLibrary.h"

double Taal::length() const
{
    double total = 0.0;
    for (auto& e : events) total += e.duration;
    return total;
}

double Taal::startOf (int index) const
{
    double s = 0.0;
    for (int i = 0; i < index && i < (int) events.size(); ++i) s += events[(size_t) i].duration;
    return s;
}

int Taal::indexAtBeat (double beat) const
{
    double s = 0.0;
    for (int i = 0; i < (int) events.size(); ++i)
    {
        const double e = s + events[(size_t) i].duration;
        if (beat >= s && beat < e) return i;
        s = e;
    }
    return -1;
}

static juce::String num (double v)
{
    auto s = juce::String (v, 4);
    if (s.contains (".")) s = s.trimCharactersAtEnd ("0").trimCharactersAtEnd (".");
    return s;
}

juce::String Taal::toText() const
{
    juce::StringArray parts;
    for (auto& e : events)
    {
        juce::String p = e.bol.equalsIgnoreCase ("Rest") ? juce::String ("-") : e.bol;
        if (std::abs (e.duration - 1.0) > 1.0e-6) p << "[" << num (e.duration) << "]";
        if (std::abs (e.emphasisDb) > 1.0e-3f) p << "{" << (e.emphasisDb > 0 ? "+" : "") << num (e.emphasisDb) << "}";
        parts.add (p);
    }
    return parts.joinIntoString (", ");
}

bool Taal::parseText (const juce::String& text, std::vector<BolEvent>& out, juce::String& error)
{
    out.clear();
    error = {};
    auto s = text.trim();
    if (s.startsWithIgnoreCase ("tabla:")) s = s.substring (6);

    auto t = s.getCharPointer();
    auto isSep = [] (juce::juce_wchar c) { return c == ',' || c == '|' || juce::CharacterFunctions::isWhitespace (c); };

    while (! t.isEmpty())
    {
        while (! t.isEmpty() && isSep (*t)) ++t;
        if (t.isEmpty()) break;

        juce::String name;
        while (! t.isEmpty() && ! isSep (*t) && *t != '[' && *t != '{') { name += *t; ++t; }

        BolEvent ev;
        if (name == "-" || name.equalsIgnoreCase ("rest")) ev.bol = "Rest";
        else
        {
            auto canon = BolLibrary::canonicalName (name);
            if (canon.isEmpty()) { error = "Unknown bol: \"" + name + "\""; return false; }
            ev.bol = canon;
        }

        auto readGroup = [&] (juce::juce_wchar open, juce::juce_wchar close, double& dest) -> bool
        {
            if (t.isEmpty() || *t != open) return true;
            ++t;
            juce::String n;
            while (! t.isEmpty() && *t != close) { n += *t; ++t; }
            if (t.isEmpty()) { error = "Missing closing bracket after " + ev.bol; return false; }
            ++t;
            if (n.contains ("/"))   // fractions: Dha[1/4]
            {
                const double den = n.fromFirstOccurrenceOf ("/", false, false).getDoubleValue();
                dest = den != 0.0 ? n.upToFirstOccurrenceOf ("/", false, false).getDoubleValue() / den : 0.0;
            }
            else dest = n.getDoubleValue();
            return true;
        };

        double dur = 1.0, db = 0.0;
        if (! readGroup ('[', ']', dur)) return false;
        if (! readGroup ('{', '}', db)) return false;
        if (dur <= 0.0 || dur > 64.0) { error = "Bad duration for " + ev.bol + " (use e.g. [0.25] or [1/4])"; return false; }
        ev.duration = dur;
        ev.emphasisDb = (float) juce::jlimit (-24.0, 12.0, db);
        out.push_back (ev);
    }
    return true;
}

juce::var Taal::toVar() const
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("name", name);
    obj->setProperty ("defaultTempo", defaultTempo);
    juce::Array<juce::var> arr;
    for (auto& e : events)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("bol", e.bol);
        o->setProperty ("duration", e.duration);
        o->setProperty ("emphasisDb", e.emphasisDb);
        arr.add (juce::var (o));
    }
    obj->setProperty ("bols", arr);
    return juce::var (obj);
}

bool Taal::fromVar (const juce::var& v, Taal& out)
{
    if (v.getDynamicObject() == nullptr || ! v["bols"].isArray()) return false;
    Taal t;
    t.name = v["name"].toString();
    if (t.name.isEmpty()) t.name = "Untitled";
    t.defaultTempo = v.hasProperty ("defaultTempo") ? (double) v["defaultTempo"] : 100.0;
    for (auto& b : *v["bols"].getArray())
    {
        BolEvent e;
        auto n = b["bol"].toString();
        e.bol = n.equalsIgnoreCase ("Rest") ? juce::String ("Rest") : BolLibrary::canonicalName (n);
        if (e.bol.isEmpty()) continue;
        e.duration = juce::jlimit (0.0625, 64.0, (double) b["duration"]);
        e.emphasisDb = (float) (double) b["emphasisDb"];
        t.events.push_back (e);
    }
    out = std::move (t);
    return true;
}

juce::String Taal::toJson() const { return juce::JSON::toString (toVar(), false); }

bool Taal::fromJson (const juce::String& json, Taal& out)
{
    juce::var v;
    if (juce::JSON::parse (json, v).failed()) return false;
    return fromVar (v, out);
}
