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

namespace
{
    // Smallest denominator (<= 64) that reproduces `v` closely enough; false if none does.
    bool asFraction (double v, int& n, int& d)
    {
        for (int den = 1; den <= 64; ++den)
        {
            const double x = v * den;
            if (std::abs (v - std::round (x) / den) < 1.0e-3)
            {
                n = (int) std::round (x);
                d = den;
                return n > 0;
            }
        }
        return false;
    }
}

double Taal::snapDuration (double beats)
{
    int n = 0, d = 1;
    return asFraction (beats, n, d) ? (double) n / (double) d : beats;
}

juce::String Taal::formatDuration (double beats)
{
    int n = 0, d = 1;
    if (! asFraction (beats, n, d)) return juce::String (beats, 3);   // odd value: still readable
    return d == 1 ? juce::String (n) : juce::String (n) + "/" + juce::String (d);
}

double Taal::parseDuration (const juce::String& text)
{
    const auto t = text.trim();
    double v;
    if (t.contains ("/"))
    {
        const double den = t.fromFirstOccurrenceOf ("/", false, false).getDoubleValue();
        v = den != 0.0 ? t.upToFirstOccurrenceOf ("/", false, false).getDoubleValue() / den : 0.0;
    }
    else v = t.getDoubleValue();
    return v > 0.0 ? snapDuration (v) : 0.0;
}

static juce::String num (double v)
{
    auto s = juce::String (v, 4);
    if (s.contains (".")) s = s.trimCharactersAtEnd ("0").trimCharactersAtEnd (".");
    return s;
}

juce::String Taal::toText() const
{
    juce::String out;
    bool first = true;
    for (auto& e : events)
    {
        juce::String p = e.bol.equalsIgnoreCase ("Rest") ? juce::String ("-") : e.bol;
        if (std::abs (e.duration - 1.0) > 1.0e-6) p << "[" << formatDuration (e.duration) << "]";
        if (std::abs (e.emphasisDb) > 1.0e-3f) p << "{" << (e.emphasisDb > 0 ? "+" : "") << num (e.emphasisDb) << "}";
        if (! first) out << (e.breaksBefore > 0 ? "," + juce::String::repeatedString ("\n", e.breaksBefore) : juce::String (", "));
        out << p;
        first = false;
    }
    return out;
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
        int newlines = 0;
        while (! t.isEmpty() && isSep (*t)) { if (*t == '\n') ++newlines; ++t; }
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
            if (open == '[') dest = snapDuration (dest);   // 0.3333 typed -> exactly 1/3
            return true;
        };

        double dur = 1.0, db = 0.0;
        if (! readGroup ('[', ']', dur)) return false;
        if (! readGroup ('{', '}', db)) return false;
        if (dur <= 0.0 || dur > 64.0) { error = "Bad duration for " + ev.bol + " (use e.g. [0.25] or [1/4])"; return false; }
        ev.duration = dur;
        ev.breaksBefore = out.empty() ? 0 : std::min (newlines, 4);
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
        if (e.breaksBefore > 0) o->setProperty ("breaksBefore", e.breaksBefore);
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
        e.duration = snapDuration (juce::jlimit (0.0625, 64.0, (double) b["duration"]));
        e.emphasisDb = (float) (double) b["emphasisDb"];
        e.breaksBefore = juce::jlimit (0, 4, (int) b["breaksBefore"]);
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
