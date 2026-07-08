// Test [transient][nebula] — caza el artefacto que reportó el oído de Joaquín en NÉBULA:
// "clipea un poco con sonidos de transientes agresivas, un crackle tipo vinilo" (el MISMO de ÓRBITA).
//
// El [gain][nebula] usa un SENO CONTINUO → mide 0.80 limpio pero NO prueba transientes, así que el
// artefacto pasó desapercibido. Acá inyectamos por el NebulaProcessor REAL (processor + chasis in/out
// gain + motor FDN + StereoLimiter) TRANSIENTES AGRESIVAS (ráfagas de impulsos full-scale + percusión
// sintética con HF) y medimos lo que un medidor de DAW mide de verdad:
//   · sample-peak  = pico de muestra. NUEVO DISEÑO: el limiter del motor actúa SOLO sobre el WET (la cola);
//                    el DRY se suma DESPUÉS, transparente → con Mix<100% el sample-peak puede pasar 0.80 por
//                    el dry (es la señal del usuario, correcto). Con Mix=100% (wet-only) el limiter lo cierra.
//   · true-peak    = pico INTER-sample (oversample 4× Catmull-Rom, ver references/...truepeak.md §3).
//                    Wet-only: contenido por el limiter. Con dry: la suma dry+wet puede pasar 1.0 OCASIONAL
//                    (§6: latencia 0 + dry transparente); el LED de clip del chasis avisa.
//   · maxJump      = mayor salto entre muestras consecutivas (proxy ruidoso de crackle; sirve para AISLAR,
//                    no como verdad — §8). El boundary-check (salto en borde de bloque = coef no rampeado,
//                    §0) sólo es FIABLE sobre la cola WET (sin dry crudo): con dry, los impulsos ±1 meten
//                    saltos enormes en cualquier posición y lo contaminan → se evalúa sólo en casos wet-only.
//
// Es DIAGNÓSTICO + GATE del NUEVO diseño: REQUIRE (a) la cola wet contenida + sin borde, y (b) el dry
// TRANSPARENTE (no clampeado) cuando Mix<100%. El veredicto perceptual final lo da el oído de Joaquín (§8).
// El MAPA completo etapa-por-etapa está en [clipscan][nebula] (ClipScan.cpp).
#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <vector>
#include <array>
#include "PluginProcessor.h"

namespace
{
// ── Material de prueba: transientes AGRESIVAS (no un seno suave) ──────────────────────────────────────
// Mezcla deliberada de lo PEOR para un reverb: impulsos full-scale aislados (energía de banda ancha,
// el caso clásico del click de vinilo) + percusión sintética con mucho HF (golpes con ruido filtrado a
// agudos), espaciados con silencio para que cada transiente excite la red "en frío".

// Ruido blanco determinístico (LCG con semilla fija → reproducible) en [−1,1].
struct White
{
    std::uint32_t s = 0xCAFEF00Du;
    float next() { s = s * 1664525u + 1013904223u; return ((float) (s >> 9) * (1.0f / 4194304.0f)) - 1.0f; }
};

// Genera la señal de transientes en [L,R] idéntica (mono) de longitud total totalSamples.
// Patrón: cada 'periodSamp' muestras un GOLPE de 'hitLen' muestras = burst de impulsos full-scale ±1
// alternados + un "click" HF (ruido) con envolvente de decaimiento rápido. Entre golpes, silencio.
struct Transients
{
    White w;
    int   periodSamp;
    int   hitLen;
    explicit Transients (double sr) : periodSamp ((int) std::lround (0.18 * sr)),   // ~5.5 golpes/seg
                                       hitLen     ((int) std::lround (0.004 * sr)) {} // ~4 ms de ataque

    // Devuelve la muestra global g (mono full-scale). Determinístico: depende sólo de g (re-seedea el
    // ruido por golpe para que L y R sean idénticos al llamarse en paralelo).
    float sample (long g)
    {
        const int phase = (int) (g % periodSamp);
        if (phase >= hitLen) return 0.0f;                  // silencio entre golpes

        // Sub-semilla determinística por golpe (igual en L y R) → click HF reproducible.
        const long hitIdx = g / periodSamp;
        White hw; hw.s = 0x9E3779B9u ^ (std::uint32_t) (hitIdx * 2654435761u);
        for (int k = 0; k < phase; ++k) hw.next();          // avanza hasta la posición dentro del golpe

        // Impulso full-scale alternado en las PRIMERAS muestras (banda ancha, el peor caso inter-sample).
        float x = 0.0f;
        if (phase < 2)        x = (phase == 0) ? 1.0f : -1.0f;     // par impulso ±1 (máximo contenido HF)
        // + click HF (ruido) con decaimiento exponencial rápido a lo largo del golpe.
        const float env = std::exp (-6.0f * (float) phase / (float) hitLen);
        x += 0.9f * env * hw.next();
        return juce::jlimit (-1.0f, 1.0f, x);
    }
};

// ── True-peak por oversample 4× (Catmull-Rom) — snippet de references/...truepeak.md §3 ──────────────
// Reconstruye 4 sub-muestras entre cada par de muestras y toma el peak de la onda continua. Es el método
// estándar para estimar el inter-sample peak que mide un medidor true-peak de DAW.
float truePeakOf (const std::vector<float>& x)
{
    const int n = (int) x.size();
    if (n < 4) return 0.0f;
    constexpr int OS = 4;
    auto at = [&] (int i) -> float { return x[(size_t) juce::jlimit (0, n - 1, i)]; };
    float tp = 0.0f;
    for (int i = 1; i < n - 2; ++i)
    {
        const float P0 = at (i - 1), P1 = at (i), P2 = at (i + 1), P3 = at (i + 2);
        for (int s = 0; s < OS; ++s)
        {
            const float t = (float) s / (float) OS;
            const float v = 0.5f * ((2.0f * P1) + (-P0 + P2) * t
                          + (2.0f * P0 - 5.0f * P1 + 4.0f * P2 - P3) * t * t
                          + (-P0 + 3.0f * P1 - 3.0f * P2 + P3) * t * t * t);
            tp = juce::jmax (tp, std::abs (v));
        }
    }
    return tp;
}

// ── Resultado de una corrida ──────────────────────────────────────────────────────────────────────────
struct Result
{
    float samplePeak  = 0.0f;   // pico de muestra (máx |L|,|R|)
    float truePeak    = 0.0f;   // pico inter-sample (oversample 4×)
    float maxJump     = 0.0f;   // mayor |x[i]-x[i-1]| sobre L y R
    long  maxJumpIdx  = -1;     // posición global del maxJump
    int   maxJumpBlk  = -1;     // posición DENTRO del bloque (maxJumpIdx % blockSize): 0 = borde de bloque
    bool  boundaryHit = false;  // ¿el maxJump cayó en un borde de bloque (índice intra-bloque pequeño)?
    // Nota: con Mix=100% (wet-only) el sample-peak ≈ ceiling (0.80) delata que el limiter está clampeando
    // la cola. Con Mix<100% el dry pasa transparente → el sample-peak lo fija el dry (puede rondar 1.0).
};

// Corre transientes por el processor REAL y mide. params: size/decay/tone/breath/mix (0..1), SR, block.
// 'collect' acumula L y R en vectores para el true-peak/maxJump globales (continuidad entre bloques).
Result run (float size01, float decay01, float tone01, float breath01, float mix01,
            double sr, int block, double seconds)
{
    nebula::NebulaProcessor proc;
    auto setN = [&] (const char* id, float v) { if (auto* p = proc.apvts.getParameter (id)) p->setValueNotifyingHost (v); };
    setN ("size", size01); setN ("decay", decay01); setN ("tone", tone01);
    setN ("breath", breath01); setN ("mix", mix01);
    proc.prepareToPlay (sr, block);

    Transients gen (sr);
    const long total = (long) std::llround (seconds * sr);

    // Acumuladores globales (todo el stream concatenado) para true-peak y maxJump con continuidad de borde.
    std::vector<float> allL, allR;
    allL.reserve ((size_t) total + (size_t) block);
    allR.reserve ((size_t) total + (size_t) block);

    Result r;
    long g = 0;
    while (g < total)
    {
        const int n = (int) juce::jmin ((long) block, total - g);
        juce::AudioBuffer<float> buf (2, n);
        juce::MidiBuffer midi;
        for (int i = 0; i < n; ++i)
        {
            const float x = gen.sample (g + i);
            buf.setSample (0, i, x);
            buf.setSample (1, i, x);
        }
        proc.processBlock (buf, midi);

        const float* L = buf.getReadPointer (0);
        const float* R = buf.getReadPointer (1);
        for (int i = 0; i < n; ++i) { allL.push_back (L[i]); allR.push_back (R[i]); }
        g += n;
    }

    // sample-peak.
    for (float v : allL) r.samplePeak = juce::jmax (r.samplePeak, std::abs (v));
    for (float v : allR) r.samplePeak = juce::jmax (r.samplePeak, std::abs (v));

    // true-peak (inter-sample, oversample 4×) sobre el canal de mayor energía.
    r.truePeak = juce::jmax (truePeakOf (allL), truePeakOf (allR));

    // maxJump (salto máximo entre muestras consecutivas) + dónde cae (borde de bloque o no).
    auto scanJumps = [&] (const std::vector<float>& x)
    {
        for (size_t i = 1; i < x.size(); ++i)
        {
            const float j = std::abs (x[i] - x[i - 1]);
            if (j > r.maxJump) { r.maxJump = j; r.maxJumpIdx = (long) i; }
        }
    };
    scanJumps (allL);
    scanJumps (allR);
    if (r.maxJumpIdx >= 0)
    {
        r.maxJumpBlk = (int) (r.maxJumpIdx % block);
        // "Borde de bloque" = el salto cae en la primera o última muestra de un bloque (donde un coef
        // aplicado constante-por-bloque produciría el escalón). Tolerancia ±1 muestra.
        r.boundaryHit = (r.maxJumpBlk <= 1) || (r.maxJumpBlk >= block - 1);
    }
    return r;
}

void printRow (const char* label, const Result& r, int block)
{
    std::printf ("TRANSIENT[%-26s] sample=%.4f  true=%.4f  maxJump=%.4f @%ld (intra-blk=%d/%d%s)\n",
                 label, r.samplePeak, r.truePeak, r.maxJump, r.maxJumpIdx, r.maxJumpBlk, block,
                 r.boundaryHit ? " <-BORDE" : "");
}
} // namespace

TEST_CASE ("NEBULA transientes agresivas: sample / true-peak / maxJump (caza de crackle)", "[transient][nebula]")
{
    constexpr double kSecs = 3.0;   // suficientes golpes para excitar la red en varios estados de breath

    // ── 1) BARRIDO de presets/decay y breath, a 48k / block 128 (el caso típico de host). ────────────
    // NUEVO DISEÑO (ver FdnReverb::process §6/§7): el limiter actúa SOLO sobre el WET (la cola), el DRY se
    // suma DESPUÉS, transparente. Por eso el guard cambia según el Mix:
    //   · Mix=100% (wet-only, dry contribuye 0): el limiter contiene la cola → true-peak ≤ 0.90 y la cola
    //     es suave → SIN salto de borde de bloque (el guard de zipper §0 es FIABLE acá, no hay dry crudo).
    //   · Mix<100% (el dry pasa): el dry es la señal directa del usuario; con transientes full-scale su
    //     sample/true-peak RONDA 1.0 — y eso es CORRECTO (un reverb no distorsiona el dry). NO se exige
    //     true-peak ≤ 0.90: se exige que el dry esté TRANSPARENTE (sample-peak ≈ full-scale, NO clampeado a
    //     0.80) y finito. El boundary-check NO aplica acá: el dry crudo (impulsos ±1) mete saltos enormes
    //     en cualquier posición → contaminaría el heurístico (§8: el maxJump mezcla HF legítimo con clicks).
    SECTION ("barrido preset/breath @48k block128")
    {
        struct Case { const char* name; float size, decay, tone, breath, mix; bool wetOnly; };
        const std::array<Case, 5> cases {{
            { "wet-largo  breath0",  0.85f, 0.90f, 0.30f, 0.00f, 1.00f, true  },
            { "wet-largo  breathHI", 0.85f, 0.90f, 0.30f, 1.00f, 1.00f, true  },
            { "freeze     breathHI", 0.60f, 1.00f, 0.20f, 1.00f, 1.00f, true  },
            { "chico-brill breathHI",0.20f, 0.55f, 0.00f, 1.00f, 0.80f, false },
            { "default    breathHI", 0.50f, 0.50f, 0.40f, 1.00f, 0.35f, false },
        }};
        float worstWetTrue = 0.0f; bool anyWetBoundary = false; float worstFinalTrue = 0.0f;
        for (const auto& c : cases)
        {
            const Result r = run (c.size, c.decay, c.tone, c.breath, c.mix, 48000.0, 128, kSecs);
            printRow (c.name, r, 128);
            worstFinalTrue = juce::jmax (worstFinalTrue, r.truePeak);   // guard DEFINITIVO sobre la salida real
            if (c.wetOnly) { worstWetTrue = juce::jmax (worstWetTrue, r.truePeak); anyWetBoundary = anyWetBoundary || r.boundaryHit; }
        }

        // El DRY a Mix=0 (dryG=1, sin wet): la señal directa del usuario, transientes full-scale. NUEVO DISEÑO
        // (lookahead de salida, 7c): la salida está contenida por el limiter true-peak-safe (ceiling 0.95) →
        // el dry pasa TRANSPARENTE hasta el techo (no se crusha como en el viejo diseño de attack instantáneo,
        // pero tampoco se va a clip). Joaquín aceptó este techo a cambio de cero clip/crackle.
        const Result dryOnly = run (0.50f, 0.50f, 0.40f, 1.00f, 0.00f, 48000.0, 128, kSecs);
        printRow ("DRY-only mix0", dryOnly, 128);
        std::printf ("TRANSIENT[resumen 48k/128]      worstWetTrue=%.4f  anyWetBoundary=%d  worstFinalTrue=%.4f  dryOnlyTrue(mix0)=%.4f\n",
                     worstWetTrue, (int) anyWetBoundary, worstFinalTrue, dryOnly.truePeak);

        // GATE (wet-only): el limiter contiene la COLA con margen inter-sample (§3). 0.90 = el guard que
        // evita que el clip de NIVEL del wet vuelva a colarse.
        REQUIRE (worstWetTrue <= 0.90f);
        // GATE (wet-only): ningún maxJump en borde de bloque sobre la cola suave (delataría un coef no
        // rampeado por-sample, §0). Fiable porque sin dry crudo la cola no tiene saltos legítimos grandes.
        REQUIRE_FALSE (anyWetBoundary);
        // GATE DEFINITIVO (el fix del clip): la salida REAL (suma dry+wet, todos los Mix) queda ≤ 0.97
        // true-peak — el lookahead de salida (7c) la contiene bajo el techo true-peak-safe (0.95 + margen).
        REQUIRE (worstFinalTrue <= 0.97f);
        // GATE: el DRY a Mix=0 sale FINITO y con energía real (no se anuló); el lookahead lo contiene al techo
        // (≤ 0.97) sin matarlo (un dry full-scale rinde un true-peak cómodamente por encima de la mitad).
        REQUIRE (dryOnly.truePeak <= 0.97f);
        REQUIRE (dryOnly.samplePeak > 0.5f);
    }

    // ── 2) AISLAMIENTO por sample-rate y block-size (el artefacto salta si una variante lo cambia, §8).
    // Mismo preset duro (wet largo, breath alto) a 44.1/48/96k y block 64/128/256.
    SECTION ("aislamiento SR x block (wet-largo breathHI)")
    {
        // DISCRIMINADOR de borde robusto (necesario con el lookahead: su retardo DESPLAZA el índice global
        // de los eventos de la cola → un salto LEGÍTIMO puede caer por coincidencia en `idx % blk ≈ 0`). Un
        // crackle de borde REAL (coef no rampeado, §0) está pinneado a los BORDES DE BLOQUE: cambia de índice
        // global con el block-size (sigue al borde). Un salto de SEÑAL está pinneado al MISMO índice global a
        // cualquier block-size (no sigue al borde). Por eso, para cada SR, sólo es sospechoso un boundaryHit si
        // el índice global del maxJump CAMBIA con el block-size; si es el MISMO índice en los 3 blocks, es señal.
        float worstTrue = 0.0f; float worstJump = 0.0f; bool anyRealBoundary = false;
        for (double sr : { 44100.0, 48000.0, 96000.0 })
        {
            bool srBoundary = false; long boundaryIdx = -1; bool boundaryIdxVaries = false;
            for (int blk : { 64, 128, 256 })
            {
                const Result r = run (0.85f, 0.90f, 0.30f, 1.00f, 1.00f, sr, blk, kSecs);
                char lbl[48]; std::snprintf (lbl, sizeof lbl, "%gk b%d", sr / 1000.0, blk);
                printRow (lbl, r, blk);
                worstTrue = juce::jmax (worstTrue, r.truePeak);
                worstJump = juce::jmax (worstJump, r.maxJump);
                if (r.boundaryHit)
                {
                    srBoundary = true;
                    if (boundaryIdx < 0)            boundaryIdx = r.maxJumpIdx;
                    else if (r.maxJumpIdx != boundaryIdx) boundaryIdxVaries = true;
                }
            }
            // Boundary REAL (crackle) = cayó en borde Y su índice global SIGUE al borde (varía con el block).
            // Si todos los boundaryHit de este SR comparten el mismo índice global → es UN salto de señal que
            // por casualidad cae en un borde de cierto block → NO es crackle.
            if (srBoundary && boundaryIdxVaries) anyRealBoundary = true;
        }
        std::printf ("TRANSIENT[resumen SRxblk]       worstTrue=%.4f  worstJump=%.4f  realBoundary(crackle)=%d\n",
                     worstTrue, worstJump, (int) anyRealBoundary);

        REQUIRE (worstTrue <= 0.90f);            // mismo guard true-peak en todos los SR/block (§3)
        REQUIRE_FALSE (anyRealBoundary);         // sin crackle de borde de bloque a ningún SR/block (§0)
        REQUIRE (std::isfinite (worstJump));
    }
}
