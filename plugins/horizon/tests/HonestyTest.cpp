#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cmath>
#include <vector>
#include "PluginProcessor.h"
#include "params/ParameterIDs.h"
#include "template/tests/OvniTestHarness.h"

// =============================================================================
// [honestidad][horizon] — BARRIDO FINO de cada macro en 5 posiciones (0/25/50/75/
// 100) con el MECANISMO ENCENDIDO (FREEZE on SIEMPRE) y señal real: cada knob
// produce un cambio MEDIBLE y monótono sobre la SALIDA TOTAL (lo que el productor
// escucha). Un macro con zona muerta NO pasa (regla de honestidad del sello: "mové
// el control a ciegas → un productor nota el cambio que el nombre promete").
//
// Métricas (FREEZE on en TODOS los casos):
//   · WHISPER → un seno CONGELADO se difunde: la energía FUERA del bin fundamental
//     sube (fase randomizada por frame → bandas laterales). 0 = vidrioso/limpio.
//   · SPREAD  → WIDTH global del freeze esparcido sube.
//   · DUCK    → el side del wet DURANTE las ráfagas del dry baja (el freeze respira).
//   · MIX     → el side total sube (el dry mono no aporta side → todo es del freeze).
//   · RATE    → la profundidad del latido (modDepth de la envolvente del wet) sube;
//     0 = pad sostenido (plano), rápido = gate marcado.
// =============================================================================

namespace
{
using namespace ovni::test;
namespace pid = horizon::params::id;

constexpr double kSR  = 48000.0;
constexpr int    kBlk = 512;
constexpr float  kPos[5] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };

// Goertzel: amplitud de un tono f en un stream (el tono es nuestro, largo y estacionario).
double goertzelAmp (const std::vector<double>& x, double f, double sr)
{
    const double w = juce::MathConstants<double>::twoPi * f / sr;
    const double c = 2.0 * std::cos (w);
    double s1 = 0.0, s2 = 0.0;
    for (const double v : x) { const double s0 = v + c * s1 - s2; s2 = s1; s1 = s0; }
    const double re = s1 - s2 * std::cos (w);
    const double im = s2 * std::sin (w);
    return 2.0 * std::sqrt (re * re + im * im) / (double) x.size();
}

// ── WHISPER: difusión de un seno CONGELADO ───────────────────────────────────
// Congela un seno BIN-ALINEADO (el motor resintetiza con avance de fase coherente:
// sólo un seno cuya frecuencia cae EXACTO en un bin se reconstruye como tono limpio
// a whisper 0). N = 2048 @48k → bin = 23.4375 Hz; bin 43 = 1007.8125 Hz. A whisper 0
// la energía vive en ese bin; al subir whisper la fase se randomiza por frame → el
// tono se difunde a los bines vecinos (la energía FUERA del fundamental sube).
// Métrica: (RMS total² − potencia del fundamental) / RMS total² ∈ [0,1].
double whisperDiffusion (float whisper01)
{
    horizon::HorizonProcessor proc;
    setParam (proc, pid::FREEZE,  1.0f);
    setParam (proc, pid::WHISPER, whisper01);
    setParam (proc, pid::SPREAD,  0.0f);    // mono: aislar la difusión espectral, no el ancho
    setParam (proc, pid::DUCK,    0.0f);
    setParam (proc, pid::RATE,    0.0f);    // sostenido: el tono congelado puro
    setParam (proc, pid::MIX,     1.0f);    // wet pleno (el freeze puro)
    proc.prepareToPlay (kSR, kBlk);

    // Bin EXACTO 43 a N=2048 @48k → reconstrucción coherente limpia a whisper 0.
    const double kTone = 43.0 * kSR / 2048.0;   // = 1007.8125 Hz
    std::vector<double> cap;
    const int capLen = (int) kSR;           // 1 s de captura
    cap.reserve ((size_t) capLen);
    long g = 0;
    // Warmup: captura + cross-fade del freeze + latencia OLA se asientan.
    for (int blk = 0; blk < 200; ++blk)
    {
        juce::AudioBuffer<float> buf (2, kBlk); juce::MidiBuffer midi;
        for (int i = 0; i < kBlk; ++i)
        {
            const float x = 0.7f * (float) std::sin (juce::MathConstants<double>::twoPi * kTone * (double) (g + i) / kSR);
            buf.setSample (0, i, x); buf.setSample (1, i, x);
        }
        g += kBlk;
        proc.processBlock (buf, midi);
    }
    while ((int) cap.size() < capLen)
    {
        juce::AudioBuffer<float> buf (2, kBlk); juce::MidiBuffer midi;
        for (int i = 0; i < kBlk; ++i)
        {
            const float x = 0.7f * (float) std::sin (juce::MathConstants<double>::twoPi * kTone * (double) (g + i) / kSR);
            buf.setSample (0, i, x); buf.setSample (1, i, x);
        }
        g += kBlk;
        proc.processBlock (buf, midi);
        const float* L = buf.getReadPointer (0);
        for (int i = 0; i < kBlk && (int) cap.size() < capLen; ++i) cap.push_back ((double) L[i]);
    }

    double tot = 0.0; for (const double v : cap) tot += v * v;
    tot /= (double) cap.size();                               // RMS² total
    const double fund = goertzelAmp (cap, kTone, kSR);        // amplitud del fundamental
    const double fundPow = 0.5 * fund * fund;                 // potencia del fundamental (amp→pow)
    return juce::jlimit (0.0, 1.0, (tot - fundPow) / (tot + 1e-15));   // fracción fuera del fundamental
}

// ── SPREAD / MIX: imagen del freeze (pink congelado) ──────────────────────────
struct ImgMeas { double width = 0.0, sideRms = 0.0; };

ImgMeas imageOf (float spread01, float mix01)
{
    horizon::HorizonProcessor proc;
    setParam (proc, pid::FREEZE,  1.0f);
    setParam (proc, pid::WHISPER, 0.0f);
    setParam (proc, pid::SPREAD,  spread01);
    setParam (proc, pid::DUCK,    0.0f);
    setParam (proc, pid::RATE,    0.0f);    // sostenido (wet continuo)
    setParam (proc, pid::MIX,     mix01);
    proc.prepareToPlay (kSR, kBlk);

    Pink pink;
    StereoImageMeter meter;
    double sTot = 0.0; long cnt = 0;
    for (int blk = 0; blk < 360; ++blk)
    {
        juce::AudioBuffer<float> buf (2, kBlk); juce::MidiBuffer midi;
        for (int n = 0; n < kBlk; ++n) { const float x = pink.next(); buf.setSample (0, n, x); buf.setSample (1, n, x); }
        proc.processBlock (buf, midi);
        if (blk < 120) continue;            // warmup
        const float* L = buf.getReadPointer (0);
        const float* R = buf.getReadPointer (1);
        meter.addBlock (L, R, kBlk);
        for (int n = 0; n < kBlk; ++n) { const double s = 0.5 * ((double) L[n] - (double) R[n]); sTot += s * s; ++cnt; }
    }
    ImgMeas out;
    out.width   = meter.finish().width;
    out.sideRms = std::sqrt (sTot / (double) juce::jmax (1L, cnt));
    return out;
}

// ── DUCK: side del wet durante las ráfagas del dry (el abanico cerrándose) ─────
// FREEZE on + SPREAD 0.7 (side audible) + RATE off. Ráfagas 24 ON / 24 OFF; mide
// el side en la ventana de ráfaga [6,22] (dry presente + duck asentado).
double duckBurstSide (float duck01)
{
    horizon::HorizonProcessor proc;
    setParam (proc, pid::FREEZE,  1.0f);
    setParam (proc, pid::WHISPER, 0.0f);
    setParam (proc, pid::SPREAD,  0.7f);
    setParam (proc, pid::DUCK,    duck01);
    setParam (proc, pid::RATE,    0.0f);
    setParam (proc, pid::MIX,     0.40f);   // MIX realista
    proc.prepareToPlay (kSR, kBlk);

    Pink pink;
    double sBurst = 0.0; long nBurst = 0;
    constexpr int kWarm = 200, kPeriod = 48, kOn = 24, kTotal = 560;
    for (int blk = 0; blk < kTotal; ++blk)
    {
        const int ph = (blk - kWarm) % kPeriod;
        const bool dryOn = (blk < kWarm) ? true : (ph >= 0 && ph < kOn);
        juce::AudioBuffer<float> buf (2, kBlk); juce::MidiBuffer midi;
        buf.clear();
        if (dryOn)
            for (int n = 0; n < kBlk; ++n) { const float x = 0.7f * pink.next(); buf.setSample (0, n, x); buf.setSample (1, n, x); }
        proc.processBlock (buf, midi);
        if (blk < kWarm) continue;
        if (ph < 6 || ph > 22) continue;
        const float* L = buf.getReadPointer (0);
        const float* R = buf.getReadPointer (1);
        for (int n = 0; n < kBlk; ++n) { const double s = 0.5 * ((double) L[n] - (double) R[n]); sBurst += s * s; ++nBurst; }
    }
    return std::sqrt (sBurst / (double) juce::jmax (1L, nBurst));
}

// ── RATE: profundidad del latido (telemetría del gate del MOTOR) ──────────────
// FREEZE on + RATE en Hz absolutos (FREE). modDepth = (max−min)/(max+min) de
// uiGateAmp (la envolvente raised-cosine REAL del latido) — NO el RMS del output,
// que arrastra la fluctuación intrínseca del frozen-pink (daría modDepth alto a RATE
// 0, falso latido). El gate del motor es la fuente honesta: 0 = pad plano (gateAmp
// clavado en 1 → modDepth 0); rápido = el gate pulsa de 0 a 1 (modDepth → 1).
double rateModDepth (float rate01)
{
    horizon::HorizonProcessor proc;
    setParam (proc, pid::FREEZE,   1.0f);
    setParam (proc, pid::RATESYNC, 0.0f);   // FREE
    setParam (proc, pid::WHISPER,  0.0f);
    setParam (proc, pid::SPREAD,   0.0f);
    setParam (proc, pid::DUCK,     0.0f);
    setParam (proc, pid::RATE,     rate01);
    setParam (proc, pid::MIX,      1.0f);
    proc.prepareToPlay (kSR, kBlk);

    Pink pink;
    std::vector<float> gate;
    gate.reserve (400);
    for (int blk = 0; blk < 400; ++blk)
    {
        juce::AudioBuffer<float> buf (2, kBlk); juce::MidiBuffer midi;
        for (int n = 0; n < kBlk; ++n) { const float x = 0.5f * pink.next(); buf.setSample (0, n, x); buf.setSample (1, n, x); }
        proc.processBlock (buf, midi);
        if (blk < 120) continue;            // warmup (captura + gate asentado)
        gate.push_back (proc.uiGateAmp.load());
    }
    float mn = gate[0], mx = gate[0];
    for (const float v : gate) { mn = std::min (mn, v); mx = std::max (mx, v); }
    return (double) (mx - mn) / (double) (mx + mn + 1e-9f);
}

// Frecuencia del gate (Hz) a un RATE FREE dado, leyendo uiGateAmp (cuenta ciclos /
// segundos). Es lo que el nombre RATE promete: la VELOCIDAD del latido. 0 = sin gate.
double rateGateHz (float rate01)
{
    horizon::HorizonProcessor proc;
    setParam (proc, pid::FREEZE,   1.0f);
    setParam (proc, pid::RATESYNC, 0.0f);
    setParam (proc, pid::WHISPER,  0.0f);
    setParam (proc, pid::SPREAD,   0.0f);
    setParam (proc, pid::DUCK,     0.0f);
    setParam (proc, pid::RATE,     rate01);
    setParam (proc, pid::MIX,      1.0f);
    proc.prepareToPlay (kSR, kBlk);

    Pink pink;
    std::vector<float> gate;
    const int warm = 120, meas = 480;       // ~5 s de medición a kBlk/kSR por bloque
    gate.reserve ((size_t) meas);
    for (int blk = 0; blk < warm + meas; ++blk)
    {
        juce::AudioBuffer<float> buf (2, kBlk); juce::MidiBuffer midi;
        for (int n = 0; n < kBlk; ++n) { const float x = 0.5f * pink.next(); buf.setSample (0, n, x); buf.setSample (1, n, x); }
        proc.processBlock (buf, midi);
        if (blk >= warm) gate.push_back (proc.uiGateAmp.load());
    }
    double mean = 0.0; for (const float v : gate) mean += v; mean /= (double) gate.size();
    int crossings = 0;
    for (size_t i = 1; i < gate.size(); ++i)
        if (gate[i - 1] >= (float) mean && gate[i] < (float) mean) ++crossings;
    const double secs = (double) meas * (double) kBlk / kSR;
    return (double) crossings / secs;
}
} // namespace

TEST_CASE ("HORIZON honestidad: barrido fino de las 5 macros en 5 posiciones (FREEZE on)", "[honestidad][horizon]")
{
    SECTION ("WHISPER: el seno congelado se difunde mas (curva perceptual, sin retroceso)")
    {
        // CURADURÍA (ES LEY): la curva perceptual del whisper es deliberadamente PLANA
        // en el tramo bajo (0–30 "casi coherente": el default 12 se OYE vidrioso, NO
        // ruidoso) y se abre en el tramo alto hacia whisperization (100 = difuso/aireado).
        // Por eso el test NO exige un escalón por cuarto en el piso (eso CONTRADIRÍA la
        // curaduría); exige: nunca RETROCEDE, el tramo alto difunde de verdad, y el viaje
        // completo es GRANDE. La difusión absoluta arranca alta por el leakage de la
        // ventana de análisis del freeze (la magnitud capturada ya está repartida en
        // bines vecinos) — lo que el knob mueve es el INCREMENTO sobre esa base.
        double d[5];
        for (int i = 0; i < 5; ++i)
        {
            d[i] = whisperDiffusion (kPos[i]);
            std::printf ("HONESTY[horizon WHISPER=%3.0f] diffusion=%.4f\n", kPos[i] * 100, d[i]);
        }
        for (int i = 1; i < 5; ++i)
            REQUIRE (d[i] >= d[i - 1] - 1.0e-3);     // monótono no-decreciente (sin retroceso/zona muerta inversa)
        REQUIRE (d[3] > d[1] + 0.02);                // el tramo ALTO (50→75) ya difunde audible
        REQUIRE (d[4] > 0.95);                        // a 100 = whisperization (casi todo difuso)
        REQUIRE (d[4] > d[0] + 0.10);                // viaje completo GRANDE (la base alta no anula el knob)
    }

    SECTION ("WHISPER default 12: vidrioso (en el regimen limpio) y NO muerto (distinto de 100)")
    {
        // GATE FINO DEL DEFAULT (cierra el hueco del review adversarial LOW#2): la curva w³ deja
        // 0/12/25 en un piso deliberadamente plano (curaduría: "0–30 casi coherente"). Eso es
        // CORRECTO por diseño, pero antes NINGÚN test fijaba que el DEFAULT 12 (a) siga en el
        // régimen LIMPIO (vidrioso, no ruidoso) y (b) NO sea un valor muerto. Acá se fija el
        // contrato de honestidad del valor 12 específico sin contradecir la curaduría:
        //   · 12 está en el régimen limpio: su difusión ≈ la de 0 (coherente), MUY lejos del techo.
        //   · 12 NO está muerto: el knob a 100 difunde MUCHO más → el control vive (no es cosmético).
        const double d0   = whisperDiffusion (0.00f);   // coherente puro (vidrioso de referencia)
        const double d12  = whisperDiffusion (0.12f);   // DEFAULT de la curaduría
        const double d100 = whisperDiffusion (1.00f);   // whisperization plena
        std::printf ("HONESTY[horizon WHISPER default] d0=%.4f d12=%.4f d100=%.4f\n", d0, d12, d100);

        // (a) RÉGIMEN LIMPIO: el default 12 se OYE vidrioso → su difusión casi no se despega de 0
        //     (≤ +1% por la curva cúbica) y queda muy por debajo del techo whisperizado.
        REQUIRE (d12 <= d0 + 0.01);
        REQUIRE (d12 <  d100 - 0.20);
        // (b) NO MUERTO: el viaje 12→100 del knob es GRANDE (whisperization real, no adorno).
        REQUIRE (d100 > d12 + 0.20);
    }

    SECTION ("SPREAD: WIDTH monotono, sin zona muerta")
    {
        double w[5];
        for (int i = 0; i < 5; ++i)
        {
            w[i] = imageOf (kPos[i], 1.0f).width;
            std::printf ("HONESTY[horizon SPREAD=%3.0f] WIDTH=%.4f\n", kPos[i] * 100, w[i]);
        }
        REQUIRE (w[0] < 0.10);                       // 0 = mono real
        for (int i = 1; i < 5; ++i)
            REQUIRE (w[i] > w[i - 1] * 1.10);        // cada cuarto abre ≥ 10 % más
        REQUIRE (w[4] > w[0] + 0.30);                // viaje completo GRANDE
    }

    SECTION ("DUCK: el side de las rafagas baja en cada paso (el freeze respira mas)")
    {
        double sb[5];
        for (int i = 0; i < 5; ++i)
        {
            sb[i] = duckBurstSide (kPos[i]);
            std::printf ("HONESTY[horizon DUCK=%3.0f] sideBurst=%.5f\n", kPos[i] * 100, sb[i]);
        }
        for (int i = 1; i < 5; ++i)
            REQUIRE (sb[i] < sb[i - 1] * 0.97);      // cada paso agacha más (sin meseta)
        const double dropDb = 20.0 * std::log10 (sb[4] / (sb[0] + 1e-12));
        INFO ("drop total = " << dropDb << " dB");
        REQUIRE (dropDb < -6.0);                     // viaje completo ≥ 6 dB (gate del sello)
    }

    SECTION ("MIX: la presencia del freeze sube monotona (el dry mono no aporta side)")
    {
        double s[5];
        for (int i = 0; i < 5; ++i)
        {
            s[i] = imageOf (0.7f, kPos[i]).sideRms;   // spread 0.7 → el freeze tiene side
            std::printf ("HONESTY[horizon MIX=%3.0f] sideRms=%.5f\n", kPos[i] * 100, s[i]);
        }
        REQUIRE (s[0] < 1e-4);                        // MIX 0 = dry puro (mono → side ~0)
        REQUIRE (s[1] > 1e-3);                        // a 25 el freeze YA está presente
        // Ley de potencia: la derivada se aplana hacia 100. Gate ×1.05 arriba, ×1.12 en el resto.
        for (int i = 1; i < 4; ++i)
            REQUIRE (s[i] > s[i - 1] * 1.12);
        REQUIRE (s[4] > s[3] * 1.05);
        REQUIRE (s[4] > 2.0 * s[1]);                  // el viaje 25→100 es GRANDE
    }

    SECTION ("RATE: 0=pad plano; >0 el gate late y la VELOCIDAD del latido sube monotona")
    {
        // El nombre RATE promete VELOCIDAD del re-trigger, NO profundidad. Por eso se mide:
        //  (a) modDepth — prueba que HAY latido (0 = pad plano sin gate; >0 = el gate pulsa).
        //  (b) frecuencia del gate (Hz) — prueba que el knob CONTROLA la velocidad (sube
        //      monótona). modDepth NO es monótono por física (a más rápido, el raised-cosine
        //      + el de-zipper del gate cierran menos por ciclo) → medirlo como "más profundo"
        //      sería deshonesto; lo honesto es que late MÁS RÁPIDO.
        // 5 posiciones del RATE FREE (Range 0..8 Hz): 0 / 2 / 4 / 6 / 8 Hz.
        double md[5], hz[5];
        for (int i = 0; i < 5; ++i)
        {
            md[i] = rateModDepth (kPos[i]);
            hz[i] = rateGateHz   (kPos[i]);
            std::printf ("HONESTY[horizon RATE=%4.1fHz] modDepth=%.4f gateHz=%.2f\n", kPos[i] * 8.0f, md[i], hz[i]);
        }
        REQUIRE (md[0] < 0.05);                       // 0 = pad sostenido (sin latido)
        REQUIRE (hz[0] < 0.3);                        // 0 = sin gate (cero ciclos)
        for (int i = 1; i < 5; ++i)
        {
            REQUIRE (md[i] > 0.3);                    // a TODO RATE>0 el gate late de verdad
            REQUIRE (hz[i] > hz[i - 1] + 1.0);        // cada paso = latido MÁS RÁPIDO (≥ +1 Hz)
        }
        REQUIRE (std::abs (hz[2] - 4.0) < 0.5);       // y la velocidad es la pedida (RATE 4 Hz ≈ 4 Hz)
    }
}
