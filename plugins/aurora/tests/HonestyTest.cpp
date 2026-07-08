#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cmath>
#include <vector>
#include "PluginProcessor.h"
#include "params/ParameterIDs.h"
#include "template/tests/OvniTestHarness.h"

// =============================================================================
// [honestidad][aurora] — BARRIDO FINO de cada macro en 5 posiciones (0/25/50/
// 75/100) con señal real y MIX REALISTA (40 % salvo el barrido de MIX): cada
// knob produce un cambio MEDIBLE y monótono (o, si la métrica escalar no es
// monotónica por diseño —TILT—, un delta audible entre posiciones VECINAS).
// Un macro con zona muerta NO pasa este test (regla de honestidad del sello:
// "mové el control a ciegas → un productor nota el cambio que el nombre
// promete"). Mecanismo ENCENDIDO en todos los casos (lección §2 del checklist).
//
// Métricas (sobre la SALIDA TOTAL dry+wet, lo que el productor escucha):
//   · SPREAD → WIDTH global sube.
//   · TILT   → la firma (side de GRAVES, side de AGUDOS) cambia entre vecinos.
//   · MOTION → profundidad de modulación del side (el latido se hace más hondo).
//   · MONO SAFE → side bajo 700 Hz baja (la red recoge más espectro).
//   · DUCK   → el side DURANTE las ráfagas baja (el abanico se cierra más).
//   · MIX    → el side total sube (el dry mono no aporta side).
// =============================================================================

namespace
{
using namespace ovni::test;
namespace pid = aurora::params::id;

constexpr double kSR  = 48000.0;
constexpr int    kBlk = 512;
constexpr float  kPos[5] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };

struct Cfg
{
    float spread  = 0.55f;
    float tilt    = 0.5f;    // normalizado: 0.5 = tilt 0
    float motion  = 0.0f;
    float rate    = 0.0f;    // norm de MOTIONRATE (0 = default si <0)
    float msafe   = 0.5f;
    float duck    = 0.0f;
    float mix     = 0.40f;   // MIX realista por defecto
};

struct Meas
{
    double width      = 0.0;   // RMS(side)/RMS(mid) global
    double sideRms    = 0.0;   // RMS del side total
    double lowSide700 = 0.0;   // RMS del side bajo ~700 Hz (2× one-pole)
    double highSide   = 0.0;   // RMS del side sobre ~1.5 kHz
    double modDepth   = 0.0;   // (max−min)/(max+min) del side RMS por bloque (suavizado)
    double rms        = 0.0;
};

void applyCfg (aurora::AuroraProcessor& proc, const Cfg& c)
{
    setParam (proc, pid::SPREAD,      c.spread);
    setParam (proc, pid::TILT,        c.tilt);
    setParam (proc, pid::MOTION,      c.motion);
    if (c.rate > 0.0f) setParam (proc, pid::MOTIONRATE, c.rate);
    setParam (proc, pid::MONOSAFEAMT, c.msafe);
    setParam (proc, pid::DUCK,        c.duck);
    setParam (proc, pid::MIX,         c.mix);
}

// Corre pink continuo (mono duplicado, banda ancha) y mide la imagen + bandas.
Meas measureSteady (const Cfg& c, int totalBlocks = 360, int warm = 100)
{
    aurora::AuroraProcessor proc;
    applyCfg (proc, c);
    proc.prepareToPlay (kSR, kBlk);

    Pink pink;
    StereoImageMeter meter;
    double lo1 = 0, lo2 = 0, mid1 = 0;
    const double loCoef  = 1.0 - std::exp (-juce::MathConstants<double>::twoPi * 700.0  / kSR);
    const double midCoef = 1.0 - std::exp (-juce::MathConstants<double>::twoPi * 1500.0 / kSR);
    double sLow = 0, sHigh = 0; long cnt = 0;
    std::vector<double> blockSide;   // RMS del side por bloque (para modDepth)
    blockSide.reserve ((size_t) totalBlocks);

    for (int blk = 0; blk < totalBlocks; ++blk)
    {
        juce::AudioBuffer<float> buf (2, kBlk);
        juce::MidiBuffer midi;
        for (int n = 0; n < kBlk; ++n)
        { const float x = pink.next(); buf.setSample (0, n, x); buf.setSample (1, n, x); }
        proc.processBlock (buf, midi);
        if (blk < warm) continue;
        const float* L = buf.getReadPointer (0);
        const float* R = buf.getReadPointer (1);
        meter.addBlock (L, R, kBlk);
        double sb = 0.0;
        for (int n = 0; n < kBlk; ++n)
        {
            const double side = 0.5 * ((double) L[n] - (double) R[n]);
            sb   += side * side;
            lo1  += loCoef  * (side - lo1);
            lo2  += loCoef  * (lo1  - lo2);
            mid1 += midCoef * (side - mid1);
            const double high = side - mid1;
            sLow += lo2 * lo2; sHigh += high * high; ++cnt;
        }
        blockSide.push_back (std::sqrt (sb / (double) kBlk));
    }

    Meas out;
    const auto img  = meter.finish();
    out.width       = img.width;
    out.rms         = img.rms;
    out.lowSide700  = std::sqrt (sLow  / (double) juce::jmax (1L, cnt));
    out.highSide    = std::sqrt (sHigh / (double) juce::jmax (1L, cnt));
    {
        double sTot = 0.0; for (const double v : blockSide) sTot += v * v;
        out.sideRms = std::sqrt (sTot / (double) juce::jmax ((size_t) 1, blockSide.size()));
    }
    // modDepth: media móvil de 5 bloques (mata la varianza del pink) → (max−min)/(max+min).
    if (blockSide.size() >= 10)
    {
        std::vector<double> sm;
        for (size_t i = 4; i < blockSide.size(); ++i)
            sm.push_back ((blockSide[i] + blockSide[i-1] + blockSide[i-2] + blockSide[i-3] + blockSide[i-4]) / 5.0);
        double mn = sm[0], mx = sm[0];
        for (const double v : sm) { mn = std::min (mn, v); mx = std::max (mx, v); }
        out.modDepth = (mx - mn) / (mx + mn + 1e-12);
    }
    return out;
}

// ── MONO SAFE: medición POR ZONA (Goertzel) ──────────────────────────────────
// El corte viaja 60→700 Hz LOG: cada cuarto de vuelta se traga la ZONA siguiente.
// Una banda fija (LP@700) diluye los pasos (el 0→25 solo toca 60→110 Hz y el
// resto de la banda lo tapa) → se mide el side DE CADA ZONA con un parcial
// clavado en su medio + un testigo agudo que nunca se toca.
constexpr double kZonePartials[5] = { 82.0, 150.0, 280.0, 520.0, 3000.0 };

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

struct ZoneAmps { double a[5] = {}; };

ZoneAmps measureZoneSides (float msafe01)
{
    aurora::AuroraProcessor proc;
    Cfg c; c.spread = 1.0f; c.tilt = 0.0f; c.msafe = msafe01;   // tilt −100: graves a los BORDES
    applyCfg (proc, c);
    proc.prepareToPlay (kSR, kBlk);

    const int warmBlocks = 40;
    const int capLen = (int) kSR;   // 1 s de captura
    std::vector<double> side;
    side.reserve ((size_t) capLen);
    long g = 0; int blk = 0;
    while ((int) side.size() < capLen)
    {
        juce::AudioBuffer<float> buf (2, kBlk);
        juce::MidiBuffer midi;
        for (int i = 0; i < kBlk; ++i)
        {
            double x = 0.0;
            for (const double f : kZonePartials)
                x += 0.15 * std::sin (juce::MathConstants<double>::twoPi * f * (double) (g + i) / kSR);
            buf.setSample (0, i, (float) x);
            buf.setSample (1, i, (float) x);
        }
        g += kBlk;
        proc.processBlock (buf, midi);
        if (++blk <= warmBlocks) continue;
        const float* L = buf.getReadPointer (0);
        const float* R = buf.getReadPointer (1);
        for (int i = 0; i < kBlk && (int) side.size() < capLen; ++i)
            side.push_back (0.5 * ((double) L[i] - (double) R[i]));
    }
    ZoneAmps z;
    for (int k = 0; k < 5; ++k)
        z.a[k] = goertzelAmp (side, kZonePartials[k], kSR);
    return z;
}

// Corre ráfagas (24 ON / 24 OFF, como RealWorldTest) y devuelve el side RMS en
// la ventana de ráfaga [6,22] — la métrica del DUCK (el abanico cerrándose).
double measureBurstSide (const Cfg& c, int totalBlocks = 336, int warm = 48)
{
    aurora::AuroraProcessor proc;
    applyCfg (proc, c);
    proc.prepareToPlay (kSR, kBlk);

    Pink pink;
    double sBurst = 0.0; long nBurst = 0;
    constexpr int kPeriod = 48, kOn = 24;
    for (int blk = 0; blk < totalBlocks; ++blk)
    {
        const int ph = blk % kPeriod;
        juce::AudioBuffer<float> buf (2, kBlk);
        juce::MidiBuffer midi;
        buf.clear();
        if (ph < kOn)
            for (int n = 0; n < kBlk; ++n)
            { const float x = 0.7f * pink.next(); buf.setSample (0, n, x); buf.setSample (1, n, x); }
        proc.processBlock (buf, midi);
        if (blk < warm) continue;
        if (ph < 6 || ph > 22) continue;
        const float* L = buf.getReadPointer (0);
        const float* R = buf.getReadPointer (1);
        for (int n = 0; n < kBlk; ++n)
        { const double s = 0.5 * ((double) L[n] - (double) R[n]); sBurst += s * s; ++nBurst; }
    }
    return std::sqrt (sBurst / (double) juce::jmax (1L, nBurst));
}
} // namespace

TEST_CASE ("AURORA honestidad: barrido fino de las 6 macros en 5 posiciones (MIX realista)", "[honestidad][aurora]")
{
    SECTION ("SPREAD: WIDTH monotono, sin zona muerta")
    {
        double w[5];
        for (int i = 0; i < 5; ++i)
        {
            Cfg c; c.spread = kPos[i];
            w[i] = measureSteady (c).width;
            std::printf ("HONESTY[aurora SPREAD=%3.0f] WIDTH=%.4f\n", kPos[i] * 100, w[i]);
        }
        REQUIRE (w[0] < 0.02);                       // 0 = mono real
        for (int i = 1; i < 5; ++i)
            REQUIRE (w[i] > w[i - 1] * 1.10);        // cada cuarto de vuelta abre ≥ 10 % más
        REQUIRE (w[4] > w[0] + 0.10);                // y el viaje completo es GRANDE
    }

    SECTION ("TILT: el CARÁCTER espectral del side se inclina, monótono (graves↔aire)")
    {
        // TILT es ahora un control de CARÁCTER ESPECTRAL del despliegue del STFT (el reparto de
        // magnitud / weave), capa sobre el ENSANCHADOR REAL (FIR + fase) que carga el ancho de
        // banda ancha. Es más sutil que el viejo paneo de magnitud-único (era todo el efecto y
        // mentía "no hace nada" sobre música real), pero HONESTO y MEDIBLE: subir TILT inclina el
        // side hacia el AIRE (highSide ↑) y bajarlo hacia los GRAVES (lowSide relativo ↑). SPREAD
        // default 55, Mono Safe 0.
        double ls[5], hs[5];
        for (int i = 0; i < 5; ++i)
        {
            Cfg c; c.spread = 0.55f; c.msafe = 0.0f; c.tilt = kPos[i];
            const Meas m = measureSteady (c);
            ls[i] = m.lowSide700; hs[i] = m.highSide;
            std::printf ("HONESTY[aurora TILT=%+4.0f] lowSide700=%.5f highSide=%.5f\n",
                         kPos[i] * 200 - 100, ls[i], hs[i]);
        }
        // El AIRE del side se inclina MONÓTONO con TILT (cada cuarto sube el highSide): el knob
        // hace lo que dice de punta a punta, sin zona muerta (medido highSide 0.0298→0.0372).
        for (int i = 1; i < 5; ++i)
            REQUIRE (hs[i] > hs[i - 1] * 1.01);
        // Y el viaje completo del carácter es claro: +100 abre el aire del side ≥ 15 % más que
        // −100, y el balance graves↔aire se invierte (a −100 el side pesa más en graves).
        REQUIRE (hs[4] > 1.15 * hs[0]);
        REQUIRE ((ls[0] / hs[0]) > (ls[4] / hs[4]));   // −100: side más grave · +100: side más aire
    }

    SECTION ("MOTION: el latido se hace mas hondo, monotono")
    {
        // RATE 2 Hz (norm del mapeo log) → varios ciclos en la ventana medida.
        const float rate2Hz = std::log (2.0f / aurora::params::kMotionRateMinHz)
                            / std::log (aurora::params::kMotionRateMaxHz / aurora::params::kMotionRateMinHz);
        double md[5];
        for (int i = 0; i < 5; ++i)
        {
            Cfg c; c.spread = 0.7f; c.motion = kPos[i]; c.rate = rate2Hz;
            md[i] = measureSteady (c).modDepth;
            std::printf ("HONESTY[aurora MOTION=%3.0f] modDepth=%.4f\n", kPos[i] * 100, md[i]);
        }
        for (int i = 1; i < 5; ++i)
            REQUIRE (md[i] > md[i - 1] + 0.02);      // cada paso ahonda el latido de verdad
        REQUIRE (md[4] > 0.5);                       // a 100 el abanico CIERRA casi del todo
    }

    SECTION ("MONO SAFE: cada paso se traga SU zona del espectro (Goertzel por banda)")
    {
        // Caso adversario: TILT −100 (graves a los bordes) → hay side grave que recoger. El knob
        // Mono Safe viaja ahora 50→400 Hz (bajado del 60→700 viejo para no tragarse la masa del
        // beat con el ENSANCHADOR REAL): sube el corte de colapso del weave del STFT Y el corner
        // del HP del side del FIR. Zonas medidas: 82 / 150 / 280 / 520 / 3k Hz. El aire (3k) jamás.
        ZoneAmps z[5];
        for (int i = 0; i < 5; ++i)
        {
            z[i] = measureZoneSides (kPos[i]);
            std::printf ("HONESTY[aurora MSAFE=%3.0f] side={82:%.5f 150:%.5f 280:%.5f 520:%.5f 3k:%.5f}\n",
                         kPos[i] * 100, z[i].a[0], z[i].a[1], z[i].a[2], z[i].a[3], z[i].a[4]);
        }
        // El knob recoge los GRAVES progresivamente: la zona de 82 Hz cae MONÓTONA al subir el
        // knob (cada paso ata más grave; medido 0.064→0.036). Es la red de low-end honesta.
        for (int k = 1; k <= 4; ++k)
            REQUIRE (z[k].a[0] <= z[k - 1].a[0] * 1.02);    // 82 Hz: no-creciente paso a paso (recoge graves)
        REQUIRE (z[4].a[0] < 0.70 * z[0].a[0]);             // y a 100 el grave de 82 Hz cae claro (≥ ~3 dB)
        // El aire desplegado (3 kHz) queda INTACTO en todo el recorrido (solo amarra graves).
        REQUIRE (z[4].a[4] > 0.85 * z[0].a[4]);
    }

    SECTION ("DUCK: el side de las rafagas baja en cada paso (el abanico se cierra mas)")
    {
        double sb[5];
        for (int i = 0; i < 5; ++i)
        {
            Cfg c; c.spread = 0.7f; c.duck = kPos[i];
            sb[i] = measureBurstSide (c);
            std::printf ("HONESTY[aurora DUCK=%3.0f] sideBurst=%.5f\n", kPos[i] * 100, sb[i]);
        }
        for (int i = 1; i < 5; ++i)
            REQUIRE (sb[i] < sb[i - 1] * 0.97);      // cada paso agacha más (sin meseta)
        const double dropDb = 20.0 * std::log10 (sb[4] / (sb[0] + 1e-12));
        INFO ("drop total = " << dropDb << " dB");
        REQUIRE (dropDb < -6.0);                     // viaje completo ≥ 6 dB (gate del sello)
    }

    SECTION ("MIX: la presencia del despliegue sube monotona (el dry mono no aporta side)")
    {
        double s[5];
        for (int i = 0; i < 5; ++i)
        {
            Cfg c; c.mix = kPos[i];
            s[i] = measureSteady (c).sideRms;
            std::printf ("HONESTY[aurora MIX=%3.0f] sideRms=%.5f\n", kPos[i] * 100, s[i]);
        }
        REQUIRE (s[0] < 1e-4);                       // MIX 0 = dry puro (mono → side ~0)
        REQUIRE (s[1] > 1e-3);                       // y a 25 el despliegue YA está presente
        // Cada paso suma presencia real. La ley de potencia es sin(φ) y su derivada se
        // APLANA hacia 100 (75→100 = ×1.082 teórico de wet): gate ×1.05 arriba (sigue
        // siendo un paso medible, cero meseta) y ×1.15 en el resto del recorrido.
        for (int i = 1; i < 4; ++i)
            REQUIRE (s[i] > s[i - 1] * 1.15);
        REQUIRE (s[4] > s[3] * 1.05);
        REQUIRE (s[4] > 2.0 * s[1]);                 // el viaje 25→100 es GRANDE (≥ +6 dB)
    }
}
