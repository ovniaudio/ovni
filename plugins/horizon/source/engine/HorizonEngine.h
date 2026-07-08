#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <vector>

#include "engines/stft/StftEngine.h"
#include "dsp/StereoLimiter.h"

// =============================================================================
// HorizonEngine — motor de FREEZE ESPECTRAL RÍTMICO de HORIZON (SPL·02), sobre el
// motor compartido ovni::engines::StftEngine (STFT/OLA Hann 75%, COLA verificada).
// REUSA EL STFT TAL CUAL: lo único nuevo es captura de frame + scheduler de gate +
// whisper + duck (la curaduría lo llama "barato una vez que AURORA existe").
//
// FÍSICA (spec 06-horizon §3 + curaduría 2026-06-10 + fix audible 2026-06-10):
//   · WET path SIEMPRE por el STFT. Con FREEZE OFF NO es identity: es un
//     ESPACIALIZADOR/DIFUSOR ESPECTRAL VIVO — SPREAD (des-correlación de fase por
//     bin, opuesta L/R) + WHISPER (difusión de fase) se aplican al espectro VIVO del
//     mid cada frame → ya ensancha al cargar y tocar (el goniómetro abre sin congelar).
//     FREEZE pasa a ser el gesto que SUSPENDE esa textura encima (captura + sostiene).
//     IN PHASE (monoSafe) anula la des-correlación → escape mono-compatible.
//   · FREEZE: al activar, captura magnitud+fase del frame y conmuta a resintetizar
//     ese frame. Avance de fase COHERENTE por bin (acumulador con el delta nominal
//     del hop, φ += 2π·k·hop/N) → vidrioso/limpio. PEAK-LOCK (Laroche-Dolson "rigid
//     phase locking", buildPeakLock): el lóbulo de cada componente tonal avanza la
//     fase a la frecuencia DE SU PICO (no cada bin a la suya) → el lóbulo se mueve
//     coherente, sin batido inter-bin = sin anillado metálico. NO toca la magnitud
//     (timbre intacto; el ruido de banda ancha no se engancha → imagen estéreo
//     intacta). Cross-fade de frame al entrar/salir (empalme de bloque, no de-zipper).
//   · WHISPER: randomiza la fase por frame (interpolación suave coherente↔random;
//     curva perceptual: 0–30 casi coherente — el default 12 se OYE vidrioso, no
//     ruidoso). A 100 = whisperization (fase 100% random por frame → difuso/aireado).
//   · GATE RÍTMICO (RATE): re-dispara/gatea la resíntesis del frame SOSTENIDO (NO
//     re-captura viva — cortada por curaduría). Envolvente RAISED-COSINE (attack/
//     decay ~5–15 ms, no rectangular). RATE off = frame plano (pad). En SYNC el
//     período sale del ppqPosition del host (rateHz = bpm/60/beatsPerCycle); el
//     scheduler ENGANCHA al ppq (no deriva libre). FREE capado a 8 Hz.
//   · SPREAD: des-correlación L/R (offset de fase por bin opuesto en L/R, escalado
//     por spread) + paneo de potencia constante. A 0 = mono/centrado; a 100 = freeze
//     esparcido en el campo sin romper energía mono.
//   · DUCK: envelope-follower del DRY (attack ~5 ms, release ~150 ms) que agacha la
//     ganancia del WET (freeze) multiplicativo suavizado → el freeze respira con la
//     performance y vuelve en los huecos.
//   · MIX por ley de potencia con el DRY RETRASADO latencySamples() (alineado al
//     wet: cero comb a mix intermedio). Rampas por-sample de las ganancias.
//   · Limiter de salida estéreo-linked techo 0.85 (true-peak margin del sello).
//
// Suavizado (anti-click, regla de oro): rampa POR-SAMPLE de toda ganancia/coef
// modulado; suavizar DESPUÉS del mapeo; MIX ley de potencia; el gate del re-trigger
// usa envolvente raised-cosine + cross-fade de frame por disparo; ScopedNoDenormals.
// =============================================================================
namespace horizon
{

// Parámetros por bloque (el processor los lee del APVTS y los pasa ya mapeados).
struct HorizonParams
{
    bool  freeze       = false;   // gesto central: capturar/sostener el frame
    float whisper01    = 0.12f;   // 0..1 coherencia↔randomización de fase (0 = vidrioso)
    float spread01     = 0.50f;   // 0..1 des-correlación + ancho del paneo
    float duck01       = 0.0f;    // 0..1 profundidad del sidechain interno (dry agacha wet)
    float mix01        = 1.0f;    // 0..1 dry/wet (ley de potencia)
    float rateHz       = 0.0f;    // rate efectivo del re-trigger (FREE o SYNC); 0 = sostenido; el motor capa a 8 Hz
    bool  rateSync     = false;   // SYNC: el latido se engancha a ppq (late con el track)
    bool  isPlaying    = false;
    double ppqPosition = 0.0;
    float beatsPerCycle = 1.0f;   // beats por ciclo de la división activa (lock de fase en SYNC)
    int   numInputChannels = 2;   // canales REALES de entrada (mono: el mid = ch0, sin -6 dB)
    bool  monoSafe     = false;   // IN PHASE: anula la des-correlación del vivo (escape mono-compatible)
};

class HorizonEngine
{
public:
    static constexpr int kVizBands = 24;   // bandas log del visualizador (etapa 2)

    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();

    // Procesa IN-PLACE el bloque estéreo del host. RT-safe (sin locks ni allocations).
    void process (juce::AudioBuffer<float>& buffer, const HorizonParams& p);

    // Latencia del pipeline (= N del STFT). El processor la declara con setLatencySamples().
    int latencySamples() const noexcept { return stft.latencySamples(); }

    // Última ganancia del limiter de salida (1 = sin reducción) → LED de clip del chasis.
    float lastLimiterGain() const noexcept { return limiter.lastGain(); }

    //== Telemetría para el visualizador (la lee el processor tras process(), audio thread) ==
    float gateAmplitude() const noexcept { return gateAmpTele; }   // 0..1 amplitud del latido (raised-cosine)
    float gatePhase()     const noexcept { return gatePhaseTele; }  // 0..1 fase del ciclo del gate
    float wetEnergy()     const noexcept { return wetEnergyTele; }  // 0..1 energía del wet (el freeze "se oye")
    float duckEnvelope()  const noexcept { return envSm; }          // 0..1 envolvente del dry (la pegada que agacha)
    bool  isFrozen()      const noexcept { return frozen; }         // el espectro está congelado (cristalizado)
    const std::array<float, kVizBands>& frozenSpectrum() const noexcept { return vizSpectrum; }  // el espectro congelado (visualizador)

private:
    void processFrame (const ovni::engines::StftEngine::FrameView& f);   // el DSP espectral por frame
    void buildPeakLock (float maxMag, int nb) noexcept;                  // peak-lock de fase del frame capturado

    // — STFT compartido + limiter del sello —
    ovni::engines::StftEngine stft;
    ovni::dsp::StereoLimiter  limiter;

    // — dry delay (alineación dry↔wet para el MIX) —
    std::vector<float> dryRing[2];   // ring por canal, tamaño N + maxBlock
    int dryWrite  = 0;
    int dryRingSz = 0;

    // — scratch del dry (copia de la entrada antes de pisar el buffer con el mid) —
    juce::AudioBuffer<float> dryScratch;

    // — FRAME CONGELADO: magnitud + fase capturadas en el instante del freeze (por bin) —
    std::vector<float> freezeMag;       // |X[k]| del frame capturado
    std::vector<float> freezePhase;     // ∠X[k] del frame capturado (fase base)
    std::vector<float> coherentPhase;   // fase ACUMULADA coherente por bin (φ += 2π·k·hop/N)
    std::vector<float> binPhaseInc;     // delta nominal del hop por bin: 2π·k·hop/N (precomputado)
    std::vector<float> lockPhaseInc;    // PEAK-LOCK: avance de fase del PICO regional por bin (anti-anillado)
    std::vector<float> binDecorr;       // offset de des-correlación FIJO por bin ∈[-1,1] (semilla fija) → SPREAD escala
    std::vector<int>   binVizBand;      // bin → banda del visualizador

    bool frozen        = false;   // el motor está resintetizando el frame congelado
    bool captureNext   = false;   // pedido de captura en el PRÓXIMO frame (gesto FREEZE recién activado)
    bool prevFreeze    = false;   // estado del gesto en el bloque anterior (detección de flanco)

    // — cross-fade de frame al entrar/salir del freeze (empalme de bloque, no de-zipper) —
    // 0 = pasando audio vivo · 1 = resintetizando el frame congelado. Rampa POR FRAME
    // (un frame = un hop; el solape 75% del OLA crossfadea suave entre frames).
    float freezeXf     = 0.0f;
    float freezeXfStep = 0.25f;   // ~4 frames de cross-fade (≈ N samples)

    // — GATE RÍTMICO: fase del ciclo del re-trigger (0..1) + envolvente raised-cosine —
    double gatePhase01      = 0.0;   // posición en el ciclo del gate (0..1)
    double gatePhaseInc     = 0.0;   // avance por frame = rateHz·hop/sr (recalculado por bloque)
    float  gateAmpSm        = 1.0f;  // amplitud del gate suavizada (cross-fade por disparo)
    bool   gateActive       = false; // RATE > 0 (hay latido) vs sostenido (pad plano)
    // coef del one-pole que suaviza la amplitud del gate. FUENTE ÚNICA: lo usan el suavizado
    // (processFrame) Y la compensación de group-delay del lock al ppq (SYNC). Si cambia, ambos
    // se mueven juntos → el latido no se desfasa de la grilla. (era un 0.5 duplicado, cazado en audit)
    static constexpr float kGateSmCoef = 0.5f;

    // — WHISPER: fase random por frame (interpolación coherente↔random) —
    juce::Random rng { 0x402E26 };   // semilla fija → reproducible
    float whisperSm = 0.12f, whisperTgt = 0.12f;

    // — SHIMMER ESPECTRAL MONO-AUDIBLE (fix 2026-06-13): LFO lento por frame que mueve la
    //   magnitud de cada bin a una fase distinta (binDecorr) → wash difuso que se OYE en mono.
    //   Aplicado IGUAL en L/R (mono-compatible) → no toca CORR/WIDTH; IN PHASE lo conserva. —
    double shimmerPhase = 0.0;     // fase del LFO lento del shimmer (radianes)
    double shimPhaseInc = 0.0;     // avance por frame = 2π·kShimRateHz·hop/sr (recalculado por bloque)

    // — SPREAD (suavizado por frame; des-correlación + ancho del paneo) —
    float spreadSm = 0.50f, spreadTgt = 0.50f;

    // — IN PHASE (mono-safe): escala 1→0 que apaga la des-correlación del vivo (rampa por frame) —
    float monoSafeSm = 1.0f, monoSafeTgt = 1.0f;   // 1 = des-correlación plena · 0 = mono-compatible

    // — envelope follower del DUCK (per-sample, sobre el mid del dry) —
    // envNorm se actualiza en las FRONTERAS DE HOP (max del período completado): los
    // frames ven la MISMA envolvente a block 32 que a 2048 (invarianza al buffer-size).
    float env        = 0.0f;       // seguidor |mid| con ballistics (per-sample)
    float envHopMax  = 0.0f;       // max de env dentro del período de hop EN CURSO
    int   hopPhase   = 0;          // samples dentro del hop actual (espejo del ring del STFT)
    float envNorm    = 0.0f;       // env normalizado 0..1 (ref -12 dBFS), fijado por hop
    float envSm      = 0.0f;       // suavizado por frame (lo consume la ganancia del wet)
    float duckSm     = 0.0f, duckTgt = 0.0f;   // profundidad del duck suavizada
    float envAtkCoef = 0.0f, envRelCoef = 0.0f;

    // — suavizado por frame —
    float smoothCoef = 0.2f;       // one-pole por frame (τ ~30 ms)
    float gammaCoef  = 0.7f;       // one-pole rápido del duck (τ ~8 ms)
    bool  primeSmoothing = true;   // primer bloque tras prepare/reset: sin fade inicial

    // — MIX (rampas por-sample de las ganancias dry/wet, ley de potencia) —
    float gDry = 0.0f, gWet = 1.0f;

    // — telemetría (escrita por frame, leída por el processor) —
    float gateAmpTele   = 1.0f;
    float gatePhaseTele = 0.0f;
    float wetEnergyTele = 0.0f;
    std::array<float, kVizBands> vizSpectrum {};

    double sr  = 48000.0;
    int    maxBlock = 0;
};

} // namespace horizon
