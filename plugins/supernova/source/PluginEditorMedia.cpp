// SupernovaEditor · MEDIA SESSION PRO (spec 2026-09-02 + ronda 2) — la sesión de media como MODELO visible:
// la tira de miniaturas, el formato del lienzo (AUTO / FREE / fijos + FIT-FILL) y el cluster SEQ PRO
// (reloj SECONDS/BEATS/KICK, orden LOOP/SHUFFLE, transición CUT/BURST, cue al compás).
//
// MISMA clase que PluginEditor.cpp, otra unidad de traducción: aquel archivo pasó las 1.300 líneas y esta
// mitad es un tema aparte (la sesión), no el chasis del editor. Cero cambios de comportamiento al mudarse.
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "image/FactoryImage.h"
#include "image/ImageLoader.h"   // extensiones aceptadas en el diálogo de relink
#include "render/IRenderer.h"       // kParticleGrid

namespace supernova
{
// ================================ MEDIA SESSION PRO (spec 2026-09-02) ================================

SeqTick SupernovaEditor::seqTickNow (double nowMs) const
{
    return SeqTick { nowMs, proc.phaseInBeats(), view.lastFrame().onsetCount };
}

// El state es del HOST hasta que el editor hidrata: mientras el sello no coincida hay un
// `setStateInformation` pendiente y lo que escribamos pisaría el proyecto recién cargado (ver PluginEditor.h).
// `hydrateSequence` fija `lastStateStamp` al entrar, así que sus propias escrituras pasan por esta compuerta
// sin cambiar de conducta — salvo que llegue OTRO restore en el medio, y ahí callarse es lo correcto.
void SupernovaEditor::syncSequenceIfCurrent()
{
    if (proc.stateStamp() == lastStateStamp) proc.syncSequenceToState();
}

std::vector<SupernovaEditor::MediaItem> SupernovaEditor::mediaItems() const
{
    std::vector<MediaItem> out;
    const auto& seq = proc.photoSequence();
    if (seq.size() > 0)
    {
        for (int i = 0; i < seq.size(); ++i)
        {
            const juce::String p = seq.pathAt (i);
            out.push_back ({ p, seq.rotationAt (i), VideoSource::looksLikeVideo (juce::File (p)), seq.isMissing (i) });
        }
        return out;
    }
    if (currentSingleFile.existsAsFile())
    {
        const bool isVid = VideoSource::looksLikeVideo (currentSingleFile);
        out.push_back ({ currentSingleFile.getFullPathName(), isVid ? 0 : singleRot, isVid });
    }
    return out;
}

int SupernovaEditor::currentMediaIndex() const
{
    const auto& seq = proc.photoSequence();
    if (seq.size() > 0) return seq.currentIndex();
    return currentSingleFile.existsAsFile() ? 0 : -1;
}

void SupernovaEditor::updateMediaStripVisibility()
{
    const bool show = mediaStripOn && ! immersive && ! browserOpen && ! lfoPanelOpen && ! mediaItems().empty();
    if (mediaStrip.isVisible() != show)
    {
        mediaStrip.setVisible (show);
        layoutCanvas();
    }
}

// MEDIA FALTANTE (ronda 3): quién está y quién no lo mira ACÁ (el modelo es puro). Barato: un stat por
// item, y sólo cuando el modelo cambia (no en el tick). Un archivo que VUELVE a su lugar deja de faltar
// solo; uno que está pero no decodifica se queda marcado (decodeFailed), o el prefetch lo reintentaría en
// cada tick.
void SupernovaEditor::refreshMissingFlags()
{
    auto& seq = proc.photoSequence();
    for (int i = 0; i < seq.size(); ++i)
    {
        const juce::String p = seq.pathAt (i);
        seq.setMissing (i, ! juce::File (p).existsAsFile() || decodeFailed.contains (p));
    }
}

void SupernovaEditor::refreshMediaStrip()
{
    refreshMissingFlags();
    const auto items = mediaItems();
    std::vector<MediaStrip::Item> si;
    si.reserve (items.size());
    for (const auto& m : items) si.push_back ({ m.path, m.rot, m.isVideo, m.missing });
    mediaStrip.setItems (si);                     // (limpia el cue armado: los índices pudieron cambiar)
    mediaStrip.setCurrent (currentMediaIndex());
    mediaStrip.setPendingCue (proc.photoSequence().pendingCue());
    // sacar / reordenar / vaciar cancelan el cue en el MODELO (los índices se corren): el pre-decode del
    // armado deja de tener sentido con él.
    if (proc.photoSequence().pendingCue() < 0) cancelCuePrefetch();
    // Miniaturas: la ACTUAL primero (es la que mira el usuario), después el resto en orden.
    const int cur = currentMediaIndex();
    if (juce::isPositiveAndBelow (cur, (int) items.size()))
        thumbs.get (items[(size_t) cur].path, items[(size_t) cur].rot, items[(size_t) cur].isVideo);
    for (const auto& m : items) thumbs.get (m.path, m.rot, m.isVideo);
    mediaCanRotate = ! items.empty() && view.gpuAvailable();
    updateMediaStripVisibility();
    resolveCanvas();
    mediaStrip.repaint();
}

// RELINK de un item faltante: el usuario encontró el archivo. Es una EDICIÓN (Cmd+Z la deshace). Si era el
// item en pantalla, se muestra ya; si no, el prefetch lo tomará cuando le toque.
bool SupernovaEditor::relinkMediaAt (int idx, const juce::File& newFile)
{
    auto& seq = proc.photoSequence();
    if (! juce::isPositiveAndBelow (idx, seq.size()) || ! newFile.existsAsFile()) return false;
    captureUndoState();
    decodeFailed.removeString (seq.pathAt (idx));   // el path viejo deja de ser relevante
    imageCache.invalidate (seq.pathAt (idx));       // …y su decode base tampoco
    if (! seq.setPathAt (idx, newFile.getFullPathName())) return false;
    seqPrefetchPath.clear();
    seqNextReady.reset();
    syncSequenceIfCurrent();
    if (idx == seq.currentIndex()) showSequenceItem (idx);
    refreshMediaStrip();
    return true;
}

// RELINK de la CARPETA: la sesión entera se mudó de directorio. Todos los faltantes que vivían en la MISMA
// carpeta vieja que `idx` se buscan por NOMBRE DE ARCHIVO en la nueva — el caso real (un disco externo que
// cambió de letra, una carpeta que se movió de proyecto) se arregla de una.
int SupernovaEditor::relinkFolderAt (int idx, const juce::File& newFolder)
{
    auto& seq = proc.photoSequence();
    if (! juce::isPositiveAndBelow (idx, seq.size()) || ! newFolder.isDirectory()) return 0;
    const juce::String oldDir = juce::File (seq.pathAt (idx)).getParentDirectory().getFullPathName();

    juce::Array<int>          hits;
    juce::Array<juce::File>   found;
    for (int i = 0; i < seq.size(); ++i)
    {
        if (! seq.isMissing (i)) continue;
        const juce::File old (seq.pathAt (i));
        if (old.getParentDirectory().getFullPathName() != oldDir) continue;
        const auto cand = newFolder.getChildFile (old.getFileName());
        if (cand.existsAsFile()) { hits.add (i); found.add (cand); }
    }
    if (hits.isEmpty()) return 0;

    captureUndoState();
    for (int k = 0; k < hits.size(); ++k)
    {
        decodeFailed.removeString (seq.pathAt (hits[k]));
        imageCache.invalidate (seq.pathAt (hits[k]));
        seq.setPathAt (hits[k], found[k].getFullPathName());
    }
    seqPrefetchPath.clear();
    seqNextReady.reset();
    syncSequenceIfCurrent();
    if (hits.contains (seq.currentIndex())) showSequenceItem (seq.currentIndex());
    refreshMediaStrip();
    return hits.size();
}

void SupernovaEditor::chooseRelink (int idx)
{
    if (! juce::isPositiveAndBelow (idx, proc.photoSequence().size())) return;
    const juce::File old (proc.photoSequence().pathAt (idx));
    juce::String pattern;
    for (const auto& ext : juce::StringArray::fromTokens (ImageLoader::imageExtensions(), ";", {}))
        pattern += "*." + ext + ";";
    pattern += "*.mp4;*.mov;*.m4v";
    imageChooser = std::make_unique<juce::FileChooser> ("Relink " + old.getFileName(),
                                                        old.getParentDirectory(), pattern);
    juce::Component::SafePointer<SupernovaEditor> safe (this);
    imageChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                               [safe, idx] (const juce::FileChooser& fc)
    {
        if (safe == nullptr) return;
        const auto f = fc.getResult();
        if (f.existsAsFile()) safe->relinkMediaAt (idx, f);
        safe->imageChooser.reset();
    });
}

void SupernovaEditor::chooseRelinkFolder (int idx)
{
    if (! juce::isPositiveAndBelow (idx, proc.photoSequence().size())) return;
    const juce::File old (proc.photoSequence().pathAt (idx));
    imageChooser = std::make_unique<juce::FileChooser> ("Relink the folder of " + old.getFileName(),
                                                        old.getParentDirectory(), juce::String());
    juce::Component::SafePointer<SupernovaEditor> safe (this);
    imageChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                               [safe, idx] (const juce::FileChooser& fc)
    {
        if (safe == nullptr) return;
        const auto d = fc.getResult();
        if (d.isDirectory()) safe->relinkFolderAt (idx, d);
        safe->imageChooser.reset();
    });
}

void SupernovaEditor::cueMedia (int idx, bool immediate, double beatPosNow)
{
    auto& seq = proc.photoSequence();
    if (! seq.active()) return;
    // MEDIA FALTANTE: cuear un archivo que no está no muestra NADA — la pantalla se queda con la foto
    // anterior y la tira marca como actual un tile con el glifo `!`. Una sola compuerta acá cubre el click
    // en el tile, las teclas 1-9/0, ← →, el menú y las notas 72-87 / 88 / 89 / 90.
    if (seq.isMissing (idx)) return;
    // BEAT SNAP: en BEATS el corte espera al próximo límite de ventana (lo que hace cualquier software de
    // VJ) — el tile queda marcado como ARMADO hasta que dispara. Shift (o el menú) corta al instante.
    if (! immediate && seq.clock() == SeqClock::Beats && idx != seq.currentIndex())
    {
        // La ventana del armado se fija YA: con el beatPos del pedido (el de la nota, si vino por MIDI) o
        // con el de este instante para el mouse / las teclas / el menú.
        seq.armCue (idx, beatPosNow >= 0.0 ? beatPosNow
                                           : seqTickNow (juce::Time::getMillisecondCounterHiRes()).beatPos);
        mediaStrip.setPendingCue (seq.pendingCue());
        prefetchCue (idx);       // el decode arranca AHORA: al llegar el compás la imagen ya está
        return;
    }
    const bool cueHit = (cueReady != nullptr && cuePath == seq.pathAt (idx));   // el armado, ya decodificado
    if (! seq.jumpTo (idx)) return;
    mediaStrip.setPendingCue (-1);
    seqPrefetchPath.clear();
    seqNextReady.reset();
    if (seq.burst()) view.triggerBurst();
    if (cueHit)      // corte INSTANTÁNEO: la imagen del armado ya está decodificada (no se espera nada)
    {
        videoSource.close();
        videoGeomReady = false;
        currentImage = cueReady;
        view.loadImage (cueReady);
        markShown (juce::File (seq.currentPath()), seq.currentRotation());
    }
    else showSequenceItem (seq.currentIndex());   // trae el onFail: un archivo que está pero no decodifica se marca
    cancelCuePrefetch();
    userImageLoaded = true;
    syncSequenceIfCurrent();
    mediaStrip.setCurrent (seq.currentIndex());
}

// CUE DE FOTOS POR MIDI (ronda 3) — un mensaje de la cola (audio thread) aplicado en el MESSAGE THREAD, que
// es el único que puede tocar la PhotoSequence. En inmersivo o fullscreen la tira no está en pantalla: el
// pad es el instrumento. La cuantización es la MISMA que la de la tira salvo que el usuario pida "now".
void SupernovaEditor::applyMidiCue (const MidiCueMsg& m)
{
    auto& seq = proc.photoSequence();
    if (! seq.active()) return;   // sin secuencia no hay a dónde cuear (una foto única no es una sesión)

    switch (m.kind)
    {
        case PhotoCueKind::Next: stepMedia (+1, midiCueNow, m.beatPos); return;
        case PhotoCueKind::Prev: stepMedia (-1, midiCueNow, m.beatPos); return;
        case PhotoCueKind::Random:
        {
            // "otra cualquiera": nunca la que está en pantalla ni un faltante. Si no queda ningún otro
            // vivo, randomOtherIndex devuelve el actual → no hay corte que hacer.
            const int r = seq.randomOtherIndex();
            if (r != seq.currentIndex()) cueMedia (r, midiCueNow, m.beatPos);
            return;
        }
        case PhotoCueKind::Tile:
            // Un tile que no existe (pad de 16 sobre una sesión de 3) no hace nada: mejor silencio que un
            // corte a la foto equivocada.
            if (juce::isPositiveAndBelow (m.index, seq.size())) cueMedia (m.index, midiCueNow, m.beatPos);
            return;
    }
}

void SupernovaEditor::setMidiCueNow (bool on)
{
    if (proc.stateStamp() != lastStateStamp) return;   // compuerta del restore (F7): el state es del host
    midiCueNow = on;
    proc.apvts.state.setProperty ("midiCueNow", on, nullptr);
}

// ← / → y las notas 88 / 89: al vecino que EXISTE más cercano en esa dirección (era el índice crudo ±1, que
// podía aterrizar en un faltante). El par es LINEAL en los dos sentidos, también bajo SHUFFLE — el sorteo es
// del reloj automático, no del pulgar: en vivo, → seguido de ← tiene que volver a donde estabas.
// Si no queda otro vivo, los dos devuelven el actual: devuelve FALSE (no hizo nada) y la tecla sigue al host.
bool SupernovaEditor::stepMedia (int delta, bool immediate, double beatPosNow)
{
    auto& seq = proc.photoSequence();
    if (! seq.active()) return false;
    const int target = delta > 0 ? seq.nextAliveIndex() : seq.prevIndex();
    if (target == seq.currentIndex()) return false;
    cueMedia (target, immediate, beatPosNow);
    return true;
}

// Sacar / reordenar / rotar / vaciar son EDICIONES: capturan estado antes (Cmd+Z las deshace). Cuear NO lo
// es — navegar por la sesión no debería gastar un paso de undo.
void SupernovaEditor::removeMediaAt (int idx)
{
    auto& seq = proc.photoSequence();
    if (seq.size() > 0)
    {
        if (! juce::isPositiveAndBelow (idx, seq.size())) return;
        captureUndoState();
        const bool wasCurrent = (idx == seq.currentIndex());
        // El path se va de la sesión: que deje de estar envenenado. Si no, volver a cargar el MISMO archivo
        // ya arreglado lo mostraba faltante para toda la vida del editor. (Con el path duplicado en otro
        // item, ese otro se desmarca y el prefetch lo vuelve a marcar al fallar: se cura solo.)
        decodeFailed.removeString (seq.pathAt (idx));
        imageCache.invalidate (seq.pathAt (idx));   // sale de la sesión: su decode base ya no hace falta
        seq.removeAt (idx);
        seqPrefetchPath.clear();
        seqNextReady.reset();
        if (seq.size() == 0) { unloadMedia(); return; }
        syncSequenceIfCurrent();
        if (wasCurrent) showItem (juce::File (seq.currentPath()), seq.currentRotation());
        demoteToSingleIfNeeded();   // quedó 1 → foto única real
        refreshMediaStrip();
        return;
    }
    if (idx == 0 && currentSingleFile.existsAsFile()) { captureUndoState(); unloadMedia(); }
}

void SupernovaEditor::moveMedia (int from, int to)
{
    auto& seq = proc.photoSequence();
    auto before = proc.apvts.copyState();          // se registra recién si el movimiento vale (sin pasos fantasma)
    if (! seq.moveItem (from, to)) return;
    history.push (before);
    seqPrefetchPath.clear();      // el "siguiente" pudo cambiar
    seqNextReady.reset();
    syncSequenceIfCurrent();
    refreshMediaStrip();          // AUTO canvas sigue al primero → resolveCanvas() adentro
}

void SupernovaEditor::rotateMediaAt (int idx)
{
    auto& seq = proc.photoSequence();
    if (! juce::isPositiveAndBelow (idx, (int) mediaItems().size())) return;   // sin paso de undo fantasma
    if (idx == proc.photoSequence().pendingCue()) cancelCuePrefetch();   // se decodificó con la rotación vieja
    captureUndoState();   // (la rotación de un VIDEO vive en el VideoSource, no en el state: ese undo no la revierte)
    if (idx == currentMediaIndex()) { rotateCurrentImage(); }
    else if (seq.size() > 0 && juce::isPositiveAndBelow (idx, seq.size()))
    {
        seq.setRotationAt (idx, seq.rotationAt (idx) + 1);
        if (seq.pathAt (idx) == seqPrefetchPath) { seqPrefetchPath.clear(); seqNextReady.reset(); }   // re-decodificar con la rotación nueva
        syncSequenceIfCurrent();
    }
    refreshMediaStrip();
}

void SupernovaEditor::clearMedia() { captureUndoState(); unloadMedia(); }

void SupernovaEditor::unloadMedia()
{
    videoSource.close();
    videoGeomReady = false;
    auto& seq = proc.photoSequence();
    if (seq.size() > 0) seq.setFiles ({});
    syncSequenceIfCurrent();
    currentSingleFile = juce::File();
    singleRot = 0;
    syncSingleToState();          // sin media: las props "singlePath"/"singleRot" se van del state
    shownPath.clear();
    shownRot = 0;
    currentImage.reset();
    userImageLoaded = false;
    seqPrefetchPath.clear();
    seqNextReady.reset();
    decodeFailed.clear();         // sesión vacía: ningún path arrastra su historia de decodes fallidos
    imageCache.clear();           // …ni su decode base cacheado (la RAM vuelve con la sesión)
    // De vuelta a la imagen de fábrica (RF1): el lattice no queda con la foto vieja.
    auto li = std::make_shared<LoadedImage>();
    li->rgba = makeFactoryImage (kParticleGrid, kParticleGrid);
    li->width = li->height = kParticleGrid;
    view.loadImage (li);
    knownSourceAspect = 0.0f;
    refreshMediaStrip();
}

void SupernovaEditor::setMediaStripVisible (bool on)
{
    if (proc.stateStamp() != lastStateStamp) return;   // compuerta del restore (F7): el state es del host
    mediaStripOn = on;
    proc.apvts.state.setProperty ("mediaStrip", on, nullptr);
    updateMediaStripVisibility();
}

// ---- canvas ----
float SupernovaEditor::sourceAspectForCanvas() const
{
    const auto items = mediaItems();
    if (items.empty()) return 0.0f;
    // El lienzo AUTO sigue al primer item QUE ESTÉ: colgarse del aspecto de un archivo que falta dejaría
    // el formato clavado en lo último que se supo de él.
    size_t firstIdx = 0;
    while (firstIdx < items.size() && items[firstIdx].missing) ++firstIdx;
    if (firstIdx >= items.size()) return knownSourceAspect;
    const auto& first = items[firstIdx];
    const int cur = currentMediaIndex();
    if (cur == (int) firstIdx && videoSource.isOpen()) return videoSource.aspect();   // incluye su rotación manual
    if (auto t = thumbs.peek (first.path, first.rot))
        if (! t->failed && t->aspect() > 0.0f) return t->aspect();
    if (cur == (int) firstIdx && currentImage != nullptr && currentImage->valid())
        return (float) currentImage->width / (float) currentImage->height;
    return knownSourceAspect;   // lo último sabido (persistido) hasta que llegue la miniatura
}

void SupernovaEditor::resolveCanvas()
{
    const float src = mediaItems().empty() ? 0.0f : sourceAspectForCanvas();
    if (std::abs (src - knownSourceAspect) > 1.0e-4f)
    {
        knownSourceAspect = src;
        proc.apvts.state.setProperty ("srcAspect", (double) src, nullptr);
    }
    const float a = canvasAspectFor (canvasFmt, knownSourceAspect);
    if (std::abs (a - resolvedAspect) > 1.0e-4f)
    {
        resolvedAspect = a;
        view.setCanvasAspect (a);   // la salida fullscreen letterboxea igual
        layoutCanvas();
        repaint();
    }
}

void SupernovaEditor::setCanvasFormat (CanvasFormat f)
{
    if (proc.stateStamp() != lastStateStamp) return;   // compuerta del restore (F7): el state es del host
    canvasFmt = f;
    proc.apvts.state.setProperty ("canvasFormat", (int) f, nullptr);
    resolveCanvas();
}

juce::String SupernovaEditor::canvasChipText() const
{
    if (canvasFmt == CanvasFormat::Auto)
        return resolvedAspect > 0.0f ? juce::String ("AUTO ") + aspectLabel (resolvedAspect) : juce::String ("AUTO");
    return canvasFormatName (canvasFmt);
}

void SupernovaEditor::setFitMode (FitMode m)
{
    if (proc.stateStamp() != lastStateStamp) return;   // compuerta del restore (F7): el state es del host
    fitModeV = m;
    view.setFitMode ((int) m);
    proc.apvts.state.setProperty ("fitMode", (int) m, nullptr);
}

// La foto/video ÚNICO como parte del ESTADO (no sólo del editor): así viaja con el proyecto del DAW, con los
// presets de usuario y con el snapshot del undo. Con secuencia activa no hay foto única: las props se van.
void SupernovaEditor::syncSingleToState()
{
    if (proc.stateStamp() != lastStateStamp) return;   // misma compuerta que syncSequenceIfCurrent(): con un
                                                       // restore pendiente el state es del host (PluginEditor.h)
    auto& st = proc.apvts.state;
    if (currentSingleFile.existsAsFile())
    {
        st.setProperty ("singlePath", currentSingleFile.getFullPathName(), nullptr);
        st.setProperty ("singleRot",  singleRot, nullptr);
    }
    else
    {
        st.removeProperty ("singlePath", nullptr);
        st.removeProperty ("singleRot",  nullptr);
    }
}

void SupernovaEditor::hydrateMediaSettings()
{
    const auto& st = proc.apvts.state;
    canvasFmt         = canvasFormatFromInt ((int) st.getProperty ("canvasFormat", 0));
    fitModeV          = ((int) st.getProperty ("fitMode", 0) == 1) ? FitMode::Fill : FitMode::Fit;
    mediaStripOn      = (bool) st.getProperty ("mediaStrip", true);
    midiCueNow        = (bool) st.getProperty ("midiCueNow", false);   // default = la regla de la tira
    knownSourceAspect = (float) (double) st.getProperty ("srcAspect", 0.0);
    view.setFitMode ((int) fitModeV);
}

// ---- seq pro ----
void SupernovaEditor::setSequenceClock (SeqClock c)
{
    auto& seq = proc.photoSequence();
    seq.setClock (c);
    // Un cue armado fuera de BEATS quedaría en el limbo (no hay ventana de beats que cruzar) y el tile
    // punteado para siempre: cambiar de reloj lo suelta.
    if (c != SeqClock::Beats && seq.pendingCue() >= 0)
    {
        seq.cancelCue();
        cancelCuePrefetch();
        mediaStrip.setPendingCue (-1);
    }
    seq.start (juce::Time::getMillisecondCounterHiRes());
    syncSequenceIfCurrent();
}
void SupernovaEditor::setSequenceOrder (SeqOrder o)
{
    proc.photoSequence().setOrder (o);
    seqPrefetchPath.clear();      // el "siguiente" cambió de lógica
    seqNextReady.reset();
    syncSequenceIfCurrent();
}
void SupernovaEditor::setSequenceBurst (bool on)
{
    proc.photoSequence().setBurst (on);
    syncSequenceIfCurrent();
}
void SupernovaEditor::stepSequenceRate (int dir)
{
    auto& seq = proc.photoSequence();
    switch (seq.clock())
    {
        case SeqClock::Seconds: seq.setIntervalSeconds (seq.intervalSeconds() + (dir > 0 ? 1.0 : -1.0)); break;
        case SeqClock::Beats:   seq.stepIntervalBeats (dir); break;
        case SeqClock::Kick:    seq.setKickGapSeconds (seq.kickGapSeconds() + (dir > 0 ? 0.25 : -0.25)); break;
    }
    syncSequenceIfCurrent();
}
}
