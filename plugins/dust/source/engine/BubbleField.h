#pragma once
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <vector>
#include "MultiTapEcho.h"   // TapRender + kMaxTaps

namespace dust::engine
{

// =================================================================================================
// BubbleField — espacializador de COSTO FIJO para los ecos de DUST.
//
// 16 buses de dirección FIJA tomados del anillo HRIR (SADIE II KU100, 72 dirs × 256 taps,
// shared/engines/binaural/HrirRing.h). Cada bus = par FIR estéreo (una dirección real del anillo)
// + ITD por bus (delay FIJO del oído lejano — prescripción de curaduría: "min-phase corto + ITD
// por bus"; el delay field del anillo vino en cero, así que el ITD se sintetiza con el modelo
// esférico de Woodworth y queda CONSTANTE por bus: nunca se modula). Cada tap/burbuja se reparte
// entre los 2 buses ADYACENTES a su azimut por ley de potencia (constant-power pan entre buses)
// -> la convolución corre UNA vez por bus, no por eco (el costo NO escala con las burbujas), y la
// deriva de VIDA / el drag del ORIGIN son RAMPAS POR-SAMPLE de esas ganancias de bus: NUNCA se
// conmuta un filtro ni un delay (anti-click estructural).
//
// monoSafe (IN PHASE): el wet cae a paneo de potencia constante POR TAP (bypass del banco HRIR,
// sin ITD -> mono-compatible de verdad), con cross-fade por LEY DE POTENCIA entre ambos caminos
// (sin clicks ni dip de nivel al togglear).
//
// CPU: buses sin energía se saltean cuando su entrada fue exactamente cero por más del largo del
// FIR + su ITD (cola ya purgada) -> bit-transparente, no cambia el audio (no es un gate audible).
// =================================================================================================
class BubbleField
{
public:
    static constexpr int kNumBuses = 16;

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    // IN PHASE: true = paneo de potencia constante (bypass HRIR). Cross-fade interno (~30 ms).
    void setMonoSafe (bool on) noexcept { hrirWeightTgt = on ? 0.0f : 1.0f; }

    // Reparte los taps en los buses (ganancias por-sample), convoluciona el banco y ESCRIBE
    // (reemplaza) el wet estéreo en outL/outR.
    void process (const std::array<TapRender, (size_t) kMaxTaps>& taps, int numTaps,
                  int numSamples, float* outL, float* outR);

    // ── test-only ────────────────────────────────────────────────────────────────────────────────
    float dbgBusEnergy (int bus) const noexcept { return (float) busEnergy[(size_t) bus]; }   // acumulada
    void  dbgResetBusEnergy() noexcept { busEnergy.fill (0.0); }
    float dbgBusAzimuthRad (int bus) const noexcept { return busAzRad[(size_t) bus]; }
    int   dbgBusItdSamples (int bus) const noexcept   // ITD del oído lejano (samples; 0 = frente/atrás)
    {
        return buses[(size_t) bus].itdL + buses[(size_t) bus].itdR;
    }

private:
    using Fir   = juce::dsp::FIR::Filter<float>;
    using Coefs = juce::dsp::FIR::Coefficients<float>;

    // Ganancias objetivo (2 buses adyacentes, ley de potencia) para un azimut dado.
    void targetGainsForAzimuth (float azRad, float* gains16) const noexcept;

    struct Bus
    {
        Fir   firL, firR;
        int   ringDir  = 0;       // índice 0..71 en el anillo
        int   zeroRun  = 0;       // samples consecutivos con entrada exactamente cero (skip de cola)

        // ITD por bus (Woodworth, sintetizado en prepare): delay FIJO en samples del oído LEJANO
        // (uno de los dos es siempre 0). Constante por bus -> jamás se modula (anti-click).
        int   itdL = 0, itdR = 0;
        std::vector<float> dly;   // buffer circular pow2 del oído lejano (vacío si ITD = 0)
        int   dlyMask = 0;
        long  dlyW    = 0;

        // ── Decorrelador de bus (DESBOXY): un all-pass Schroeder de delay FIJO y DISTINTO por bus,
        // aplicado IDÉNTICO a L y R del bus (no toca ITD/ILD = dirección; magnitud unitaria = no
        // colorea). Rompe el COMB inter-bus: power-pan reparte cada tap en 2 buses adyacentes que
        // llevan la MISMA señal por HRIR distintas -> al sumar combaban en agudos (el "encajonado").
        // Al dispersar la fase HF distinto por bus, las copias dejan de cancelarse coherentemente.
        // Coeficientes FIJOS -> sin clicks. Estado de delay propio (apL/apR comparten el largo dApN).
        std::array<float, 64> apL {}, apR {};   // buffers del all-pass (delay <= 63)
        int   apN = 0;                           // largo del delay del all-pass (0 = sin decorrelar)
        long  apWL = 0, apWR = 0;
    };

    std::array<Bus, (size_t) kNumBuses>   buses;
    std::array<float, (size_t) kNumBuses> busAzRad {};      // azimut real de cada bus (rad, + = izquierda)
    std::vector<Coefs::Ptr>               coefL, coefR;     // [kNumBuses] preasignados (sin new en audio)

    // Ganancias de bus POR SLOT (persisten entre bloques -> la rampa por-sample arranca del valor real).
    std::array<std::array<float, (size_t) kNumBuses>, (size_t) kMaxTaps> slotGain {};
    // Paneo de potencia constante por slot (camino monoSafe), también rampeado.
    std::array<float, (size_t) kMaxTaps> slotPanL {}, slotPanR {};

    std::vector<float> busBuf;        // kNumBuses bloques mono contiguos
    std::vector<float> scratch;       // bloque mono temporal (copia para firL)
    std::vector<float> panL, panR;    // camino de paneo (monoSafe)

    float  hrirWeightSm = 1.0f, hrirWeightTgt = 1.0f;   // 1 = HRIR pleno · 0 = paneo (IN PHASE)
    float  hrirWeightCoefBlock = 0.0f;                  // suavizado por bloque (~30 ms)
    int    firTaps    = 0;
    int    maxBlock   = 0;

    // ── Air shelf (DESBOXY): high-shelf de 1er orden FIJO aplicado IDÉNTICO a L y R del wet HRIR
    // (post-banco, pre-crossfade IN PHASE). Compensa el déficit de agudos COMÚN del banco KU100
    // (la coloración de pabellón por dirección + el piso del comb residual): devuelve presencia/aire
    // SIN tocar la imagen estéreo (mismo filtro en ambos canales -> CORR/WIDTH/colocación intactos).
    // Honesto: restaura el aire que la coloración fija del banco se llevaba (no es maquillaje, es
    // de-coloración declarada). Coeficientes fijos -> sin clicks. Sólo corre en el camino HRIR.
    float  airB0 = 1.0f, airB1 = 0.0f, airA1 = 0.0f;    // biquad high-shelf (1er orden)
    float  airZL = 0.0f, airZR = 0.0f;                  // estado (transposed direct form II)

    std::array<double, (size_t) kNumBuses> busEnergy {};   // test-only: energía acumulada por bus
};

} // namespace dust::engine
