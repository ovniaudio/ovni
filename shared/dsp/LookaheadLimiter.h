#pragma once
#include <vector>

namespace ovni::dsp {

// Afinables del limiter de salida con LOOKAHEAD (red de seguridad anti-clip DEFINITIVA sobre la SUMA final).
//   ceiling      ~0.95 (~−0.45 dB): techo true-peak-safe. Deja margen para el pico inter-sample (la onda
//                reconstruida ENTRE muestras sobrepasa la muestra un factor fijo); con 0.95 el true-peak peor
//                caso queda con holgura bajo 0 dBFS sin sacrificar nivel audible.
//   lookaheadMs  ~3.0 ms: cuánto se "adelanta" el limiter para ver el pico que viene. = latencia que se le
//                reporta al host. Joaquín aceptó ~3 ms a cambio de que NO clipee ni cruja.
//   releaseMs    ~80 ms: recuperación suave de la ganancia tras contener un pico (sin bombeo ni armónicos).
struct LookaheadTuning
{
    float ceiling     = 0.95f;
    float lookaheadMs = 3.0f;
    float releaseMs   = 80.0f;
};

// Limiter de salida ESTÉREO-LINKED con LOOKAHEAD. A diferencia del StereoLimiter (attack instantáneo, latencia
// 0, que al contener un transiente le mete un escalón de ganancia y lo distorsiona = "crackle"), este RETRASA
// la señal L samples y, mirando hacia adelante, BAJA la ganancia con una rampa SUAVE que ya está abajo cuando
// el pico sale del retardo. Resultado: el pico emerge atenuado SIN escalón en el transiente → ni clip ni
// crujido. Estéreo-linked (la MISMA ganancia a L y R, del pico mayor de ambos) → imagen estéreo exacta.
//
// Va al FINAL de la cadena, sobre la SUMA dry+wet (la señal que sale al DAW). Ver
// references/anti-click-clip-truepeak.md §3/§6.
//
// Cómo se conoce "el pico de los próximos L samples" (sliding peak): una VENTANA DESLIZANTE de máximo, con un
// deque monótono respaldado por un ring (amortizado O(1) por sample, RT-safe, sin asignar). Para cada sample
// de salida sabemos el |pico| EXACTO de las próximas L muestras de entrada → la ganancia objetivo es el mínimo
// necesario y se SOSTIENE mientras ese pico esté en la ventana (no recupera antes de tiempo). Sobre ese target
// va un attack/release de 1-polo: el target ya está fijado L samples ANTES de que el pico emerja, así que el
// attack (≈L samples) lo alcanza a tiempo SIN escalón; release lento y suave.
//
// TRUE-PEAK: lo que importa NO es el pico de MUESTRA sino el inter-sample (la onda reconstruida ENTRE muestras
// sobrepasa la muestra hasta ~×1.25 con HF agresivo). Por eso el detector NO mira |x| crudo: oversamplea 4× con
// Catmull-Rom (el mismo método del medidor true-peak del DAW, §3) y alimenta el sliding-max con ESE pico
// inter-sample. Así el target = ceiling/truePeak → la onda continua queda bajo el techo, no sólo las muestras.
// El detector tiene 1 sample de latencia interna (necesita el vecino siguiente para interpolar), absorbida por
// el retardo de L samples (L≫1).
class LookaheadLimiter
{
public:
    // Dimensiona el retardo y la ventana (L = round(lookaheadMs·sr/1000)) y los buffers. El procesado es
    // por-sample, sin asignar en RT (maxBlock documenta el peor bloque pero no afecta el tamaño del retardo).
    void prepare (double sampleRate, int maxBlock);
    void reset() noexcept;                          // vacía el retardo/ventana y deja la ganancia en 1
    void setTuning (LookaheadTuning t);             // recalcula L/coefs (re-dimensiona si cambió el lookahead)

    // Procesa in-place L/R (estéreo-linked). RT-safe, sin asignaciones. La señal sale RETRASADA latencySamples().
    void process (float* left, float* right, int numSamples) noexcept;

    // Latencia introducida (= L samples del lookahead). El processor la reporta al host (setLatencySamples).
    int latencySamples() const noexcept { return lookahead; }

    // Última ganancia aplicada: 1 = sin reducción, <1 = conteniendo picos. Para el LED de clip.
    float lastGain() const noexcept { return gain; }

private:
    void rebuild();          // recalcula lookahead + coefs y re-dimensiona los buffers/estado
    void updateCoefs();      // attack (converge dentro de L samples) y release (releaseMs) en coefs por-sample

    double          sampleRate = 48000.0;
    int             maxBlock   = 512;
    LookaheadTuning tuning {};

    int   lookahead = 0;      // L samples (= latencia). round(lookaheadMs·sr/1000)
    float atkCoef   = 1.0f;   // coef de attack (1-polo); converge dentro de L samples (τ = L/4)
    float relCoef   = 0.0f;   // coef de release (1-polo) con cte de tiempo = releaseMs

    // Retardo circular por canal (capacidad = lookahead). La salida del sample i es la entrada de i−L.
    std::vector<float> delayL, delayR;
    int                writePos = 0;

    // Ventana deslizante de MÁXIMO (deque monótono respaldado por ring): para cada sample, el pico TRUE-PEAK
    // exacto de las próximas L muestras. 'mqVal' guarda valores decrecientes; 'mqIdx' el índice global de cada uno.
    std::vector<float>  mqVal;   // valores del deque (capacidad L; frente = máximo de la ventana)
    std::vector<long>   mqIdx;   // índices globales correspondientes (para expirar por antigüedad)
    int                 mqHead = 0;   // posición del frente en el ring
    int                 mqTail = 0;   // posición tras el último (cola); vacío ⇔ mqHead==mqTail && mqSize==0
    int                 mqSize = 0;   // cantidad de elementos vivos en el deque
    long                detIdx = 0;   // índice global del último pico TRUE-PEAK encolado (detector con 1-sample lag)

    // Detector TRUE-PEAK (inter-sample): historia signed de 4 muestras por canal para Catmull-Rom 4×. El pico
    // del segmento [h1,h2] (sub-muestras entre las dos del medio) se atribuye al índice de h2 → 1 sample de lag.
    float hL[4] {};          // x[n-3..n] del canal L (h3 = más nuevo)
    float hR[4] {};          // x[n-3..n] del canal R
    int   primed = 0;        // cuántas muestras entraron (hasta llenar la historia de 4)

    float gain = 1.0f;        // ganancia actual aplicada (1 = transparente)
};

} // namespace ovni::dsp
