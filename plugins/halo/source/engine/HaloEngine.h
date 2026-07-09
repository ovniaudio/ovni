#pragma once
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <memory>
#include <vector>
#include "engines/fdn/FdnReverb.h"
#include "engines/pitch/PitchShifter.h"
#include "engines/movement/Trajectory.h"
#include "dsp/StereoLimiter.h"
#include "dsp/Smoothing.h"
#include "dsp/LowCut.h"
#include "dsp/HiCut.h"
#include "BloomAllpass.h"

namespace halo {

// =====================================================================================
// HaloEngine — SHIMMER ESPACIAL GLACIAL (rediseño "VASTEDAD", base técnica §2/§9).
//
// TOPOLOGÍA (shimmer canónico Eno/Lanois con órbita binaural; el pitch DENTRO del lazo, el espacio FUERA):
//
//   dry ─┬───────────────────────────────────────────────────────── dry (al frente, azimut 0) ──────────┐
//        │                                                                                                │
//        │  ┌───────────┐  ┌────────┐                                                                     │
//        └─►│ PRE-DELAY  │─►│ BLOOM  │─►(+)─► FDN 8×8 ─► wet ─► [OS4x → PITCH(8va+5ta) → OS↓] ─► LP ─► HP ─┐│
//           │ 60–120 ms  │  │ 4 AP   │  ▲                                                          (DAMP) ││
//           └───────────┘  │ c=0.55 │  │                                                                  ││
//                          └────────┘  └──── DECAY·shimmerReturn ◄── (banda EQ del lazo) ◄────────────────┘│
//                                            (cap kRegenMax < 1)                                            │
//                                                                                                          │
//                                       wet EQ ESTÉREO decorrelado (post-lazo, lo que va al MIX) ─┐         │
//                                                                                                 ▼         │
//                              ┌──── PAN BINAURAL del wash (FUERA del lazo; NO suma a mono) ───────┐        │
//                              │  Trajectory(Ellipse) → azimut θ(t)   [orbitRate de RATE/SYNC]      │        │
//                              │  ITD = delay interaural ≤0.7 ms (Lagrange3rd) en el oído lejano    │        │
//                              │  ILD = sombra de cabeza: atenúa el oído lejano, con MÁS atenuación │        │
//                              │        en agudos (high-shelf ~1.6 kHz) que en graves (realista)    │        │
//                              │  profundidad del pan ∝ ORBIT (0 = wash quieto; 1 = órbita plena)   │        │
//                              └─────────────────┬───────────────────────────────────────────────┘        │
//                                                ▼ (wet ANCHO orbitando alrededor de la cabeza)             ▼
//                                              (×MIX) ──────────────────────────────────────────────────► (+) ─► OUT → StereoLimiter
//
// CLAVE espacial (FIX envolvimiento): el wet del FDN es un wash ESTÉREO decorrelado (L/R distintos). En vez
// de sumarlo a mono y reconstruir UN punto (que lo angostaba: "más ORBIT = más angosto", bug cazado con
// Insight), paneamos el wash ANCHO COMPLETO alrededor de la cabeza con cues binaurales reales: ITD (delay
// interaural en el oído lejano) + ILD (sombra de cabeza dependiente de frecuencia: atenúa más en agudos que
// en graves, como una cabeza real). Atenuar/retrasar UN canal de un wash decorrelado NO lo angosta (los
// términos cruzados ≈0) → conserva su ancho/IACC bajo y ADEMÁS orbita. NO carga HRIR ni convolución: la
// "binauralidad" son los dos cues primarios (ITD+ILD) modulados por el azimut, no un HRTF horneado.
//
// CLAVE de honestidad (base técnica §1): las voces del shimmer son FIJAS = octava (+12.00) + quinta
// (+7.05, micro-detune +5c). NO hay "scale-aware": la matemática del feedback la desmiente (sólo la octava
// es invariante bajo recursión). El ScaleQuantizer queda en el código (costo 0) por si se repara como modo
// ARMONÍA sobre el dry en el futuro — pero NO se expone.
//
// CLAVE anti-divergencia (base técnica §6): pitch-up concentra energía en agudos → un LP 1-polo float64 en
// el lazo la drena cada vuelta (= TONE). DC blocker (HP) + cap del feedback (kRegenMax). El pan binaural va
// FUERA del lazo (sobre el wet EQ ANTES de panear) → el ITD/ILD no se acumula vuelta a vuelta.
//
// CLAVE de alias (house-standard): el PitchShifter (interpolación) corre con OS 4× LOCAL (FIR equiripple).
// Su latencia queda DENTRO del lazo de feedback (carácter, no se reporta PDC del dry — FX de cola).
//
// FREEZE (base técnica §6): modo aparte → feedback=1.0, input→0 (xfade), damping=0, modDepth=0. "Capturá
// la nube y tocá encima". El dry sigue pasando.
//
// RT-safe: process() no asigna (todo en prepare()). float64 en el lazo de feedback (regla house §1).
// =====================================================================================

struct HaloParams
{
    float shimmer01 = 0.55f;   // cantidad de capa pitched reinyectada al lazo (0 = reverb a secas)
    float decay01   = 0.75f;   // feedback del lazo (piso log; "cuánto dura"; cap kRegenMax)
    float size01    = 0.65f;   // tamaño del difusor FDN + pre-delay (ataque/tamaño percibido)
    float tone01    = 0.45f;   // LP del lazo: más TONE = más oscuro (LP más bajo) = más estable
    float orbit01   = 0.40f;   // profundidad del pan binaural ITD/ILD: cuánto "te rodea" el halo (0 = wash quieto)
    float lowCut01  = 0.0f;    // high-pass del WET que entra a la cola (0 = 20 Hz = off → 1 = 500 Hz). DRY intacto.
    float hiCut01   = 0.0f;    // low-pass del WET de la cola (0 = 20 kHz = off → 1 = 1.5 kHz). DRY intacto.
    float mix01     = 0.40f;   // dry/wet (ley de potencia). Dry siempre seco/frente.
    bool  freeze    = false;   // FREEZE: feedback=1.0, input→0, damping=0, modDepth=0
    bool  monoSafe  = false;   // IN PHASE: colapsa el wet a mono-compatible antes del mix (lo inyecta el chasis)

    // RATE de la órbita (control CORE del sello), en ciclos/seg. >0 = el motor orbita a ESA frecuencia (sea la
    // que eligió la perilla RATE en FREE, sea BPM·división en SYNC — el processor resuelve cuál). ≤0 = fallback
    // libre (freeHz orgánico 0.08 Hz) para tests que construyen HaloParams directo sin setearlo.
    float orbitRateHz = -1.0f;

    // BREATH del difusor FDN: ahora INTERNO fijo (no es perilla; base técnica §7). El processor lo pasa fijo.
    float breath01 = 0.30f;
};

class HaloEngine
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset() noexcept;

    // Procesa in-place estéreo: dry + cola de shimmer glacial orbitando. transport conduce la trayectoria
    // espacial (y el rate orbital si va en SYNC). RT-safe, sin asignaciones.
    void process (juce::AudioBuffer<float>& buffer, const HaloParams& p,
                  const ovni::engines::TransportInfo& transport) noexcept;

    // Ganancia del limiter de salida (1 = sin reducción) → LED de clip del chasis.
    float lastLimiterGain() const noexcept { return outLimiter.lastGain(); }

    // Latencia del granular (medio grano) DENTRO del lazo. En un FX de cola es carácter (no se reporta PDC
    // del dry). El OS añade latencia propia que también queda dentro del lazo. Ver house-standard §1.
    int latencySamples() const noexcept { return 0; }   // el dry es nulo (MIX=0) → 0 sample de PDC reportada

    // ── Telemetría (no RT-crítica de leer). Energía RMS del lazo (post-EQ) del último bloque → el test de
    //    estabilidad la usa como proxy de "energía acotada", y el visualizer como brillo de los anillos.
    float loopEnergy() const noexcept { return lastLoopRms; }

    // ── Diagnóstico [alias]: forzar el OS del pitch on/off para medir el piso de alias con/sin OS. En
    //    producción SIEMPRE on. NO toca el camino normal del usuario (sólo el test lo llama).
    void setPitchOversampling (bool on) noexcept { osEnabled = on; }

    // ── Mapeos perceptuales públicos (única fuente de verdad con los tests). Puros y estáticos.
    // decay01 -> coeficiente de feedback del lazo (piso log, techo kRegenMax < 1).
    static float feedbackForRegen (float decay01) noexcept;
    // tone01 -> corte del LP de banda del lazo (Hz). Más TONE = más bajo = más oscuro y más estable.
    static float loopCutoffForTone (float tone01) noexcept;

    // Techo del feedback del lazo (NUNCA llega a 1 → el shimmer no diverge). En FREEZE se usa 1.0 exacto
    // (modo aparte). Ver feedbackForRegen + el LP del lazo en process().
    static constexpr float kRegenMax = 0.972f;

private:
    double sampleRate = 48000.0;
    int    maxBlock   = 512;
    int    numCh      = 2;

    // ── Pre-delay (mono, recto, sin feedback): el "vacío" inicial antes del FDN. Deriva de SIZE (60–120 ms).
    using DelayLine = juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear>;
    DelayLine preDelayL { 1 }, preDelayR { 1 };
    ovni::dsp::LinearRamp preDelaySmL { 0.0f }, preDelaySmR { 0.0f };   // de-zipper del largo del pre-delay

    // ── BLOOM (difusor de fade-in): 4 allpass primos, c≈0.55. A la entrada del wet, mono por canal.
    BloomAllpass bloomL, bloomR;

    // ── LOW CUT (high-pass TPT estéreo, bloque compartido): saca los graves del WET que ALIMENTA la cola, ANTES
    //    del FDN/lazo de shimmer (la familia FDN acumula bajos en el tail → el HP los limpia ahí). SOLO sobre el
    //    wet; el DRY se suma al final por MIX sin tocar. Default 0 → 20 Hz → transparente (no cambia el sonido).
    //    De-zipper del cutoff INTERNO (TPT modulación-safe, sin clicks). RT-safe (todo en prepare()).
    ovni::dsp::LowCut lowCut;

    // ── HI CUT (low-pass TPT estéreo, bloque compartido): el PAR del LowCut — oscurece/suaviza la cola cortando
    //    los AGUDOS del WET. Va encadenado DESPUÉS del LowCut sobre el MISMO wet de entrada a la cola (lowcut
    //    saca graves, hicut saca agudos). SOLO sobre el wet; el DRY se suma intacto por MIX. Default 0 → 20 kHz →
    //    transparente (no cambia el sonido). Bypass cuando hiCut01==0 (no llama process). RT-safe (todo en prepare()).
    ovni::dsp::HiCut hiCut;

    // ── Difusor FDN (motor de NÉBULA, intacto). 100% wet (mix interno = 1). El carácter de HALO lo gobiernan
    //    sus macros; el FDN difunde. Breath INTERNO (mod lenta del difusor).
    ovni::engines::FdnReverb fdn;

    // ── Difusión de la EXCITACIÓN (allpass Schroeder, por canal): densifica la cola sin re-difundir el
    //    RETORNO del shimmer en cada vuelta (por eso el FDN corre con inputDiffusion=false — re-difundir
    //    el lazo cambiaba la subida de la escalera de octavas, medido). Se aplica al dry pre-delayed+bloom,
    //    ANTES de sumar shimmerReturn.
    ovni::engines::FdnReverb::InputDiffuser exDiffL, exDiffR;

    // ── Pitch granular poly DENTRO del lazo: voces FIJAS octava+quinta. Corre con OS 4× LOCAL.
    ovni::engines::PitchShifter pitch;
    std::unique_ptr<juce::dsp::Oversampling<float>> os;   // 4× (factor 2), FIR equiripple — anti-alias del pitch
    bool osEnabled = true;                                 // diagnóstico [alias]: off = medir alias sin OS

    // ── Trajectory: conduce la ÓRBITA del wet (azimut(t)), FUERA del lazo. El SYNC engancha su freeHz al
    //    tempo (ver process()).
    ovni::engines::Trajectory      traj;

    // ── ÓRBITA por ITD+ILD sobre el wash ESTÉREO (FIX envolvimiento). El viejo camino sumaba el wash a mono
    //    (SpatialEngine) y lo reconstruía como UN punto → más ORBIT = más ANGOSTO (bug que Joaquín cazó con
    //    Insight). En su lugar paneamos el wash ANCHO COMPLETO alrededor de la cabeza con los cues binaurales
    //    reales (ITD = delay interaural, ILD = nivel interaural) modulados por el azimut de la órbita. El wash
    //    NO se suma a mono → conserva su ancho/decorrelación (IACC bajo) y ADEMÁS orbita. ORBIT = profundidad
    //    del pan (0 = wash quieto; 1 = órbita plena). NUNCA mono. ITD ≤ ~0.7 ms (≈34 samples @48k) = rango
    //    interaural humano; lo dimensiona prepare(). IN PHASE (monoSafe) apaga el pan + bass-mono → colapsa
    //    seguro.
    //
    //    ILD = SOMBRA DE CABEZA DEPENDIENTE DE FRECUENCIA (más binaural que la atenuación plana original): una
    //    cabeza real casi no atenúa los graves (los rodean) pero sombrea fuerte los agudos. Modelamos el oído
    //    LEJANO con un high-shelf 1-polo: graves a unidad, agudos atenuados, y la atenuación HF sigue el azimut
    //    (más cierre = más sombra HF). LP fijo @ kHeadShadowHz (corner del shelf, en prepare); sólo el residuo
    //    HF (x − LP) se escala por shelfGain∈[0,1] (1 = sin sombra; <1 = HF sombreado). Una pizca de atenuación
    //    plana acompaña (cue de nivel global real), pero la HF DOMINA. shelfGain se de-zippea por-sample.
    using ItdLine = juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd>;
    ItdLine orbitItdL { 64 }, orbitItdR { 64 };
    float   orbitItdMaxSamp = 34.0f;       // ITD máx en samples (se computa en prepare según SR)
    // High-shelf de sombra de cabeza (ILD dependiente de frecuencia): LP 1-polo fijo @ kHeadShadowHz cuyo
    // residuo HF se atenúa en el oído lejano. Estado del LP por canal (sin alloc en process()).
    float   orbitShelfCoef = 0.0f;         // coef del LP del corner del shelf ~1.6 kHz (1-polo), fijo en prepare
    float   orbitShelfLpL = 0.0f, orbitShelfLpR = 0.0f;
    // De-zipper por bloque del pan (anti-click cuando el azimut cambia rápido). gLsm/gRsm = ganancia plana
    // residual; hLsm/hRsm = transmisión HF del shelf (1 = sin sombra) del oído L/R.
    float   orbitGLsm = 1.0f, orbitGRsm = 1.0f, orbitDLsm = 0.0f, orbitDRsm = 0.0f;
    float   orbitHLsm = 1.0f, orbitHRsm = 1.0f;

    // ── EQ de banda del lazo (float64): LP (TONE) + HP fijo (DC blocker). 1-polo propio por canal en double
    //    (regla house §1: el lazo de feedback con decay > 1 s va en float64; el LP/HP viven en ese lazo).
    struct OnePoleLP64 { double z = 0.0, a = 0.0; void setCutoff (double fc, double sr) noexcept; double process (double x) noexcept; void reset() noexcept { z = 0.0; } };
    struct OnePoleHP64 { double z = 0.0, a = 0.0; void setCutoff (double fc, double sr) noexcept; double process (double x) noexcept; void reset() noexcept { z = 0.0; } };
    std::array<OnePoleLP64, 2> loopLP;
    std::array<OnePoleHP64, 2> loopHP;

    // El "retorno" del lazo (la cola pitched+EQ del bloque anterior) reinyectada al input. float64 (lazo).
    std::array<std::vector<double>, 2> shimmerReturn;

    // Scratch: el wet del difusor (entra al pitch). Dimensionado al maxBlock (+ holgura para el OS).
    juce::AudioBuffer<float> wetBuf;
    // Scratch del wet EQ orbitado (el wash ancho paneado por ITD/ILD) que va al MIX.
    juce::AudioBuffer<float> spatialMix;

    // De-zipper de las macros (después del mapeo). Lineales salvo el corte del LP (multiplicativo).
    using SmLin = juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>;
    using SmMul = juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>;
    SmLin shimmerSm;     // cantidad de capa reinyectada (0..1)
    SmLin regenSm;       // coeficiente de feedback del lazo
    SmLin mixSm;         // dry/wet 0..1 (a power-law por-sample)
    ovni::dsp::LinearRamp inputSm { 1.0f };   // FREEZE: ganancia de entrada al lazo (xfade a 0); LinearRamp (value/advance)
    SmMul loopCutSm;     // corte del LP del lazo (Hz) — multiplicativo (octava-uniforme)

    bool firstBlock = true;

    // Limiter de SALIDA estéreo-linked (anti-clip de la suma dry+wet, techo del sello, latencia 0).
    ovni::dsp::StereoLimiter outLimiter;

    // Telemetría: RMS del lazo (post-EQ) del último bloque.
    float lastLoopRms = 0.0f;
};

} // namespace halo
