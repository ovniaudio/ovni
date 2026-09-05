#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "ui/TopBar.h"
#include "params/ParamMapping.h"
#include "tempo/LfoModulation.h"
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

}

// Decode + saliencia + máscara del sujeto en hilo de fondo (RNF4: Vision JAMÁS en el message thread);
// onDone llega SIEMPRE en el MESSAGE THREAD (callAsync), venga del disco o del caché. Compartido por la
// carga simple (RF1) y el prefetch de la PHOTO SEQUENCE (spec §D).
//
// RONDA 4 · R1 — girar es INSTANTÁNEO. Antes, cada cuarto de vuelta del ⟳ volvía a leer el archivo, a
// decodificarlo entero y a correr las DOS pasadas de Vision, aunque girar 90° no cambia ni un byte del
// archivo ni el contenido de la foto: con 12 MP eran cientos de ms por toque (el "tarda en ponerse en el
// preview"). Ahora se decodifica SIEMPRE la BASE (rotación 0), se cachea por path (DecodedImageCache,
// acotado por MB con LRU) y cualquier rotación se deriva de ella permutando píxeles y las dos grillas.
// Eso además hace coherentes los dos caminos a la misma foto girada — reabrir un proyecto guardado así, o
// girarla a mano — porque los dos son ahora la misma operación sobre la misma base.
//
// El giro NO va en el message thread: son ~24 ms de min con 12 MP, pero cientos con la máquina ocupada.
// Va en el mismo hilo de fondo que el decode, con el mismo SafePointer.
void SupernovaEditor::decodeImageAsync (const juce::File& chosen, int quarterTurns,
                                        std::function<void (std::shared_ptr<const LoadedImage>)> onDone)
{
    auto cached = imageCache.get (chosen);   // el caché es del editor: sólo el message thread lo toca
    juce::Component::SafePointer<SupernovaEditor> safe (this);
    juce::Thread::launch ([chosen, quarterTurns, safe, cached, onDone = std::move (onDone)]
    {
        auto base = cached;
        const bool fresh = (base == nullptr);
        if (fresh) base = std::make_shared<const LoadedImage> (decodeBaseImage (chosen));
        // Sin rotación no hay nada que permutar: se entrega la MISMA base (sin copiar 48 MB).
        auto shown = (! base->valid() || (quarterTurns % 4 + 4) % 4 == 0)
                       ? base
                       : std::make_shared<const LoadedImage> (rotatedCopy (*base, quarterTurns));
        juce::MessageManager::callAsync ([safe, chosen, fresh, base, shown, onDone]
        {
            if (fresh && safe != nullptr && base->valid()) safe->imageCache.put (chosen, base);
            onDone (shown);
        });
    });
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
    lfoPanel.timeSec  = [this] { return proc.timeInSeconds(); };  // …y el reloj de los LFO en modo libre

    // MEDIA STRIP (MEDIA SESSION PRO): la tira de miniaturas de la sesión, entre el visual y los knobs.
    addToCanvas (mediaStrip);
    mediaStrip.setVisible (false);
    mediaStrip.thumbSource = [this] (const MediaStrip::Item& it) -> juce::Image
    {
        auto t = thumbs.get (it.path, it.rot, it.isVideo);
        return t != nullptr ? t->image : juce::Image();
    };
    thumbs.onThumbReady    = [this] { mediaStrip.repaint(); resolveCanvas(); };
    mediaStrip.onPick        = [this] (int i, bool now) { cueMedia (i, now); };
    mediaStrip.onRemove      = [this] (int i) { removeMediaAt (i); };
    mediaStrip.onRotate      = [this] (int i) { rotateMediaAt (i); };
    mediaStrip.onMoveToStart = [this] (int i) { moveMedia (i, 0); };
    mediaStrip.onMove        = [this] (int f, int t) { moveMedia (f, t); };
    mediaStrip.onAdd         = [this] { chooseImage(); };
    mediaStrip.onClearAll    = [this] { clearMedia(); };
    mediaStrip.onReveal      = [this] (int i)
    {
        if (auto* it = mediaStrip.itemAt (i)) juce::File (it->path).revealToUser();
    };
    mediaStrip.onInsertFiles  = [this] (const juce::StringArray& f, int at) { insertMedia (f, at); };
    mediaStrip.onRelink       = [this] (int i) { chooseRelink (i); };         // media faltante (ronda 3)
    mediaStrip.onRelinkFolder = [this] (int i) { chooseRelinkFolder (i); };

    // LA BARRA — la misma TopBar pro de la app (EXPORT/LFO/PRESETS/mundos/SEQ/medidor/gain). En app-mode
    // la app la esconde (setChromeVisible(false)) y monta su AppTopBar (derivada) fuera del editor.
    topBar = std::make_unique<TopBar> (*this, p);
    addToCanvas (*topBar);

    hydrateMediaSettings();      // formato de lienzo / FIT-FILL / ▦ / aspecto conocido (antes del primer layout)
    hydrateSequence();
    hydratePerformanceState();   // Phase B: mapeos MIDI/LFOs/escenas desde el state (message thread)
    refreshMediaStrip();

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
    lastPresetIdx  = (int) proc.apvts.getRawParameterValue (pid::PRESET)->load();
    morphPresetIdx = lastPresetIdx;
    morphState     = snapshotFromApvts (proc.apvts);
    morph.syncTo (morphState);

    // Los LFO se re-evalúan en el CUADRO del render (VBlank, 60/120 Hz), no en el timer de 30 Hz: a 30 Hz un
    // seno a 2 Hz salta 21 % del rango entre muestras y el render dibuja ese escalón 2-4 veces (informe 24).
    view.setOnRenderFrame ([this] { renderFrameTick(); });

    startTimerHz (kAnalysisRingHz);   // = la tasa del anillo de análisis que consume el export
}

SupernovaEditor::~SupernovaEditor()
{
    stopTimer();
    view.setOnRenderFrame (nullptr);                    // higiene: soltar el callback del VBlank
    thumbCancel.store (true);
    exportCancel.store (true);                          // corta el loop de export → el join no congela la UI
    if (thumbThread.joinable()) thumbThread.join();     // no dejar el hilo de thumbnails colgando (Phase C)
    if (exportThread.joinable()) exportThread.join();   // no dejar el hilo de export colgando (Phase C)
}

bool SupernovaEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (ImageLoader::looksLikeImage (juce::File (f)) || VideoSource::looksLikeVideo (juce::File (f))
            || juce::File (f).isDirectory())   // una CARPETA = todas sus fotos/videos (MEDIA SESSION PRO)
            return true;
    return false;
}

// Feedback del drop: la vista Metal se compone ENCIMA del pintado JUCE → la señal visible es el tinte
// hue de la TopBar (en app-mode la barra interna está oculta y la señal es el propio drop, como siempre).
void SupernovaEditor::fileDragEnter (const juce::StringArray&, int, int) { if (topBar) topBar->setDragHover (true); }
void SupernovaEditor::fileDragExit  (const juce::StringArray&)           { if (topBar) topBar->setDragHover (false); }

// MEDIA SESSION PRO: una CARPETA soltada se expande a sus fotos/videos, ordenados por nombre natural (como
// Photos/VLC/Resolume). No recursiva: la carpeta que soltaste, no todo el disco.
juce::StringArray SupernovaEditor::expandFolders (const juce::StringArray& files)
{
    juce::StringArray expanded;
    for (const auto& f : files)
    {
        const juce::File file (f);
        if (! file.isDirectory()) { expanded.add (f); continue; }
        juce::Array<juce::File> kids = file.findChildFiles (juce::File::findFiles, false);
        juce::File::NaturalFileComparator cmp (false);
        kids.sort (cmp);
        for (const auto& k : kids)
            if (ImageLoader::looksLikeImage (k) || VideoSource::looksLikeVideo (k))
                expanded.add (k.getFullPathName());
    }
    return expanded;
}

void SupernovaEditor::filesDropped (const juce::StringArray& files, int, int)
{
    if (topBar) topBar->setDragHover (false);
    ingestMedia (expandFolders (files));
    refreshMediaStrip();
}

// Soltar ENTRE dos tiles (ronda 3): con una secuencia armada, las fotos entran EN esa posición — el orden
// es parte del pase y reordenar de a una era trabajo que la tira ya no debería pedir. Es una EDICIÓN
// (Cmd+Z la deshace). Sin secuencia todavía, no hay "posición": va por el camino normal de ingesta.
void SupernovaEditor::insertMedia (const juce::StringArray& files, int at)
{
    const juce::StringArray expanded = expandFolders (files);
    juce::StringArray media;
    for (const auto& f : expanded)
        if (ImageLoader::looksLikeImage (juce::File (f)) || VideoSource::looksLikeVideo (juce::File (f)))
            media.add (f);
    if (media.isEmpty()) return;

    auto& seq = proc.photoSequence();
    if (! seq.active()) { ingestMedia (media); refreshMediaStrip(); return; }

    captureUndoState();
    seq.insertFiles (at, media);
    seqPrefetchPath.clear();      // el "siguiente" pudo cambiar
    seqNextReady.reset();
    syncSequenceIfCurrent();
    refreshMediaStrip();          // AUTO canvas sigue al primero → resolveCanvas() adentro
}

void SupernovaEditor::ingestMedia (const juce::StringArray& files)
{
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
            syncSequenceIfCurrent();
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
            singleRot = 0;
            syncSingleToState();                              // ya no hay foto única que persistir
            syncSequenceIfCurrent();
        }
        else                // nada cargado (o mismo archivo): carga simple (imagen o video)
        {
            if (VideoSource::looksLikeVideo (f))
            {
                // Reemplaza cualquier resto de secuencia. El caché se vacía con ella (mismo patrón que
                // unloadMedia y que el alta de secuencia): si no, sus decodes base quedan de zombis
                // compitiendo en el LRU con la foto actual, el prefetch y el cue. Hallazgo del review de R4.
                if (seq.size() > 0) { seq.setFiles ({}); imageCache.clear(); syncSequenceIfCurrent(); }
                currentSingleFile = juce::File();
                singleRot = 0;
                if (openVideo (f))          // sesión de 1 item SOLO si el video abrió de verdad (review: sin GPU no)
                {
                    currentSingleFile = f;  // la tira lo muestra; el ⟳ va por videoSource
                    markShown (f, 0);
                }
                syncSingleToState();
            }
            else loadImageFile (f);
        }
        return;
    }

    // 2+ items = SEQUENCE (spec §D): reemplaza la lista, muestra el primero y arranca a rotar.
    imageCache.clear();   // sesión NUEVA: los decodes base de la anterior no vuelven a hacer falta
    seq.setFiles (media);
    seq.setPlaying (true);
    seq.start (juce::Time::getMillisecondCounterHiRes());
    seqPrefetchPath.clear();
    seqNextReady.reset();
    currentSingleFile = juce::File();   // ya no es "uno solo": el ⟳ opera sobre el item actual
    singleRot = 0;
    syncSingleToState();
    showItem (juce::File (seq.currentPath()), seq.currentRotation());
    syncSequenceIfCurrent();
    mediaCanRotate = view.gpuAvailable();   // la TopBar pollea y enciende el ⟳ + el cluster SEQ
}

// Muestra un item de la secuencia: video (abre la fuente viva) o imagen (decode async). Cierra el video
// previo al pasar a una imagen.
void SupernovaEditor::showItem (const juce::File& file, int turns, std::function<void()> onFail)
{
    markShown (file, turns);
    if (VideoSource::looksLikeVideo (file))
    {
        if (! openVideo (file) && onFail) onFail();   // sin GPU, o el archivo no abre
        return;
    }
    videoSource.close();
    videoGeomReady = false;
    juce::Component::SafePointer<SupernovaEditor> safe (this);
    decodeImageAsync (file, turns, [safe, onFail = std::move (onFail)] (std::shared_ptr<const LoadedImage> loaded)
    {
        if (safe == nullptr) return;
        if (loaded->valid()) { safe->currentImage = loaded; safe->view.loadImage (loaded); safe->userImageLoaded = true; }
        else if (onFail)     onFail();
    });
}

// El item ACTUAL de la secuencia. Si no decodifica (archivo corrupto, o borrado entre el listado y el
// decode) NO se poda: se marca FALTANTE, como cualquier otro que no está — el tile se queda y se relinkea.
void SupernovaEditor::showSequenceItem (int idx)
{
    auto& seq = proc.photoSequence();
    if (! juce::isPositiveAndBelow (idx, seq.size())) return;
    const juce::String path = seq.pathAt (idx);
    juce::Component::SafePointer<SupernovaEditor> safe (this);
    showItem (juce::File (path), seq.rotationAt (idx), [safe, path]
    {
        if (safe == nullptr) return;
        safe->decodeFailed.addIfNotAlreadyThere (path);
        auto& sq = safe->proc.photoSequence();
        for (int i = 0; i < sq.size(); ++i) if (sq.pathAt (i) == path) sq.setMissing (i, true);
        safe->refreshMediaStrip();
    });
}

// Abre un video como fuente de color. La geometría se fija con el PRIMER frame (videoTick), los siguientes
// solo recolorean. Cierra cualquier video previo.
bool SupernovaEditor::openVideo (const juce::File& file)
{
    videoGeomReady = false;
    if (! view.gpuAvailable()) return false;   // sin GPU no hay lattice que colorear
    if (videoSource.open (file))
    {
        userImageLoaded = true;
        mediaCanRotate  = true;
        return true;
    }
    return false;
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
    syncSingleToState();                             // pasa a persistirse como foto única
    seq.setFiles ({});                               // deja de ser secuencia
    syncSequenceIfCurrent();                      // la TopBar refleja el cluster SEQ apagado en su poll
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
    // Patrón del diálogo = las MISMAS extensiones que acepta el drop (HEIC/WebP/TIFF en mac) + videos.
    juce::String pattern;
    for (const auto& ext : juce::StringArray::fromTokens (ImageLoader::imageExtensions(), ";", {}))
        pattern += "*." + ext + ";";
    pattern += "*.mp4;*.mov;*.m4v";
    imageChooser = std::make_unique<juce::FileChooser> (
        "Choose images or videos (multi-select = sequence)",
        juce::File::getSpecialLocation (juce::File::userPicturesDirectory), pattern);
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
        imageCache.clear();       // la secuencia se va: sus decodes base también (ver ingestMedia)
        syncSequenceIfCurrent();
    }
    currentSingleFile = chosen;
    singleRot = 0;
    markShown (chosen, 0);
    syncSingleToState();          // la foto viaja con el proyecto / el preset (y con el undo)
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

// CUE ARMADO (ronda 2b): al armar el cue se lanza YA el decode del item, para que al cruzar el compás la
// imagen esté en la mano y el corte caiga EN el beat. El callback usa SafePointer (el editor puede morir con
// el decode en vuelo) y verifica cuePath: si el cue se canceló o se armó otro, el resultado se descarta.
void SupernovaEditor::prefetchCue (int idx)
{
    cancelCuePrefetch();
    const auto& seq = proc.photoSequence();
    const juce::String p = seq.pathAt (idx);
    if (p.isEmpty() || VideoSource::looksLikeVideo (juce::File (p))) return;   // el video se abre en el disparo
    cuePath = p;
    juce::Component::SafePointer<SupernovaEditor> safe (this);
    decodeImageAsync (juce::File (p), seq.rotationAt (idx), [safe, p] (std::shared_ptr<const LoadedImage> loaded)
    {
        if (safe == nullptr || safe->cuePath != p) return;      // llegó tarde: el cue se canceló o cambió
        if (loaded->valid()) safe->cueReady = loaded;
        else                 safe->cuePath.clear();             // no decodifica: el disparo cae al camino de siempre
    });
}

// Botón ⟳: rota 90° CW la foto ACTUAL. En secuencia, la rotación es POR FOTO y persiste; suelta, re-decodifica
// con la rotación acumulada y re-sube. Sin secuencia, rota la foto única cargada.
void SupernovaEditor::rotateCurrentImage()
{
    if (videoSource.isOpen())   // el item actual es un VIDEO: gira su salida en vivo (sin re-decodificar)
    {
        videoSource.rotate();
        videoGeomReady = false;   // el próximo frame re-fija la geometría con el nuevo aspecto
        // Video ÚNICO: la rotación se persiste como la de una foto, así vuelve al reabrir el proyecto y
        // Cmd+Z la revierte. (Un video DENTRO de una secuencia lleva su rotación en la secuencia.)
        if (! proc.photoSequence().active() && currentSingleFile.existsAsFile())
        {
            singleRot = (singleRot + 1) % 4;
            markShown (currentSingleFile, singleRot);
            syncSingleToState();
        }
        return;
    }
    juce::Component::SafePointer<SupernovaEditor> safe (this);
    auto& seq = proc.photoSequence();
    if (seq.active())
    {
        seq.rotateCurrent();
        syncSequenceIfCurrent();
        const auto file = juce::File (seq.currentPath());
        const int turns = seq.currentRotation();
        markShown (file, turns);
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
        markShown (file, turns);
        syncSingleToState();
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
    // La TIRA de media (MEDIA SESSION PRO) entre el visual y los knobs — sólo con media cargado y fuera del inmersivo.
    if (mediaStrip.isVisible())
        mediaStrip.setBounds (body.removeFromBottom (MediaStrip::kHeight));
    // El LIENZO: centrado con el aspecto resuelto (AUTO/preset) dentro del área; 0 = llena todo (como siempre).
    visualArea = body;
    const auto r = fitRect (body.getX(), body.getY(), body.getWidth(), body.getHeight(), resolvedAspect);
    view.setBounds (r.x, r.y, r.w, r.h);
}

// Letterbox del lienzo: negro puro alrededor + hairline (program monitor). La vista Metal se compone ENCIMA
// de esto en su rect; sin GPU la vista fallback pinta lo suyo.
void SupernovaEditor::paintBody (juce::Graphics& g)
{
    if (browserOpen || lfoPanelOpen || ! (resolvedAspect > 0.0f) || visualArea.isEmpty()) return;
    const auto vb = view.getBounds();
    if (vb == visualArea) return;
    g.setColour (ovni::ui::theme::bg0);
    g.fillRect (visualArea);
    g.setColour (ovni::ui::theme::line);
    g.drawRect (vb.expanded (1), 1);
}

// Persistencia del tamaño del editor (por instancia, en el estado del DAW): reabrir el proyecto reabre el
// editor del tamaño que lo dejaste — como la app restaura su windowState. Se escribe desde el TIMER cuando
// el tamaño se ASIENTA (igual dos ticks seguidos), no en cada resized(): un drag vivo dispara resized()
// decenas de veces por segundo y spamearía apvts.state (review 2026-07-16).
void SupernovaEditor::persistEditorSizeIfSettled()
{
    if (proc.stateStamp() != lastStateStamp) return;   // compuerta del restore (F7): el state es del host
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
    updateMediaStripVisibility();          // la tira también se va en inmersivo
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
// El estado restaurado trae las persistencias planas (lfoBank / midiCcMap / sceneBank) como propiedades del
// árbol, pero los OBJETOS vivos no las releían: sin hydratePerformanceState el undo dejaba el banco de LFO
// como estaba y el próximo syncLfosToState pisaba la propiedad restaurada.
bool SupernovaEditor::doUndo()
{
    if (auto s = history.undo (proc.apvts.copyState()))
    { proc.apvts.replaceState (*s); hydrateSequence(); hydratePerformanceState(); return true; }
    return false;
}
bool SupernovaEditor::doRedo()
{
    if (auto s = history.redo (proc.apvts.copyState()))
    { proc.apvts.replaceState (*s); hydrateSequence(); hydratePerformanceState(); return true; }
    return false;
}

// EXPORT A VIDEO (Phase C): renderiza el LOOK actual (imagen + params + análisis reciente) por un camino
// OFFSCREEN dedicado (renderer FRESCO, como los golden tests → no perturba la vista viva) y lo encodea a MP4
// en ~/Movies/SUPERNOVA. Corre en un hilo de fondo; reporta por callAsync con SafePointer (editor puede cerrar).
void SupernovaEditor::exportVideo (ExportFormat fmt, int seconds, int fps, bool withSound)
{
    if (exporting.load()) return;
    fps = juce::jmax (1, fps);   // el período de audio divide por fps: clampear ACÁ, no sólo en totalFrames
    if (! view.gpuAvailable()) { if (onExportDone) onExportDone (false, "No GPU available for export"); return; }

    // Snapshots (copias → sin estado compartido con el message thread).
    auto img = currentImage;
    ParticleParams pp = lastPp;

    // AUDIO (export con sonido): snapshot del anillo del processor — el MISMO audio cuyo análisis está en
    // recentFrames. Video y audio loopean con el MISMO período → quedan clavados en sync.
    double asr = 48000.0;
    std::vector<float> audio;
    if (withSound) audio = proc.audioRingSnapshot (asr);
    const bool wantAudio = audio.size() >= (size_t) asr;   // ≥ ~0.5 s grabado (audio = L/R interleaved)

    // VENTANA del análisis (ronda 2b): el anillo corre a kAnalysisRingHz, no a los fps del clip. Con sonido,
    // análisis y audio tienen que cubrir el MISMO tramo — el ring de audio guarda kAudioRingSeconds — o el
    // MP4 muestra 20 s de reactividad sobre 12 s de sonido. Sin sonido, el anillo entero.
    const int ringHz = kAnalysisRingHz;
    const ExportWindow win = exportLoopWindow ((int) recentFrames.size(), ringHz,
                                               (double) SupernovaProcessor::kAudioRingSeconds, wantAudio);
    std::vector<AnalysisFrame> frames (recentFrames.begin() + win.first,
                                       recentFrames.begin() + win.first + win.count);
    const ExportDims dims = exportDims (fmt);
    const int totalFrames = juce::jmax (1, seconds) * fps;
    const bool cutout = pp.cutoutAmt > 0.0f;
    const int  fitM   = (int) fitModeV;   // FIT/FILL del lienzo → el mismo encuadre en el MP4

    // Snapshot de la SECUENCIA de fotos (si hay ≥2) → el export CICLA las fotos COMO SE VEN: el reloj
    // (SECONDS / BEATS / KICK), el orden (LOOP / SHUFFLE) y la transición (BURST) son los de la secuencia
    // viva. Antes se ciclaba SIEMPRE por segundos, lineal y sin explosión: un loop armado al compás o al
    // kick, o en shuffle, se exportaba distinto de como sonaba/veía. Todo se resuelve ACÁ (message thread) y
    // viaja al hilo como un PLAN de slots por valor (el hilo sólo lo lee).
    const auto& seq = proc.photoSequence();
    juce::StringArray seqPaths; juce::Array<int> seqRots;
    if (seq.active())
        for (int i = 0; i < seq.size(); ++i)
            { seqPaths.add (seq.pathAt (i)); seqRots.add (seq.rotationAt (i)); }

    std::vector<ExportSlot> plan;
    const bool burst = seq.burst();
    if (seqPaths.size() >= 2)
    {
        // KICK: los cambios salen de los onsets del MISMO anillo de análisis que reproduce el clip, con el
        // gap mínimo aplicado. Si el anillo no trae onsets (silencio, o recién abierto) no habría un solo
        // cambio en todo el clip: se cae a SECONDS, que es lo que el usuario esperaría ver.
        std::vector<int> switches;
        if (seq.clock() == SeqClock::Kick)
        {
            std::vector<unsigned> counts;
            counts.reserve (frames.size());
            for (const auto& af : frames) counts.push_back (af.onsetCount);
            switches = kickSwitchFrames (onsetFramesFromCounts (counts, totalFrames, fps, ringHz),
                                         seq.kickGapSeconds(), fps);
        }
        const int perPhoto = seq.clock() == SeqClock::Beats
                                 ? framesPerPhotoBeats (seq.intervalBeats(), proc.tempoBpm(), fps)
                                 : framesPerPhoto (seq.intervalSeconds(), fps);
        // Cuántos slots hay que caminar: uno por cambio (+ el actual). Acotado: un clip largo con kick denso
        // no debe pedir un orden gigante.
        const int slots = 2 + (switches.empty() ? totalFrames / juce::jmax (1, perPhoto) : (int) switches.size());
        plan = exportSlotPlan (totalFrames, perPhoto, playOrderFrom (seq, juce::jmin (4096, slots)), switches);
    }

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
    // El hilo NO captura `this`: lo único que comparte con el editor es la bandera de cancelación, y el
    // destructor la prende ANTES del join() → el puntero sigue vivo mientras el hilo corre.
    std::atomic<bool>* cancel = &exportCancel;
    exportThread = std::thread ([safe, cancel, img, pp, frames, dims, totalFrames, fps, ringHz, path, seqPaths, seqRots,
                                 plan, burst, cutout, fitM, wantAudio, audio = std::move (audio), asr]() mutable
    {
        // Audio en sync con el LOOP del análisis: el video repite la ventana de análisis cada loopV CUADROS
        // (frames·fps/ringHz, no frames), y el audio repite el TRAMO FINAL del ring con ese mismo período.
        const size_t ringF = audio.size() / 2;
        const int    loopV = frames.empty() ? totalFrames : exportLoopFrames ((int) frames.size(), fps, ringHz);
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
                r.setFitMode (fitM);

                const bool  cycle   = seqPaths.size() >= 2 && ! plan.empty();   // secuencia → ciclar las fotos
                int         curSlot = -1;
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
                for (int i = 0; i < totalFrames && ok && ! cancel->load(); ++i)   // cancelable (close)
                {
                    const bool switched = cycle && i < (int) plan.size() && plan[(size_t) i].changed;
                    if (cycle && i < (int) plan.size())
                    {
                        const int slot = plan[(size_t) i].slot;   // reloj + orden ya resueltos (ExportPreset.h)
                        if (slot != curSlot) { curSlot = slot; uploadSlot (slot); }
                    }
                    // El anillo se reproduce a SU tasa: a 60 fps / 30 Hz, cada frame de análisis dura dos cuadros.
                    AnalysisFrame af = frames.empty()
                                           ? AnalysisFrame {}
                                           : frames[(size_t) analysisIndexForFrame (i, fps, ringHz, (int) frames.size())];
                    // BURST: la explosión dura UN cuadro, el del cambio (igual que en vivo, MetalViewComponent);
                    // el resto del clip va sin explosión (no exportar el loop entero explotado).
                    pp.explode = (burst && switched) ? 1.0f : 0.0f;
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

// LFO sync (Phase B): devuelve el valor del param con la modulación (visual) de los LFOs que lo apuntan,
// escalada al rango del param y CLAMPEADA al rango legal. La regla vive en LfoModulation.h (pura, testeada):
// depth 100 % recorre el rango entero, no el doble, y ninguna suma de slots saca el param de su dominio.
float SupernovaEditor::lfoModulation (const char* paramId, float base, double beatPos, double timeSec) const
{
    if (! proc.lfoBank().targets (paramId)) return base;   // sin LFO: ni buscamos el rango (camino de cuadro)
    const auto range = proc.apvts.getParameterRange (juce::String (paramId));
    return lfoModulated (proc.lfoBank(), paramId, base, range.start, range.end, beatPos, timeSec);
}

// CUADRO DEL RENDER (VBlank): la base (morph/APVTS/preset) es la que dejó el timer a 30 Hz; lo que se
// recalcula acá, por cuadro, es la MODULACIÓN — los LFO se leen con proc.phaseInBeats() del momento.
void SupernovaEditor::renderFrameTick()
{
    const double beatPos = proc.phaseInBeats();     // LFOs sync al tempo (Phase B)
    const double timeSec = proc.timeInSeconds();   // …y reloj para los que corren libres en Hz (ronda 5)
    const MorphSnapshot& s = morphState;

    // Continuos ease-ados; explode/choices (motion/shape) en vivo. Mapeo compartido (ParamMapping.h). El
    // renderer late en explode>0.5.
    ParticleParams pp = mapParticleParams ([this, &s, beatPos, timeSec] (const char* id) -> float
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
        // LFO sync (Phase B): aplica la modulación de los LFOs que apuntan a este param, escalada al RANGO
        // del param y clampeada → tempo-synced VISUAL (no escribe APVTS: no pelea con el usuario ni spamea
        // automatización).
        return lfoModulation (id, base, beatPos, timeSec);
    });

    // VARIATION (morphable, también ease-ada): randomización CURADA determinista alrededor del preset activo.
    // La semilla es el VALOR del param 'preset' → el mismo estado guardado siempre reproduce el mismo mundo.
    // Los HOTKNOBS por dominio NO pasan por acá: escriben los params reales de su fila desde el ControlStrip
    // (los knobs se MUEVEN). Nota (review): si el preset entró por el browser del header (applyFactory) el
    // param puede no reflejar esa fila → la semilla difiere del índice de tabla del render tool. Cosmético.
    for (int i = 0; i < MorphSnapshot::N; ++i)
        if (std::strcmp (kMorphIds[i], pid::VARIATION) == 0)
        {
            applyVariation (pp, morphPresetIdx, s.v[i] / 100.0f);
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
    lastPp = pp;   // el export arranca de acá (copia ANTES de levantar la bandera `exporting`)
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
    updateMediaStripVisibility();   // la tira se va con el overlay
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
    updateMediaStripVisibility();
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
    updateMediaStripVisibility();
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
    syncSequenceIfCurrent();   // la TopBar refleja ▸/⏸ en su poll
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
    // MEDIA SESSION PRO: ← / → = foto anterior / siguiente (cue). Sólo con secuencia; si no, el host se queda
    // la tecla — y tampoco se consume cuando el paso no tiene a dónde ir (todos los vecinos faltan): fingir
    // que la tomamos le robaba al host una tecla que no hizo nada.
    if ((k == juce::KeyPress::leftKey || k == juce::KeyPress::rightKey) && proc.photoSequence().active())
        return stepMedia (k == juce::KeyPress::rightKey ? +1 : -1, k.getModifiers().isShiftDown());   // Shift = ya
    // MEDIA SESSION PRO: 1..9 y 0 cuean el tile N (0 = el décimo), como los decks de cualquier software de
    // VJ. Se usa el KEY CODE (no el carácter): con Shift, un '2' del teclado escribe '@' pero sigue siendo
    // la tecla 2. Con Cmd la tecla es del host (presets del DAW) y no se roba; si ese tile no existe, la
    // tecla tampoco se consume. Shift = cortar YA aunque el reloj sea BEATS (si no, se arma al compás).
    if (k.getKeyCode() >= '0' && k.getKeyCode() <= '9' && ! k.getModifiers().isCommandDown())
    {
        const int digit = k.getKeyCode() - '0';
        const int idx   = digit == 0 ? 9 : digit - 1;
        // Un tile FALTANTE no se puede mostrar: la tecla NO se consume (sigue al host) en vez de fingir
        // un corte que no pasa.
        if (proc.photoSequence().active() && idx < proc.photoSequence().size()
            && ! proc.photoSequence().isMissing (idx))
        {
            cueMedia (idx, k.getModifiers().isShiftDown());
            return true;
        }
        return false;
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

    // CUE DE FOTOS POR MIDI (ronda 3): drenar la cola del audio thread y aplicar los cues acá, en el message
    // thread — la PhotoSequence no se toca en ningún otro lado. Va ANTES de sequenceTick: un "MIDI cue: now"
    // corta en ESTE tick (no en el siguiente) y un armado queda visible y evaluable en el mismo tick.
    // …salvo que haya un restore del host pendiente (el sello no coincide): entonces los cues esperan UN
    // tick — `sequenceTick` hidrata primero y el próximo tick los aplica sobre la sesión FRESCA. Si no,
    // cuearían sobre la sesión vieja y su escritura pisaría el proyecto recién cargado.
    if (proc.stateStamp() == lastStateStamp)
    {
        MidiCueMsg cue;
        while (proc.midiCueQueue().pop (cue)) applyMidiCue (cue);
    }

    sequenceTick (now);   // PHOTO SEQUENCE: prefetch + switch (no-op sin secuencia)
    videoTick();          // VIDEO: bombea el último frame al lattice (no-op sin video abierto)
    persistEditorSizeIfSettled();   // tamaño del editor → estado del DAW (coalescido: al asentarse)

    // MEDIA SESSION PRO: la tira sigue al item actual (+ progreso hacia el próximo cambio) y el lienzo AUTO se
    // resuelve en cuanto se conoce el aspecto del primer item (miniatura / imagen decodificada / video).
    if (mediaStrip.isVisible())
    {
        auto& sq = proc.photoSequence();
        mediaStrip.setCurrent (currentMediaIndex());
        mediaStrip.setProgress (sq.active() && sq.playing() ? sq.progress (seqTickNow (now)) : 0.0f);
    }
    resolveCanvas();

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
    // Se CACHEA: ésta es la BASE, y avanza a la tasa del timer. La MODULACIÓN de los LFO se le suma por
    // CUADRO del render (renderFrameTick, VBlank) — a 30 Hz un seno a 2 Hz escalonaba 21 % del rango.
    const MorphSnapshot live = snapshotFromApvts (proc.apvts);
    morphState     = morph.active() ? morph.tick (dt) : (morph.syncTo (live), live);
    morphPresetIdx = presetIdx;

    // Sin GPU no hay VBlank que dispare el cuadro: el timer sigue siendo el que empuja los params.
    renderFrameTick();

    // EXPORT (Phase C): retené los params + el anillo de análisis reciente (kAnalysisRingSeconds a la tasa
    // del timer) para que el export reproduzca el LOOK reactivo (el hilo toma copias). No graba exportando.
    if (! exporting.load())
    {
        recentFrames.push_back (view.lastFrame());
        if ((int) recentFrames.size() > kAnalysisRingHz * kAnalysisRingSeconds) recentFrames.erase (recentFrames.begin());
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
    hydrateMediaSettings();   // el host restauró: formato/FIT/▦/aspecto vienen con el mismo state
    const auto vt = proc.apvts.state.getChildWithName ("sequence");
    // La sesión del state son DOS cosas excluyentes: el child "sequence" (2+ items) o la foto/video ÚNICO
    // (props "singlePath"/"singleRot"). Las dos viajan con el proyecto, el preset y el snapshot del undo.
    const juce::File single (proc.apvts.state.getProperty ("singlePath", juce::String()).toString());
    const int        singleTurns = (((int) proc.apvts.state.getProperty ("singleRot", 0)) % 4 + 4) % 4;
    // Un undo de knob / un restore del MISMO proyecto no tiene por qué reconstruir nada: si lo restaurado es
    // idéntico a lo que hay en pantalla, la sesión se deja como está (sin re-armar el reloj ni re-decodificar).
    auto& seq = proc.photoSequence();
    const bool sameSeq    = vt.isValid() ? vt.isEquivalentTo (seq.toValueTree()) : seq.size() == 0;
    const bool sameSingle = seq.size() > 0
                            || (single == currentSingleFile && (! single.existsAsFile() || singleTurns == singleRot));
    if (sameSeq && sameSingle)
    {
        refreshMediaStrip();
        return;
    }
    // Sin child "sequence" (un undo/restore hacia un estado SIN secuencia) la secuencia viva también se
    // vacía: si no, quedaría una sesión fantasma que el state ya no tiene.
    seq = vt.isValid() ? PhotoSequence::fromValueTree (vt) : PhotoSequence {};
    if (seq.size() > 0)
    {
        seq.start (juce::Time::getMillisecondCounterHiRes());
        currentSingleFile = juce::File();
        singleRot = 0;
        syncSingleToState();                 // manda la secuencia: no hay foto única que persistir
        refreshMissingFlags();               // el disco pudo cambiar desde que se guardó el proyecto
        // Si la foto que el proyecto dejó en pantalla ya no está, se abre en la primera que SÍ esté; el
        // tile faltante se queda en la tira, marcado, para relinkearlo. Si faltan TODAS, no se muestra
        // nada (la fábrica) pero la sesión sobrevive entera.
        const int alive = seq.firstAliveIndex();
        if (alive >= 0 && seq.isMissing (seq.currentIndex()))
        {
            seq.jumpTo (alive);
            syncSequenceIfCurrent();      // el índice nuevo VUELVE al state: si no, el proyecto seguía
                                             // apuntando al muerto y cada reapertura repetía el salto
        }
        // Sólo se re-muestra si CAMBIÓ lo que está en pantalla (deshacer un reordenamiento deja la misma
        // foto a la vista: re-decodificarla sería un hipo gratis).
        if (alive >= 0 && (shownPath != seq.currentPath() || shownRot != seq.currentRotation()))
            showSequenceItem (seq.currentIndex());   // imagen o video
        mediaCanRotate = view.gpuAvailable();
    }
    else if (single.existsAsFile())
    {
        // FOTO / VIDEO ÚNICO persistido: vuelve con su rotación. Si el archivo ya no está se vuelve a fábrica
        // (no hay tira donde marcarlo faltante: la sesión de un solo item es la foto misma).
        const bool samePath = (shownPath == single.getFullPathName());
        currentSingleFile = single;
        singleRot = singleTurns;
        if (VideoSource::looksLikeVideo (single))
        {
            if (samePath && videoSource.isOpen())
            {
                // Deshacer la rotación de un video NO tiene por qué reabrirlo (perdía el punto de
                // reproducción y el primer frame): se gira la salida viva por el delta.
                const int delta = ((singleTurns - shownRot) % 4 + 4) % 4;
                for (int k = 0; k < delta; ++k) videoSource.rotate();
                if (delta != 0) videoGeomReady = false;   // el próximo frame re-fija la geometría
                markShown (single, singleTurns);
            }
            // Si el video no abre (sin GPU, archivo ilegible), la sesión NO puede quedar "cargada" con la
            // fábrica en pantalla: mismo guard que ingestMedia.
            else if (! openVideo (single)) { unloadMedia(); return; }
            else
            {
                // openVideo no lleva la rotación adentro (el ⟳ del video gira la salida viva): se re-aplica.
                for (int k = 0; k < singleTurns; ++k) videoSource.rotate();
                markShown (single, singleTurns);
            }
        }
        else if (! samePath || shownRot != singleTurns)
        {
            // Un decode que falla tampoco puede dejar una foto fantasma en la sesión.
            juce::Component::SafePointer<SupernovaEditor> safe (this);
            showItem (single, singleTurns, [safe] { if (safe != nullptr) safe->unloadMedia(); });
        }
        mediaCanRotate = view.gpuAvailable();
        userImageLoaded = true;
    }
    else
    {
        unloadMedia();   // el estado restaurado no tiene media (o el archivo ya no existe): imagen de fábrica
        return;
    }
    seqPrefetchPath.clear();
    seqNextReady.reset();
    demoteToSingleIfNeeded();   // un restore que dejó 1 sola foto viva → foto única (no secuencia muerta)
    refreshMediaStrip();
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
    if (! seq.active()) return;

    // CUE ARMADO (BEATS): esperó al próximo límite de ventana → recién ahora corta. Va ANTES del reloj (el
    // corte pedido a mano manda) y vale también con la secuencia en pausa: es una acción del usuario.
    if (seq.pendingCue() >= 0)
    {
        if (seq.shouldFireCue (seqTickNow (nowMs))) { cueMedia (seq.pendingCue(), true); return; }
    }
    if (! seq.playing()) return;

    const auto next        = seq.nextPath();
    const bool nextIsVideo = VideoSource::looksLikeVideo (juce::File (next));
    // Si el "siguiente" es el ACTUAL, es que no queda ningún otro item vivo (todos faltan): no hay nada que
    // pre-decodificar ni a dónde avanzar. La sesión se congela donde está hasta que se relinkee algo.
    // (Alcanza con esta comparación: `nextIndex()` nunca devuelve un faltante salvo por ese mismo fallback
    // al actual, así que preguntar además por `isMissing (nextIndex())` era un disyunto muerto.)
    if (seq.nextIndex() == seq.currentIndex()) return;

    // El cue ARMADO apunta al MISMO item que sigue naturalmente: su decode ya está en vuelo (o listo) →
    // se reusa. Si no, la misma foto se decodificaría DOS veces (JUCE + Vision + máscara, 100-400 ms cada
    // una) por el sólo hecho de cuear el próximo tile.
    if (! cuePath.isEmpty() && cuePath == next && seqNextReady == nullptr)
    {
        seqPrefetchPath = next;                 // el prefetch normal ya no arranca
        if (cueReady != nullptr) seqNextReady = cueReady;   // todavía en vuelo: lo recoge el próximo tick
    }
    // PREFETCH sólo de items IMAGEN: la siguiente foto se decodifica en fondo ANTES del switch (cero hipo).
    // Un item de VIDEO no se prefetchea (se abre en el switch); marcamos el path como "listo" para no
    // intentar decodificarlo como imagen (lo marcaría faltante por "no decodifica").
    else if (seqPrefetchPath != next)
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
                    // La foto no decodifica (borrada/corrupta). Antes se PODABA y desaparecía de la sesión
                    // sin explicación; ahora queda marcada como faltante: el reloj la saltea y el tile
                    // sigue ahí para relinkearla.
                    safe->decodeFailed.addIfNotAlreadyThere (next);
                    auto& sq = safe->proc.photoSequence();
                    for (int i = 0; i < sq.size(); ++i) if (sq.pathAt (i) == next) sq.setMissing (i, true);
                    safe->seqPrefetchPath.clear();
                    safe->refreshMediaStrip();
                }
            });
        }
    }

    // El switch: para imagen, espera a que el decode esté listo (no se saltea). Para video, se abre directo.
    // El reloj puede ser SECONDS (pared), BEATS (BeatClock del host/tap) o KICK (onsets del análisis).
    const SeqTick tick = seqTickNow (nowMs);
    if (seq.shouldAdvance (tick) && (nextIsVideo || seqNextReady != nullptr))
    {
        seq.advanced (tick);
        if (seq.burst()) view.triggerBurst();   // BURST: la foto vieja estalla y se re-arma como la nueva
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
        syncSequenceIfCurrent();        // la TopBar muestra "SEQ n/N" fresco en su poll
        mediaStrip.setCurrent (seq.currentIndex());
    }
}
}
