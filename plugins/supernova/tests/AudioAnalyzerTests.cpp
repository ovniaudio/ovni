// [supernova][analyzer] — AudioAnalyzer con señales sintéticas deterministas (spec §6/§9.1).
#include <catch2/catch_test_macros.hpp>
#include <juce_core/juce_core.h>
#include <vector>
#include <cmath>
#include "analysis/AudioAnalyzer.h"

using supernova::AudioAnalyzer;
using supernova::AnalysisFrame;

namespace
{
constexpr double kSR = 48000.0;

std::vector<float> makeSine (double freq, int n, float amp = 1.0f)
{
    std::vector<float> v ((size_t) n);
    for (int i = 0; i < n; ++i)
        v[(size_t) i] = amp * std::sin (juce::MathConstants<double>::twoPi * freq * i / kSR);
    return v;
}

// Empuja la señal y devuelve el último frame estable (post-warmup).
AnalysisFrame lastFrame (AudioAnalyzer& a, const std::vector<float>& sig)
{
    a.push (sig.data(), (int) sig.size());
    AnalysisFrame f {}, last {};
    bool got = false;
    while (a.popFrame (f)) { last = f; got = true; }
    REQUIRE (got);
    return last;
}
}

TEST_CASE ("analyzer: un seno grave enciende la banda de graves", "[supernova][analyzer]")
{
    AudioAnalyzer a; a.prepare (kSR, 11);
    const auto f = lastFrame (a, makeSine (100.0, (int) kSR));   // 100 Hz, 1 s
    std::printf ("ANALYZER_100HZ bass=%.3f mid=%.3f treb=%.3f\n", f.bass, f.mid, f.treble);
    REQUIRE (f.bass > 0.1f);                    // el mecanismo hizo algo
    REQUIRE (f.bass > f.mid * 3.0f);
    REQUIRE (f.bass > f.treble * 3.0f);
}

TEST_CASE ("analyzer: un seno agudo enciende la banda de agudos", "[supernova][analyzer]")
{
    AudioAnalyzer a; a.prepare (kSR, 11);
    const auto f = lastFrame (a, makeSine (8000.0, (int) kSR));  // 8 kHz, 1 s
    std::printf ("ANALYZER_8KHZ bass=%.3f mid=%.3f treb=%.3f\n", f.bass, f.mid, f.treble);
    REQUIRE (f.treble > 0.1f);
    REQUIRE (f.treble > f.bass * 3.0f);
}

TEST_CASE ("analyzer: el RMS sigue la amplitud", "[supernova][analyzer]")
{
    AudioAnalyzer a; a.prepare (kSR, 11);
    const auto f = lastFrame (a, makeSine (1000.0, (int) kSR, 0.5f));   // amplitud 0.5
    std::printf ("ANALYZER_RMS=%.3f (esperado ~0.5)\n", f.rms);
    REQUIRE (f.rms > 0.4f);
    REQUIRE (f.rms < 0.6f);
}

TEST_CASE ("analyzer: onsets disparan con clicks y NO con un seno estable", "[supernova][analyzer]")
{
    // Tren de clicks (impulsos en silencio) cada 0.2 s durante 1.6 s.
    const int n = (int) (kSR * 1.6);
    std::vector<float> clicks ((size_t) n, 0.0f);
    for (int t = (int) (kSR * 0.2); t < n; t += (int) (kSR * 0.2))
        clicks[(size_t) t] = 1.0f;

    AudioAnalyzer a; a.prepare (kSR, 11);
    a.push (clicks.data(), n);
    AnalysisFrame f {}; int onsets = 0;
    while (a.popFrame (f)) if (f.onset) ++onsets;
    std::printf ("ANALYZER_ONSETS_CLICKS=%d\n", onsets);
    REQUIRE (onsets >= 3);              // varios de los ~7 clicks disparan

    // Seno estable: tras el warmup no debería haber onsets.
    AudioAnalyzer b; b.prepare (kSR, 11);
    {
        auto s = makeSine (440.0, (int) kSR);
        b.push (s.data(), (int) s.size());
    }
    AnalysisFrame g {}; int falseOnsets = 0; int frames = 0;
    while (b.popFrame (g)) { ++frames; if (g.onset && frames > 4) ++falseOnsets; }
    std::printf ("ANALYZER_ONSETS_SINE=%d (post-warmup)\n", falseOnsets);
    REQUIRE (falseOnsets == 0);
}
