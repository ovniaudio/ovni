#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "ui/TopBar.h"
#include "params/ParamMapping.h"
#include "params/ParameterIDs.h"
#include "presets/CanvasState.h"
#include "presets/PresetTarget.h"
#include "ui/theme.h"
#include "ui-kit/Fonts.h"
#include "image/ImageLoader.h"
#include "image/ImageField.h"
#include "image/VisionField.h"
#include "image/FactoryImage.h"
#include "render/metal/MetalRenderer.h"
#include "render/metal/AutoreleasePool.h"
#include "video/VideoExporter.h"
#include "presets/PresetTypes.h"
#include "ui/WorldCards.h"
#include <cstring>

namespace supernova
{
namespace pid = params::id;

namespace
{
// ÓPTICA DE CINE (Phase A) — el baseline sutil que hace que TODOS los mundos se vean "caros" de fábrica.
// Estos son el TUNEO FINO de Joaquín: subir el grano da más textura de película; la aberración se escala con
// el kick en el renderer (0.35..1.0×). Viven solo en el path del plugin/app → goldens byte-exactos.
constexpr float kFilmGrain      = 0.06f;   // 0 = limpio · ~0.06 = grano de cine sutil · 0.2 = súper 8
constexpr float kFilmDither     = 1.0f;    // 1 = dither anti-banding on (recomendado; casi invisible)
constexpr float kFilmAberration = 0.40f;   // 0 = sin split · ~0.4 = borde RGB sutil que pulsa con el kick
constexpr float kFilmBloomWide  = 0.55f;   // 0 = solo glow tight · ~0.55 = halo envolvente cinematográfico
// GRADE DE COLORISTA (Phase A6) — un toque de contraste + split-tone teal-orange de cine. Neutros = 0/1/0.
constexpr float kGradeLift      = 0.02f;   // levanta apenas las sombras (aire cinematográfico)
constexpr float kGradeContrast  = 1.07f;   // 1 = plano · ~1.07 = punch sutil
constexpr float kSplitAmt       = 0.14f;   // 0 = sin split · ~0.14 = teal-orange sutil (sombras cyan/luces ámbar)

constexpr int kStripH = ControlStrip::kHeight;   // franja de controles (2 filas: mundo físico + vocabulario)

// Tamaño inicial del editor en el DAW (primer open; después se persiste por instancia en el estado).
// El canvas es FLEXIBLE (full-bleed como la app): el host puede redimensionar entre estos límites.
constexpr int kDefaultW = 1100, kDefaultH = 760;
constexpr int kMinW = 720, kMinH = 480;          // los mismos mínimos que la ventana de la app

// Decode + saliencia + máscara del sujeto en hilo de fondo (RNF4: Vision JAMÁS en el message thread);
// onDone llega en el MESSAGE THREAD (callAsync). Compartido por la carga simple (RF1) y el prefetch
// de la PHOTO SEQUENCE (spec §D).
void decodeImageAsync (const juce::File& chosen, int quarterTurns,
                       std::function<void (std::shared_ptr<const LoadedImage>)> onDone)
{
    juce::Thread::launch ([chosen, quarterTurns, onDone = std::move (onDone)]
    {
        auto img = ImageLoader::fromFile (chosen);          // ya endereza por EXIF
        if (img.valid() && quarterTurns != 0)
            ImageLoader::rotate90 (img, quarterTurns);       // rotación manual del usuario (antes de saliencia)
        if (img.valid())
        {
            img.saliency = visionSaliency (img.rgba.data(), img.width, img.height,
                                           kParticleGrid, kParticleGrid);
            // Máscara del sujeto para el CUTOUT — cascada: (1) FONDO PLANO por color (logos/gráficos:
            // la IA fotográfica los confunde; el chroma-key los recorta perfecto) → (2) Vision
            // (Quitar-fondo 14+ / personas 12+) → (3) saliencia (universal).
            img.subjectMask = ImageField::maskFromFlatBackground (img.rgba.data(), img.width, img.height,
                                                                  kParticleGrid, kParticleGrid);
            if (img.subjectMask.empty())
                img.subjectMask = visionSubjectMask (img.rgba.data(), img.width, img.height,
                                                     kParticleGrid, kParticleGrid);
            if (img.subjectMask.empty() && ! img.saliency.empty())
                img.subjectMask = ImageField::maskFromSaliency (img.saliency, kParticleGrid, kParticleGrid);
        }
        auto loaded = std::make_shared<const LoadedImage> (std::move (img));
        juce::MessageManager::callAsync ([onDone, loaded] { onDone (loaded); });
    });
}
}

SupernovaEditor::SupernovaEditor (SupernovaProcessor& p)
    : ovni::PluginEditorBase (p, "SNV·08"), proc (p), controls (p.apvts, look::hue), lfoPanel (p.lfoBank())
{
    setFamilyHue (look::hue);
    headerHeight = 48;

    view.setAnalysisSource (&proc.analysis());        // el único consumidor del TripleBuffer
    view.setMidiTriggerSource (&proc.midiTriggers()); // triggers MIDI visuales (explosión/rayo)
    addToCanvas (view);
    addToCanvas (controls);
    controls.onBeforeEdit = [this] { captureUndoState(); };   // UNDO: RANDOM captura el estado previo (Phase C)
    // MIDI-LEARN (Phase B UI): click-derecho en un knob → arma/olvida el mapeo CC→param en el processor.
    controls.onMidiLearn    = [this] (juce::String id) { proc.midiCcMap().armLearn (id.toStdString()); };
    controls.onMidiForget   = [this] (juce::String id) { proc.midiCcMap().clearParam (id.toStdString());
                                                         proc.syncCcMapToState(); };
    controls.midiCcForParam = [this] (juce::String id) { return proc.midiCcMap().ccForParam (id.toStdString()); };

    // WORLD BROWSER (Phase C): oculto hasta el toggle; click en un tile aplica ese mundo (param 'preset').
    addToCanvas (worldBrowser);
    worldBrowser.setVisible (false);
    worldBrowser.onClose = [this] { closeWorldBrowser(); };
    worldBrowser.onPick  = [this] (int idx)
    {
        if (auto* pp = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (pid::PRESET)))
        {
            captureUndoState();
            pp->beginChangeGesture(); *pp = idx; pp->endChangeGesture();   // el editor morphea al mundo
        }
        closeWorldBrowser();
    };

    // LFO PANEL (Phase B UI): overlay de configuración; onChange persiste, onClose restaura la vista.
    addToCanvas (lfoPanel);
    lfoPanel.setVisible (false);
    lfoPanel.onChange = [this] { proc.syncLfosToState(); };
    lfoPanel.onClose  = [this] { toggleLfoPanel(); };
    lfoPanel.beatPos  = [this] { return proc.phaseInBeats(); };   // medidor de salida EN VIVO del panel

    // LA BARRA — la misma TopBar pro de la app (EXPORT/LFO/PRESETS/mundos/SEQ/medidor/gain). En app-mode
    // la app la esconde (setChromeVisible(false)) y monta su AppTopBar (derivada) fuera del editor.
    topBar = std::make_unique<TopBar> (*this, p);
    addToCanvas (*topBar);

    hydrateSequence();
    hydratePerformanceState();   // Phase B: mapeos MIDI/LFOs/escenas desde el state (message thread)

    setWantsKeyboardFocus (true);   // Tab (inmersivo) / F (fullscreen) / Space (secuencia)

    // CANVAS FLEXIBLE + editor REDIMENSIONABLE (pedido "el plugin igual a la app"): el visual llena TODO el
    // editor (full-bleed, sin base×zoom ni chrome de catálogo). El tamaño se persiste POR INSTANCIA en el
    // estado del DAW (lo escribe el timer al asentarse el resize); primer open = kDefault.
    setHeaderVisible (false);
    setFlexibleCanvas (true);
    setResizable (true, false);                       // el host ofrece el drag de marco; sin corner overlay
    setResizeLimits (kMinW, kMinH, 100000, 100000);   // mínimos sanos (los de la app); sin tope real
    // Restaurar CLAMPEADO a la pantalla — mismo patrón que applyZoom del chasis ("NUNCA más grande que la
    // pantalla", bug de campo del zoom L): el tamaño guardado puede venir de OTRA máquina con un monitor
    // más grande (proyecto del DAW, o un preset de usuario portable: copyState lleva las props del root).
    int w0 = (int) proc.apvts.state.getProperty ("editorW", kDefaultW);
    int h0 = (int) proc.apvts.state.getProperty ("editorH", kDefaultH);
    if (auto* disp = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
    {
        const auto ua = disp->userArea;
        w0 = juce::jmin (w0, ua.getWidth()  - 24);    // margen para el chrome del host (como applyZoom)
        h0 = juce::jmin (h0, ua.getHeight() - 64);
    }
    setSize (juce::jmax (kMinW, w0), juce::jmax (kMinH, h0));

    // Estado inicial del morph: sin animación espuria en el primer tick (arranca en los valores en vivo).
    lastTickMs    = juce::Time::getMillisecondCounterHiRes();
    lastPresetIdx = (int) proc.apvts.getRawParameterValue (pid::PRESET)->load();
    morph.syncTo (snapshotFromApvts (proc.apvts));

    startTimerHz (30);
}

SupernovaEditor::~SupernovaEditor()
{
    stopTimer();
    thumbCancel.store (true);
    exportCancel.store (true);                          // corta el loop de export → el join no congela la UI
    if (thumbThread.joinable()) thumbThread.join();     // no dejar el hilo de thumbnails colgando (Phase C)
    if (exportThread.joinable()) exportThread.join();   // no dejar el hilo de export colgando (Phase C)
}

bool SupernovaEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (ImageLoader::looksLikeImage (juce::File (f)) || VideoSource::looksLikeVideo (juce::File (f)))
            return true;
    return false;
}

// Feedback del drop: la vista Metal se compone ENCIMA del pintado JUCE → la señal visible es el tinte
// hue de la TopBar (en app-mode la barra interna está oculta y la señal es el propio drop, como siempre).
void SupernovaEditor::fileDragEnter (const juce::StringArray&, int, int) { if (topBar) topBar->setDragHover (true); }
void SupernovaEditor::fileDragExit  (const juce::StringArray&)           { if (topBar) topBar->setDragHover (false); }

void SupernovaEditor::filesDropped (const juce::StringArray& files, int, int)
{
    if (topBar) topBar->setDragHover (false);

    juce::StringArray media;   // imágenes Y videos (los items de la secuencia pueden ser cualquiera)
    for (const auto& f : files)
        if (ImageLoader::looksLikeImage (juce::File (f)) || VideoSource::looksLikeVideo (juce::File (f)))
            media.add (f);
    if (media.isEmpty()) return;

    auto& seq = proc.photoSequence();

    if (media.size() == 1)
    {
        const juce::File f (media[0]);
        if (seq.active())   // "meter más": un item sobre una secuencia activa se AGREGA al final
        {
            seq.appendFiles (media);
            proc.syncSequenceToState();
        }
        else if (currentSingleFile.existsAsFile() && currentSingleFile != f)
        {
            // Ya hay UNA foto cargada y cae otra → se ARMA la secuencia [actual, nueva] sola (así el
            // cluster SEQ con los segundos por imagen se enciende cargando de a una, no solo con multi-drop).
            seq.setFiles ({ currentSingleFile.getFullPathName(), f.getFullPathName() });
            seq.setRotationAt (0, singleRot);                 // la rotación manual de la actual se conserva
            seq.setPlaying (true);
            seq.start (juce::Time::getMillisecondCounterHiRes());
            seqPrefetchPath.clear();
            seqNextReady.reset();
            currentSingleFile = juce::File();                 // el ⟳ pasa a operar sobre la secuencia
            proc.syncSequenceToState();
        }
        else                // nada cargado (o mismo archivo): carga simple (imagen o video)
        {
            if (VideoSource::looksLikeVideo (f)) { currentSingleFile = juce::File(); openVideo (f); }
            else                                   loadImageFile (f);
        }
        return;
    }

    // 2+ items = SEQUENCE (spec §D): reemplaza la lista, muestra el primero y arranca a rotar.
    seq.setFiles (media);
    seq.setPlaying (true);
    seq.start (juce::Time::getMillisecondCounterHiRes());
    seqPrefetchPath.clear();
    seqNextReady.reset();
    currentSingleFile = juce::File();   // ya no es "uno solo": el ⟳ opera sobre el item actual
    showItem (juce::File (seq.currentPath()), seq.currentRotation());
    proc.syncSequenceToState();
    mediaCanRotate = view.gpuAvailable();   // la TopBar pollea y enciende el ⟳ + el cluster SEQ
}

// Muestra un item de la secuencia: video (abre la fuente viva) o imagen (decode async). Cierra el video
// previo al pasar a una imagen.
void SupernovaEditor::showItem (const juce::File& file, int turns)
{
    if (VideoSource::looksLikeVideo (file))
    {
        openVideo (file);
        return;
    }
    videoSource.close();
    videoGeomReady = false;
    juce::Component::SafePointer<SupernovaEditor> safe (this);
    decodeImageAsync (file, turns, [safe] (std::shared_ptr<const LoadedImage> loaded)
    {
        if (safe != nullptr && loaded->valid()) { safe->currentImage = loaded; safe->view.loadImage (loaded); safe->userImageLoaded = true; }
    });
}

// Abre un video como fuente de color. La geometría se fija con el PRIMER frame (videoTick), los siguientes
// solo recolorean. Cierra cualquier video previo.
void SupernovaEditor::openVideo (const juce::File& file)
{
    videoGeomReady = false;
    if (! view.gpuAvailable()) return;   // sin GPU no hay lattice que colorear
    if (videoSource.open (file))
    {
        userImageLoaded = true;
        mediaCanRotate  = true;
    }
}

// Una secuencia que quedó en EXACTAMENTE 1 item (por poda de un decode fallido o por un restore donde
// sobrevivió una sola foto) NO es una secuencia (active() pide ≥2): se convierte en foto ÚNICA real para
// que el botón ⟳ funcione y para que no persista un estado degenerado (hallazgos #2/#3 del review). Un
// VIDEO solitario se deja como está (lo maneja videoSource, y su ⟳ va por ahí).
void SupernovaEditor::demoteToSingleIfNeeded()
{
    auto& seq = proc.photoSequence();
    if (seq.size() != 1) return;
    const juce::File f (seq.currentPath());
    if (VideoSource::looksLikeVideo (f)) return;     // video único: se maneja por videoSource
    currentSingleFile = f;
    singleRot = seq.currentRotation();
    seq.setFiles ({});                               // deja de ser secuencia
    proc.syncSequenceToState();                      // la TopBar refleja el cluster SEQ apagado en su poll
}

// Bombea el último frame del video al lattice (message thread, desde el timer). El PRIMER frame fija la
// geometría (loadImage → uploadImage: flow/peso una vez); los siguientes van por el camino rápido.
void SupernovaEditor::videoTick()
{
    if (! videoSource.isOpen()) return;
    if (! videoSource.latestFrame (videoBuf, videoW, videoH)) return;
    if (! videoGeomReady)
    {
        auto li = std::make_shared<LoadedImage>();
        li->rgba = videoBuf; li->width = videoW; li->height = videoH;
        view.loadImage (li);          // fija homes/flow/peso una vez (sin Vision: física por luma)
        videoGeomReady = true;
    }
    else
    {
        view.updateColors (videoBuf.data(), videoW, videoH);   // camino rápido: solo colores
    }
}

void SupernovaEditor::chooseImage()
{
    // FileChooser ASYNC (nada de modal loops: pluginval-safe, host-safe). El unique_ptr lo mantiene vivo.
    // Multi-select: 1 archivo = carga simple (RF1); 2+ = PHOTO SEQUENCE (mismo camino que el drop).
    imageChooser = std::make_unique<juce::FileChooser> (
        "Choose images or videos (multi-select = sequence)",
        juce::File::getSpecialLocation (juce::File::userPicturesDirectory),
        "*.png;*.jpg;*.jpeg;*.gif;*.mp4;*.mov;*.m4v");
    juce::Component::SafePointer<SupernovaEditor> safe (this);
    imageChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                   | juce::FileBrowserComponent::canSelectMultipleItems,
                               [safe] (const juce::FileChooser& fc)
    {
        if (safe == nullptr) return;
        juce::StringArray picked;
        for (const auto& f : fc.getResults())
            if (f.existsAsFile()) picked.add (f.getFullPathName());
        if (! picked.isEmpty()) safe->filesDropped (picked, 0, 0);   // mismo camino que el drop
        safe->imageChooser.reset();
    });
}

void SupernovaEditor::loadImageFile (const juce::File& chosen)
{
    // Carga SIMPLE (una foto, sin secuencia): recuerda el archivo y su rotación para el botón ⟳.
    videoSource.close();          // una foto reemplaza cualquier video en curso
    videoGeomReady = false;
    // Una foto ÚNICA reemplaza cualquier secuencia previa (incluida una degenerada de 1 item que quedó
    // en el estado): si no, en el próximo save/reload resucitaría la foto vieja (hallazgo #3 del review).
    if (proc.photoSequence().size() > 0)
    {
        proc.photoSequence().setFiles ({});
        proc.syncSequenceToState();
    }
    currentSingleFile = chosen;
    singleRot = 0;
    juce::Component::SafePointer<SupernovaEditor> safe (this);
    decodeImageAsync (chosen, 0, [safe] (std::shared_ptr<const LoadedImage> loaded)
    {
        if (safe != nullptr && loaded->valid())
        {
            safe->currentImage = loaded; safe->view.loadImage (loaded);
            safe->userImageLoaded = true;   // el knob CUTOUT actúa sobre la máscara del sujeto
        }
    });
    mediaCanRotate = view.gpuAvailable();
}

// Botón ⟳: rota 90° CW la foto ACTUAL. En secuencia, la rotación es POR FOTO y persiste; suelta, re-decodifica
// con la rotación acumulada y re-sube. Sin secuencia, rota la foto única cargada.
void SupernovaEditor::rotateCurrentImage()
{
    if (videoSource.isOpen())   // el item actual es un VIDEO: gira su salida en vivo (sin re-decodificar)
    {
        videoSource.rotate();
        videoGeomReady = false;   // el próximo frame re-fija la geometría con el nuevo aspecto
        return;
    }
    juce::Component::SafePointer<SupernovaEditor> safe (this);
    auto& seq = proc.photoSequence();
    if (seq.active())
    {
        seq.rotateCurrent();
        proc.syncSequenceToState();
        const auto file = juce::File (seq.currentPath());
        const int turns = seq.currentRotation();
        decodeImageAsync (file, turns, [safe] (std::shared_ptr<const LoadedImage> loaded)
        {
            if (safe != nullptr && loaded->valid()) { safe->currentImage = loaded; safe->view.loadImage (loaded); }
        });
    }
    else if (currentSingleFile.existsAsFile())
    {
        singleRot = (singleRot + 1) % 4;
        const auto file = currentSingleFile;
        const int turns = singleRot;
        decodeImageAsync (file, turns, [safe] (std::shared_ptr<const LoadedImage> loaded)
        {
            if (safe != nullptr && loaded->valid()) { safe->currentImage = loaded; safe->view.loadImage (loaded); }
        });
    }
}

void SupernovaEditor::layoutBody (juce::Rectangle<int> body)
{
    // LA BARRA (la misma de la app, 2 filas). En app-mode (chromeVisible=false) NO se reserva: la app pone
    // su AppTopBar fuera del editor y la vista Metal gana también esta franja.
    if (chromeVisible && topBar != nullptr)
        topBar->setBounds (body.removeFromTop (TopBar::kHeight));

    // OVERLAYS (Phase B/C): browser de mundos o panel de LFOs — ocupan TODO el body (vista Metal + strip ocultos).
    if (browserOpen)  { worldBrowser.setBounds (body); return; }
    if (lfoPanelOpen) { lfoPanel.setBounds (body);     return; }

    // Franja de knobs abajo — se ESCONDE en modo inmersivo (las partículas llenan la ventana).
    if (! immersive)
        controls.setBounds (body.removeFromBottom (kStripH));
    view.setBounds (body);
}

// Persistencia del tamaño del editor (por instancia, en el estado del DAW): reabrir el proyecto reabre el
// editor del tamaño que lo dejaste — como la app restaura su windowState. Se escribe desde el TIMER cuando
// el tamaño se ASIENTA (igual dos ticks seguidos), no en cada resized(): un drag vivo dispara resized()
// decenas de veces por segundo y spamearía apvts.state (review 2026-07-16).
void SupernovaEditor::persistEditorSizeIfSettled()
{
    const juce::Point<int> cur (getWidth(), getHeight());
    if (cur == lastSeenEditorSize && cur != lastSavedEditorSize
        && isFlexibleCanvas() && cur.x >= kMinW && cur.y >= kMinH)
    {
        proc.apvts.state.setProperty ("editorW", cur.x, nullptr);
        proc.apvts.state.setProperty ("editorH", cur.y, nullptr);
        lastSavedEditorSize = cur;
    }
    lastSeenEditorSize = cur;
}

void SupernovaEditor::setImmersive (bool on)
{
    if (immersive == on) return;
    immersive = on;
    controls.setVisible (! on);            // oculta los knobs (fuera del área de la vista Metal)
    // OJO: resized() del chasis NO re-corre layoutBody (solo fija el canvas base, mismo tamaño = no-op) —
    // el bug de campo 'quedan los knobs escondidos pero el hueco sigue'. layoutCanvas() es el layout real.
    layoutCanvas();                        // re-layout: la vista crece/encoge DE VERDAD
    // (El toggle ⤢ de la TopBar se refleja solo: la barra pollea isImmersive() a 30 Hz.)
}

void SupernovaEditor::setChromeVisible (bool on)
{
    if (chromeVisible == on) return;
    chromeVisible = on;
    if (topBar != nullptr) topBar->setVisible (on);   // app-mode: la app pone su AppTopBar afuera
    layoutCanvas();
}

// UNDO/REDO (Phase C): estado COMPLETO del APVTS (copyState/replaceState). captureUndoState() antes de una
// acción destructiva; doUndo manda el actual a redo y restaura el previo. Los attachments propagan a los knobs.
void SupernovaEditor::captureUndoState()
{
    history.push (proc.apvts.copyState());
}
bool SupernovaEditor::doUndo()
{
    if (auto s = history.undo (proc.apvts.copyState())) { proc.apvts.replaceState (*s); return true; }
    return false;
}
bool SupernovaEditor::doRedo()
{
    if (auto s = history.redo (proc.apvts.copyState())) { proc.apvts.replaceState (*s); return true; }
    return false;
}

// EXPORT A VIDEO (Phase C): renderiza el LOOK actual (imagen + params + análisis reciente) por un camino
// OFFSCREEN dedicado (renderer FRESCO, como los golden tests → no perturba la vista viva) y lo encodea a MP4
// en ~/Movies/SUPERNOVA. Corre en un hilo de fondo; reporta por callAsync con SafePointer (editor puede cerrar).
void SupernovaEditor::exportVideo (ExportFormat fmt, int seconds, int fps, bool withSound)
{
    if (exporting.load()) return;
    if (! view.gpuAvailable()) { if (onExportDone) onExportDone (false, "No GPU available for export"); return; }

    // Snapshots (copias → sin estado compartido con el message thread).
    auto img = currentImage;
    ParticleParams pp = lastPp;
    std::vector<AnalysisFrame> frames = recentFrames;

    // AUDIO (export con sonido): snapshot del anillo del processor — el MISMO audio cuyo análisis está en
    // recentFrames. Video y audio loopean con el MISMO período → quedan clavados en sync.
    double asr = 48000.0;
    std::vector<float> audio;
    if (withSound) audio = proc.audioRingSnapshot (asr);
    const ExportDims dims = exportDims (fmt);
    const int totalFrames = juce::jmax (1, seconds) * juce::jmax (1, fps);
    const bool cutout = pp.cutoutAmt > 0.0f;

    // Snapshot de la SECUENCIA de fotos (si hay ≥2) → el export CICLA las fotos como en vivo (antes exportaba
    // una sola imagen: por eso "un loop de varias fotos" quedaba mal). Cada foto se muestra su intervalo.
    juce::StringArray seqPaths; juce::Array<int> seqRots;
    const double seqInterval = proc.photoSequence().intervalSeconds();
    if (proc.photoSequence().active())
        for (int i = 0; i < proc.photoSequence().size(); ++i)
            { seqPaths.add (proc.photoSequence().pathAt (i)); seqRots.add (proc.photoSequence().rotationAt (i)); }

    const char* tag = fmt == ExportFormat::UHD4K ? "4K" : fmt == ExportFormat::Square1080 ? "1x1"
                    : fmt == ExportFormat::Vertical1080 ? "9x16" : "1080p";
    auto dir = juce::File::getSpecialLocation (juce::File::userMoviesDirectory).getChildFile ("SUPERNOVA");
    dir.createDirectory();
    auto stamp = juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S");
    const std::string path = dir.getChildFile ("SUPERNOVA-" + juce::String (tag) + "-" + stamp + ".mp4")
                                 .getFullPathName().toStdString();

    exporting.store (true);
    exportCancel.store (false);
    if (exportThread.joinable()) exportThread.join();
    juce::Component::SafePointer<SupernovaEditor> safe (this);
    exportThread = std::thread ([this, safe, img, pp, frames, dims, totalFrames, fps, path, seqPaths, seqRots, seqInterval, cutout,
                                 audio = std::move (audio), asr]() mutable
    {
        // Audio en sync con el LOOP del análisis: el video repite recentFrames cada loopV cuadros; el audio
        // repite el TRAMO FINAL del ring (lo más reciente) con ese mismo período, sr/fps muestras por cuadro.
        const bool   wantAudio = audio.size() >= (size_t) asr;      // ≥ ~0.5s grabado (audio = L/R interleaved)
        const size_t ringF     = audio.size() / 2;
        const int    loopV     = frames.empty() ? totalFrames : (int) frames.size();
        const size_t periodA   = wantAudio ? juce::jmin (ringF, (size_t) std::llround ((double) loopV * asr / (double) fps)) : 0;
        const size_t baseA     = ringF - periodA;
        const double samplesPerFrame = asr / (double) fps;

        VideoExporter::Config cfg;
        cfg.width = dims.width; cfg.height = dims.height; cfg.fps = fps;
        cfg.bitsPerSecond = recommendedBitrate (dims, fps);
        cfg.path = path;
        cfg.withAudio = wantAudio && periodA > 0;
        cfg.audioSampleRate = (int) std::llround (asr);
        cfg.audioChannels = 2;

        VideoExporter exporter;
        juce::String msg;
        bool ok = exporter.begin (cfg);
        if (ok)
        {
            supernova::withAutoreleasePool ([&]     // el hilo no tiene pool → prepare/uploadImage se filtrarían
            {
                MetalRenderer r;                               // renderer FRESCO (patrón de los golden tests)
                r.prepare (kParticleGrid, kParticleGrid);

                const bool  cycle    = seqPaths.size() >= 2;   // secuencia → ciclar las fotos
                const int   perPhoto = framesPerPhoto (seqInterval, fps);   // ExportPreset.h (puro, testeado)
                int         curSlot  = -1;
                auto uploadSlot = [&] (int slot)
                {
                    auto li = ImageLoader::fromFile (juce::File (seqPaths[slot]));   // decodifica + endereza EXIF
                    if (seqRots[slot] != 0) ImageLoader::rotate90 (li, seqRots[slot]);
                    if (li.valid()) r.uploadImage (li.source (cutout));
                };

                if (! cycle)   // una sola imagen (o la factory) para el clip entero
                {
                    if (img && img->valid())
                        r.uploadImage (img->source (cutout));
                    else
                    { auto f = makeFactoryImage (kParticleGrid, kParticleGrid); r.uploadImage ({ f.data(), kParticleGrid, kParticleGrid }); }
                }

                std::vector<uint8_t> rgba ((size_t) dims.width * dims.height * 4);
                std::vector<float> aChunk;                     // scratch del push de audio por cuadro
                double aAcc = 0.0; size_t aPos = 0;            // acumulador fraccional + cursor modular
                for (int i = 0; i < totalFrames && ok && ! exportCancel.load(); ++i)   // cancelable (close)
                {
                    if (cycle)
                    {
                        const int slot = photoSlotForFrame (i, perPhoto, seqPaths.size());   // cada foto su ventana, loop
                        if (slot != curSlot) { curSlot = slot; uploadSlot (slot); }
                    }
                    AnalysisFrame af = frames.empty() ? AnalysisFrame {} : frames[(size_t) (i % (int) frames.size())];
                    pp.explode = 0.0f;                         // no exportar el clip entero en plena explosión
                    r.renderOffscreen (af, pp, dims.width, dims.height, rgba.data());

                    // AUDIO PRIMERO (sr/fps muestras, loop modular): el muxer intercala exigiendo que el
                    // audio CUBRA el PTS del video entrante — si empatan, el input de video se traba.
                    if (cfg.withAudio)
                    {
                        aAcc += samplesPerFrame;
                        const int nA = (int) aAcc; aAcc -= (double) nA;
                        if (nA > 0)
                        {
                            aChunk.resize ((size_t) nA * 2);
                            for (int k = 0; k < nA; ++k)
                            {
                                const size_t src = (baseA + ((aPos + (size_t) k) % periodA)) * 2;
                                aChunk[(size_t) k * 2 + 0] = audio[src + 0];
                                aChunk[(size_t) k * 2 + 1] = audio[src + 1];
                            }
                            aPos = (aPos + (size_t) nA) % periodA;
                            ok = exporter.pushAudio (aChunk.data(), nA);
                        }
                    }
                    if (ok) ok = exporter.pushFrame (rgba.data(), dims.width, dims.height);
                }
            });
            ok = exporter.finish() && ok;
            msg = ok ? juce::String (path) : ("Export failed: " + juce::String (exporter.lastError()));
        }
        else msg = "Export failed: " + juce::String (exporter.lastError());

        const bool result = ok;
        juce::MessageManager::callAsync ([safe, result, msg]
        {
            if (safe != nullptr) { safe->exporting.store (false); if (safe->onExportDone) safe->onExportDone (result, msg); }
        });
    });
}

// LFO sync (Phase B): suma la modulación (visual) de los LFOs habilitados que apuntan a `paramId`, escalada
// al rango del param. valueFor devuelve [-depth,depth] (bipolar) o [0,depth]; ·rango = unidades del param.
float SupernovaEditor::lfoModulation (const char* paramId, double beatPos) const
{
    float mod = 0.0f;
    auto& lfos = proc.lfoBank();
    for (int i = 0; i < LfoBank::kNum; ++i)
    {
        const auto& sl = lfos.slot (i);
        if (! sl.enabled || sl.target.empty() || sl.target != paramId) continue;
        const auto range = proc.apvts.getParameterRange (juce::String (paramId));
        mod += lfos.valueFor (i, beatPos) * (range.end - range.start);
    }
    return mod;
}

// WORLD BROWSER (Phase C · #2 mover): oculta la vista Metal (sin guerra de oclusión) y muestra la grilla; los
// thumbnails los rinde generateThumbnails() en un hilo de fondo (un renderer FRESCO por mundo = arranque
// determinista, patrón golden test). Click en un tile → aplica ese mundo. Esc / CLOSE cierran.
void SupernovaEditor::toggleWorldBrowser() { if (browserOpen) closeWorldBrowser(); else openWorldBrowser(); }

void SupernovaEditor::openWorldBrowser()
{
    if (browserOpen) return;
    browserOpen = true;
    juce::StringArray names;
    if (auto* pc = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (pid::PRESET)))
        names = pc->choices;
    worldBrowser.setNames (names);
    if (auto* raw = proc.apvts.getRawParameterValue (pid::PRESET)) worldBrowser.setActive ((int) raw->load());
    view.setVisible (false);
    controls.setVisible (false);
    worldBrowser.setVisible (true);
    worldBrowser.toFront (false);
    layoutCanvas();
    // fix 3 · cache: sólo (re)generar si la imagen cambió (o es la primera vez). Cache hit → los 36 tiles ya
    // están en el browser (setNames no los borra si el conteo no cambió) → apertura instantánea.
    if (! (thumbsValid && thumbsForImage == currentImage.get()))
    {
        worldBrowser.clearThumbnails();
        generateThumbnails (names.size());
    }
}

void SupernovaEditor::closeWorldBrowser()
{
    if (! browserOpen) return;
    browserOpen = false;
    thumbCancel.store (true);
    worldBrowser.setVisible (false);
    view.setVisible (true);
    controls.setVisible (! immersive);   // respeta el modo inmersivo
    layoutCanvas();
}

void SupernovaEditor::generateThumbnails (int count)
{
    // CARDS PROCEDURALES (pedido Joaquín): la FIRMA de cada mundo como imagen instantánea derivada de sus
    // PARAMS (WorldCards.h) — en vez del render Metal offscreen (36× prepare+21 frames = segundos de
    // "rendering..."). Sin GPU, sin hilo, determinista: la grilla aparece completa EN EL ACTO, y cada
    // mundo se distingue por color/patrón/glifo aunque la foto del usuario sea la misma.
    const auto& presets = ovni::presets::factoryPresets();
    for (int idx = 0; idx < count && idx < (int) presets.size(); ++idx)
        worldBrowser.setThumbnail (idx, cards::renderWorldCard (presets[(size_t) idx], idx, 256, 132));
    thumbsForImage = currentImage.get();   // cards no dependen de la foto; el cache queda válido igual
    thumbsValid = true;
}

// LFO PANEL (Phase B UI): overlay para configurar los 4 LFOs sync (ON/destino/velocidad/forma/depth). Como el
// browser: oculta la vista Metal mientras está abierto. La modulación ya está cableada → al habilitar un slot,
// el param elegido late al compás en vivo.
void SupernovaEditor::toggleLfoPanel()
{
    if (browserOpen) closeWorldBrowser();
    lfoPanelOpen = ! lfoPanelOpen;
    if (lfoPanelOpen) lfoPanel.refreshFromBank();
    lfoPanel.setVisible (lfoPanelOpen);
    if (lfoPanelOpen) lfoPanel.toFront (false);
    view.setVisible (! lfoPanelOpen);
    controls.setVisible (! lfoPanelOpen && ! immersive);
    layoutCanvas();
}

void SupernovaEditor::clearCanvas()
{
    captureUndoState();   // CLEAR es reversible (Phase C)
    canvas::applyClearState (proc.apvts);
    morph.syncTo (snapshotFromApvts (proc.apvts));   // sin easing espurio: el salto al lienzo es inmediato
    view.snapToHome();   // CORTE (pedido de campo): el lienzo aparece YA — sin viaje de vuelta al hogar
}

void SupernovaEditor::toggleSequencePlayback()
{
    auto& seq = proc.photoSequence();
    seq.setPlaying (! seq.playing());
    if (seq.playing()) seq.start (juce::Time::getMillisecondCounterHiRes());
    proc.syncSequenceToState();   // la TopBar refleja ▸/⏸ en su poll
}

bool SupernovaEditor::keyPressed (const juce::KeyPress& k)
{
    // fix 4: SOLO Tab togglea inmersivo (toggle deliberado). Se quitó la 'i'/'I' suelta — una letra accidental
    // escondía los knobs y hacía sentir la visibilidad "aleatoria". El botón ⤢ de la barra es el otro camino.
    if (k == juce::KeyPress::tabKey)
    {
        setImmersive (! immersive);
        return true;
    }
    if (k.getTextCharacter() == 'f' || k.getTextCharacter() == 'F')
    {
        if (onAppFullscreenToggle) onAppFullscreenToggle();   // app: kiosk fullscreen de la pantalla actual
        else                       setFullscreen (! isFullscreen());  // plugin/DAW: salida a 2º monitor
        return true;
    }
    if (k == juce::KeyPress::escapeKey && browserOpen)   // Esc cierra el world browser (Phase C)
    {
        closeWorldBrowser();
        return true;
    }
    if (k == juce::KeyPress::escapeKey && lfoPanelOpen)  // Esc cierra el panel de LFOs (Phase B UI)
    {
        toggleLfoPanel();
        return true;
    }
    if (k == juce::KeyPress::escapeKey && appFsActive && onAppFullscreenToggle)   // fix 1: Esc sale del kiosk
    {
        onAppFullscreenToggle();
        return true;
    }
    if (k == juce::KeyPress::spaceKey)
    {
        toggleSequencePlayback();
        return true;
    }
    // UNDO/REDO (Phase C): Cmd/Ctrl+Z deshace, +Shift rehace. RANDOM/CLEAR dejaron de ser puertas de una vía.
    if ((k.getTextCharacter() == 'z' || k.getTextCharacter() == 'Z') && k.getModifiers().isCommandDown())
    {
        if (k.getModifiers().isShiftDown()) doRedo(); else doUndo();
        return true;
    }
    return false;
}

void SupernovaEditor::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const double dt  = juce::jlimit (0.0, 0.1, (now - lastTickMs) * 0.001);   // clamp anti-stall
    lastTickMs = now;

    sequenceTick (now);   // PHOTO SEQUENCE: prefetch + switch (no-op sin secuencia)
    videoTick();          // VIDEO: bombea el último frame al lattice (no-op sin video abierto)
    persistEditorSizeIfSettled();   // tamaño del editor → estado del DAW (coalescido: al asentarse)

    // MIDI-LEARN (Phase B): drenar los CC crudos (audio→cola) y rutearlos a params vía el mapa. Si un CC está
    // en learn, feed() lo bindea al param armado; si ya está asignado, mueve el param (valor normalizado 0..1).
    {
        MidiCcMsg cc;
        bool touched = false;
        while (proc.midiCcQueue().pop (cc))
        {
            if (auto hit = proc.midiCcMap().feed (cc.cc, cc.value))
            {
                if (auto* p = proc.apvts.getParameter (juce::String (hit->paramId)))
                    p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, hit->value01));
                touched = true;
            }
        }
        if (touched) proc.syncCcMapToState();   // un learn recién bindeado se persiste
    }

    // ¿Cambió el preset (automatización RF8 o header browser vía applyFactory→reset)? Arma el morph A→B.
    const int presetIdx = (int) proc.apvts.getRawParameterValue (pid::PRESET)->load();
    if (presetIdx != lastPresetIdx)
    {
        lastPresetIdx = presetIdx;
        morph.setTarget (presetTargetContinuous (proc.apvts, presetIdx), kPresetMorphSeconds);
        // Los HOTKNOBS vuelven a 0: sus tomas eran del mundo ANTERIOR (la base capturada ya no vale). El
        // guard de lastWritten del ControlStrip evita que el 0 "restaure" valores viejos sobre el preset.
        for (const char* id : { pid::VAR_MOVEMENT, pid::VAR_MATTER, pid::VAR_CAMERA, pid::VAR_COLOR })
            canvas::setParamAsGesture (proc.apvts, id, 0.0f);
    }

    // Snapshot ease-ado (en morph) o valores en vivo (idle: el arrastre directo de knobs pasa de largo).
    const MorphSnapshot live = snapshotFromApvts (proc.apvts);
    const MorphSnapshot s = morph.active() ? morph.tick (dt) : (morph.syncTo (live), live);

    // Continuos ease-ados; explode/choices (motion/shape) en vivo. Mapeo compartido (ParamMapping.h). El
    // renderer late en explode>0.5.
    const double beatPos = proc.phaseInBeats();   // LFOs sync al tempo (Phase B)
    ParticleParams pp = mapParticleParams ([this, &s, beatPos] (const char* id) -> float
    {
        float base = 0.0f;
        bool found = false;
        for (int i = 0; i < MorphSnapshot::N; ++i)
            if (std::strcmp (id, kMorphIds[i]) == 0) { base = s.v[i]; found = true; break; }
        if (! found)
        {
            auto* raw = proc.apvts.getRawParameterValue (id);   // explode / motion / shape (no-morph)
            base = raw != nullptr ? raw->load() : 0.0f;
        }
        // LFO sync (Phase B): suma la modulación de los LFOs que apuntan a este param, escalada al RANGO del
        // param → tempo-synced VISUAL (no escribe APVTS: no pelea con el usuario ni spamea automatización).
        base += lfoModulation (id, beatPos);
        return base;
    });

    // VARIATION (morphable, también ease-ado): randomización CURADA determinista alrededor del preset activo.
    // La semilla es el VALOR del param 'preset' → el mismo estado guardado siempre reproduce el mismo mundo.
    // Los HOTKNOBS por dominio NO pasan por acá: escriben los params reales de su fila desde el ControlStrip
    // (los knobs se MUEVEN). Nota (review): si el preset entró por el browser del header (applyFactory) el
    // param puede no reflejar esa fila → la semilla difiere del índice de tabla del render tool. Cosmético.
    for (int i = 0; i < MorphSnapshot::N; ++i)
        if (std::strcmp (kMorphIds[i], pid::VARIATION) == 0)
        {
            applyVariation (pp, presetIdx, s.v[i] / 100.0f);
            break;
        }
    // ÓPTICA DE CINE (Phase A) — baseline del PRODUCTO EN VIVO. NO va en mapParticleParams (compartido con el
    // render tool) → el tool y los goldens quedan en 0 = byte-exacto; solo el plugin/app ven la óptica. Grano
    // sutil + dither anti-banding + aberración cromática que pulsa con el kick. Son el tuneo fino de Joaquín.
    pp.grainAmt      = kFilmGrain;         // grano de película modulado por luma
    pp.ditherAmt     = kFilmDither;        // dither (mata el banding de 8 bits — prácticamente gratis)
    pp.aberrationAmt = kFilmAberration;    // RGB-split radial, escalado por el kick en el renderer
    pp.bloomWideAmt  = kFilmBloomWide;     // halo envolvente ancho (glow multi-escala)
    pp.gradeLift     = kGradeLift;         // grade de colorista: lift + contraste + split-tone teal-orange
    pp.gradeContrast = kGradeContrast;
    pp.splitAmt      = kSplitAmt;
    view.setParams (pp);

    // EXPORT (Phase C): retené los params + un anillo de ~10s de análisis reciente para que el export
    // reproduzca el LOOK reactivo (el hilo de export toma copias). No graba mientras se está exportando.
    if (! exporting.load())
    {
        lastPp = pp;
        recentFrames.push_back (view.lastFrame());
        if (recentFrames.size() > 600) recentFrames.erase (recentFrames.begin());
    }

    // (El HUD fino de fps se retiró con la TopBar — la app tampoco lo muestra. El estado de Syphon/fullscreen/
    // inmersivo lo refleja la barra, que pollea a 30 Hz; si Syphon se desarma solo (RNF3), el toggle se apaga.)
}

// ================================ PHOTO SEQUENCE (spec §D) ================================

// Phase B (blindaje thread-safety): deserializa mapeos MIDI / LFOs / escenas desde apvts.state SOLO en el
// message thread (a diferencia de setStateInformation, que el host puede llamar en cualquier hilo). Estos
// objetos (std::vector/std::string) los toca únicamente el editor → sin data race.
void SupernovaEditor::hydratePerformanceState()
{
    proc.midiCcMap().deserialize (proc.apvts.state.getProperty ("midiCcMap", juce::String()).toString().toStdString());
    proc.lfoBank().deserialize   (proc.apvts.state.getProperty ("lfoBank",   juce::String()).toString().toStdString());
    proc.sceneBank().deserialize (proc.apvts.state.getProperty ("sceneBank", juce::String()).toString().toStdString());
    if (lfoPanelOpen) lfoPanel.refreshFromBank();
}

void SupernovaEditor::hydrateSequence()
{
    lastStateStamp = proc.stateStamp();
    const auto vt = proc.apvts.state.getChildWithName ("sequence");
    if (vt.isValid())
        proc.photoSequence() = PhotoSequence::fromValueTree (vt);
    auto& seq = proc.photoSequence();
    if (seq.size() > 0)
    {
        seq.start (juce::Time::getMillisecondCounterHiRes());
        currentSingleFile = juce::File();
        showItem (juce::File (seq.currentPath()), seq.currentRotation());   // imagen o video
        mediaCanRotate = view.gpuAvailable();
    }
    seqPrefetchPath.clear();
    seqNextReady.reset();
    demoteToSingleIfNeeded();   // un restore que dejó 1 sola foto viva → foto única (no secuencia muerta)
}

void SupernovaEditor::sequenceTick (double nowMs)
{
    if (proc.stateStamp() != lastStateStamp)   // el host restauró estado con el editor abierto
    {
        hydrateSequence();
        hydratePerformanceState();   // re-hidrata mapeos MIDI/LFOs/escenas (message thread) tras un restore
        return;
    }
    auto& seq = proc.photoSequence();
    if (! seq.active() || ! seq.playing()) return;

    const auto next        = seq.nextPath();
    const bool nextIsVideo = VideoSource::looksLikeVideo (juce::File (next));

    // PREFETCH sólo de items IMAGEN: la siguiente foto se decodifica en fondo ANTES del switch (cero hipo).
    // Un item de VIDEO no se prefetchea (se abre en el switch); marcamos el path como "listo" para no
    // intentar decodificarlo como imagen (lo podaría por "no decodifica").
    if (seqPrefetchPath != next)
    {
        seqPrefetchPath = next;
        seqNextReady.reset();
        if (! nextIsVideo)
        {
            juce::Component::SafePointer<SupernovaEditor> safe (this);
            decodeImageAsync (juce::File (next), seq.nextRotation(), [safe, next] (std::shared_ptr<const LoadedImage> loaded)
            {
                if (safe == nullptr || safe->seqPrefetchPath != next) return;   // llegó tarde: descartar
                if (loaded->valid())
                    safe->seqNextReady = loaded;
                else
                {
                    // La foto no decodifica (borrada/corrupta): se poda y se sigue.
                    safe->proc.photoSequence().removePath (next);
                    safe->proc.syncSequenceToState();
                    safe->seqPrefetchPath.clear();
                    safe->demoteToSingleIfNeeded();     // si quedó 1 item → foto única (⟳ funciona)
                }
            });
        }
    }

    // El switch: para imagen, espera a que el decode esté listo (no se saltea). Para video, se abre directo.
    if (seq.shouldAdvance (nowMs) && (nextIsVideo || seqNextReady != nullptr))
    {
        seq.advanced (nowMs);
        if (nextIsVideo)
        {
            openVideo (juce::File (seq.currentPath()));
        }
        else
        {
            videoSource.close();           // el item previo pudo ser un video
            videoGeomReady = false;
            view.loadImage (seqNextReady);
            seqNextReady.reset();
        }
        userImageLoaded = true;
        seqPrefetchPath.clear();           // el próximo tick pre-carga lo que sigue
        proc.syncSequenceToState();        // la TopBar muestra "SEQ n/N" fresco en su poll
    }
}
}
