#pragma once
#include <juce_core/juce_core.h>
#include <atomic>
#include <memory>
#include <vector>
#include "analysis/AnalysisBus.h"
#include "analysis/AnalysisFrame.h"
#include "analysis/CqtFrame.h"
#include "analysis/FieldFrame.h"   // ===== 53 =====
#include "analysis/History.h"
#include "analysis/ScopeFrame.h"
#include "analysis/SecondHistory.h"   // ===== 55 =====
#include "analysis/SpectrogramRing.h"
#include "analysis/SpectrumFrame.h"
#include "analysis/StereoBandsFrame.h"
#include "analysis/TripleBuffer.h"
#include "analysis/modules/Cqt.h"
#include "analysis/modules/Field.h"   // ===== 53 =====
#include "analysis/modules/Loudness.h"
#include "analysis/modules/Spectrum.h"
#include "analysis/modules/Stereo.h"
#include "analysis/modules/StereoBands.h"
// ===== 54: TONAL BALANCE (lente 12) =====
#include "analysis/FileAnalysis.h"
#include "analysis/ReferenceFrame.h"
#include "analysis/modules/Reference.h"

// ========================================================================================================
// AnalysisThread — el worker de TELESCOPE. Drena el AnalysisBus, arma HOPS de 100 ms, corre SÓLO los
// módulos habilitados (lente a demanda) y publica un AnalysisFrame latest-wins + un punto de historia.
//
// Nunca toca el audio thread ni el message thread: el bus es lock-free y el frame va por TripleBuffer.
// En PAUSA sigue drenando pero DESCARTA (así el bus no se desborda y el contador de descartes no miente);
// el frame publicado queda congelado, que es lo que el usuario ve.
// ========================================================================================================
namespace telescope
{
class AnalysisThread : public juce::Thread
{
public:
    // `scopeOut` va aparte del frame a propósito: son ~40 KB de muestras que sólo SCOPE dibuja
    // (ver analysis/ScopeFrame.h). Es opcional para que los tests puedan armar el thread sin él.
    AnalysisThread (AnalysisBus& busIn, TripleBuffer<AnalysisFrame>& out, LoudnessHistory& hist,
                    TripleBuffer<ScopeFrame>* scopeOut = nullptr, ClipHistory* clipHist = nullptr,
                    TripleBuffer<SpectrumFrame>* spectrumOut = nullptr,
                    SpectrogramRing* spectrogramOut = nullptr,
                    TripleBuffer<StereoBandsFrame>* bandsOut = nullptr,
                    StereoSpectrogramRing* stereoSpectrogramOut = nullptr,
                    TripleBuffer<CqtFrame>* cqtOut = nullptr);
    ~AnalysisThread() override;

    void prepare (double sampleRate);   // (re)arranca el thread con el sample rate del host
    // Para el worker y ESPERA a que salga (releaseResources); idempotente. Devuelve si salió dentro del
    // plazo de gracia: quien lo llama tiene que poder saberlo antes de tocar los buffers del worker (M-1).
    bool stop();

    void requestReset() noexcept   { resetRequested.store (true, std::memory_order_release); notify(); }
    void setPaused (bool p) noexcept { paused.store (p, std::memory_order_release); }
    bool isPaused() const noexcept   { return paused.load (std::memory_order_acquire); }

    void setEnabledModules (juce::uint32 mask) noexcept { enabledMask.store (mask, std::memory_order_release); }
    juce::uint32 enabledModules() const noexcept { return enabledMask.load (std::memory_order_acquire); }

    // Ventana de integración del módulo Stereo (100 / 300 / 1 000 ms). Setting de la lente SCOPE: viaja
    // por atomic, no por parámetro (no tiene sentido automatizar "integrá 300 ms en vez de 100").
    void setStereoWindowMs (int ms) noexcept { stereoWindowMs.store (ms, std::memory_order_release); }
    int  stereoWindowMs_() const noexcept    { return stereoWindowMs.load (std::memory_order_acquire); }

    // Umbral de clip en dBTP (setting de la lente DYNAMICS). Cambiarlo reinicia el conteo: ver README.
    void  setClipThresholdDbtp (float dbtp) noexcept { clipThreshold.store (dbtp, std::memory_order_release); }
    float clipThresholdDbtp_() const noexcept        { return clipThreshold.load (std::memory_order_acquire); }

    // Settings del módulo Spectrum (los de la lente SPECTRUM y los de SPECTROGRAM). Viajan por atomics,
    // uno por campo: si alguien cambia dos settings a la vez, el peor caso es que UN frame se calcule con
    // una mezcla de los dos estados. Es un cambio que hizo una persona con el mouse, no una carrera.
    void setSpectrumSettings (const Spectrum::Settings& s) noexcept;
    Spectrum::Settings spectrumSettings() const noexcept;

    // Ventana de integración del estéreo POR BANDA (0.3 / 1 / 3 s, por índice). Setting de las lentes
    // BAND CORRELATION y STEREO SPECTROGRAM; viaja por atomic, igual que el resto de los settings de lente.
    void setBandsWindowIndex (int i) noexcept { bandsWindowIdx.store (i, std::memory_order_release); }
    int  bandsWindowIndex_() const noexcept   { return bandsWindowIdx.load (std::memory_order_acquire); }

    // Settings del módulo Cqt (los de las lentes CQT y SPIRAL). Viajan por atomics, uno por campo, igual
    // que los del espectro. `holdDecayDbPerSec` NO viaja acá: es el MISMO setting de SPECTRUM y se lee de
    // sus atomics — dos decaimientos distintos para el mismo gesto serían dos gestos.
    void setCqtSettings (const Cqt::Settings& s) noexcept;
    Cqt::Settings cqtSettings() const noexcept;

    // Lo que el módulo está corriendo AHORA (tamaño de FFT efectivo, tasa de frames, decimación): lo lee
    // la lente para rotular sus ejes sin tener que recalcularlo por su cuenta.
    double spectrumEmitRate() const noexcept { return emitRateHz.load (std::memory_order_acquire); }

    // ===== 53: el módulo Field (lente 11) =====
    //
    // Su TripleBuffer vive ACÁ y no en el processor por la misma razón por la que el anillo del
    // espectrograma estéreo vive en el heap: el TelescopeProcessor se construye EN LA PILA en los tests, y
    // un FieldFrame son 74 KB × 3 slots. Y mantenerlo adentro deja el constructor del AnalysisThread —
    // que ya tiene nueve argumentos y lo tocan varios prompts a la vez — sin cambiar una coma.
    //
    // La constante de tiempo del decaimiento (0.5 / 1 / 2 s por índice) viaja por atomic como todos los
    // settings de lente: no tiene sentido automatizar "olvidate en un segundo en vez de en dos".
    void setFieldDecayIndex (int i) noexcept { fieldDecayIdx.store (i, std::memory_order_release); }
    int  fieldDecayIndex_() const noexcept   { return fieldDecayIdx.load (std::memory_order_acquire); }
    TripleBuffer<FieldFrame>& fieldFrames_() noexcept { return *fieldFrames; }
    // ========================================================================================================
    // ===== 54: TONAL BALANCE · la referencia =====
    //
    // El frame de referencia sale por su propio TripleBuffer, que se ENGANCHA en vez de pasarse al
    // constructor: el ctor ya tiene nueve parámetros y agregarle un décimo cambiaría una firma que usan
    // el processor y los tests. Sin engancharlo, el módulo funciona igual y no publica nada.
    void setReferenceOutput (TripleBuffer<ReferenceFrame>* p) noexcept { refFrames = p; }

    // ===== 55: el ring de la historia por segundo (lente 13). Se engancha igual que el de arriba.
    void setSecondHistory (SecondHistory* p) noexcept { secondHistory = p; }

    // La carga y el borrado los hace el message thread (el usuario soltó un archivo); adentro, el módulo
    // los protege con su propia CriticalSection. Ver analysis/modules/Reference.h.
    void setReference (const FileAnalysis& a) { reference.setReference (a); }
    void clearReference()                     { reference.clearReference(); }
    bool hasReference() const                 { return reference.hasReference(); }
    // ========================================================================================================

    void run() override;

private:
    void doReset();
    void processHop();
    void feedSpectrum (int n);   // el espectro come del CHUNK drenado, no del hop

    AnalysisBus&                 bus;
    TripleBuffer<AnalysisFrame>& frames;
    LoudnessHistory&             history;
    TripleBuffer<ScopeFrame>*    scopeFrames = nullptr;
    ClipHistory*                 clipHistory = nullptr;
    TripleBuffer<SpectrumFrame>* spectrumFrames = nullptr;
    SpectrogramRing*             spectrogramRing = nullptr;
    TripleBuffer<StereoBandsFrame>* bandsFrames = nullptr;
    StereoSpectrogramRing*          stereoRing  = nullptr;
    TripleBuffer<CqtFrame>*         cqtFrames   = nullptr;

    double sr = 48000.0;
    int    hopSamples = 4800;          // 100 ms
    double analysedSeconds = 0.0;

    std::vector<float> chunkL, chunkR;   // lo que se saca del bus de una
    std::vector<float> hopL, hopR;       // el hop que se está llenando
    int                hopFill = 0;

    Loudness loudness;   // sólo corre si kLoudness está en la máscara (lente a demanda)
    Stereo   stereo;     // ídem con kStereo
    // El espectro NO se alimenta por hops de 100 ms: se le pasa lo que se drena del bus (cada ≤ 2 ms),
    // y él decide sus propias posiciones de frame. Ver el comentario de run().
    Spectrum spectrum;
    // El estéreo POR BANDA cuelga del espectro: come sus complejos L/R por el FrameSink, no vuelve a
    // transformar nada. Sólo se engancha cuando kStereoBands está en la máscara.
    StereoBands stereoBands;
    // El CONSTANT-Q come del hop de 100 ms como el medidor y el estéreo (no del chunk, como el espectro):
    // su cadencia ES la del hop. Sólo corre con kCqt en la máscara, y sus kernels se construyen recién en
    // el primer frame — o sea recién cuando alguien abre CQT o SPIRAL.
    Cqt cqt;

    // ===== 53: el CAMPO cuelga del estéreo por banda (su FieldSink), que a su vez cuelga del espectro.
    // En el heap los dos: el frame de campo son 74 KB y el módulo lo tiene adentro (ver el comentario de
    // fieldFrames_() arriba).
    std::unique_ptr<Field>                    field;
    std::unique_ptr<TripleBuffer<FieldFrame>> fieldFrames;

    // Acumulador del ring de 1 Hz: diez hops de 100 ms hacen un segundo exacto.
    juce::uint32 clipsThisSecond = 0;
    int          hopsThisSecond  = 0;

    // La máscara con la que se corrió el hop ANTERIOR: sirve para detectar el flanco de subida de cada
    // módulo y limpiarle la ventana deslizante al reactivarse (ver processHop).
    juce::uint32 lastEnabledMask = 0;

    std::atomic<bool>         resetRequested { false };
    std::atomic<bool>         paused         { false };
    std::atomic<juce::uint32> enabledMask    { kLoudness };
    std::atomic<int>          stereoWindowMs { Stereo::kDefaultWindowMs };
    std::atomic<int>          bandsWindowIdx { StereoBands::kDefaultWindowIndex };
    std::atomic<float>        clipThreshold  { Loudness::kDefaultClipThresholdDbtp };

    // Settings de Spectrum, uno por atomic (ver setSpectrumSettings).
    struct SpectrumAtomics
    {
        std::atomic<int>   fftOrder { 12 }, window { 0 }, overlapIndex { 1 }, channel { Spectrum::leftRight };
        std::atomic<float> slopeDbPerOct { 3.0f };
        std::atomic<int>   avgMode { Spectrum::avgExp };
        std::atomic<float> avgSeconds { 0.5f };
        std::atomic<bool>  peakHold { true };
        std::atomic<float> holdDecayDbPerSec { 12.0f };
        std::atomic<int>   bandsMode { 0 }, rangeDbIndex { 1 }, historySecIndex { 1 };
    } spectrumAtomics;

    // Settings de Cqt, uno por atomic (mismo criterio que los de Spectrum).
    struct CqtAtomics
    {
        std::atomic<int> channel { Cqt::mid }, chromaSecIndex { Cqt::kDefaultChromaSecIndex };
    } cqtAtomics;

    juce::uint32 lastCqtEmitted = 0;

    std::atomic<double> emitRateHz { 0.0 };   // frames de espectro por segundo que el módulo está emitiendo
    juce::uint32        lastSpectrumEmitted = 0;
    bool                spectrumWasOn = false;
    juce::uint32        lastBandsEmitted = 0;
    bool                bandsWasOn = false;

    // ===== 53 =====
    std::atomic<int> fieldDecayIdx { Field::kDefaultDecayIndex };
    juce::uint32     lastFieldEmitted = 0;
    bool             fieldWasOn = false;
    // ========================================================================================================
    // ===== 54: TONAL BALANCE · 55 (1c): LA REFERENCIA TIENE SU PROPIA FFT =====
    //
    // Hasta el 54 el lado VIVO de TONAL BALANCE colgaba del FrameSink de la instancia principal de
    // `Spectrum` — la que corre con los settings que el usuario eligió en la lente SPECTRUM. Eso hacía
    // que la curva viva cambiara de geometría cuando alguien tocaba el tamaño de FFT, la ventana o el
    // solape, MIENTRAS el lado archivo se analiza siempre con settings fijos (orden 12 · Hann · 75 % ·
    // L+R). Las dos curvas quedaban medidas con dos resoluciones distintas y nada en pantalla lo decía:
    // la comparación divergía en silencio, que es la peor forma de estar mal (MEDIUM del revisor del 54).
    //
    // Ahora el módulo tiene SU PROPIA instancia, clavada en los settings fijos del análisis de archivo.
    // Es una FFT de 4 096 más —46.9 frames/s con 75 % de solape, dos canales— y se paga SÓLO con
    // kReference encendido, o sea con TONAL BALANCE o VERDICT a la vista. A cambio: el vivo y el archivo
    // miden con la MISMA geometría siempre, y el analizador de espectro queda libre para ser lo que la
    // lente 3 quiera sin arrastrar a la 12 y a la 13.
    //
    // `feedReference` le pasa el chunk (no el hop: decide sus propias posiciones de frame, igual que la
    // instancia principal) y detecta el flanco de subida de kReference para arrancar limpio.
    // `publishReference` arma y publica el ReferenceFrame una vez por hop.
    void feedReference (int n, juce::uint32 mask);
    void publishReference();

    Reference                     reference;
    // ===== 55: la historia POR SEGUNDO. El constructor NO cambia (ya tiene diez parámetros y lo tocan
    // varios prompts a la vez): el ring se ENGANCHA, como el TripleBuffer de la referencia. Sin
    // engancharlo el motor funciona igual y no publica filas.
    SecondBuilder                 secondBuilder;
    SecondHistory*                secondHistory = nullptr;
    // Muestras que ya se le pasaron al espectro desde el RESET (la posición absoluta del stream).
    long long                     samplesFed = 0;
    // Lo que la fila del segundo necesita del medidor y no viaja en el AnalysisFrame (se vuelca en el
    // mismo hop en que se calcula, así que nunca se mezcla con el de otro).
    juce::uint32                  lastHopClips = 0;
    double                        lastHopDcL = 0.0, lastHopDcR = 0.0;
    float                         lastDcL = 0.0f, lastDcR = 0.0f;
    Spectrum                      refSpectrum;   // settings FIJOS (Spectrum::Settings{}), nunca los de la lente
    FrameSinkFanout               sinkFanout;    // los consumidores del espectro de la LENTE
    FrameSinkFanout               refFanout;     // los consumidores del espectro FIJO
    TripleBuffer<ReferenceFrame>* refFrames = nullptr;
    bool                          referenceWasOn = false;
    // ========================================================================================================

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnalysisThread)
};
}
