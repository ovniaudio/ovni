#include "PluginProcessor.h"
#include "data/Rules.h"   // ===== 55: los idiomas de VERDICT (D-50) =====
#include "PluginEditor.h"
#include "lenses/LensIds.h"
#include "lenses/Palettes.h"   // ===== 57b: look::kNumPalettes =====
#include "analysis/modules/Loudness.h"
#include "analysis/modules/Stereo.h"
#include "analysis/modules/StereoBands.h"
#include "data/StreamingTargets.h"
#include "presets/PresetTypes.h"

namespace telescope
{
juce::AudioProcessorValueTreeState::ParameterLayout TelescopeProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // `bypass` lo declara cada plugin (el chasis sólo lo LEE). inGain/output/monoSafe los inyecta la base
    // y quedan FUERA de la UI de TELESCOPE (el host igual los lista: limitación conocida del chasis).
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "bypass", 1 }, "Bypass", false));

    // LENTE. Las 13 desde el día uno, en el orden del spec §2: el índice es CONTRATO (estado y presets
    // guardados con una lente tienen que seguir abriendo esa lente cuando estén las 13 construidas).
    juce::StringArray lensNames;
    // Las tres tablas de abajo son `const char*` con UTF-8 crudo: siempre por fromUTF8 (String (const char*)
    // decodifica byte a byte y "Loudness · libre" salía "Loudness Â· libre" en el host; PresetNamesTest.cpp).
    for (const auto* n : kLensNames) lensNames.add (juce::String::fromUTF8 (n));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "lens", 1 },
                                                              "Lens", lensNames, 0));

    // OBJETIVO de plataforma (ver data/StreamingTargets.h y su nota de honestidad).
    juce::StringArray targetNames;
    for (const auto& t : kStreamingTargets) targetNames.add (juce::String::fromUTF8 (t.name));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "target", 1 },
                                                              "Target", targetNames, 0));

    // PRESET de fábrica ("vistas": lente + objetivo), como SUPERNOVA.
    juce::StringArray presetNames;
    for (const auto& p : ovni::presets::factoryPresets()) presetNames.add (juce::String::fromUTF8 (p.name));
    if (presetNames.isEmpty()) presetNames.add ("Default");
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "preset", 1 },
                                                              "Preset", presetNames, 0));

    return layout;
}

TelescopeProcessor::TelescopeProcessor()
    : ovni::PluginProcessorBase ("TELESCOPE", createParameterLayout()),
      bandsFrames (std::make_unique<TripleBuffer<StereoBandsFrame>>()),
      stereoRing (std::make_unique<StereoSpectrogramRing>()),
      cqtFrames (std::make_unique<TripleBuffer<CqtFrame>>())
{
    syncLensSettings();
}

//======================================================================================= settings de lente
// Propiedades del ValueTree, no parámetros (ver la nota del header). El getter lee el árbol; el setter
// escribe el árbol Y empuja al motor. Así el estado guardado y lo que el worker usa nunca divergen.
int  TelescopeProcessor::stereoWindowMs() const
{
    return (int) apvts.state.getProperty (kStereoWindowMs, Stereo::kDefaultWindowMs);
}

void TelescopeProcessor::setStereoWindowMs (int ms)
{
    apvts.state.setProperty (kStereoWindowMs, ms, nullptr);
    analysisThread.setStereoWindowMs (ms);
}

bool TelescopeProcessor::scopePolar() const   { return (bool) apvts.state.getProperty (kScopePolar, false); }
void TelescopeProcessor::setScopePolar (bool on)   { apvts.state.setProperty (kScopePolar, on, nullptr); }

bool TelescopeProcessor::scopeTrigger() const { return (bool) apvts.state.getProperty (kScopeTrigger, true); }
void TelescopeProcessor::setScopeTrigger (bool on) { apvts.state.setProperty (kScopeTrigger, on, nullptr); }

float TelescopeProcessor::clipThresholdDbtp() const
{
    return (float) (double) apvts.state.getProperty (kClipThreshold, Loudness::kDefaultClipThresholdDbtp);
}

void TelescopeProcessor::setClipThresholdDbtp (float dbtp)
{
    const float v = juce::jlimit (Loudness::kMinClipThresholdDbtp, Loudness::kMaxClipThresholdDbtp, dbtp);
    apvts.state.setProperty (kClipThreshold, (double) v, nullptr);
    analysisThread.setClipThresholdDbtp (v);
}

// La ventana del estéreo por banda (0.3 / 1 / 3 s) y la fila secundaria de BAND CORRELATION.
int TelescopeProcessor::bandsWindowIndex() const
{
    return juce::jlimit (0, StereoBands::kNumWindowOptions - 1,
                         (int) apvts.state.getProperty (kBandsWindow, StereoBands::kDefaultWindowIndex));
}

void TelescopeProcessor::setBandsWindowIndex (int i)
{
    const int v = juce::jlimit (0, StereoBands::kNumWindowOptions - 1, i);
    apvts.state.setProperty (kBandsWindow, v, nullptr);
    analysisThread.setBandsWindowIndex (v);
}

int  TelescopeProcessor::bandsRow() const { return juce::jlimit (0, 2, (int) apvts.state.getProperty (kBandsRow, 0)); }
void TelescopeProcessor::setBandsRow (int row) { apvts.state.setProperty (kBandsRow, juce::jlimit (0, 2, row), nullptr); }

//------------------------------------------------------------------------------------ settings del CQT
// El decaimiento del peak hold sale de los settings de SPECTRUM: es la misma perilla, y el motor la lee
// de ahí (ver AnalysisThread::cqtSettings). Guardarla dos veces sería garantizar que algún día difieran.
Cqt::Settings TelescopeProcessor::cqtSettings() const
{
    Cqt::Settings d, s;
    s.channel           = (int) apvts.state.getProperty (kCqtChannel, d.channel);
    s.chromaSecIndex    = (int) apvts.state.getProperty (kCqtChromaSec, d.chromaSecIndex);
    s.holdDecayDbPerSec = spectrumSettings().holdDecayDbPerSec;
    s.sanitise();
    return s;
}

void TelescopeProcessor::setCqtSettings (const Cqt::Settings& s)
{
    Cqt::Settings v = s;
    v.sanitise();
    apvts.state.setProperty (kCqtChannel, v.channel, nullptr);
    apvts.state.setProperty (kCqtChromaSec, v.chromaSecIndex, nullptr);
    analysisThread.setCqtSettings (v);
}

//------------------------------------------------------------------ ===== 53: las dos lentes 3D =====
int TelescopeProcessor::waterfallLinesIndex() const
{
    return juce::jlimit (0, kNumWaterfallLineOptions - 1,
                         (int) apvts.state.getProperty (kWaterfallLines, kDefaultWaterfallLinesIndex));
}

void TelescopeProcessor::setWaterfallLinesIndex (int i)
{
    apvts.state.setProperty (kWaterfallLines, juce::jlimit (0, kNumWaterfallLineOptions - 1, i), nullptr);
}

int TelescopeProcessor::waterfallTiltIndex() const
{
    return juce::jlimit (0, kNumWaterfallTiltOptions - 1,
                         (int) apvts.state.getProperty (kWaterfallTilt, kDefaultWaterfallTiltIndex));
}

void TelescopeProcessor::setWaterfallTiltIndex (int i)
{
    apvts.state.setProperty (kWaterfallTilt, juce::jlimit (0, kNumWaterfallTiltOptions - 1, i), nullptr);
}

// La paleta de los mapas de calor (57b). De VISTA: no se le empuja nada al motor.
int TelescopeProcessor::paletteIndex() const
{
    return juce::jlimit (0, look::kNumPalettes - 1,
                         (int) apvts.state.getProperty (kPalette, kDefaultPaletteIndex));
}

void TelescopeProcessor::setPaletteIndex (int i)
{
    apvts.state.setProperty (kPalette, juce::jlimit (0, look::kNumPalettes - 1, i), nullptr);
}

// El suavizado de pantalla de SPECTRUM (57b). De DIBUJO: tampoco se le empuja nada al motor.
int TelescopeProcessor::spectrumSmoothIndex() const
{
    return juce::jlimit (0, kNumSpectrumSmoothOptions - 1,
                         (int) apvts.state.getProperty (kSpectrumSmooth, kDefaultSpectrumSmoothIndex));
}

void TelescopeProcessor::setSpectrumSmoothIndex (int i)
{
    apvts.state.setProperty (kSpectrumSmooth, juce::jlimit (0, kNumSpectrumSmoothOptions - 1, i), nullptr);
}

int TelescopeProcessor::fieldDecayIndex() const
{
    return juce::jlimit (0, Field::kNumDecayOptions - 1,
                         (int) apvts.state.getProperty (kFieldDecay, Field::kDefaultDecayIndex));
}

void TelescopeProcessor::setFieldDecayIndex (int i)
{
    const int v = juce::jlimit (0, Field::kNumDecayOptions - 1, i);
    apvts.state.setProperty (kFieldDecay, v, nullptr);
    analysisThread.setFieldDecayIndex (v);   // éste SÍ es de motor
}

//------------------------------------------------------------------------------------ settings de espectro
Spectrum::Settings TelescopeProcessor::spectrumSettings() const
{
    Spectrum::Settings d;   // los defaults del módulo son los defaults del árbol
    Spectrum::Settings s;
    s.fftOrder          = (int) apvts.state.getProperty (kSpectrumFftOrder, d.fftOrder);
    s.window            = (int) apvts.state.getProperty (kSpectrumWindow, d.window);
    s.overlapIndex      = (int) apvts.state.getProperty (kSpectrumOverlap, d.overlapIndex);
    s.channel           = (int) apvts.state.getProperty (kSpectrumChannel, d.channel);
    s.slopeDbPerOct     = (float) (double) apvts.state.getProperty (kSpectrumSlope, (double) d.slopeDbPerOct);
    s.avgMode           = (int) apvts.state.getProperty (kSpectrumAvgMode, d.avgMode);
    s.avgSeconds        = (float) (double) apvts.state.getProperty (kSpectrumAvgSeconds, (double) d.avgSeconds);
    s.peakHold          = (bool) apvts.state.getProperty (kSpectrumPeakHold, d.peakHold);
    s.holdDecayDbPerSec = (float) (double) apvts.state.getProperty (kSpectrumHoldDecay, (double) d.holdDecayDbPerSec);
    s.bandsMode         = (int) apvts.state.getProperty (kSpectrumBandsMode, d.bandsMode);
    s.rangeDbIndex      = (int) apvts.state.getProperty (kSpectrumRange, d.rangeDbIndex);
    s.historySecIndex   = (int) apvts.state.getProperty (kSpectrogramHistory, d.historySecIndex);
    s.sanitise();
    return s;
}

void TelescopeProcessor::setSpectrumSettings (const Spectrum::Settings& in)
{
    Spectrum::Settings s = in;
    s.sanitise();

    apvts.state.setProperty (kSpectrumFftOrder,  s.fftOrder, nullptr);
    apvts.state.setProperty (kSpectrumWindow,    s.window, nullptr);
    apvts.state.setProperty (kSpectrumOverlap,   s.overlapIndex, nullptr);
    apvts.state.setProperty (kSpectrumChannel,   s.channel, nullptr);
    apvts.state.setProperty (kSpectrumSlope,     (double) s.slopeDbPerOct, nullptr);
    apvts.state.setProperty (kSpectrumAvgMode,   s.avgMode, nullptr);
    apvts.state.setProperty (kSpectrumAvgSeconds, (double) s.avgSeconds, nullptr);
    apvts.state.setProperty (kSpectrumPeakHold,  s.peakHold, nullptr);
    apvts.state.setProperty (kSpectrumHoldDecay, (double) s.holdDecayDbPerSec, nullptr);
    apvts.state.setProperty (kSpectrumBandsMode, s.bandsMode, nullptr);
    apvts.state.setProperty (kSpectrumRange,     s.rangeDbIndex, nullptr);
    apvts.state.setProperty (kSpectrogramHistory, s.historySecIndex, nullptr);

    analysisThread.setSpectrumSettings (s);
}

void TelescopeProcessor::syncLensSettings()
{
    // Escribir los defaults que falten deja el árbol EXPLÍCITO: un estado guardado hoy sigue abriendo
    // igual mañana aunque cambie el default del código.
    if (! apvts.state.hasProperty (kStereoWindowMs)) apvts.state.setProperty (kStereoWindowMs, Stereo::kDefaultWindowMs, nullptr);
    if (! apvts.state.hasProperty (kScopePolar))     apvts.state.setProperty (kScopePolar, false, nullptr);
    if (! apvts.state.hasProperty (kScopeTrigger))   apvts.state.setProperty (kScopeTrigger, true, nullptr);
    if (! apvts.state.hasProperty (kClipThreshold))  apvts.state.setProperty (kClipThreshold, (double) Loudness::kDefaultClipThresholdDbtp, nullptr);
    if (! apvts.state.hasProperty (kBandsWindow))    apvts.state.setProperty (kBandsWindow, StereoBands::kDefaultWindowIndex, nullptr);
    if (! apvts.state.hasProperty (kBandsRow))       apvts.state.setProperty (kBandsRow, 0, nullptr);
    if (! apvts.state.hasProperty (kCqtChannel))    apvts.state.setProperty (kCqtChannel, Cqt::Settings{}.channel, nullptr);
    if (! apvts.state.hasProperty (kCqtChromaSec))  apvts.state.setProperty (kCqtChromaSec, Cqt::Settings{}.chromaSecIndex, nullptr);
    // ===== 53 =====
    if (! apvts.state.hasProperty (kWaterfallLines)) apvts.state.setProperty (kWaterfallLines, kDefaultWaterfallLinesIndex, nullptr);
    if (! apvts.state.hasProperty (kWaterfallTilt))  apvts.state.setProperty (kWaterfallTilt, kDefaultWaterfallTiltIndex, nullptr);
    if (! apvts.state.hasProperty (kFieldDecay))     apvts.state.setProperty (kFieldDecay, Field::kDefaultDecayIndex, nullptr);
    // ===== 57b =====
    if (! apvts.state.hasProperty (kPalette))        apvts.state.setProperty (kPalette, kDefaultPaletteIndex, nullptr);
    if (! apvts.state.hasProperty (kSpectrumSmooth))
        apvts.state.setProperty (kSpectrumSmooth, kDefaultSpectrumSmoothIndex, nullptr);

    analysisThread.setStereoWindowMs (stereoWindowMs());
    analysisThread.setClipThresholdDbtp (clipThresholdDbtp());
    analysisThread.setBandsWindowIndex (bandsWindowIndex());
    analysisThread.setFieldDecayIndex (fieldDecayIndex());   // ===== 53 =====
    setSpectrumSettings (spectrumSettings());   // escribe los defaults que falten Y empuja al motor
    setCqtSettings (cqtSettings());             // (va DESPUÉS: lee holdDecay de los del espectro)

    // ===== 54: TONAL BALANCE · engancha la salida y RE-ANALIZA la referencia del estado =====
    analysisThread.setReferenceOutput (&refFrames);
    analysisThread.setSecondHistory (&seconds);   // ===== 55 =====
    syncReference();

    // ===== 55: VERDICT =====
    if (! apvts.state.hasProperty (kVerdictLanguage))
        apvts.state.setProperty (kVerdictLanguage, juce::String (rules::kDefaultLanguage), nullptr);
    if (! apvts.state.hasProperty (kVerdictMode)) apvts.state.setProperty (kVerdictMode, verdictLive, nullptr);
    syncVerdictFile();
}

//------------------------------------------------------------------------------------ 55 · VERDICT
juce::String TelescopeProcessor::verdictLanguage() const
{
    const auto code = apvts.state.getProperty (kVerdictLanguage, juce::String (rules::kDefaultLanguage)).toString();
    // Un estado guardado con un idioma que esta versión no tiene NO deja la lente muda: cae al default.
    return rules::hasLanguage (code.toRawUTF8()) ? code : juce::String (rules::kDefaultLanguage);
}

void TelescopeProcessor::setVerdictLanguage (const juce::String& code)
{
    if (! rules::hasLanguage (code.toRawUTF8())) return;
    apvts.state.setProperty (kVerdictLanguage, code, nullptr);
}

int  TelescopeProcessor::verdictMode() const
{
    return juce::jlimit ((int) verdictLive, (int) verdictFile, (int) apvts.state.getProperty (kVerdictMode, verdictLive));
}

void TelescopeProcessor::setVerdictMode (int mode)
{
    apvts.state.setProperty (kVerdictMode, juce::jlimit ((int) verdictLive, (int) verdictFile, mode), nullptr);
}

void TelescopeProcessor::loadVerdictFile (const juce::File& f)
{
    apvts.state.setProperty (kVerdictFilePath, f.getFullPathName(), nullptr);
    setVerdictMode (verdictFile);   // cargar un archivo ES pedir el modo archivo
    syncVerdictFile();
}

void TelescopeProcessor::clearVerdictFile()
{
    apvts.state.setProperty (kVerdictFilePath, juce::String(), nullptr);
    setVerdictMode (verdictLive);
    if (verdictAnalyzer != nullptr) verdictAnalyzer->cancel();
}

void TelescopeProcessor::syncVerdictFile()
{
    if (verdictAnalyzer == nullptr) verdictAnalyzer = std::make_unique<FileAnalyzer>();

    const auto path = verdictFilePath();
    if (path.isEmpty()) return;

    const juce::File f (path);
    if (f.existsAsFile()) verdictAnalyzer->start (f);
}

juce::String TelescopeProcessor::verdictFileName() const
{
    return verdictAnalyzer != nullptr ? verdictAnalyzer->currentName() : juce::String();
}

// Mismo criterio que referenceError(): el error es del archivo QUE ESTÁ CARGADO. Sin path no hay archivo.
juce::String TelescopeProcessor::verdictFileError() const
{
    if (verdictAnalyzer == nullptr || verdictAnalyzer->busy() || verdictFilePath().isEmpty()) return {};
    return verdictAnalyzer->result().error;
}

const FileAnalysis TelescopeProcessor::verdictAnalysis() const
{
    return verdictAnalyzer != nullptr ? verdictAnalyzer->result() : FileAnalysis{};
}

void TelescopeProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    ovni::PluginProcessorBase::setStateInformation (data, sizeInBytes);
    syncLensSettings();   // replaceState cambió el árbol entero: el motor tiene que enterarse
}

TelescopeProcessor::~TelescopeProcessor()
{
    analysisThread.stop();
}

void TelescopeProcessor::prepareEngine (const juce::dsp::ProcessSpec& spec)
{
    monoScratch.assign ((size_t) juce::jmax (1u, spec.maximumBlockSize), 0.0f);
    analysisThread.prepare (spec.sampleRate);
}

void TelescopeProcessor::releaseResources()
{
    analysisThread.stop();
}

// TAP READ-ONLY. Todo lo de acá es RT-safe: punteros de lectura, un push lock-free drop-on-full y, en el
// caso mono, una copia a un scratch ya dimensionado en prepareEngine. Sin new, sin locks, sin I/O.
// Y sobre todo: NO se escribe el buffer. Es el contrato del plugin.
void TelescopeProcessor::processAudio (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const int n   = buffer.getNumSamples();
    const int nch = buffer.getNumChannels();
    if (n <= 0) return;

    if (nch >= 2)
    {
        bus.push (buffer.getReadPointer (0), buffer.getReadPointer (1), n);
    }
    else if (nch == 1)
    {
        // Fuente mono: se mide el mismo canal en L y R (correlación +1, ancho 0 — que es la verdad).
        const float* m = buffer.getReadPointer (0);
        if ((int) monoScratch.size() < n) return;   // bloque más grande que el declarado: se saltea, no se aloca
        std::copy (m, m + n, monoScratch.begin());
        bus.push (m, monoScratch.data(), n);
    }
}

juce::AudioProcessorEditor* TelescopeProcessor::createEditor()
{
    return new TelescopeEditor (*this);
}

// ========================================================================================================
// ===== 54: TONAL BALANCE · la referencia =====
//
// El path persiste; los NÚMEROS no. Al abrir una sesión guardada, si el archivo sigue estando se vuelve a
// analizar (tarda un cuarto de segundo por minuto de audio: no hay nada que ahorrar guardando la curva) y
// si no está, la lente lo dice con el nombre. Guardar 30 números en el preset y dibujarlos como si fueran
// el archivo sería mostrar una referencia que ya no existe, sin que nadie pueda notarlo.
// ========================================================================================================
void TelescopeProcessor::syncReference()
{
    if (fileAnalyzer == nullptr)
    {
        fileAnalyzer = std::make_unique<FileAnalyzer>();
        // El resultado se le empuja al motor DESDE EL THREAD DEL ANALIZADOR: el módulo Reference lo
        // protege con su propia CriticalSection, así que no hace falta rebotar por el message thread
        // (que puede no estar corriendo: en un host sin ventana abierta nadie bombea mensajes).
        fileAnalyzer->onFinished = [this] { analysisThread.setReference (fileAnalyzer->result()); };
    }

    const auto path = referencePath();
    if (path.isEmpty()) { refMissing.clear(); analysisThread.clearReference(); return; }

    const juce::File f (path);
    if (! f.existsAsFile())
    {
        refMissing = f.getFileName();
        analysisThread.clearReference();
        return;
    }

    refMissing.clear();
    fileAnalyzer->start (f);
}

void TelescopeProcessor::loadReference (const juce::File& f)
{
    apvts.state.setProperty (kRefPath, f.getFullPathName(), nullptr);
    syncReference();
}

void TelescopeProcessor::clearReference()
{
    apvts.state.setProperty (kRefPath, juce::String(), nullptr);
    refMissing.clear();
    if (fileAnalyzer != nullptr) fileAnalyzer->cancel();
    analysisThread.clearReference();
}

juce::String TelescopeProcessor::referenceName() const
{
    if (refMissing.isNotEmpty()) return refMissing;
    if (fileAnalyzer == nullptr) return {};
    return fileAnalyzer->currentName();
}

// EL ERROR Y EL AVISO SON DE LA REFERENCIA QUE ESTÁ CARGADA, no del último archivo que pasó por el
// analizador (LOW 3 del revisor del 54). `FileAnalyzer::result()` guarda su resultado hasta el próximo
// análisis, así que después de QUITAR la referencia la lente seguía mostrando el aviso de un archivo que
// ya no está — un cartel verdadero sobre algo que dejó de existir es un cartel falso. Sin path en el
// estado no hay referencia, y sin referencia no hay nada que advertir.
juce::String TelescopeProcessor::referenceError() const
{
    if (fileAnalyzer == nullptr || fileAnalyzer->busy() || referencePath().isEmpty()) return {};
    return fileAnalyzer->result().error;
}

juce::String TelescopeProcessor::referenceWarning() const
{
    if (fileAnalyzer == nullptr || fileAnalyzer->busy() || referencePath().isEmpty()) return {};
    return fileAnalyzer->result().warning;
}

const FileAnalysis TelescopeProcessor::referenceAnalysis() const
{
    return fileAnalyzer != nullptr ? fileAnalyzer->result() : FileAnalysis{};
}
}

// Entry point de JUCE.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new telescope::TelescopeProcessor(); }
