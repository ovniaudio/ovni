#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <vector>

#include "engines/stft/StftEngine.h"
#include "dsp/StereoLimiter.h"

// =============================================================================
// AuroraEngine — motor de PANEO ESPECTRAL de AURORA (SPL·01), sobre el motor
// compartido ovni::engines::StftEngine (STFT/OLA Hann 75%, COLA verificada).
//
// FÍSICA (spec 04-aurora §3 + curaduría 2026-06-10):
//   · Fuente del despliegue = el MID (suma mono de la entrada) — decisión v1 de
//     curaduría: honesto, simple, mono-safe. El STFT corre con 2 canales que
//     reciben el MISMO mid → cada frame ve el MISMO espectro X[k] en L y R y el
//     FrameProcessor reparte la magnitud por bin.
//   · Por bin k: θ[k] = γ · C(k, tilt) ∈ [−1, +1] (− = izquierda, + = derecha).
//     Reparto L/R por POTENCIA CONSTANTE (el CARÁCTER del despliegue / TILT):
//       α = (θ+1)·π/4 ;  gL = √2·cos(α) ;  gR = √2·sin(α)
//   · ENSANCHADOR REAL (el fix audible 2026-06-10, causa raíz de "no hacía nada"):
//     SOBRE el reparto, un OFFSET DE FASE por bin OPUESTO en L/R, RANDOM por bin
//     (semilla fija) y escalado por γ·spread:  φ_off[k] = γ · spreadMax · decorr[k].
//       L[k] = gL·X[k]·e^(−jφ_off) ;  R[k] = gR·X[k]·e^(+jφ_off)
//     El paneo SOLO de magnitud (fase preservada) era el objetivo de diseño
//     EQUIVOCADO: sobre música real CONCENTRADA (kick/bajo/snare) deja la imagen
//     MONO (goniómetro vertical) porque un único espectro coherente escalado por
//     dos ganancias reales sigue 100 % correlacionado. La decorrelación de FASE
//     random-por-bin baja CORR de verdad (estilo HORIZON-spread / PULSAR-widener):
//     a spread 0 = mono (offset 0); a spread alto cada bin sale a un ángulo distinto
//     → CORR cae monótona. TRADE-OFF HONESTO declarado: la suma mono pierde unos dB
//     a spread alto (es lo que hace CUALQUIER widener) — IN PHASE es el escape
//     mono-safe (apaga el offset → la imagen vuelve ~al dry, suma mono ~0 dB).
//   · C(k,tilt) = e(u)·weave(u): u = posición log-f del bin (40 Hz→16 kHz),
//     e(u) = envolvente de apertura (tilt 0: graves centro / agudos bordes;
//     negativo invierte por morph continuo; positivo abre antes en frecuencia),
//     weave(u) = sin(2π·kWeaveCycles·u) → el espectro "serpentea" L↔R al subir
//     en frecuencia (el abanico desplegado), con balance L/R ≈ 0 por construcción.
//   · MOTION: LFO senoidal sobre γ (profundidad = motion01). COTA ANTI-AM
//     CABLEADA: la variación efectiva del ángulo se capa a < 20 Hz
//     (kAntiAmCapHz) — más rápido = AM audible (spec §3.2). No es opción.
//   · MONO SAFE: bins bajo el corte (60→700 Hz log) colapsan al centro con
//     transición suave de UNA OCTAVA por frecuencia (sin escalón espectral).
//   · DUCK: envelope-follower del DRY (attack ~5 ms, release ~150 ms) que escala
//     γ hacia 0 multiplicativo → el despliegue se cierra cuando pega el dry.
//   · MIX por ley de potencia con el DRY RETRASADO latencySamples() (alineado al
//     wet: cero comb a mix intermedio). Rampas por-sample de las ganancias.
//   · Limiter de salida estéreo-linked techo 0.85 (true-peak margin del sello).
//
// Suavizado (anti-click, DESPUÉS del mapeo, en el dominio del destino): spread/
// tilt/duck-depth en lineal y el corte de Mono Safe en log2(Hz), one-pole POR
// FRAME (el solape 75% del OLA crossfadea los cambios de ganancia entre frames);
// las ganancias de MIX en rampas POR-SAMPLE; γ con one-pole rápido por frame.
// =============================================================================
namespace aurora
{

// Parámetros por bloque (el processor los lee del APVTS y los pasa ya mapeados).
struct AuroraParams
{
    float spread01      = 0.55f;   // 0..1 escala global del ángulo
    float tiltBi        = 0.0f;    // -1..+1 curva frecuencia→posición
    float motion01      = 0.0f;    // 0..1 profundidad del LFO del abanico
    float monoSafe01    = 0.0f;    // 0..1 → corte 50→400 Hz log (0 = sin red de usuario; el piso sub <55 Hz es física del motor)
    float duck01        = 0.0f;    // 0..1 profundidad del sidechain interno
    float mix01         = 1.0f;    // 0..1 dry/wet (ley de potencia)
    float motionRateHz  = 0.3f;    // rate efectivo (FREE o SYNC); el motor lo capa a < 20 Hz
    bool  motionSync    = false;   // SYNC: la fase se engancha a ppq (late con el track)
    bool  isPlaying     = false;
    double ppqPosition  = 0.0;
    float beatsPerCycle = 4.0f;    // beats por ciclo de la división activa (lock de fase en SYNC)
    int   numInputChannels = 2;    // canales REALES de entrada (mono: el mid = ch0, sin -6 dB)
    bool  inPhase       = false;   // IN PHASE (toggle del chasis): colapsa la DECORRELACIÓN de fase del
                                   // SPREAD → la imagen vuelve ~al dry (mono-compatible). El bass-mono
                                   // genérico del chasis corre DESPUÉS; acá apagamos el ensanchador propio.
};

class AuroraEngine
{
public:
    static constexpr int kVizBands = 24;   // bandas log del visualizador (etapa 3)

    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();

    // Procesa IN-PLACE el bloque estéreo del host. RT-safe (sin locks ni allocations).
    void process (juce::AudioBuffer<float>& buffer, const AuroraParams& p);

    // Latencia del pipeline (= N del STFT). El processor la declara con setLatencySamples().
    int latencySamples() const noexcept { return stft.latencySamples(); }

    // Última ganancia del limiter de salida (1 = sin reducción) → LED de clip del chasis.
    float lastLimiterGain() const noexcept { return limiter.lastGain(); }

    //== Telemetría para el visualizador (la lee el processor tras process(), audio thread) ==
    float currentGamma() const noexcept   { return gammaSm; }    // γ efectivo (spread·motion·duck): el abanico late/respira
    float duckEnvelope() const noexcept   { return envSm; }      // envolvente del duck 0..1 (el visual ve "la pegada")
    const std::array<float, kVizBands>& bandEnergies()  const noexcept { return vizEnergy; }  // RMS-ish por banda
    const std::array<float, kVizBands>& bandPositions() const noexcept { return vizPos; }     // azimut -1..+1 por banda

private:
    void processFrame (const ovni::engines::StftEngine::FrameView& f);   // el DSP espectral por bin

    // — STFT compartido + limiter del sello —
    ovni::engines::StftEngine stft;
    ovni::dsp::StereoLimiter  limiter;

    // — dry delay (alineación dry↔wet para el MIX) —
    std::vector<float> dryRing[2];   // ring por canal, tamaño N + maxBlock
    int dryWrite  = 0;
    int dryRingSz = 0;

    // — scratch del dry (copia de la entrada antes de pisar el buffer con el mid) —
    juce::AudioBuffer<float> dryScratch;

    // — tablas por bin (precomputadas en prepare; el frame sólo las lee) —
    std::vector<float> binU;       // posición log-f 0..1 (40 Hz→16 kHz) por bin
    std::vector<float> binWeave;   // sin(2π·kWeaveCycles·u): el lado del serpenteo
    std::vector<float> binLog2F;   // log2(f_k) (para el corte suave de Mono Safe)
    std::vector<float> binDriveRamp; // rampa log 350 Hz→1.4 kHz del drive de TILT>0 (anti-espurias)
    std::vector<float> binSpreadDrive; // 1 + kSpreadDrive·rampa² (drive de EXCURSIÓN, band-limitado al cuadrado): el viejo paneo de magnitud
    std::vector<float> binDecorr;  // offset de fase RANDOM por bin (semilla fija): decorrelación de AIRE (>~350 Hz, OLA-limpia)
    std::vector<int>   binVizBand; // bin → banda del visualizador

    // — ENSANCHADOR DE LOW-MIDS (post-STFT, dominio del tiempo, OLA-INMUNE) —
    // El STFT sin zero-pad no puede decorrelar la fase del low-mid de un tono sin time-alias
    // (medido: el beat vive 80–350 Hz y ahí el skirt es angosto → alias). Por eso el ancho del
    // CUERPO del beat lo hace acá, sobre la señal RECONSTRUIDA: una réplica del MID pasada por
    // un cascade de ALLPASS de Schroeder (magnitud plana, fase scrambleada → decorrelada del
    // mid), inyectada como SIDE PURO y opuesto en L/R. La suma mono L+R = 2·mid EXACTA (el side
    // se cancela) → mono-compatible por construcción (suma mono ~0 dB a cualquier spread). HP
    // de graves (Mono Safe + IN PHASE) mantiene el kick/sub al centro. Sin latencia extra.
    // Decorrelador FIR DISPERSO ("velvet"): N taps con signo random en posiciones pseudo-random,
    // amplitud que decae con el retardo (réplica difusa, NO eco). FIR puro → estable y SIN
    // espurias por construcción (un IIR/allpass de Schroeder daba espurias medidas en el gate
    // [alias] sobre tono puro). Magnitud ~plana (taps random), fase scrambleada → decorrela.
    struct VelvetFir
    {
        std::vector<float> ring;          // línea de retardo circular
        std::vector<int>   tapPos;        // posiciones de los taps (samples de retardo)
        std::vector<float> tapGain;       // ganancia (con signo) de cada tap
        int   sz = 0, w = 0;
        void prepareLine (int maxDelay)
        {
            sz = juce::jmax (1, maxDelay + 1);
            ring.assign ((size_t) sz, 0.0f); w = 0;
        }
        void reset() { std::fill (ring.begin(), ring.end(), 0.0f); w = 0; }
        inline float process (float x) noexcept
        {
            ring[(size_t) w] = x;
            float y = 0.0f;
            for (size_t t = 0; t < tapPos.size(); ++t)
            {
                int r = w - tapPos[t]; if (r < 0) r += sz;
                y += tapGain[t] * ring[(size_t) r];
            }
            if (++w >= sz) w = 0;
            return y;
        }
    };
    VelvetFir decorFir;             // genera la réplica decorrelada del mid (FIR disperso)
    float decorHpLp[4] = { 0.0f, 0.0f, 0.0f, 0.0f };   // 4× one-pole (cascada LP → HP por resta): falda 24 dB/oct que ata el sub/kick al centro
    float decorSpreadSm   = 0.0f;   // profundidad del ensanchador suavizada (one-pole por-sample, anti-zipper + block-size invariante)
    float decorDepCoef    = 0.0f;   // coef del one-pole por-sample de la profundidad (τ fijo en s → invariante al block-size)
    double decorMeanDelaySamples = 0.0;   // retardo de grupo medio del FIR (centroide por |gain|²) → lo compensa el lock de SYNC

    // — estado del MOTION (fase del LFO, avanza por frame) —
    double motionPhase = 0.0;      // radianes
    double phaseIncPerFrame = 0.0; // 2π·rate·hop/sr (recalculado por bloque)

    // — envelope follower del DUCK (per-sample, sobre el mid del dry) —
    // envNorm se actualiza en las FRONTERAS DE HOP (max del período completado), no por
    // bloque del host: los frames ven la MISMA envolvente a block 32 que a 2048
    // (invarianza al buffer-size MEDIDA por [buffersize]/[consistency] — QA checklist §2).
    float env        = 0.0f;       // seguidor |mid| con ballistics (per-sample)
    float envHopMax  = 0.0f;       // max de env dentro del período de hop EN CURSO
    int   hopPhase   = 0;          // samples dentro del hop actual (espejo del ring del STFT)
    float envNorm    = 0.0f;       // env normalizado 0..1 (ref -12 dBFS), fijado por hop
    float envSm      = 0.0f;       // suavizado por frame (lo consume γ)
    float envAtkCoef = 0.0f, envRelCoef = 0.0f;

    // — suavizado por frame (post-mapeo, dominio del destino) —
    float spreadSm = 0.55f, spreadTgt = 0.55f;
    float tiltSm   = 0.0f,  tiltTgt   = 0.0f;
    float duckSm   = 0.0f,  duckTgt   = 0.0f;
    float msAmtSm  = 0.5f,  msAmtTgt  = 0.5f;    // cantidad de Mono Safe 0..1
    float cutLog2Sm = 0.0f, cutLog2Tgt = 0.0f;   // corte de Mono Safe en log2(Hz)
    float motion01Sm = 0.0f, motion01Tgt = 0.0f;
    float inPhaseSm = 0.0f, inPhaseTgt = 0.0f;   // IN PHASE 0→1 suavizado (0 = ensanchador ON, 1 = colapsado): cross-fade anti-click del escape mono-safe
    float gammaSm  = 0.55f;                      // γ efectivo suavizado (telemetría + DSP)
    float smoothCoef = 0.2f;                     // one-pole por frame (τ ~30 ms)
    float gammaCoef  = 0.7f;                     // one-pole rápido de γ (τ ~8 ms)
    double gammaLagSamples = 0.0;                // retardo de grupo del one-pole de γ (samples) — lo compensa el lock de SYNC
    bool  primeSmoothing = true;                 // primer bloque tras prepare/reset: sin fade inicial

    // — MIX (rampas por-sample de las ganancias dry/wet, ley de potencia) —
    float gDry = 0.0f, gWet = 1.0f;

    // — telemetría por banda (escrita por frame, leída por el processor) —
    std::array<float, kVizBands> vizEnergy {};
    std::array<float, kVizBands> vizPos {};

    double sr  = 48000.0;
    int    maxBlock = 0;
};

} // namespace aurora
