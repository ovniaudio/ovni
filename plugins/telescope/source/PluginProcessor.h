#pragma once
#include <atomic>
#include <memory>
#include "template/PluginProcessorBase.h"
#include "analysis/AnalysisBus.h"
#include "analysis/AnalysisFrame.h"
#include "analysis/AnalysisThread.h"
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
#include "analysis/modules/Spectrum.h"
#include "analysis/modules/StereoBands.h"
// ===== 54: TONAL BALANCE (lente 12) =====
#include "analysis/FileAnalyzer.h"
#include "analysis/ReferenceFrame.h"

// ========================================================================================================
// TELESCOPE — el analizador espacial del sello OVNI (9º plugin).
//
// NO procesa audio. `processAudio` sólo LEE el buffer y empuja L/R al AnalysisBus; el buffer sale
// bit-exacto (verificado en PassThroughTest.cpp). El chasis, con sus params de utilidad en default
// (inGain/output 0 dB, monoSafe off), tampoco altera nada: las rampas de ganancia son identidad y el
// bass-mono se saltea.
//
// El análisis vive en OTRO thread (AnalysisThread): el audio nunca espera por una FFT.
// ========================================================================================================
namespace telescope
{
class TelescopeProcessor : public ovni::PluginProcessorBase
{
public:
    TelescopeProcessor();
    ~TelescopeProcessor() override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorEditor* createEditor() override;
    void releaseResources() override;
    // Se re-lee la configuración de lentes del ValueTree y se empuja al motor (ver setLensDefaults()).
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ==== lo que lee la lente (message thread) ====
    TripleBuffer<AnalysisFrame>& analysis() noexcept       { return frames; }
    TripleBuffer<ScopeFrame>&    scope() noexcept          { return scopeFrames; }
    TripleBuffer<SpectrumFrame>& spectrum() noexcept       { return spectrumFrames; }
    const SpectrogramRing&       spectrogram() const noexcept { return spectrogramRing; }
    TripleBuffer<StereoBandsFrame>& stereoBands() noexcept    { return *bandsFrames; }
    TripleBuffer<CqtFrame>&      cqt() noexcept              { return *cqtFrames; }
    const StereoSpectrogramRing& stereoSpectrogram() const noexcept { return *stereoRing; }
    // ===== 53: la grilla del campo (lente 11). Vive dentro del AnalysisThread, en el heap: ver
    // AnalysisThread::fieldFrames_(). =====
    TripleBuffer<FieldFrame>&    field() noexcept { return analysisThread.fieldFrames_(); }
    const LoudnessHistory&       loudnessHistory() const noexcept { return history; }
    const ClipHistory&           clipHistory() const noexcept     { return clips; }
    // ===== 55: la historia POR SEGUNDO (lente VERDICT). 10 minutos a 1 Hz desde el último RESET.
    const SecondHistory&         secondHistory() const noexcept   { return seconds; }

    // ==== control del análisis (editor → processor; atomics, NO params) ====
    void resetAnalysis() noexcept                    { analysisThread.requestReset(); }
    void setAnalysisPaused (bool p) noexcept         { analysisThread.setPaused (p); }
    bool isAnalysisPaused() const noexcept           { return analysisThread.isPaused(); }
    // Lente a demanda: el editor fija la máscara según la lente visible (lo que no se ve, no se calcula).
    void setEnabledModules (juce::uint32 mask) noexcept { analysisThread.setEnabledModules (mask); }
    juce::uint32 enabledModules() const noexcept        { return analysisThread.enabledModules(); }

    // ==== VENTANAS VIVAS (lo que cierra la lente a demanda por el otro extremo) ====
    // Sin esto, cerrar la ventana con SPECTRUM/SPECTROGRAM/SCOPE a la vista dejaba el módulo caro
    // corriendo PARA SIEMPRE (FFT de hasta orden 15 a ~375 frames/s) sin nadie mirando — el costo justo
    // que la lente a demanda existe para no pagar (HIGH del revisor del 50).
    //
    // Se CUENTAN los editores porque algunos hosts abren dos ventanas del mismo plugin: cerrar una no
    // puede apagarle el módulo a la otra. El contador es atómico (los editores viven en el message
    // thread, pero el motor lee la máscara desde el suyo).
    void editorOpened() noexcept { liveEditors.fetch_add (1, std::memory_order_acq_rel); }
    void editorClosed() noexcept
    {
        if (liveEditors.fetch_sub (1, std::memory_order_acq_rel) <= 1)
            setEnabledModules (kAlwaysOnModules);
    }
    int liveEditorCount() const noexcept { return liveEditors.load (std::memory_order_acquire); }

    // ==== SETTINGS DE LENTE ====
    // No son parámetros: viven como propiedades del ValueTree del APVTS (persisten con el estado y los
    // presets, el host NO los lista ni los automatiza — no tiene sentido automatizar "integrá 300 ms").
    // El setter escribe el árbol Y empuja el valor al motor por atomic; setStateInformation los re-sincroniza.
    static constexpr const char* kStereoWindowMs = "stereoWindowMs";
    static constexpr const char* kScopePolar     = "scopePolar";
    static constexpr const char* kScopeTrigger   = "scopeTrigger";
    static constexpr const char* kClipThreshold  = "clipThresholdDbtp";
    // Estéreo por banda (lentes 9 y 10): la ventana de integración y qué fila secundaria muestra
    // BAND CORRELATION (0 = MONO LOSS · 1 = WIDTH · 2 = BALANCE).
    static constexpr const char* kBandsWindow    = "bandsWindowIndex";
    static constexpr const char* kBandsRow       = "bandsRow";

    int  stereoWindowMs() const;
    void setStereoWindowMs (int ms);
    bool scopePolar() const;
    void setScopePolar (bool on);
    bool scopeTrigger() const;
    void setScopeTrigger (bool on);
    // Umbral de "clip" en dBTP (-3…0, paso 0.1). Cambiarlo REINICIA el conteo: un contador que mezcla
    // eventos medidos contra dos techos distintos no querría decir nada (ver README).
    float clipThresholdDbtp() const;
    void  setClipThresholdDbtp (float dbtp);

    int   bandsWindowIndex() const;
    void  setBandsWindowIndex (int i);
    float bandsWindowSec() const { return StereoBands::kWindowSecOptions[bandsWindowIndex()]; }
    int   bandsRow() const;
    void  setBandsRow (int row);

    // CONSTANT-Q (lentes 6 y 7): el canal analizado (L / R / M) y el suavizado del cromagrama. El
    // decaimiento del peak hold NO está acá: es el mismo setting de SPECTRUM (ver AnalysisThread).
    static constexpr const char* kCqtChannel   = "cqtChannel";
    static constexpr const char* kCqtChromaSec = "cqtChromaSec";

    Cqt::Settings cqtSettings() const;
    void          setCqtSettings (const Cqt::Settings& s);
    // ===== 53: settings de las dos lentes 3D =====
    //
    // WATERFALL sólo tiene settings de VISTA (cuántas líneas y cuánta inclinación): no tocan el motor, así
    // que no viajan por atomic — el módulo Spectrum ya está corriendo igual, y lo único que cambia es cómo
    // se dibujan las columnas que el anillo ya tiene. FIELD sí tiene uno de MOTOR (la constante de tiempo
    // del decaimiento), que empuja al worker como todos los demás.
    static constexpr const char* kWaterfallLines = "waterfallLinesIndex";
    static constexpr const char* kWaterfallTilt  = "waterfallTiltIndex";
    static constexpr const char* kFieldDecay     = "fieldDecayIndex";

    static constexpr int kNumWaterfallLineOptions = 3;
    static constexpr int kWaterfallLineOptions[kNumWaterfallLineOptions] = { 60, 90, 120 };
    static constexpr int kDefaultWaterfallLinesIndex = 1;    // 90 líneas
    static constexpr int kNumWaterfallTiltOptions = 3;
    // 57b — la MÁS INCLINADA por default (era la del medio). «Waterfall […] por default muy inclinado»
    // (Joaquín, 9-sep). El prompt pedía agregar una cuarta opción de tilt 0.55 / depth 0.30 y hacerla
    // default; la opción 2 que ya existía ES tilt 0.55 / depth 0.32, o sea la misma a dos decimales.
    // Agregarla habría dejado dos entradas indistinguibles en un botón que CICLA, así que lo que se hace
    // es mover el default a la que ya estaba. El resultado que se pidió —el waterfall abre muy
    // inclinado— es el mismo. Ver WaterfallLens::kTiltOptions.
    static constexpr int kDefaultWaterfallTiltIndex = 2;

    int  waterfallLinesIndex() const;
    void setWaterfallLinesIndex (int i);
    int  waterfallLines() const { return kWaterfallLineOptions[waterfallLinesIndex()]; }
    int  waterfallTiltIndex() const;
    void setWaterfallTiltIndex (int i);

    int   fieldDecayIndex() const;
    void  setFieldDecayIndex (int i);
    float fieldDecaySec() const { return Field::kDecaySecOptions[fieldDecayIndex()]; }

    // ===== 57b: LA PALETA DE LOS MAPAS DE CALOR =====
    //
    // UNA sola propiedad para las CUATRO lentes en las que el color codifica nivel (SPECTROGRAM, STEREO
    // SPECTROGRAM, WATERFALL, FIELD), por el mismo motivo que `language` es una sola para las trece: son
    // la misma decisión del usuario mirada desde cuatro lentes, y cuatro settings separados sólo darían
    // cuatro maneras de que el instrumento se vea desparejo consigo mismo.
    //
    // No toca el motor: es de VISTA pura, como los dos de WATERFALL.
    static constexpr const char* kPalette = "paletteIndex";
    static constexpr int kDefaultPaletteIndex = 1;   // inferno (ver lenses/Palettes.h)

    int  paletteIndex() const;
    void setPaletteIndex (int i);

    // ===== 57b: EL SUAVIZADO DE PANTALLA DE SPECTRUM =====
    //
    // Es de DIBUJO, no de análisis: promedia la curva que se pinta sobre un ancho fijo en OCTAVAS (como
    // SPAN), y no toca ni un bin. Los números del readout, el peak hold y los tests de bin siguen leyendo
    // el dato crudo — un analizador que suaviza el número que reporta deja de servir para medir.
    //
    // Vive en el ValueTree y no en `Spectrum::Settings` justamente porque no viaja al motor.
    static constexpr const char* kSpectrumSmooth = "spectrumSmoothIndex";
    static constexpr int kNumSpectrumSmoothOptions = 4;
    // 0 = sin suavizado. El resto, el DENOMINADOR de la fracción de octava.
    static constexpr int kSpectrumSmoothDenom[kNumSpectrumSmoothOptions] = { 0, 24, 12, 6 };
    static constexpr int kDefaultSpectrumSmoothIndex = 2;   // 1/12 de octava

    int  spectrumSmoothIndex() const;
    void setSpectrumSmoothIndex (int i);

    // Los ONCE settings del módulo Spectrum viajan JUNTOS, en un solo struct: son un estado coherente
    // (cambiar el tamaño de FFT sin cambiar el solape cambia la tasa de frames), y once pares de
    // getter/setter serían once oportunidades de olvidarse uno.
    static constexpr const char* kSpectrumFftOrder     = "spectrumFftOrder";
    static constexpr const char* kSpectrumWindow       = "spectrumWindow";
    static constexpr const char* kSpectrumOverlap      = "spectrumOverlap";
    static constexpr const char* kSpectrumChannel      = "spectrumChannel";
    static constexpr const char* kSpectrumSlope        = "spectrumSlopeDbPerOct";
    static constexpr const char* kSpectrumAvgMode      = "spectrumAvgMode";
    static constexpr const char* kSpectrumAvgSeconds   = "spectrumAvgSeconds";
    static constexpr const char* kSpectrumPeakHold     = "spectrumPeakHold";
    static constexpr const char* kSpectrumHoldDecay    = "spectrumHoldDecayDbPerSec";
    static constexpr const char* kSpectrumBandsMode    = "spectrumBandsMode";
    static constexpr const char* kSpectrumRange        = "spectrumRangeDb";
    static constexpr const char* kSpectrogramHistory   = "spectrogramHistorySec";

    Spectrum::Settings spectrumSettings() const;
    void               setSpectrumSettings (const Spectrum::Settings& s);

    // Frames de espectro por segundo que el motor está emitiendo de verdad (la lente rotula con esto).
    double spectrumEmitRate() const noexcept { return analysisThread.spectrumEmitRate(); }

    // Índice del objetivo de plataforma vigente (ver source/data/StreamingTargets.h). Lo lee la lente.
    int targetIndex() const noexcept
    {
        const auto* p = apvts.getRawParameterValue ("target");
        return p != nullptr ? (int) std::lround (p->load()) : 0;
    }

    // ========================================================================================================
    // ===== 54: TONAL BALANCE · la referencia =====
    //
    // El PATH del archivo persiste en el estado (una propiedad del ValueTree, como el resto de los settings
    // de lente). Al restaurar, si el archivo sigue estando se RE-ANALIZA; si no está, la lente dice
    // "referencia no encontrada: <nombre>" y no dibuja ninguna curva. Nunca se inventa una: guardar 30
    // números en el preset y mostrarlos como si fueran el archivo sería mostrar una referencia que ya no
    // existe, y nadie podría notarlo.
    static constexpr const char* kRefPath = "referencePath";

    TripleBuffer<ReferenceFrame>& reference() noexcept { return refFrames; }

    // Carga por API. La elección de archivo (FileChooser) la hace la lente de forma ASÍNCRONA y termina
    // llamando acá: nada modal, y los tests cargan una referencia sin fabricar diálogos.
    void loadReference (const juce::File& f);
    void clearReference();

    juce::String referencePath() const { return apvts.state.getProperty (kRefPath, juce::String()).toString(); }
    // Nombre del archivo que el estado pide y NO está. Vacío si no falta nada.
    juce::String referenceMissingName() const { return refMissing; }

    bool         referenceBusy() const noexcept     { return fileAnalyzer != nullptr && fileAnalyzer->busy(); }
    float        referenceProgress() const noexcept { return fileAnalyzer != nullptr ? fileAnalyzer->progress() : 0.0f; }
    juce::String referenceName() const;
    // El error del último análisis (formato no reconocido, archivo ilegible…). Vacío si salió bien.
    juce::String referenceError() const;
    juce::String referenceWarning() const;
    const FileAnalysis referenceAnalysis() const;
    // ========================================================================================================
    // ===== 55: VERDICT (lente 13) =====
    //
    // TRES settings, y ninguno es un parámetro: viven en el ValueTree como el resto de los de lente.
    //   · `language`  código ISO 639-1 (D-50: default "en", y todos los idiomas que tenga la tabla).
    //   · `mode`      0 = EN VIVO (desde el RESET) · 1 = ARCHIVO.
    //   · `filePath`  el archivo del modo ARCHIVO. Persiste; al restaurar se RE-ANALIZA si sigue estando.
    //
    // EL ANALIZADOR DE VERDICT ES OTRO, no el de la referencia de TONAL BALANCE. Es la misma CLASE
    // (FileAnalyzer, ver spec §5.7) con su propio thread y sus propias instancias del motor: cargar un
    // tema para que VERDICT lo analice NO puede pisarle al usuario la referencia que eligió para comparar
    // el tilt. Son dos cosas distintas que la UI pone en dos lentes distintas.
    static constexpr const char* kVerdictLanguage = "language";   // UNA sola propiedad para las 13 lentes (D-50): la misma que lee strings::languageOf()
    static constexpr const char* kVerdictMode     = "verdictMode";
    static constexpr const char* kVerdictFilePath = "verdictFilePath";

    enum VerdictMode { verdictLive = 0, verdictFile };

    juce::String verdictLanguage() const;
    void         setVerdictLanguage (const juce::String& code);
    int          verdictMode() const;
    void         setVerdictMode (int mode);

    void         loadVerdictFile (const juce::File& f);
    void         clearVerdictFile();
    juce::String verdictFilePath() const { return apvts.state.getProperty (kVerdictFilePath, juce::String()).toString(); }
    juce::String verdictFileName() const;
    juce::String verdictFileError() const;
    bool         verdictFileBusy() const noexcept     { return verdictAnalyzer != nullptr && verdictAnalyzer->busy(); }
    float        verdictFileProgress() const noexcept { return verdictAnalyzer != nullptr ? verdictAnalyzer->progress() : 0.0f; }
    juce::uint32 verdictFileRevision() const noexcept { return verdictAnalyzer != nullptr ? verdictAnalyzer->revision() : 0; }
    const FileAnalysis verdictAnalysis() const;
    // ========================================================================================================

protected:
    // TELESCOPE mide, no procesa: acá no se escribe una sola muestra del buffer.
    void processAudio (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    void prepareEngine (const juce::dsp::ProcessSpec& spec) override;

private:
    // Fija los defaults que falten en el ValueTree y empuja TODOS los settings al motor. Se llama al
    // construir y después de cada setStateInformation.
    void syncLensSettings();

    AnalysisBus                 bus;
    TripleBuffer<AnalysisFrame> frames;
    TripleBuffer<ScopeFrame>    scopeFrames;
    TripleBuffer<SpectrumFrame> spectrumFrames;
    SpectrogramRing             spectrogramRing;
    // EN EL HEAP, no como miembros directos: el anillo del espectrograma estéreo son 3.7 MB (dos bytes por
    // celda) y el frame de coherencia por bin otros 0.4, y el processor se construye EN LA PILA en los
    // tests. Con todo adentro el objeto pasaba de 8 MB y el test se caía con SIGSEGV antes de la primera
    // aserción — un desbordamiento de pila que no dice nada sobre el código que se quería probar.
    std::unique_ptr<TripleBuffer<StereoBandsFrame>> bandsFrames;
    std::unique_ptr<StereoSpectrogramRing>          stereoRing;
    // El frame del CQT son ~4 KB por slot: chico, pero va al heap por el mismo motivo que los otros dos
    // (el processor se construye EN LA PILA en los tests).
    std::unique_ptr<TripleBuffer<CqtFrame>>         cqtFrames;
    LoudnessHistory             history;
    ClipHistory                 clips;
    SecondHistory               seconds;   // ===== 55: ~228 KB, el precio de poder decir "entre 0:20 y 0:35"
    AnalysisThread              analysisThread { bus, frames, history, &scopeFrames, &clips,
                                                 &spectrumFrames, &spectrogramRing,
                                                 bandsFrames.get(), stereoRing.get(), cqtFrames.get() };

    std::vector<float> monoScratch;   // canal derecho sintético cuando la fuente es mono (1 canal)

    std::atomic<int> liveEditors { 0 };   // ventanas abiertas (ver editorOpened/editorClosed)

    // ========================================================================================================
    // ===== 54: TONAL BALANCE =====
    // El analizador va DESPUÉS de analysisThread en la declaración a propósito: los miembros se destruyen
    // en orden inverso, así que su thread se junta ANTES de que muera el thread de análisis al que le
    // empuja el resultado.
    // Crea el analizador si hace falta y (re)dispara el análisis de lo que diga el estado.
    void syncReference();

    TripleBuffer<ReferenceFrame>  refFrames;
    std::unique_ptr<FileAnalyzer> fileAnalyzer;
    // ===== 55: el analizador de VERDICT, aparte del de la referencia (ver el comentario de arriba).
    void                          syncVerdictFile();
    std::unique_ptr<FileAnalyzer> verdictAnalyzer;
    juce::String                  refMissing;   // nombre del archivo que el estado pide y no está
    // ========================================================================================================

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TelescopeProcessor)
};
}
