#include "BolLibrary.h"
#include <cmath>
#include <BinaryData.h>

namespace
{
    constexpr double kSr = 44100.0;
    constexpr double kTwoPi = 6.283185307179586;
    constexpr double kDayanHz = 277.18; // C#4, TaalMala's default tabla pitch

    using Buf = std::vector<float>;

    Buf makeBuf (double seconds) { return Buf ((size_t) (seconds * kSr), 0.0f); }

    // Dayan (treble drum): a handful of near-harmonic partials with faster decay for higher ones.
    void addDayan (Buf& b, double f0, std::initializer_list<std::pair<double, double>> partials,
                   double tau, double tauSlope, double gain, double bend = 0.0)
    {
        for (auto [ratio, amp] : partials)
        {
            const double pTau = tau / (1.0 + tauSlope * (ratio - 1.0) * 0.5);
            double phase = 0.0;
            for (size_t i = 0; i < b.size(); ++i)
            {
                const double t = (double) i / kSr;
                const double f = f0 * ratio * (1.0 + bend * std::exp (-t / 0.03));
                phase += kTwoPi * f / kSr;
                const double env = std::min (1.0, t / 0.0008) * std::exp (-t / pTau);
                b[i] += (float) (gain * amp * env * std::sin (phase));
            }
        }
    }

    // Bayan (bass drum): sine that glides down in pitch plus a touch of second harmonic.
    void addBayan (Buf& b, double fStart, double fEnd, double glideTau, double tau, double gain, double harmonic)
    {
        double phase = 0.0;
        for (size_t i = 0; i < b.size(); ++i)
        {
            const double t = (double) i / kSr;
            const double f = fEnd + (fStart - fEnd) * std::exp (-t / glideTau);
            phase += kTwoPi * f / kSr;
            const double env = std::min (1.0, t / 0.002) * std::exp (-t / tau);
            b[i] += (float) (gain * env * (std::sin (phase) + harmonic * std::sin (2.0 * phase)));
        }
    }

    // Low-passed noise burst for stick/skin slaps and attack clicks.
    void addNoise (Buf& b, juce::Random& rng, double tau, double gain, double lowpass)
    {
        double y = 0.0;
        for (size_t i = 0; i < b.size(); ++i)
        {
            const double t = (double) i / kSr;
            y += lowpass * ((double) rng.nextFloat() * 2.0 - 1.0 - y);
            b[i] += (float) (gain * y * std::exp (-t / tau));
        }
    }

    void mixInto (Buf& dst, const Buf& src, float gain = 1.0f)
    {
        for (size_t i = 0; i < dst.size() && i < src.size(); ++i) dst[i] += src[i] * gain;
    }

    // ---- individual strokes ------------------------------------------------------------------
    Buf na (juce::Random& r, double tau, double f = kDayanHz)
    {
        auto b = makeBuf (1.0);
        addDayan (b, f, { { 1, 0.35 }, { 2, 1.0 }, { 3, 0.7 }, { 4, 0.35 }, { 5, 0.2 } }, tau, 1.2, 0.5);
        addNoise (b, r, 0.004, 0.45, 0.7);
        return b;
    }

    Buf tin (juce::Random& r)
    {
        auto b = makeBuf (1.4);
        addDayan (b, kDayanHz, { { 1, 0.2 }, { 2, 1.0 }, { 3, 0.9 }, { 4, 0.6 }, { 5, 0.35 }, { 6, 0.2 } }, 0.5, 0.9, 0.45);
        addNoise (b, r, 0.003, 0.5, 0.85);
        return b;
    }

    Buf tun (juce::Random& r, double tau)
    {
        auto b = makeBuf (1.6);
        addDayan (b, kDayanHz, { { 1, 1.0 }, { 2, 0.55 }, { 3, 0.25 }, { 4, 0.1 } }, tau, 1.5, 0.55, 0.012);
        addNoise (b, r, 0.005, 0.25, 0.5);
        return b;
    }

    Buf te (juce::Random& r, double tau, double f, float gain)
    {
        auto b = makeBuf (0.3);
        addDayan (b, f, { { 1, 0.5 }, { 2, 1.0 }, { 3, 0.6 }, { 4, 0.3 } }, tau, 0.5, 0.5);
        addNoise (b, r, 0.006, 0.55, 0.6);
        for (auto& s : b) s *= gain;
        return b;
    }

    Buf ge (double fStart, double fEnd, double tau, double harmonic)
    {
        auto b = makeBuf (1.5);
        addBayan (b, fStart, fEnd, 0.1, tau, 0.9, harmonic);
        return b;
    }

    Buf ke (juce::Random& r, double noiseTau, float gain)
    {
        auto b = makeBuf (0.3);
        addNoise (b, r, noiseTau, 1.1, 0.35);
        addBayan (b, 150.0, 110.0, 0.02, 0.035, 0.5, 0.2);
        for (auto& s : b) s *= gain;
        return b;
    }

    void normalise (Buf& b, float peak = 0.8f)
    {
        float m = 0.0f;
        for (auto s : b) m = std::max (m, std::abs (s));
        if (m < 1.0e-6f) return;
        const float g = peak / m;
        for (auto& s : b) s *= g;

        // trim the tail once it has decayed below -66 dB so voices free up quickly
        size_t last = b.size();
        while (last > 1 && std::abs (b[last - 1]) < 0.0005f) --last;
        b.resize (std::min (b.size(), last + 64));
        const size_t fade = std::min<size_t> (b.size(), 256);
        for (size_t i = 0; i < fade; ++i) b[b.size() - 1 - i] *= (float) i / (float) fade;
    }

    Buf synthesise (const juce::String& name)
    {
        juce::Random rng ((juce::int64) name.hashCode64());
        Buf b;
        auto sum = [] (Buf x, const Buf& y) { if (y.size() > x.size()) x.resize (y.size(), 0.0f); mixInto (x, y); return x; };

        if      (name == "Na")   b = na (rng, 0.22);
        else if (name == "Ta")   b = na (rng, 0.14, kDayanHz * 1.01);
        else if (name == "Tin")  b = tin (rng);
        else if (name == "Tun")  b = tun (rng, 0.9);
        else if (name == "Tu")   b = tun (rng, 0.3);
        else if (name == "Te")   b = te (rng, 0.035, kDayanHz, 1.0f);
        else if (name == "Ti")   b = te (rng, 0.055, kDayanHz * 1.02, 0.9f);
        else if (name == "Tit")  b = te (rng, 0.02, kDayanHz * 0.98, 0.8f);
        else if (name == "Re")   b = te (rng, 0.03, kDayanHz * 1.05, 0.6f);
        else if (name == "Ra")   b = te (rng, 0.04, kDayanHz * 1.03, 0.7f);
        else if (name == "Ge")   b = ge (150.0, 92.0, 0.45, 0.25);
        else if (name == "Ghe")  b = ge (175.0, 105.0, 0.65, 0.35);
        else if (name == "Ke")   b = ke (rng, 0.012, 1.0f);
        else if (name == "Ka")   b = ke (rng, 0.02, 0.8f);
        else if (name == "Ki")   b = ke (rng, 0.01, 0.6f);
        else if (name == "Kat")  b = sum (ke (rng, 0.014, 1.0f), te (rng, 0.03, kDayanHz, 0.7f));
        else if (name == "Dha")  b = sum (na (rng, 0.22), ge (150.0, 92.0, 0.45, 0.25));
        else if (name == "Dhin") b = sum (tin (rng), ge (150.0, 92.0, 0.5, 0.25));
        else if (name == "Dhi")  b = sum (te (rng, 0.09, kDayanHz * 1.02, 1.0f), ge (150.0, 92.0, 0.4, 0.25));
        else if (name == "Dhe")  b = sum (te (rng, 0.04, kDayanHz, 1.0f), ge (150.0, 92.0, 0.35, 0.25));
        else                     b = makeBuf (0.05);

        normalise (b);
        return b;
    }

    // ---- bol table ---------------------------------------------------------------------------
    std::vector<BolLibrary::Def> buildDefs()
    {
        using D = BolLibrary::Def;
        std::vector<D> v;
        auto base = [&] (const char* n, const char* d) { v.push_back ({ n, d, {} }); };
        auto comp = [&] (const char* n, const char* d, juce::StringArray p) { v.push_back ({ n, d, p }); };

        // The order of the base bols defines the index in SampleBank.
        base ("Dha",  "Na (dayan rim) + Ge (open bayan)");
        base ("Dhin", "Tin (dayan ring) + Ge (open bayan)");
        base ("Dhi",  "Closed dayan + Ge");
        base ("Dhe",  "Short closed dayan + Ge");
        base ("Na",   "Dayan rim stroke, index finger");
        base ("Ta",   "Dayan rim stroke, short");
        base ("Tin",  "Dayan ringing stroke on the maidan");
        base ("Tun",  "Dayan centre ringing stroke");
        base ("Tu",   "Dayan centre stroke, short");
        base ("Te",   "Dayan closed stroke");
        base ("Ti",   "Dayan closed stroke, soft");
        base ("Tit",  "Dayan closed stroke, very short");
        base ("Re",   "Soft closed dayan stroke");
        base ("Ra",   "Soft closed dayan stroke");
        base ("Ge",   "Bayan open stroke with pitch glide");
        base ("Ghe",  "Bayan open stroke, heavier");
        base ("Ke",   "Bayan closed slap");
        base ("Ka",   "Bayan closed slap, soft");
        base ("Ki",   "Bayan closed slap, light");
        base ("Kat",  "Bayan slap with dayan closed stroke");

        comp ("Dhage",    "Dha Ge in one beat",             { "Dha", "Ge" });
        comp ("Dhere",    "Dhe Re in one beat",             { "Dhe", "Re" });
        comp ("Tete",     "Te Te in one beat",              { "Te", "Te" });
        comp ("Tirakita", "Ti Ra Ki Ta in one beat",        { "Ti", "Ra", "Ki", "Ta" });
        comp ("Terekite", "Te Re Ki Te in one beat",        { "Te", "Re", "Ki", "Te" });
        comp ("Dhatit",   "Dha Ti Ta in one beat",          { "Dha", "Ti", "Ta" });
        return v;
    }

    int numBaseBols()
    {
        int n = 0;
        for (auto& d : BolLibrary::defs()) if (d.parts.isEmpty()) ++n;
        return n;
    }
}

const std::vector<BolLibrary::Def>& BolLibrary::defs()
{
    static const std::vector<Def> d = buildDefs();
    return d;
}

juce::StringArray BolLibrary::playableNames()
{
    juce::StringArray n;
    for (auto& d : defs()) n.add (d.name);
    return n;
}

const BolLibrary::Def* BolLibrary::find (const juce::String& name)
{
    for (auto& d : defs())
        if (d.name.equalsIgnoreCase (name.trim())) return &d;
    return nullptr;
}

juce::String BolLibrary::canonicalName (const juce::String& name)
{
    if (auto* d = find (name)) return d->name;
    return {};
}

int BolLibrary::baseIndex (const juce::String& name)
{
    int idx = 0;
    for (auto& d : defs())
    {
        if (! d.parts.isEmpty()) continue;
        if (d.name.equalsIgnoreCase (name)) return idx;
        ++idx;
    }
    return -1;
}

int BolLibrary::numBase() { static const int n = numBaseBols(); return n; }

juce::File BolLibrary::defaultFolder()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("TablaVST").getChildFile ("Samples");
}

namespace
{
    bool readInto (juce::AudioFormatReader* reader, Sample& s, float normaliseTo)
    {
        if (reader == nullptr || reader->lengthInSamples <= 0) return false;
        juce::AudioBuffer<float> tmp ((int) reader->numChannels, (int) reader->lengthInSamples);
        reader->read (&tmp, 0, (int) reader->lengthInSamples, 0, true, true);
        s.data.setSize (1, tmp.getNumSamples());
        s.data.clear();
        for (int c = 0; c < tmp.getNumChannels(); ++c)
            s.data.addFrom (0, 0, tmp, c, 0, tmp.getNumSamples(), 1.0f / (float) tmp.getNumChannels());
        s.sampleRate = reader->sampleRate;
        s.custom = true;
        if (normaliseTo > 0.0f)
        {
            const float peak = s.data.getMagnitude (0, s.data.getNumSamples());
            if (peak > 1.0e-5f) s.data.applyGain (normaliseTo / peak);
        }
        return true;
    }

    // Bols that are two strokes played together; used when only the single strokes are recorded.
    struct Layered { const char* name; const char* a; const char* b; };
    const Layered kLayered[] = { { "Dha", "Na", "Ge" }, { "Dhin", "Tin", "Ghe" }, { "Dhi", "Ti", "Ge" },
                                 { "Dhe", "Te", "Ge" }, { "Kat", "Ke", "Te" } };
}

std::shared_ptr<SampleBank> BolLibrary::buildBank (const juce::File& folder)
{
    auto bank = std::make_shared<SampleBank>();
    bank->folder = folder;
    bank->samples.resize ((size_t) numBase());

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();

    // 1. user folder, 2. samples embedded in the plugin (BinaryData), 3. synthesised placeholder
    int idx = 0;
    for (auto& d : defs())
    {
        if (! d.parts.isEmpty()) continue;
        auto& s = bank->samples[(size_t) idx++];

        if (folder.isDirectory())
        {
            for (auto& f : folder.findChildFiles (juce::File::findFiles, false))
            {
                if (! f.getFileNameWithoutExtension().equalsIgnoreCase (d.name)) continue;
                std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (f));
                if (readInto (reader.get(), s, 0.0f)) { ++bank->customCount; break; }
            }
        }

        if (! s.custom)
        {
            int size = 0;
            if (auto* data = BinaryData::getNamedResource ((d.name + "_wav").toRawUTF8(), size))
            {
                std::unique_ptr<juce::AudioFormatReader> reader (
                    fm.createReaderFor (std::make_unique<juce::MemoryInputStream> (data, (size_t) size, false)));
                readInto (reader.get(), s, 0.9f);
            }
        }
    }

    // 4. layered bols (Dha = Na + Ge ...) when they have no recording of their own
    for (auto& l : kLayered)
    {
        auto& dst = bank->samples[(size_t) baseIndex (l.name)];
        auto& a = bank->samples[(size_t) baseIndex (l.a)];
        auto& b = bank->samples[(size_t) baseIndex (l.b)];
        if (dst.custom || ! (a.custom || b.custom) || a.sampleRate != b.sampleRate) continue;

        const int n = std::max (a.data.getNumSamples(), b.data.getNumSamples());
        dst.data.setSize (1, n);
        dst.data.clear();

        // Balance by loudness of the first 80 ms (bayan Ge is much louder than a ringing Tin),
        // then renormalise so the sum can never clip.
        auto attackRms = [] (const juce::AudioBuffer<float>& x)
        {
            const int k = std::min (x.getNumSamples(), 3528);
            return std::max (1.0e-4f, x.getRMSLevel (0, 0, k));
        };
        const float target = 0.5f * (attackRms (a.data) + attackRms (b.data));
        dst.data.addFrom (0, 0, a.data, 0, 0, a.data.getNumSamples(), target / attackRms (a.data));
        dst.data.addFrom (0, 0, b.data, 0, 0, b.data.getNumSamples(), target / attackRms (b.data));
        const float peak = dst.data.getMagnitude (0, n);
        if (peak > 1.0e-5f) dst.data.applyGain (0.9f / peak);
        dst.sampleRate = a.sampleRate;
        dst.custom = true;
    }

    // 5. whatever is still empty gets the synthesised placeholder
    idx = 0;
    for (auto& d : defs())
    {
        if (! d.parts.isEmpty()) continue;
        auto& s = bank->samples[(size_t) idx++];
        if (s.custom) continue;
        auto buf = synthesise (d.name);
        s.data.setSize (1, (int) buf.size());
        s.data.copyFrom (0, 0, buf.data(), (int) buf.size());
        s.sampleRate = kSr;
    }
    return bank;
}
