#pragma once
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include "engines/binaural/Reflections.h"
#include "engines/binaural/Crosstalk.h"

namespace ovni::engines {

// Motor binaural de posición VARIABLE (M2): mono -> 2 voces HRIR (las dos direcciones
// del anillo que bracketean el azimut) -> crossfade de SALIDAS + ITD por voz -> estéreo.
// Latencia 0 (FIR forma directa). Sin clicks: las recargas de coeficientes ocurren a
// peso cero (ping-pong) y el azimut se recorre por sub-bloques alineados a segmento.
class SpatialEngine
{
public:
    // Ajuste del near-field (parámetros de diseño; default = Duda-Martens físico, normalizado).
    struct NearFieldTuning   // default = perfil "Pro" (elegido por la batería [nfpro]: máximo cue
    {                        // físico real con timbre limpio y headroom seguro; ILD asimétrico Duda-Martens)
        float bassDB    = 3.5f;   // boost de graves de proximidad (dB) [moderado de 6 -> menos engorde, el limiter no bombea en el extremo]
        float ildDB     = 24.0f;  // ILD de graves near-field máx (dB, a 90° y distancia mínima)
        float cornerHz  = 800.0f; // esquina del low-shelf de proximidad (bajo = no ensucia agudos)
        bool  normalize = true;   // nearAmt llega a 1 en la distancia mínima del plugin (Radio al fondo)
    };
    void setNearField (const NearFieldTuning& t) { nearTune = t; } // llamar ANTES de prepare (recalcula el shelf)

    // Doppler: amplitud máx de la modulación de delay (metros) + piso de delay (Lagrange3rd estable). Afinable.
    struct DopplerTuning { float maxAmpMeters = 3.0f; float minSafeSamples = 2.0f; float maxSlew = 0.20f; }; // maxSlew = Δdelay máx/sample: limita el pitch -> anti-aliasing, suaviza el extremo
    void setDoppler (const DopplerTuning& t) { dopplerTune = t; } // llamar ANTES de prepare (dimensiona la linea)

    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();

    // Parámetros de un bloque de proceso. Designated-init en el call site (C++20).
    //   azimuthRad: objetivo al FINAL del bloque (0 = frente, + = CCW / izquierda).
    //   mix01: 0 = seco (centro) .. 1 = efectado (binaural).
    //   room01: cantidad de reflexiones tempranas (externalización), 0 = ninguna.
    //   width01: eje espacial del wet. 0 = mono (reservado) .. 0.5 = natural .. 1 = paneo "volador".
    //   monoSafe: fuerza graves a mono (compatible mono siempre).
    //   radius01: distancia BASE (knob Radio). 0 = al oído .. 1 = lejos. Baja el directo (1/r) +
    //             air-absorption; las reflexiones quedan constantes -> DRR = cue de profundidad.
    //   speakerMode: true = cancelación de crosstalk RACE (binaural traduce a parlantes).
    struct EngineParams
    {
        float azimuthRad  = 0.0f;
        float mix01       = 1.0f;
        float room01      = 0.0f;
        float width01     = 0.5f;
        bool  monoSafe    = false;
        float radius01    = 0.5f;
        bool  speakerMode = false;
        float distance01  = -1.0f;  // distancia INSTANTANEA modulada (fly-by). <0 => usar radius01 (sin Doppler)
        float doppler01   = 0.0f;   // 0..1: cantidad de Doppler (bypass de la linea si 0)
    };

    // Espacializa in-place. numInputChannels = canales reales (se suman a mono).
    void process (juce::AudioBuffer<float>& buffer, int numInputChannels, const EngineParams& p);

    // Compat: firma posicional (tests existentes). Delega en la versión EngineParams.
    void process (juce::AudioBuffer<float>& buffer, int numInputChannels,
                  float azimuthRad, float mix01, float room01, float width01, bool monoSafe,
                  float radius01, bool speakerMode)
    {
        process (buffer, numInputChannels,
                 EngineParams { azimuthRad, mix01, room01, width01, monoSafe, radius01, speakerMode });
    }

    // Limiter de salida ESTÉREO-LINKED (latencia 0): attack instantáneo (nunca clipea) + release suave
    // (contiene sin armónicos duros). La misma ganancia va a L y R -> imagen estéreo intacta. Afinable.
    struct LimiterTuning { float ceiling = 0.85f; float releaseMs = 60.0f; }; // ceiling ~-1.4 dB: margen generoso para inter-sample peaks (true-peak) -> ni el medidor true-peak de un DAW marca
    void setLimiter (const LimiterTuning& t) noexcept { limTune = t; }

    // Última ganancia del limiter (paso 8): 1 = sin reducción, <1 = conteniendo picos. Se lee en el
    // AUDIO thread (processBlock) para alimentar el LED de clip — mismo hilo que process(), sin atómico.
    float lastLimiterGain() const noexcept { return limGain; }

private:
    using Fir   = juce::dsp::FIR::Filter<float>;
    using Coefs = juce::dsp::FIR::Coefficients<float>;
    using Itd   = juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd>;

    struct Voice
    {
        Fir firL, firR;
        Itd itdL { 64 }, itdR { 64 };
        int dir = -1;
    };

    void  loadVoice (int slot, int dir);   // recarga punteros de coef + delays (RT-safe)
    void  ensureBracket (long segK);       // garantiza voice[lo]=d0, voice[1-lo]=d1
    void  renderVoice (Voice& v, const float* monoIn, juce::AudioBuffer<float>& wetL,
                       juce::AudioBuffer<float>& wetR, int len);

    std::vector<Coefs::Ptr> coefL, coefR;  // [kNumDirs] — preasignados (recarga sin new)
    std::vector<float>      dlyL, dlyR;    // [kNumDirs] — ITD en samples al SR de la sesión
    Voice  voice[2];
    int    lo       = 0;
    double posSteps = 0.0;                  // posición continua en unidades de paso del anillo
    bool   primed   = false;

    juce::AudioBuffer<float> mono, w0L, w0R, w1L, w1R; // scratch mono (1 canal c/u)
    juce::AudioBuffer<float> dryBuf;                    // seco = entrada original (estéreo)
    Reflections space;                                  // reflexiones tempranas (externalización)
    Crosstalk   xtalk;                                  // cancelación de crosstalk (modo Parlantes)

    // Bass-mono ("EN FASE"): crossover 1-polo por canal; los graves van a mono (mono-safe).
    float bassCoef = 0.0f, bassLpL = 0.0f, bassLpR = 0.0f;

    // Distancia: air-absorption LP 1-polo por canal (corte cae con la distancia).
    double sampleRate = 48000.0;
    float airLpL = 0.0f, airLpR = 0.0f;
    float directGainSm = 1.0f;            // 1/r rampeado por-sample (anti-click a Doppler alto: la distancia oscila)

    // Width: ganancia del corte de oído lejano, rampeada por bloque (anti-click a Speed alto).
    float gFarLsm = 1.0f, gFarRsm = 1.0f;

    // Near-field ("al oído", d<1 m): low-shelf por oído sobre el directo — boost de graves de
    // proximidad (común a ambos) + ILD de graves diferencial (Duda-Martens). gNear rampeada por
    // bloque (anti-click: el ILD sigue al azimut). nearCoef = LP 1-polo de la banda grave del shelf.
    NearFieldTuning nearTune;
    float nearCoef = 0.0f, nearLpL = 0.0f, nearLpR = 0.0f;
    float gNearLsm = 1.0f, gNearRsm = 1.0f;

    // Doppler: delay de propagacion mono modulado (el pitch emerge de variar el delay; latencia 0,
    // bypass a doppler=0). Centro = headroom para la modulacion; sólo la DERIVADA del delay -> pitch.
    DopplerTuning dopplerTune;
    Itd   dopplerLine { 2048 };           // misma DelayLine Lagrange3rd que el ITD
    float dopplerCenterSamples = 0.0f;    // = maxAmpSamples + minSafe (computado en prepare)
    float dopplerMaxAmpSamples = 0.0f;    // = maxAmpMeters * SR / c
    float dopplerDelayPrev     = 0.0f;    // delay del ultimo sample del bloque previo (continuidad C0)
    static constexpr double kSoundC = 343.0; // m/s

    LimiterTuning limTune;                // red de seguridad de salida (afinable)
    float limGain    = 1.0f;              // ganancia actual del limiter (1 = sin reducción)
    float limRelCoef = 0.0f;              // coef de release (computado en prepare)
};

} // namespace ovni::engines
