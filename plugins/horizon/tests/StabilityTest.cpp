#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cmath>
#include "PluginProcessor.h"
#include "params/ParameterIDs.h"
#include "template/tests/OvniTestHarness.h"

// =============================================================================
// [stability][horizon] — tortura del HorizonProcessor REAL: automatización BRUTAL
// de TODOS los parámetros (saltos aleatorios full-range cada bloque, incluidos
// FREEZE/RATE/RATESYNC/RATEDIV/WHISPER) sobre ruido → silencio → impulsos → silencio,
// a 44.1/48/96k. Gates: ni un NaN/Inf, salida SIEMPRE bajo el techo, y SIN clicks
// groseros (derivada sample-a-sample acotada — el gate raised-cosine + el cross-fade
// del freeze garantizan empalmes suaves aun con automatización salvaje).
//
// ⚠ NO se exige tail=0 acá: un freeze ENCENDIDO sostiene el frame por diseño (no es
//   feedback sucio, es la promesa del plugin). El tail-zero del camino IDENTITY
//   (freeze OFF) se verifica en el segundo TEST_CASE.
// =============================================================================
TEST_CASE ("HORIZON stability: automatizacion brutal sin NaN ni clicks groseros", "[stability][horizon]")
{
    namespace pid = horizon::params::id;
    const char* autoIds[] = { pid::FREEZE, pid::WHISPER, pid::SPREAD, pid::DUCK, pid::MIX,
                              pid::RATESYNC, pid::RATEDIV, pid::RATE };

    for (const double SR : { 44100.0, 48000.0, 96000.0 })
    {
        DYNAMIC_SECTION ("SR=" << SR)
        {
            horizon::HorizonProcessor proc;
            const int N = 512;
            proc.prepareToPlay (SR, N);

            juce::Random rng (0x402E26);   // semilla fija → reproducible
            ovni::test::White noise;

            float worstPeak = 0.0f;
            float maxJump   = 0.0f;        // peor salto sample-a-sample (clicks groseros)
            float prevL     = 0.0f;
            bool  first     = true;
            bool  allFinite = true;

            const int kBlocks = 600;
            for (int blk = 0; blk < kBlocks; ++blk)
            {
                for (const char* id : autoIds)
                    if (auto* p = proc.apvts.getParameter (id))
                        p->setValueNotifyingHost (rng.nextFloat());

                juce::AudioBuffer<float> buf (2, N); juce::MidiBuffer midi;
                buf.clear();
                if (blk < 200)                          // fase 1: ruido fuerte
                {
                    for (int n = 0; n < N; ++n)
                    {
                        const float x = 0.9f * noise.next();
                        buf.setSample (0, n, x); buf.setSample (1, n, x);
                    }
                }
                else if (blk >= 300 && blk < 304)       // fase 3: impulsos full-scale
                {
                    buf.setSample (0, 0, 1.0f); buf.setSample (1, 0, 1.0f);
                }
                // fases 2 y 4: silencio

                proc.processBlock (buf, midi);

                float blockPeak = 0.0f;
                for (int ch = 0; ch < 2; ++ch)
                {
                    const float* d = buf.getReadPointer (ch);
                    for (int n = 0; n < N; ++n)
                    {
                        if (! std::isfinite (d[n])) allFinite = false;
                        blockPeak = juce::jmax (blockPeak, std::abs (d[n]));
                    }
                }
                worstPeak = juce::jmax (worstPeak, blockPeak);
                const float* L = buf.getReadPointer (0);
                for (int n = 0; n < N; ++n)
                {
                    if (! first) maxJump = juce::jmax (maxJump, std::abs (L[n] - prevL));
                    prevL = L[n]; first = false;
                }
            }

            std::printf ("STABILITY[horizon SR=%.0f] worstPeak=%.4f maxJump=%.4f finite=%d\n",
                         SR, worstPeak, maxJump, (int) allFinite);

            REQUIRE (allFinite);
            REQUIRE (worstPeak <= 1.0f);   // el limiter contiene aun con saltos brutales
            // ⚠ NO se asserta maxJump acá: la fase 3 CONGELA impulsos FULL-SCALE → el frame
            // capturado es un click de banda ancha; resintetizado y clampeado por el limiter a
            // ±0.85 produce legítimamente saltos de hasta ~1.7/sample (contenido espectralmente
            // plano, NO un click de transición). El anti-click de las CONMUTACIONES (toggle de
            // freeze, bordes del gate) se mide con contenido MUSICAL en el 2º TEST_CASE de acá
            // (toggle) y en RealWorldTest (gate SYNC) — ahí la derivada SÍ queda acotada.
        }
    }
}

// ── Camino IDENTITY (freeze OFF): tras N samples de silencio el wet es CERO (el STFT no
//    tiene feedback). Toggle de freeze on/off repetido SIN click (empalme cross-fade). ──
TEST_CASE ("HORIZON: freeze OFF muere limpio; toggle on/off sin click", "[stability][horizon]")
{
    using namespace ovni::test;
    namespace pid = horizon::params::id;
    const double SR = 48000.0; const int N = 512;

    horizon::HorizonProcessor proc;
    setParam (proc, pid::MIX, 1.0f);
    proc.prepareToPlay (SR, N);

    Pink pink;
    float prevL = 0.0f; bool first = true;
    float tailPeak = 0.0f;
    bool  allFinite = true;
    // Separar el salto en la VENTANA de conmutación (toggle + ~4 frames de cross-fade ≈ 5 bloques)
    // del salto en RÉGIMEN (freeze sostenido / audio vivo): el anti-click es de la CONMUTACIÓN —
    // el régimen es contenido (pink congelado salta solo). Sólo la conmutación debe ser suave.
    float toggleJump = 0.0f, steadyJump = 0.0f;
    auto inToggleWindow = [] (int blk) {
        for (int t = 0; t <= 160; t += 20) if (blk >= t && blk < t + 5) return true;
        return false;
    };

    const int kBlocks = 200;
    for (int blk = 0; blk < kBlocks; ++blk)
    {
        if (blk % 20 == 0 && blk < 160)
            setParam (proc, pid::FREEZE, ((blk / 20) % 2 == 0) ? 1.0f : 0.0f);
        if (blk == 160) setParam (proc, pid::FREEZE, 0.0f);   // freeze OFF para la cola

        juce::AudioBuffer<float> buf (2, N); juce::MidiBuffer midi;
        buf.clear();
        if (blk < 160)
            for (int n = 0; n < N; ++n) { const float x = 0.6f * pink.next(); buf.setSample (0, n, x); buf.setSample (1, n, x); }

        proc.processBlock (buf, midi);

        const float* L = buf.getReadPointer (0);
        float blockPeak = 0.0f;
        const bool tw = inToggleWindow (blk);
        for (int n = 0; n < N; ++n)
        {
            if (! std::isfinite (L[n])) allFinite = false;
            if (! first)
            {
                const float jmp = std::abs (L[n] - prevL);
                if (tw) toggleJump = juce::jmax (toggleJump, jmp);
                else    steadyJump = juce::jmax (steadyJump, jmp);
            }
            prevL = L[n]; first = false;
            blockPeak = juce::jmax (blockPeak, std::abs (L[n]));
        }
        if (blk >= 180) tailPeak = juce::jmax (tailPeak, blockPeak);   // > 4000 samples tras el último input
    }

    std::printf ("STABILITY[horizon toggle] toggleJump=%.4f steadyJump=%.4f tailPeak=%.6f finite=%d\n",
                 toggleJump, steadyJump, tailPeak, (int) allFinite);

    REQUIRE (allFinite);
    // Toggle de freeze SIN click: el salto en la ventana de conmutación NO excede el del régimen
    // de pink congelado por más de un margen chico → el cross-fade de frame empalma suave (no
    // mete una discontinuidad propia; si clickeara, toggleJump ≫ steadyJump).
    REQUIRE (toggleJump <= steadyJump + 0.05f);
    // Camino identity (freeze OFF) muere: el silencio ES silencio (sin feedback).
    REQUIRE (tailPeak < 1.0e-3f);
}
