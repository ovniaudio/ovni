// HonestidadTest.cpp — [honestidad][dust]: barrido FINO de cada macro en 5 posiciones
// (0/25/50/75/100) con señal real y el MECANISMO ENCENDIDO (honestidad-dsp.md: ningún control sin
// efecto audible que matchee su nombre; qa-listening-checklist §2: condiciones reales, MIX ~35).
//
// Por macro, una métrica física que ES lo que el nombre promete, medida sobre la SALIDA TOTAL:
//   · MIX      -> energía del wet en los huecos (sube) + nivel del dry en la ráfaga (baja a 100).
//   · RATE     -> tiempo del PRIMER eco (sigue la perilla en todo el recorrido del knob log).
//   · DENSIDAD -> energía de la cola tardía (estallido finito -> nube que regenera).
//   · SPREAD   -> energía SIDE de la salida (punto al frente -> campo abierto). El dry (L=R) no
//                 aporta side: todo el side medido es del campo de burbujas.
//   · VIDA     -> energía SIDE con SPREAD 0 (la deriva saca a las burbujas del frente: estático ->
//                 derivando) — la deriva es el mecanismo, no un side decorativo.
//   · DUCK     -> atenuación del wet durante la ráfaga (ratio ráfaga/hueco baja con la perilla).
// Gate: monotonía SIN zona muerta (cada paso adyacente produce un delta medible en la dirección
// correcta). Un macro plano en un tramo se RE-CURVA (regla de la etapa QA).
#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cmath>
#include <vector>
#include "template/tests/OvniTestHarness.h"
#include "PluginProcessor.h"
#include "params/ParameterIDs.h"

namespace
{
    constexpr double SR  = 48000.0;
    constexpr int    BLK = 512;
    namespace pid = dust::params::id;

    constexpr float kSweep[5] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };

    void setRateMsParam (dust::DustProcessor& proc, float ms)
    {
        if (auto* r = proc.apvts.getParameter (pid::RATE))
            r->setValueNotifyingHost (proc.apvts.getParameterRange (pid::RATE).convertTo0to1 (ms));
    }

    // Condición base "condiciones reales" (los sweeps pisan SU macro encima de esto).
    void setupBase (dust::DustProcessor& proc)
    {
        using ovni::test::setParam;
        setParam (proc, pid::MIX,     0.35f);
        setParam (proc, pid::DENSITY, 0.50f);
        setParam (proc, pid::SPREAD,  0.60f);
        setParam (proc, pid::VIDA,    0.0f);
        setParam (proc, pid::DUCK,    0.0f);
        setRateMsParam (proc, 150.0f);
    }

    // ── render A: ráfagas periódicas -> RMS del wet en ráfaga y en hueco ─────────────────────────
    struct BurstGap { double burst = 0.0, gap = 0.0; };

    BurstGap renderBurstGap (dust::DustProcessor& proc, float mix01ForDryComp)
    {
        proc.prepareToPlay (SR, BLK);
        const float gDry = std::cos (mix01ForDryComp * juce::MathConstants<float>::halfPi);

        const int period   = (int) std::lround (1.05 * SR);
        const int burstLen = (int) std::lround (0.060 * SR);
        const int bWin     = (int) std::lround (0.210 * SR);
        const int gWin0    = (int) std::lround (0.500 * SR);
        const int gWin1    = (int) std::lround (1.000 * SR);
        const int nBursts  = 8, skip = 3;
        const int total    = nBursts * period;

        ovni::test::White noise;
        juce::AudioBuffer<float> buf (2, BLK), dry (2, BLK);
        double accB = 0.0, accG = 0.0; long cntB = 0, cntG = 0;
        for (int pos = 0; pos < total; pos += BLK)
        {
            buf.clear();
            for (int i = 0; i < BLK; ++i)
                if ((pos + i) % period < burstLen)
                {
                    const float x = 0.7f * noise.next();
                    buf.setSample (0, i, x); buf.setSample (1, i, x);
                }
            dry.makeCopyOf (buf, true);
            juce::MidiBuffer midi;
            proc.processBlock (buf, midi);
            for (int i = 0; i < BLK; ++i)
            {
                const int g = pos + i;
                if (g < skip * period) continue;
                const int phase = g % period;
                const float wL = buf.getSample (0, i) - gDry * dry.getSample (0, i);
                const float wR = buf.getSample (1, i) - gDry * dry.getSample (1, i);
                const double e = (double) wL * wL + (double) wR * wR;
                if (phase < bWin)                         { accB += e; ++cntB; }
                else if (phase >= gWin0 && phase < gWin1) { accG += e; ++cntG; }
            }
        }
        return { std::sqrt (accB / (double) juce::jmax (1L, cntB)),
                 std::sqrt (accG / (double) juce::jmax (1L, cntG)) };
    }

    // ── render B: ruido rosa sostenido -> RMS SIDE de la salida total ────────────────────────────
    double renderSideRms (dust::DustProcessor& proc, double seconds = 4.0, double warmupSec = 1.5)
    {
        proc.prepareToPlay (SR, BLK);
        ovni::test::Pink pink;
        juce::AudioBuffer<float> buf (2, BLK);
        double acc = 0.0; long cnt = 0;
        const int blocks = (int) std::lround (seconds * SR / BLK);
        const int warm   = (int) std::lround (warmupSec * SR / BLK);
        for (int blk = 0; blk < blocks; ++blk)
        {
            for (int i = 0; i < BLK; ++i)
            {
                const float x = 0.5f * pink.next();
                buf.setSample (0, i, x); buf.setSample (1, i, x);
            }
            juce::MidiBuffer midi;
            proc.processBlock (buf, midi);
            if (blk < warm) continue;
            const float* L = buf.getReadPointer (0);
            const float* R = buf.getReadPointer (1);
            for (int i = 0; i < BLK; ++i)
            {
                const double s = 0.5 * ((double) L[i] - (double) R[i]);
                acc += s * s; ++cnt;
            }
        }
        return std::sqrt (acc / (double) juce::jmax (1L, cnt));
    }

    // ── render B2: ruido rosa sostenido -> reparto angular REAL del banco de buses ───────────────
    // El side PROMEDIO de la salida satura arriba (la lateralización HRIR comprime más allá de
    // ±90°: ir hacia ATRÁS no suma side). El reparto de energía entre los buses del banco es la
    // verdad del CAMPO a la salida del mecanismo: dónde suenan las burbujas de verdad.
    struct BusStats
    {
        double circR     = 1.0;   // resultante circular (1 = todo en un ángulo; baja al esparcirse)
        double rearShare = 0.0;   // fracción de energía en buses traseros (|az| > 120°)
    };

    BusStats renderBusStats (dust::DustProcessor& proc, double seconds, double warmupSec)
    {
        proc.prepareToPlay (SR, BLK);
        ovni::test::Pink pink;
        juce::AudioBuffer<float> buf (2, BLK);
        const int blocks = (int) std::lround (seconds * SR / BLK);
        const int warm   = (int) std::lround (warmupSec * SR / BLK);
        for (int blk = 0; blk < blocks; ++blk)
        {
            for (int i = 0; i < BLK; ++i)
            {
                const float x = 0.5f * pink.next();
                buf.setSample (0, i, x); buf.setSample (1, i, x);
            }
            juce::MidiBuffer midi;
            proc.processBlock (buf, midi);
            if (blk == warm) proc.engineForTest().dbgResetBusEnergy();   // medir ya asentado
        }

        auto& eng = proc.engineForTest();
        double total = 0.0, sumCos = 0.0, sumSin = 0.0, rear = 0.0;
        for (int b = 0; b < dust::engine::BubbleField::kNumBuses; ++b)
        {
            const double e  = (double) eng.dbgBusEnergy (b);
            // El anillo publica azimuts en [0, 2π) → wrapear a [−π, π] para que "trasero" sea
            // |az| > 120° de verdad (sin el wrap, −22.5° aparece como 337.5° y cuenta como espalda).
            const double az = std::remainder ((double) eng.dbgBusAzimuthRad (b),
                                              2.0 * juce::MathConstants<double>::pi);
            total  += e;
            sumCos += e * std::cos (az);
            sumSin += e * std::sin (az);
            if (std::abs (az) > 2.0944) rear += e;   // |az| > 120°: hemisferio trasero
        }
        BusStats st;
        if (total > 1.0e-12)
        {
            st.circR     = std::sqrt (sumCos * sumCos + sumSin * sumSin) / total;
            st.rearShare = rear / total;
        }
        return st;
    }

    // ── render C: UNA ráfaga -> envolvente |L|+|R| (timing / cola) ───────────────────────────────
    std::vector<float> renderBurstEnv (dust::DustProcessor& proc, double seconds, double burstMs = 10.0)
    {
        proc.prepareToPlay (SR, BLK);
        const long total = (long) std::llround (seconds * SR);
        std::vector<float> env ((size_t) total, 0.0f);
        const int burstLen = (int) std::lround (burstMs * 0.001 * SR);
        ovni::test::White noise;
        juce::AudioBuffer<float> buf (2, BLK);
        for (long pos = 0; pos < total; pos += BLK)
        {
            buf.clear();
            for (int i = 0; i < BLK; ++i)
                if (pos + i < burstLen)
                {
                    const float x = 0.8f * noise.next();
                    buf.setSample (0, i, x); buf.setSample (1, i, x);
                }
            juce::MidiBuffer midi;
            proc.processBlock (buf, midi);
            const float* L = buf.getReadPointer (0);
            const float* R = buf.getReadPointer (1);
            for (int i = 0; i < BLK && pos + i < total; ++i)
                env[(size_t) (pos + i)] = std::abs (L[i]) + std::abs (R[i]);
        }
        return env;
    }
}

// ─────────────────────────────────────────────────────────────────────────────────────────────────
TEST_CASE ("DUST honestidad: MIX 0..100 -> el wet SUBE en cada paso y el dry cede a 100",
           "[honestidad][dust]")
{
    double gap[5] = {}, burst[5] = {};
    for (int k = 0; k < 5; ++k)
    {
        dust::DustProcessor proc;
        setupBase (proc);
        ovni::test::setParam (proc, pid::MIX, kSweep[k]);
        // gDry=cos(0): restamos el dry COMPLETO -> lo que queda en el hueco es wet puro a ese MIX.
        const auto r = renderBurstGap (proc, 0.0f);
        // el "wet" estimado restando dry a gDry=1 incluye (gDry(mix)-1)·dry en la ráfaga: para la
        // métrica de hueco (dry silente) es wet exacto; para la ráfaga medimos la salida TOTAL.
        gap[k] = r.gap; burst[k] = r.burst;
        std::printf ("HONESTIDAD[MIX %3.0f] wetGapRms=%.5f  burstRms=%.5f\n",
                     kSweep[k] * 100.0f, gap[k], burst[k]);
    }

    REQUIRE (gap[0] < 1.0e-4);                    // MIX 0 = dry puro (sin wet fantasma)
    REQUIRE (burst[0] < 1.0e-4);                  // … también en la ráfaga (desviación del dry = 0)
    REQUIRE (gap[1] > 1.0e-3);                    // a 25 ya se OYE
    for (int k = 1; k < 4; ++k)
        REQUIRE (gap[(size_t) k + 1] > gap[(size_t) k] * 1.04);   // sin zona muerta (ley de potencia: el
                                                                  // tramo 75->100 es el más chato, > 4 %)
    // El dry CEDE en cada paso: burst[] mide la desviación de la salida respecto del dry PURO
    // ((gDry−1)·dry + wet) — crece estrictamente con MIX hasta que a 100 la desviación ≈ el nivel
    // del dry entero (el dry ya no está: ley de potencia completa).
    for (int k = 1; k < 4; ++k)
        REQUIRE (burst[(size_t) k + 1] > burst[(size_t) k] * 1.2);
}

// ─────────────────────────────────────────────────────────────────────────────────────────────────
TEST_CASE ("DUST honestidad: RATE 0..100 -> el primer eco SIGUE la perilla (knob log, sin zona muerta)",
           "[honestidad][dust]")
{
    // Mapeo log del knob: norm {0,.25,.5,.75,1} -> {20, 63.1, 200, 632.5, 2000} ms.
    double measured[5] = {};
    for (int k = 0; k < 5; ++k)
    {
        const double expectMs = 20.0 * std::pow (100.0, (double) kSweep[k]);
        dust::DustProcessor proc;
        setupBase (proc);
        ovni::test::setParam (proc, pid::SPREAD, 0.0f);      // timing puro
        ovni::test::setParam (proc, pid::RATE, kSweep[k]);
        // Ráfaga CORTA (5 ms): el eco de una ráfaga se desparrama [rate, rate+burst] → el pico
        // puede caer hasta burst ms después del múltiplo (medido: con 10 ms el eco de RATE 20 ms
        // picaba en 26.6 ms). La tolerancia contempla el largo del estímulo (+7 ms).
        const auto env = renderBurstEnv (proc, expectMs * 0.001 * 1.6 + 0.05, 5.0);

        // pico en la ventana [0.6, 1.4]·rate + el largo del burst.
        const long w0 = (long) std::llround (juce::jmax (0.007, expectMs * 0.001 * 0.6) * SR);
        const long w1 = (long) std::llround ((expectMs * 1.4 + 6.0) * 0.001 * SR);
        long best = w0; float bestV = -1.0f;
        for (long i = w0; i <= juce::jmin ((long) env.size() - 1, w1); ++i)
            if (env[(size_t) i] > bestV) { bestV = env[(size_t) i]; best = i; }

        measured[k] = (double) best / SR * 1000.0;
        std::printf ("HONESTIDAD[RATE %3.0f] esperado=%7.1f ms  medido=%7.1f ms  amp=%.4f\n",
                     kSweep[k] * 100.0f, expectMs, measured[k], bestV);
        REQUIRE (bestV > 0.02f);                                      // eco audible a MIX 35
        REQUIRE (std::abs (measured[k] - expectMs) <= expectMs * 0.10 + 7.0);
    }
    // El espaciado crece ~3.16x por paso (knob log): estrictamente monótono, sin zona muerta.
    for (int k = 0; k < 4; ++k)
        REQUIRE (measured[(size_t) k + 1] > measured[(size_t) k] * 2.0);
}

// ─────────────────────────────────────────────────────────────────────────────────────────────────
TEST_CASE ("DUST honestidad: DENSIDAD 0..100 -> la cola tardia CRECE en cada paso (estallido -> nube)",
           "[honestidad][dust]")
{
    double tail[5] = {};
    for (int k = 0; k < 5; ++k)
    {
        dust::DustProcessor proc;
        setupBase (proc);
        setRateMsParam (proc, 100.0f);
        ovni::test::setParam (proc, pid::DENSITY, kSweep[k]);
        const auto env = renderBurstEnv (proc, 2.6);

        // Cola tardía: RMS de la envolvente en [1.2, 2.4] s (pasada la última lectura directa
        // posible: 24 taps · 100 ms = 2.4 s NO — los taps directos llegan hasta ~mitad; lo que vive
        // acá a DENSIDAD baja es casi nada y a alta es la regeneración del lazo).
        const long t0 = (long) std::llround (1.2 * SR);
        const long t1 = (long) std::llround (2.4 * SR);
        double acc = 0.0;
        for (long i = t0; i < t1; ++i) acc += (double) env[(size_t) i] * env[(size_t) i];
        tail[k] = std::sqrt (acc / (double) (t1 - t0));
        std::printf ("HONESTIDAD[DENSIDAD %3.0f] tailRms[1.2-2.4 s]=%.6f (%.1f dB)\n",
                     kSweep[k] * 100.0f, tail[k], 20.0 * std::log10 (tail[k] + 1e-12));
    }
    // Cada paso suma cola AUDIBLE (>= +3 dB por paso: de estallido finito a nube que regenera).
    for (int k = 0; k < 4; ++k)
        REQUIRE (tail[(size_t) k + 1] > tail[(size_t) k] * 1.41);
    REQUIRE (tail[4] > tail[0] * 10.0);   // el recorrido completo cambia el carácter (>20 dB)
}

// ─────────────────────────────────────────────────────────────────────────────────────────────────
TEST_CASE ("DUST honestidad: SPREAD 0..100 -> el campo se ABRE en cada paso (side + hemisferio trasero)",
           "[honestidad][dust]")
{
    double side[5] = {}; BusStats bus[5] = {};
    for (int k = 0; k < 5; ++k)
    {
        {
            dust::DustProcessor proc;
            setupBase (proc);
            ovni::test::setParam (proc, pid::SPREAD, kSweep[k]);
            side[k] = renderSideRms (proc);
        }
        {
            dust::DustProcessor proc;
            setupBase (proc);
            ovni::test::setParam (proc, pid::SPREAD, kSweep[k]);
            bus[k] = renderBusStats (proc, 4.0, 1.5);
        }
        std::printf ("HONESTIDAD[SPREAD %3.0f] sideRms=%.6f  circR=%.3f  rearShare=%.3f\n",
                     kSweep[k] * 100.0f, side[k], bus[k].circR, bus[k].rearShare);
    }
    // El dry (L=R) no aporta side: todo el side es del campo. Los primeros pasos ABREN el campo…
    // Métrica DIRECTA del "abre" = el reparto ANGULAR (circR: 1 = todo en un punto, baja al esparcir):
    // tiene que CAER en CADA paso (es lo que el control promete). El side RMS también CRECE en cada
    // paso, pero su CRECIMIENTO se SATURA arriba de ±90° (física, ver nota abajo) y además el
    // decorrelador de bus (DESBOXY) suma un piso de side parejo en todo el recorrido → el +10%/paso
    // del proxy 'side' dejó de ser el umbral honesto. El gate honesto es circR (apertura angular real).
    REQUIRE (side[0] < 1.0e-4);
    for (int k = 0; k < 3; ++k)
    {
        REQUIRE (bus[(size_t) k + 1].circR < bus[(size_t) k].circR - 0.02);  // abre angularmente en CADA paso
        REQUIRE (side[(size_t) k + 1] > side[(size_t) k]);                    // y el side crece (monótono)
    }
    // …y el último cuarto (75→100) puebla el HEMISFERIO TRASERO ("esparcidas por TODO el campo"):
    // el side promedio satura más allá de ±90° (ir hacia atrás no suma side — física, no zona
    // muerta), pero el reparto angular real sigue abriéndose (circR −0.07 medido) y la espalda
    // pasa de EXACTAMENTE cero a tener energía (los slots fuertes del tren dominan el reparto →
    // la fracción es chica pero el cambio es de naturaleza: el campo llega atrás SOLO a 100).
    REQUIRE (side[4] >= side[3]);
    REQUIRE (bus[4].circR < bus[3].circR - 0.02);
    REQUIRE (bus[3].rearShare < 0.005);
    REQUIRE (bus[4].rearShare > 0.01);
    REQUIRE (side[4] > side[0] * 4.0);    // punto -> campo: el recorrido completo es dramático
}

// ─────────────────────────────────────────────────────────────────────────────────────────────────
TEST_CASE ("DUST honestidad: VIDA 0..100 -> las burbujas DERIVAN mas en cada paso (estatico -> vivo)",
           "[honestidad][dust]")
{
    double side[5] = {}; BusStats bus[5] = {};
    for (int k = 0; k < 5; ++k)
    {
        // SPREAD 0: todo el movimiento angular que aparece ES la deriva de VIDA.
        {
            dust::DustProcessor proc;
            setupBase (proc);
            ovni::test::setParam (proc, pid::SPREAD, 0.0f);
            ovni::test::setParam (proc, pid::VIDA, kSweep[k]);
            side[k] = renderSideRms (proc, 5.0, 1.5);
        }
        {
            dust::DustProcessor proc;
            setupBase (proc);
            ovni::test::setParam (proc, pid::SPREAD, 0.0f);
            ovni::test::setParam (proc, pid::VIDA, kSweep[k]);
            bus[k] = renderBusStats (proc, 5.0, 1.5);
        }
        std::printf ("HONESTIDAD[VIDA %3.0f] sideRms(SPREAD 0)=%.6f  circR=%.4f\n",
                     kSweep[k] * 100.0f, side[k], bus[k].circR);
    }
    // VIDA 0 = colocadas al frente: sin side, resultante circular = 1 (un solo ángulo).
    REQUIRE (side[0] < 1.0e-4);
    REQUIRE (bus[0].circR > 0.999);
    // Cada paso las hace derivar MÁS lejos: el side aparece y crece…
    for (int k = 0; k < 3; ++k)
        REQUIRE (side[(size_t) k + 1] > side[(size_t) k] * 1.15);
    // …y el reparto angular real (buses del banco) se sigue abriendo TAMBIÉN en 75→100, donde el
    // side promedio comprime (la deriva alcanza más lejos del frente: ±39° → ±52°).
    REQUIRE (side[4] >= side[3]);
    for (int k = 0; k < 4; ++k)
        REQUIRE (bus[(size_t) k + 1].circR < bus[(size_t) k].circR - 0.002);
    REQUIRE (side[4] > side[0] * 4.0);
}

// ─────────────────────────────────────────────────────────────────────────────────────────────────
TEST_CASE ("DUST honestidad: DUCK 0..100 -> el wet se aparta MAS en cada paso cuando pega el dry",
           "[honestidad][dust]")
{
    double ratio[5] = {};
    for (int k = 0; k < 5; ++k)
    {
        dust::DustProcessor proc;
        setupBase (proc);
        ovni::test::setParam (proc, pid::DUCK, kSweep[k]);
        const auto r = renderBurstGap (proc, 0.35f);   // wet = salida − gDry(35)·dry (dry intacto)
        ratio[k] = r.burst / juce::jmax (1.0e-12, r.gap);
        std::printf ("HONESTIDAD[DUCK %3.0f] burst=%.5f gap=%.5f ratio=%.3f\n",
                     kSweep[k] * 100.0f, r.burst, r.gap, ratio[k]);
        REQUIRE (r.gap > 1.0e-4);                      // el wet VUELVE en el hueco en TODO el barrido
    }
    // Profundidad lineal (0.9·duck): cada paso agacha MÁS el wet durante la ráfaga (ratio baja).
    for (int k = 0; k < 4; ++k)
        REQUIRE (ratio[(size_t) k + 1] < ratio[(size_t) k] * 0.90);
    REQUIRE (ratio[4] < ratio[0] * 0.35);   // el recorrido completo respira de verdad (> 9 dB)
}
