// [diccionario][horizon] — REGLA ANTI-BUG PULSAR ampliada: TODAS las macros mueven el campo
// (HorizonField), no sólo freeze/gate. Se prueba el visualizador DIRECTO con atomics (como los
// circulitos de HALO / las cortinas de AURORA), por la geometría DIBUJADA (alpha + X frac), que
// es la MISMA fórmula que paintLive (helpers compartidos, DRY):
//   · WHISPER → el cristal TIEMBLA: jitter horizontal por lámina (X se mueve frame a frame).
//   · SPREAD  → el espectro se ABRE: las bandas extremas se separan del centro.
//   · DUCK    → el cristal se ATENÚA cuando entra el dry (duck·duckEnv baja el alpha).
//   · MIX     → intensidad global: a más Mix, más alpha (piso de luz: nunca apagado).
//   · RATE    → el latido RECORRE el espectro (barrido de luz por fase del gate).
//   · reduced-motion → un frame estático COHERENTE (no negro): el campo se LEE igual, sin animar.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <atomic>
#include <array>
#include <cmath>
#include "ui/HorizonField.h"

namespace
{
constexpr int kB = horizon::ui::HorizonField::kBands;

struct Tele
{
    std::atomic<float> whisper {0.0f}, spread {0.5f}, duck {0.0f}, mix {1.0f}, freeze {1.0f};
    std::atomic<float> gateAmp {1.0f}, gatePhase {0.0f}, wetEnergy {0.0f}, duckEnv {0.0f}, rate {0.0f};
    std::array<std::atomic<float>, kB> spectrum {};
    Tele() { for (int b = 0; b < kB; ++b) spectrum[(size_t) b].store (0.55f); }
};

float meanAlpha (horizon::ui::HorizonField& f)
{
    float s = 0.0f;
    for (int b = 0; b < kB; ++b) s += f.dbgBandAlpha (b);
    return s / (float) kB;
}
}

// WHISPER → el cristal TIEMBLA: la X dibujada de una lámina cambia entre frames (jitter vivo).
TEST_CASE ("HORIZON campo: WHISPER hace temblar el cristal (la X se mueve)", "[diccionario][horizon]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    Tele t;

    auto maxXTravel = [] (Tele& tt, int frames)
    {
        horizon::ui::HorizonField f (tt.whisper, tt.spread, tt.duck, tt.mix, tt.freeze,
                                     tt.gateAmp, tt.gatePhase, tt.wetEnergy, tt.duckEnv, tt.rate, tt.spectrum);
        f.dbgAdvanceFrames (40);                       // converger one-poles
        const int band = kB / 3;                       // una banda no central (se mueve con jitter)
        float lo =  1.0f, hi = -1.0f;
        for (int i = 0; i < frames; ++i) { f.dbgAdvanceFrames (1);
            const float x = f.dbgBandX (band); lo = juce::jmin (lo, x); hi = juce::jmax (hi, x); }
        return hi - lo;
    };

    t.whisper.store (0.0f);
    const float still = maxXTravel (t, 60);
    t.whisper.store (1.0f);
    const float trembling = maxXTravel (t, 60);
    INFO ("still=" << still << "  trembling=" << trembling);
    REQUIRE (still < 1.0e-3f);            // whisper 0 = cristalino quieto (sin temblor)
    REQUIRE (trembling > 0.004f);         // whisper 100 = tiembla visiblemente
}

// SPREAD → el espectro se ABRE: una banda interior (no clampeada en el borde) se EMPUJA hacia su
// lado al subir el spread (las graves a la izquierda, las agudas a la derecha). Mido bandas
// interiores para que el clamp [0..1] no oculte el abanico.
TEST_CASE ("HORIZON campo: SPREAD abre el espectro a los lados", "[diccionario][horizon]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    Tele t;
    t.whisper.store (0.0f);   // sin jitter para medir el abanico puro

    auto xAt = [&] (int band, float spread01)
    {
        t.spread.store (spread01);
        horizon::ui::HorizonField f (t.whisper, t.spread, t.duck, t.mix, t.freeze,
                                     t.gateAmp, t.gatePhase, t.wetEnergy, t.duckEnv, t.rate, t.spectrum);
        f.dbgAdvanceFrames (120);
        return f.dbgBandX (band);
    };

    const int lowBand  = kB / 6;          // banda grave interior (lado izquierdo del centro)
    const int highBand = kB - 1 - kB / 6; // banda aguda interior (lado derecho del centro)

    const float lowClosed  = xAt (lowBand, 0.0f),  lowOpen  = xAt (lowBand, 1.0f);
    const float highClosed = xAt (highBand, 0.0f), highOpen = xAt (highBand, 1.0f);
    INFO ("low: " << lowClosed << "->" << lowOpen << "   high: " << highClosed << "->" << highOpen);
    REQUIRE (lowOpen  < lowClosed  - 0.02f);   // la grave se empuja a la IZQUIERDA (se abre)
    REQUIRE (highOpen > highClosed + 0.02f);   // la aguda se empuja a la DERECHA (se abre)
}

// DUCK → el cristal se ATENÚA cuando entra el dry (duck·duckEnv baja el alpha). Sin pegada
// (duckEnv 0) el duck no hace nada (honesto: reacciona a la señal, no es un volumen fijo).
TEST_CASE ("HORIZON campo: DUCK atenua el cristal cuando entra el dry", "[diccionario][horizon]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    Tele t;

    auto alphaWith = [&] (float duck01, float env01)
    {
        t.duck.store (duck01); t.duckEnv.store (env01);
        horizon::ui::HorizonField f (t.whisper, t.spread, t.duck, t.mix, t.freeze,
                                     t.gateAmp, t.gatePhase, t.wetEnergy, t.duckEnv, t.rate, t.spectrum);
        f.dbgAdvanceFrames (120);
        return meanAlpha (f);
    };

    const float noPunch  = alphaWith (1.0f, 0.0f);   // duck alto pero SIN pegada → sin efecto
    const float full     = alphaWith (0.0f, 1.0f);   // sin duck, con pegada → referencia
    const float ducked   = alphaWith (1.0f, 1.0f);   // duck alto + pegada → el cristal se aparta
    INFO ("noPunch=" << noPunch << "  full=" << full << "  ducked=" << ducked);
    REQUIRE (noPunch == Catch::Approx (full).margin (0.01f));   // sin pegada, el duck no atenúa
    REQUIRE (ducked < full * 0.95f);                            // con pegada, el cristal SE ATENÚA (se VE)
}

// MIX → intensidad global con PISO DE LUZ: a Mix 0 el cristal se atenúa pero NO desaparece
// (gancho a 320 px); a Mix 100 brilla pleno. Monótono.
TEST_CASE ("HORIZON campo: MIX sube la intensidad con piso de luz (nunca apagado)", "[diccionario][horizon]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    Tele t;

    auto alphaAtMix = [&] (float mix01)
    {
        t.mix.store (mix01);
        horizon::ui::HorizonField f (t.whisper, t.spread, t.duck, t.mix, t.freeze,
                                     t.gateAmp, t.gatePhase, t.wetEnergy, t.duckEnv, t.rate, t.spectrum);
        f.dbgAdvanceFrames (120);
        return meanAlpha (f);
    };

    const float lo = alphaAtMix (0.0f);
    const float hi = alphaAtMix (1.0f);
    INFO ("mix0=" << lo << "  mix100=" << hi);
    REQUIRE (lo > 0.05f);          // piso de luz: a Mix 0 el cristal se LEE (no apagado)
    REQUIRE (hi > lo * 1.3f);      // monótono: más Mix = más intensidad
}

// RATE (latido vivo) → el barrido de luz RECORRE el espectro: con RATE>0 la lámina cercana a la
// cresta del gate (gatePhase) brilla MÁS que la lejana; con RATE 0 (pad) no hay barrido.
TEST_CASE ("HORIZON campo: con RATE el latido recorre el espectro (barrido de luz)", "[diccionario][horizon]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    Tele t;
    t.whisper.store (0.0f);
    t.gatePhase.store (0.0f);     // cresta en los graves (banda 0)

    auto headVsTail = [&] (float rate01)
    {
        t.rate.store (rate01);
        horizon::ui::HorizonField f (t.whisper, t.spread, t.duck, t.mix, t.freeze,
                                     t.gateAmp, t.gatePhase, t.wetEnergy, t.duckEnv, t.rate, t.spectrum);
        f.dbgAdvanceFrames (120);
        return f.dbgBandAlpha (0) - f.dbgBandAlpha (kB / 2);   // cerca de la cresta − lejos
    };

    const float pad   = headVsTail (0.0f);   // RATE 0 = pad: sin barrido (head ≈ tail)
    const float beat  = headVsTail (1.0f);   // RATE vivo: la cresta brilla más
    INFO ("pad=" << pad << "  beat=" << beat);
    REQUIRE (std::abs (pad) < 0.02f);        // pad quieto: no hay barrido
    REQUIRE (beat > pad + 0.02f);            // con RATE, el latido recorre el espectro (se VE)
}

// FRAME ESTÁTICO COHERENTE (lo que ve reduced-motion): el ctor SIEMBRA el estado desde los
// atomics + un advanceFrame → el campo se LEE legible SIN correr el timer (no negro). Es la
// garantía que usa reduced-motion (la base congela la animación pero muestra este frame).
TEST_CASE ("HORIZON campo: el ctor deja un frame estatico coherente (legible sin animar)", "[diccionario][horizon]")
{
    juce::ScopedJuceInitialiser_GUI gui;

    Tele t;   // freeze on, mix 100, espectro lleno → debe verse aun sin correr el timer
    horizon::ui::HorizonField f (t.whisper, t.spread, t.duck, t.mix, t.freeze,
                                 t.gateAmp, t.gatePhase, t.wetEnergy, t.duckEnv, t.rate, t.spectrum);
    // El ctor ya sembró el estado + un advanceFrame: el campo es coherente sin animar más.
    const float a = meanAlpha (f);
    INFO ("frame estatico meanAlpha=" << a);
    REQUIRE (a > 0.10f);   // legible (no apagado) con la animación congelada (reduced-motion)
}
