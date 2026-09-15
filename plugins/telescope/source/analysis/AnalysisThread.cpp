#include "analysis/AnalysisThread.h"
#include <cmath>

namespace telescope
{
namespace
{
// Un trozo de drenaje nunca menor a 4 800 muestras (un hop a 48 k): sacar de a poco haría al worker
// despertarse por nada.
constexpr int kMinChunk = 4800;

// Plazo de gracia de stop(): el bucle chequea threadShouldExit() en cada vuelta y espera de a 2 ms, así
// que en la práctica sale en un par de milisegundos.
constexpr int kStopGraceMs = 2000;
}

AnalysisThread::AnalysisThread (AnalysisBus& busIn, TripleBuffer<AnalysisFrame>& out, LoudnessHistory& hist,
                                TripleBuffer<ScopeFrame>* scopeOut, ClipHistory* clipHist,
                                TripleBuffer<SpectrumFrame>* spectrumOut, SpectrogramRing* spectrogramOut,
                                TripleBuffer<StereoBandsFrame>* bandsOut,
                                StereoSpectrogramRing* stereoSpectrogramOut,
                                TripleBuffer<CqtFrame>* cqtOut)
    : juce::Thread ("TelescopeAnalysis"), bus (busIn), frames (out), history (hist),
      scopeFrames (scopeOut), clipHistory (clipHist), spectrumFrames (spectrumOut),
      spectrogramRing (spectrogramOut), bandsFrames (bandsOut), stereoRing (stereoSpectrogramOut),
      cqtFrames (cqtOut)
{
    spectrum.setSpectrogramRing (spectrogramRing);
    stereoBands.setSpectrogramRing (stereoRing);

    // ===== 53: el módulo del campo y su TripleBuffer, en el heap (ver AnalysisThread.h) =====
    field       = std::make_unique<Field>();
    fieldFrames = std::make_unique<TripleBuffer<FieldFrame>>();

    // ===== 55 (1c): el espectro FIJO de la referencia. Su sink se engancha UNA vez y no se vuelve a
    // tocar: la instancia entera existe sólo para estos consumidores (ver AnalysisThread.h).
    refFanout.set (0, &reference);
    // ===== 55: la historia por segundo come del MISMO espectro fijo. Por eso el archivo y el vivo dan
    // las mismas filas: es literalmente la misma geometría y la misma clase sumando las mismas potencias.
    refFanout.set (1, &secondBuilder);
    refSpectrum.setFrameSink (&refFanout);
}

//===================================================================================== settings de Spectrum
void AnalysisThread::setSpectrumSettings (const Spectrum::Settings& s) noexcept
{
    spectrumAtomics.fftOrder         .store (s.fftOrder,          std::memory_order_release);
    spectrumAtomics.window           .store (s.window,            std::memory_order_release);
    spectrumAtomics.overlapIndex     .store (s.overlapIndex,      std::memory_order_release);
    spectrumAtomics.channel          .store (s.channel,           std::memory_order_release);
    spectrumAtomics.slopeDbPerOct    .store (s.slopeDbPerOct,     std::memory_order_release);
    spectrumAtomics.avgMode          .store (s.avgMode,           std::memory_order_release);
    spectrumAtomics.avgSeconds       .store (s.avgSeconds,        std::memory_order_release);
    spectrumAtomics.peakHold         .store (s.peakHold,          std::memory_order_release);
    spectrumAtomics.holdDecayDbPerSec.store (s.holdDecayDbPerSec, std::memory_order_release);
    spectrumAtomics.bandsMode        .store (s.bandsMode,         std::memory_order_release);
    spectrumAtomics.rangeDbIndex     .store (s.rangeDbIndex,      std::memory_order_release);
    spectrumAtomics.historySecIndex  .store (s.historySecIndex,   std::memory_order_release);
}

Spectrum::Settings AnalysisThread::spectrumSettings() const noexcept
{
    Spectrum::Settings s;
    s.fftOrder          = spectrumAtomics.fftOrder         .load (std::memory_order_acquire);
    s.window            = spectrumAtomics.window           .load (std::memory_order_acquire);
    s.overlapIndex      = spectrumAtomics.overlapIndex     .load (std::memory_order_acquire);
    s.channel           = spectrumAtomics.channel          .load (std::memory_order_acquire);
    s.slopeDbPerOct     = spectrumAtomics.slopeDbPerOct    .load (std::memory_order_acquire);
    s.avgMode           = spectrumAtomics.avgMode          .load (std::memory_order_acquire);
    s.avgSeconds        = spectrumAtomics.avgSeconds       .load (std::memory_order_acquire);
    s.peakHold          = spectrumAtomics.peakHold         .load (std::memory_order_acquire);
    s.holdDecayDbPerSec = spectrumAtomics.holdDecayDbPerSec.load (std::memory_order_acquire);
    s.bandsMode         = spectrumAtomics.bandsMode        .load (std::memory_order_acquire);
    s.rangeDbIndex      = spectrumAtomics.rangeDbIndex     .load (std::memory_order_acquire);
    s.historySecIndex   = spectrumAtomics.historySecIndex  .load (std::memory_order_acquire);
    return s;
}

//===================================================================================== settings de Cqt
void AnalysisThread::setCqtSettings (const Cqt::Settings& s) noexcept
{
    cqtAtomics.channel       .store (s.channel,        std::memory_order_release);
    cqtAtomics.chromaSecIndex.store (s.chromaSecIndex, std::memory_order_release);
}

Cqt::Settings AnalysisThread::cqtSettings() const noexcept
{
    Cqt::Settings s;
    s.channel        = cqtAtomics.channel       .load (std::memory_order_acquire);
    s.chromaSecIndex = cqtAtomics.chromaSecIndex.load (std::memory_order_acquire);
    // El decaimiento del peak hold es el MISMO gesto que en SPECTRUM: se lee de sus atomics, no se
    // duplica. Dos settings para la misma perilla se separan el día que alguien toca uno solo.
    s.holdDecayDbPerSec = spectrumAtomics.holdDecayDbPerSec.load (std::memory_order_acquire);
    return s;
}

AnalysisThread::~AnalysisThread()
{
    stop();
}

void AnalysisThread::prepare (double sampleRate)
{
    stop();

    sr         = sampleRate > 0.0 ? sampleRate : 48000.0;
    hopSamples = juce::jmax (1, (int) std::llround (sr / 10.0));   // 100 ms

    const int chunk = juce::jmax (kMinChunk, hopSamples);
    chunkL.assign ((size_t) chunk, 0.0f);
    chunkR.assign ((size_t) chunk, 0.0f);
    hopL.assign   ((size_t) hopSamples, 0.0f);
    hopR.assign   ((size_t) hopSamples, 0.0f);

    loudness.prepare (sr);
    stereo.prepare (sr);
    stereoBands.setWindowIndex (bandsWindowIdx.load (std::memory_order_acquire));
    spectrum.applySettings (spectrumSettings());
    spectrum.prepare (sr);
    // ===== 55 (1c): settings FIJOS, los mismos con los que FileAnalyzer analiza el archivo. No salen de
    // `spectrumSettings()` a propósito: es justo lo que no tiene que depender de la lente SPECTRUM.
    refSpectrum.applySettings (Spectrum::Settings{});
    refSpectrum.prepare (sr);
    secondBuilder.prepare (sr);
    cqt.applySettings (cqtSettings());
    cqt.prepare (sr);   // barato: los ~229 kernels se construyen recién en el primer frame (ver Cqt.h)
    emitRateHz.store (spectrum.emitRate(), std::memory_order_release);
    doReset();
    startThread();
}

bool AnalysisThread::stop()
{
    // 2 s de gracia: el bucle chequea threadShouldExit() en cada vuelta y espera de a 2 ms, así que en la
    // práctica sale en un par de milisegundos (lo miden AnalysisChainTest y LifecycleTest).
    //
    // M-1 (revisor del 48): el retorno NO se descarta. `prepare()` llama a `stop()` y ACTO SEGUIDO
    // reasigna chunkL/R y hopL/R — justo los vectores que el worker escribe en run(). Si el worker no
    // salió, esa reasignación es un use-after-free silencioso: en release no crashea, corrompe memoria
    // dentro del DAW del usuario.
    //
    // Por eso, si no salió en el plazo, esperamos PARA SIEMPRE (stopThread(-1)). Un cuelgue honesto —
    // visible en cualquier sampler, con el stack del worker a la vista — es infinitamente mejor que
    // memoria corrupta. El jassert lo caza en debug antes de que llegue a nadie.
    const bool exited = stopThread (kStopGraceMs);
    jassert (exited);
    if (! exited)
    {
        // El jassert sólo existe en debug. En release, si esto pasa dentro del DAW de alguien, el único
        // rastro que queda es este log: sin él, el host se cuelga sin que nadie pueda decir por qué.
        juce::Logger::writeToLog ("TELESCOPE: el thread \"" + getThreadName() + "\" no salio en "
                                  + juce::String (kStopGraceMs) + " ms; esperando sin plazo antes de tocar "
                                  "sus buffers (ver AnalysisThread::stop)");
        stopThread (-1);
    }
    return exited;
}

void AnalysisThread::doReset()
{
    bus.reset();
    history.clear();
    loudness.reset();
    stereo.reset();
    spectrum.reset();
    stereoBands.reset();
    cqt.reset();
    lastCqtEmitted = 0;
    lastSpectrumEmitted = 0;
    spectrumWasOn = false;
    lastBandsEmitted = 0;
    bandsWasOn = false;
    // ===== 53: el campo también arranca de cero. El contador local va JUNTO con el del módulo: si
    // `lastFieldEmitted` quedara viejo, el frame que vuelva a caer en ese índice no se publicaría.
    field->reset();
    lastFieldEmitted = 0;
    fieldWasOn = false;
    if (clipHistory != nullptr) clipHistory->clear();
    clipsThisSecond = 0;
    hopsThisSecond  = 0;
    lastEnabledMask = 0;   // todo queda "apagado": el próximo hop vuelve a limpiar las ventanas (idempotente)
    analysedSeconds = 0.0;
    hopFill = 0;

    AnalysisFrame empty;
    empty.enabledModules = enabledMask.load (std::memory_order_acquire);
    frames.writeSlot() = empty;
    frames.publish();

    if (scopeFrames != nullptr)
    {
        scopeFrames->writeSlot() = ScopeFrame{};
        scopeFrames->publish();
    }

    if (spectrumFrames != nullptr)
    {
        spectrum.frame().copyTo (spectrumFrames->writeSlot());
        spectrumFrames->publish();
    }

    if (cqtFrames != nullptr)
    {
        cqtFrames->writeSlot() = cqt.frame();
        cqtFrames->publish();
    }
    // ===== 54: RESET limpia el ACUMULADOR VIVO de la referencia, NO la referencia cargada =====
    // Es la misma regla que el resto del reset: se tira lo MEDIDO, no lo que el usuario configuró. Un
    // RESET que además descargara el archivo de referencia obligaría a volver a cargarlo cada vez que uno
    // quiere volver a medir desde cero, que es justo lo que uno hace todo el tiempo con una referencia.
    reference.resetLive();
    refSpectrum.reset();
    referenceWasOn = false;
    publishReference();

    // ===== 55: la historia por segundo arranca de cero con el resto. El builder y el ring VAN JUNTOS:
    // un builder a medio segundo con el ring vacío publicaría una primera fila de menos de un segundo.
    secondBuilder.reset();
    samplesFed = 0;
    if (secondHistory != nullptr) secondHistory->clear();
}

// ========================================================================================================
// EL ESPECTRO NO VA POR HOPS. El medidor de loudness y el estéreo consumen hops de 100 ms EXACTOS y de ahí
// sale su determinismo (casa-4, [stereo] bloque); un analizador de espectro atado a esa cadencia
// actualizaría a 10 Hz y se vería muerto — un SPAN refresca a 30-60.
//
// Así que el espectro come del CHUNK que se acaba de drenar (el worker drena lo que haya cada ≤ 2 ms) y
// decide sus propias posiciones de frame contando muestras desde el reset. Los dos caminos comen las
// MISMAS muestras en el MISMO orden; lo único que cambia es cada cuánto cierra cada uno su ventana.
// Por eso el cambio de cadencia no mueve un solo bit de [ebu], casa-4 ni [stereo] bloque.
// ========================================================================================================
void AnalysisThread::feedSpectrum (int n)
{
    const auto mask = enabledMask.load (std::memory_order_acquire);
    // kStereoBands IMPLICA el motor de espectro: el estéreo por banda come de los complejos de la STFT.
    // Las dos lentes que lo piden declaran kSpectrum | kStereoBands, pero el motor no depende de eso.
    const bool bandsOn = (mask & kStereoBands) != 0;
    const bool on      = (mask & kSpectrum) != 0 || bandsOn;

    // ===== 55 (1c): la referencia come de SU PROPIA instancia, con settings fijos. Va ANTES del retorno
    // de abajo porque no depende de kSpectrum: TONAL BALANCE y VERDICT piden kReference y tienen que
    // medir aunque nadie esté mirando el analizador de espectro.
    feedReference (n, mask);

    if (! on) { spectrumWasOn = false; bandsWasOn = false; spectrum.setFrameSink (nullptr); return; }

    // Flanco de subida: el ring de muestras y los promedios arrancan de cero (mismo criterio que Stereo).
    if (! spectrumWasOn)
    {
        spectrum.reset();
        lastSpectrumEmitted = 0;
        spectrumWasOn = true;
    }

    // El sink se engancha SÓLO con la máscara puesta: sin él, el módulo no paga las transformadas extra
    // de L y R. Y al reactivarse, la ventana por banda arranca limpia (misma regla del 49 que Stereo:
    // el primer número tras volver no puede mezclar hasta tres segundos de audio de antes de apagarse).
    if (bandsOn != bandsWasOn)
    {
        bandsWasOn = bandsOn;
        if (bandsOn)
        {
            spectrum.reset();          // los frames viejos del ring de muestras no son de esta ventana
            lastSpectrumEmitted = 0;
            stereoBands.reset();
            lastBandsEmitted = 0;
        }
    }

    const auto wanted = spectrumSettings();

    if (bandsOn)
    {
        // El espectrograma estéreo comparte los settings de historia y rango con el sonograma de nivel:
        // son la misma pantalla mirada de dos maneras, y dos historias distintas sólo confundirían.
        stereoBands.setWindowIndex (bandsWindowIdx.load (std::memory_order_acquire));
        stereoBands.setHistorySeconds (wanted.historySeconds());
    }

    // ===== 53: el CAMPO cuelga del estéreo por banda. Sin kStereoBands no hay paneo por bin que comer,
    // así que la lente FIELD declara kSpectrum | kStereoBands | kField y acá se exige la cadena entera.
    const bool fieldOn = bandsOn && (mask & kField) != 0;
    if (fieldOn != fieldWasOn)
    {
        fieldWasOn = fieldOn;
        stereoBands.setFieldSink (fieldOn ? field.get() : nullptr);
        if (fieldOn)
        {
            // Al volver, la grilla arranca limpia: acumular sobre lo que quedó de antes de apagarse
            // mostraría energía de un rato que el usuario no estaba mirando (la regla del 49).
            field->reset();
            lastFieldEmitted = 0;
        }
    }
    if (fieldOn) field->setDecayIndex (fieldDecayIdx.load (std::memory_order_acquire));

    if (wanted != spectrum.settings())
    {
        spectrum.applySettings (wanted);
        emitRateHz.store (spectrum.emitRate(), std::memory_order_release);
        lastSpectrumEmitted = 0;
        // Cambió la geometría del análisis: la ventana por banda tampoco puede mezclar los dos lados.
        stereoBands.reset();
        lastBandsEmitted = 0;
    }

    // El sink de la instancia PRINCIPAL, fijado en CADA vuelta y ANTES de process (si se fijara después,
    // los frames de este chunk se perderían y el resultado dependería de en qué pedazos entró el audio).
    // Desde el 55 (1c) su único consumidor es el estéreo por banda: la referencia se fue a su propia
    // instancia. El fan-out queda igual —con tres slots libres— para quien tenga que engancharse después.
    sinkFanout.set (0, bandsOn ? &stereoBands : nullptr);
    spectrum.setFrameSink (sinkFanout.empty() ? nullptr : &sinkFanout);

    spectrum.process (chunkL.data(), chunkR.data(), n);

    // Latest-wins: si el drenaje produjo varios frames emitidos, se publica el último. La UI dibuja 30-60
    // veces por segundo; publicar tres frames que nadie va a leer es memoria tirada.
    if (spectrumFrames != nullptr && spectrum.framesEmitted() != lastSpectrumEmitted)
    {
        lastSpectrumEmitted = spectrum.framesEmitted();
        spectrum.frame().copyTo (spectrumFrames->writeSlot());
        spectrumFrames->publish();
    }

    if (bandsOn && bandsFrames != nullptr && stereoBands.framesEmitted() != lastBandsEmitted)
    {
        lastBandsEmitted = stereoBands.framesEmitted();
        stereoBands.frame().copyTo (bandsFrames->writeSlot());
        bandsFrames->publish();
    }

    // ===== 53: latest-wins también acá — la lente dibuja 30 veces por segundo y el campo emite hasta 60.
    if (fieldOn && field->framesEmitted() != lastFieldEmitted)
    {
        lastFieldEmitted = field->framesEmitted();
        fieldFrames->writeSlot() = field->frame();
        fieldFrames->publish();
    }
}

void AnalysisThread::processHop()
{
    AnalysisFrame f;

    analysedSeconds += (double) hopSamples / sr;
    f.timeSeconds     = analysedSeconds;
    f.droppedSamples  = bus.droppedSamples();
    f.enabledModules  = enabledMask.load (std::memory_order_acquire);

    float peakL = 0.0f, peakR = 0.0f;
    double sumL = 0.0, sumR = 0.0;
    for (int i = 0; i < hopSamples; ++i)
    {
        const float l = hopL[(size_t) i], r = hopR[(size_t) i];
        peakL = juce::jmax (peakL, std::abs (l));
        peakR = juce::jmax (peakR, std::abs (r));
        sumL += (double) l * l;
        sumR += (double) r * r;
    }
    f.peakL = peakL;
    f.peakR = peakR;
    f.rmsL  = (float) std::sqrt (sumL / (double) hopSamples);
    f.rmsR  = (float) std::sqrt (sumR / (double) hopSamples);

    // FLANCO DE SUBIDA de cada módulo con VENTANA DESLIZANTE: al reactivarse hay que limpiarle el ring.
    // Si no, el primer número tras volver a la lente mezcla hasta un segundo de audio de ANTES de apagarse
    // con el de ahora: el correlímetro diría +1 sobre una señal que ya es -1 (LOW del revisor del 49).
    // `loudness` NO se limpia acá a propósito: es el módulo que acumula desde el reset y ya no se apaga
    // (kAlwaysOnModules); limpiarlo sería tirar el integrado.
    const juce::uint32 rising = f.enabledModules & ~lastEnabledMask;
    lastEnabledMask = f.enabledModules;
    if ((rising & kStereo) != 0) stereo.reset();
    // El CQT también tiene ventana deslizante (hasta 1.24 s en el bin grave) y acumuladores desde el
    // reset (el % del tiempo de la tonalidad): al volver a la lente, arranca limpio.
    if ((rising & kCqt) != 0) { cqt.reset(); lastCqtEmitted = 0; }

    // ===== 55: se limpian ANTES de cada hop. Si kLoudness estuviera apagado (hoy no puede: es
    // kAlwaysOnModules), arrastrar los del hop anterior sumaría dos veces la misma continua.
    lastHopClips = 0;
    lastHopDcL = lastHopDcR = 0.0;

    // MÓDULOS HABILITADOS. Lo que no se ve, no se calcula: si la lente visible no pide loudness, el
    // medidor ni se toca (y su sección del frame queda en el piso, que es lo honesto: no hay medición).
    if ((f.enabledModules & kLoudness) != 0)
    {
        // Cambiar el umbral de clip REINICIA el conteo (lo hace el propio módulo): un contador que mezcla
        // eventos medidos contra dos techos distintos no querría decir nada. Ver README.
        const float wantedThreshold = clipThreshold.load (std::memory_order_acquire);
        if (wantedThreshold != loudness.clipThresholdDbtp())
        {
            loudness.setClipThresholdDbtp (wantedThreshold);
            if (clipHistory != nullptr) clipHistory->clear();
            clipsThisSecond = 0;
            hopsThisSecond  = 0;
        }

        loudness.process (hopL.data(), hopR.data(), hopSamples);
        const auto r = loudness.result();
        f.loudness.momentary       = r.momentary;
        f.loudness.shortTerm       = r.shortTerm;
        f.loudness.integrated      = r.integrated;
        f.loudness.lra             = r.lra;
        f.loudness.truePeakMax     = r.truePeakMax;
        f.loudness.momentaryMax    = r.momentaryMax;
        f.loudness.shortTermMax    = r.shortTermMax;
        f.loudness.integratedValid = r.integratedValid;

        // ---- prompt 57c ---- el cableado de los campos APPEND-ONLY del AnalysisFrame. Seis copias, sin
        // una sola cuenta: los números los hace el módulo (ver analysis/modules/Loudness.h) y ninguno de
        // los de arriba cambia. Va acá y no en otro lado porque el frame se llena campo por campo en este
        // bucle: un campo nuevo que nadie copia es un campo que siempre vale su valor por defecto.
        f.truePeakHopL = r.truePeakHopL;
        f.truePeakHopR = r.truePeakHopR;
        f.truePeakMaxL = r.truePeakMaxL;
        f.truePeakMaxR = r.truePeakMaxR;
        f.momentaryPartial = r.momentaryPartial;
        f.shortTermPartial = r.shortTermPartial;

        f.truePeakHop = r.truePeakHop;
        f.psr         = r.psr;
        f.plr         = r.plr;
        f.psrValid    = r.psrValid;
        f.plrValid    = r.plrValid;
        f.clipEvents  = r.clipEvents;
        for (int i = 0; i < Loudness::kHistogramBins; ++i)
            f.histogram[i] = r.histogram[(size_t) i];

        // ===== 55: lo que la fila del segundo necesita del medidor y no viaja en el AnalysisFrame.
        lastHopClips = r.clipEventsHop;
        lastHopDcL   = r.dcSumHopL;
        lastHopDcR   = r.dcSumHopR;
        lastDcL      = r.dcL;
        lastDcR      = r.dcR;

        // Ring de 1 Hz: diez hops de 100 ms hacen un segundo EXACTO, así que no hace falta mirar el reloj.
        clipsThisSecond += r.clipEventsHop;
        if (++hopsThisSecond == 10)
        {
            if (clipHistory != nullptr) clipHistory->push (clipsThisSecond);
            clipsThisSecond = 0;
            hopsThisSecond  = 0;
        }
    }

    // ESTÉREO de banda ancha. Los buffers de onda van por su propio TripleBuffer (~40 KB): meterlos en el
    // AnalysisFrame haría que las otras 12 lentes copien un osciloscopio que no dibujan.
    if ((f.enabledModules & kStereo) != 0)
    {
        stereo.setWindowMs (stereoWindowMs.load (std::memory_order_acquire));
        stereo.process (hopL.data(), hopR.data(), hopSamples);

        const auto s = stereo.result();
        f.corr            = s.corr;
        f.width           = s.width;
        f.balanceDb       = s.balanceDb;
        f.monoLossDb      = s.monoLossDb;
        f.stereoWindowSec = s.windowSeconds;

        if (scopeFrames != nullptr)
        {
            auto& slot = scopeFrames->writeSlot();
            slot = stereo.scope();
            slot.timeSeconds = analysedSeconds;
            scopeFrames->publish();
        }
    }

    // ESTÉREO POR BANDA. Los 30×4 números viajan en el AnalysisFrame (son 480 bytes, no 130 KB como la
    // coherencia por bin, que va por su propio TripleBuffer). El módulo lo alimenta el espectro, no el
    // hop: acá sólo se LEE lo último que publicó.
    if ((f.enabledModules & kStereoBands) != 0)
    {
        const auto& b = stereoBands.result();
        for (int i = 0; i < StereoBands::kNumBands; ++i)
        {
            f.bandCorr[i]       = b.corr[i];
            f.bandWidth[i]      = b.width[i];
            f.bandBalanceDb[i]  = b.balanceDb[i];
            f.bandMonoLossDb[i] = b.monoLossDb[i];
        }
        f.bandsWindowSec = b.windowSeconds;
        f.bandsValid     = b.valid;
    }

    // CONSTANT-Q. Come del hop, igual que el medidor y el estéreo: 100 ms exactos, posiciones fijas del
    // stream. Sus kernels se construyen la PRIMERA vez que se le pide un frame — o sea acá, en el worker,
    // y sólo si alguien abrió CQT o SPIRAL.
    if ((f.enabledModules & kCqt) != 0)
    {
        const auto wanted = cqtSettings();
        if (wanted != cqt.settings()) cqt.applySettings (wanted);
        cqt.process (hopL.data(), hopR.data(), hopSamples);

        const auto& c = cqt.frame();
        f.keyTonic        = c.keyTonic;
        f.keyMode         = c.keyMode;
        f.keyConfidence   = c.keyConfidence;
        f.keyTimeFraction = c.keyTimeFraction;

        if (cqtFrames != nullptr && cqt.framesEmitted() != lastCqtEmitted)
        {
            lastCqtEmitted = cqt.framesEmitted();
            cqtFrames->writeSlot() = c;
            cqtFrames->publish();
        }
    }

    // ===== 55: LA FILA DEL SEGUNDO =====
    // El builder junta diez hops (un segundo EXACTO de posiciones del stream, no de reloj de pared) con
    // los frames del espectro FIJO que caen en ese mismo segundo, y devuelve la fila cuando cierra. Es el
    // mismo objeto que usa el análisis de archivo: por eso las filas salen idénticas al bit.
    {
        SecondBuilder::HopInput in;
        in.modules       = f.enabledModules;
        in.shortTerm     = f.loudness.shortTerm;
        in.momentary     = f.loudness.momentary;
        in.truePeakHop   = f.truePeakHop;
        in.clipEventsHop = lastHopClips;
        in.dcSumL        = lastHopDcL;
        in.dcSumR        = lastHopDcR;
        in.hopSamples    = hopSamples;
        in.keyTonic      = f.keyTonic;
        in.keyMode       = f.keyMode;
        in.keyConfidence = f.keyConfidence;

        SecondRow row;
        if (secondBuilder.pushHop (in, row) && secondHistory != nullptr)
            secondHistory->push (row);
    }
    f.secondsAnalysed = (juce::uint32) secondBuilder.secondsClosedCount();
    f.dcL = lastDcL;
    f.dcR = lastDcR;

    frames.writeSlot() = f;
    frames.publish();
    history.push (f.loudness.momentary, f.loudness.shortTerm);

    // ===== 54: TONAL BALANCE =====
    // El integrado sale del MISMO medidor, no de un segundo: si kLoudness estuviera apagado (hoy no puede:
    // es kAlwaysOnModules) llegaría el piso con integratedValid en false, y la lente diría que todavía no
    // hay con qué normalizar en vez de dibujar una curva sin sentido.
    reference.setLiveLoudness (f.loudness.integrated, f.loudness.integratedValid);
    publishReference();
}

void AnalysisThread::run()
{
    while (! threadShouldExit())
    {
        if (resetRequested.exchange (false, std::memory_order_acq_rel))
            doReset();

        const int got = bus.pop (chunkL.data(), chunkR.data(), (int) chunkL.size());
        if (got <= 0)
        {
            wait (2);   // ms — nada que analizar todavía
            continue;
        }

        // En pausa se DRENA igual (para que el bus no se desborde y el contador de descartes no mienta),
        // pero no se integra ni se publica: el frame queda congelado.
        if (paused.load (std::memory_order_acquire))
            continue;

        feedSpectrum (got);

        for (int i = 0; i < got; ++i)
        {
            hopL[(size_t) hopFill] = chunkL[(size_t) i];
            hopR[(size_t) hopFill] = chunkR[(size_t) i];

            if (++hopFill == hopSamples)
            {
                processHop();
                hopFill = 0;
            }
        }
    }
}

// ========================================================================================================
// ===== 55 (1c): el cableado de la referencia · SU PROPIA FFT =====
//
// El módulo Reference (y, desde el punto 2, la historia por segundo) comen de `refSpectrum`, una instancia
// clavada en los settings FIJOS del análisis de archivo. Así el lado vivo y el lado archivo miden con la
// misma geometría siempre, mire el usuario el analizador de espectro con el orden que mire.
//
// COME DEL CHUNK, no del hop: como la instancia principal, decide sus propias posiciones de frame contando
// muestras desde el reset, así que el tamaño del bloque de entrada no mueve un bit.
//
// FLANCO DE SUBIDA de kReference: el promedio infinito arranca LIMPIO y el ring de muestras también. Sin
// esto, el primer promedio tras abrir la lente mezclaría audio de antes de encenderla — la regla del 49.
// ========================================================================================================
void AnalysisThread::feedReference (int n, juce::uint32 mask)
{
    const bool refOn = (mask & kReference) != 0;

    if (refOn != referenceWasOn)
    {
        referenceWasOn = refOn;
        if (refOn)
        {
            refSpectrum.reset();
            reference.resetLive();
            // El `streamPos` del espectro vuelve a cero acá; el reloj de los hops no. Se le dice al
            // builder en qué muestra ABSOLUTA quedó ese cero, así cada frame cae en el segundo del tema
            // que le corresponde y no en el que le tocaría según cuándo se abrió la lente.
            secondBuilder.setSpectrumOrigin (samplesFed);
        }
    }

    if (refOn) refSpectrum.process (chunkL.data(), chunkR.data(), n);

    // Se cuenta SIEMPRE, esté o no encendido: es la posición del stream, no la del módulo.
    samplesFed += n;
}

// Se publica SIEMPRE (una vez por hop), esté kReference prendido o no: con el módulo apagado el frame sale
// con liveValid en false y la lente lo dice. Un TripleBuffer que deja de refrescarse deja en pantalla el
// último frame bueno, que es la manera más silenciosa de mentir.
void AnalysisThread::publishReference()
{
    if (refFrames == nullptr) return;
    reference.fill (refFrames->writeSlot());
    refFrames->publish();
}
}
