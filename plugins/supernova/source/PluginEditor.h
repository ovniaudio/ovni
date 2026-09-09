#pragma once
#include "template/PluginEditorBase.h"
#include "ui/SupernovaView.h"
#include "ui/ControlStrip.h"
#include "ui/WorldBrowser.h"
#include "ui/LfoPanel.h"
#include "ui/MediaStrip.h"
#include "image/MediaThumbs.h"
#include "image/DecodedImageCache.h"
#include "image/CanvasFormat.h"
#include "image/PhotoSequence.h"
#include "analysis/MidiCueQueue.h"
#include "presets/PresetMorph.h"
#include "presets/UndoStack.h"
#include "video/VideoSource.h"
#include "video/ExportPreset.h"
#include "video/ExportAudioLoop.h"
#include "render/Dissolve.h"
#include <vector>
#include <thread>
#include <atomic>

namespace supernova
{
class SupernovaProcessor;
class TopBar;

// Editor SUPERNOVA — la MISMA experiencia que la app standalone (pedido Joaquín 2026-07-16: "el plugin
// igual a la app"): TopBar pro de 2 filas arriba (EXPORT/LFO/PRESETS/mundos/SEQ/medidor), canvas FLEXIBLE
// full-bleed (el visual llena el editor, redimensionable en el DAW, sin chrome de catálogo ni zoom S·M·L)
// y la franja de knobs abajo. La vista Metal se compone por encima del render JUCE: barra y franja viven
// FUERA de su área. Es FileDragAndDropTarget: soltar una imagen la carga como fuente de partículas (RF1).
// En app-mode la app esconde esta barra interna (setChromeVisible(false)) y pone su AppTopBar (derivada).
class SupernovaEditor : public ovni::PluginEditorBase,
                        public  juce::FileDragAndDropTarget,
                        private juce::Timer
{
public:
    explicit SupernovaEditor (SupernovaProcessor& p);
    ~SupernovaEditor() override;

    // --- drag & drop de imágenes (RF1) ---
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray& files, int x, int y) override;
    void fileDragExit  (const juce::StringArray& files) override;
    void filesDropped  (const juce::StringArray& files, int x, int y) override;

    bool keyPressed (const juce::KeyPress& k) override;   // Tab / I → inmersivo · F → fullscreen · Space → seq

    // --- H14 · hooks para el shell de la app (ADITIVOS; capa UI, NO tocan render/motor → goldens intactos) ---
    void setImmersive (bool on);                            // esconde/muestra la franja de knobs
    bool isImmersive() const noexcept          { return immersive; }
    void setChromeVisible (bool on);                        // app-mode: esconde el HUD propio (la app pone su barra)
    void setFullscreen (bool on)               { view.setFullscreen (on); }
    bool isFullscreen() const noexcept         { return view.isFullscreen(); }
    void setSyphonEnabled (bool on)            { view.setSyphonEnabled (on); }
    bool isSyphonActive() const noexcept       { return view.isSyphonActive(); }
    // fix 1 · app kiosk fullscreen (llena la pantalla ACTUAL con el visual, barra oculta). Si la app cablea
    // este hook, la tecla F togglea el fullscreen de la VENTANA de la app; si no, F cae al fullscreen de
    // salida a monitor del plugin (view.setFullscreen, para un 2º display dentro del DAW).
    std::function<void()> onAppFullscreenToggle;
    void setAppFullscreenActive (bool a) noexcept { appFsActive = a; }   // la app avisa el estado → Esc sale
    void toggleSequencePlayback();                          // Space / botón externo: play-pausa de la secuencia
    void clearCanvas();                                     // CLEAR desde la barra de la app (lienzo quieto YA)
    // UNDO/REDO (Phase C): RANDOM y CLEAR dejaron de ser puertas de una vía. captureUndoState() se llama
    // ANTES de una acción destructiva; doUndo/doRedo restauran el estado completo del APVTS (Cmd/Ctrl+Z, +Shift).
    void captureUndoState();
    bool doUndo();
    bool doRedo();
    float lfoModulation (const char* paramId, float base, double beatPos, double timeSec) const;   // base + LFOs, clampeado
    // CUADRO DEL RENDER (VBlank, message thread): recalcula los params del visual con la fase del momento y
    // se los pasa a la vista. Es lo que MetalViewComponent::tick dispara por cuadro — así los LFO corren a
    // 60/120 Hz en vez de a los 30 Hz del timer (informe 24 · M3/M4). El morph y el anillo de análisis del
    // export siguen en el timer: la base cambia a 30 Hz, la modulación a la tasa de la pantalla.
    void renderFrameTick();
    const ParticleParams& visualParams() const noexcept { return lastPp; }   // lo último que vio el visual
    void openWorldBrowser();
    void closeWorldBrowser();
    void generateThumbnails (int count);   // hilo de fondo: rinde un keyframe de cada mundo → tiles
    void openMediaPicker()            { chooseImage(); }    // LOAD de la barra (foto/video, multi = secuencia)
    void rotateMedia()                { rotateCurrentImage(); }  // ⟳ de la barra
    bool mediaRotatable() const       { return mediaCanRotate; }

    // EXPORT A VIDEO (Phase C · el loop para Reels/TikTok/release). Renderiza el LOOK actual con el audio
    // reciente por un camino offscreen dedicado (renderer fresco, no perturba la vista viva) → MP4 en ~/Movies.
    // Corre en un hilo de fondo; reporta al terminar por callAsync. No user-facing string en español.
    void exportVideo (ExportFormat fmt, int seconds = 8, int fps = 60, bool withSound = false);
    bool isExporting() const noexcept { return exporting.load(); }
    std::function<void (bool ok, juce::String msg)> onExportDone;   // la app muestra el resultado

    // WORLD BROWSER (Phase C · #2 mover): grilla de los 36 mundos con thumbnail vivo. Toggle desde la barra.
    void toggleWorldBrowser();
    bool isBrowserOpen() const noexcept { return browserOpen; }
    void toggleLfoPanel();   // panel de LFOs sync (Phase B UI)

    // ---- MEDIA SESSION PRO (spec 2026-09-02): la sesión de media como MODELO visible ----
    // La tira (MediaStrip) muestra estos items en orden; la secuencia (PhotoSequence) sigue siendo la fuente
    // de verdad; la foto/video ÚNICOS se presentan como sesión de 1 item.
    struct MediaItem { juce::String path; int rot = 0; bool isVideo = false; bool missing = false; };
    std::vector<MediaItem> mediaItems() const;
    int  currentMediaIndex() const;
    // click en la tira / ← → : salta al item. Con el reloj en BEATS el cue se ARMA y corta en el próximo
    // compás (beat snap); `immediate` (Shift, o el menú) corta al instante. En SECONDS/KICK siempre corta ya.
    // `beatPosNow` >= 0 = el beatPos EN QUE SE PIDIÓ el cue (lo estampa la cola MIDI); -1 = "ahora", que el
    // editor resuelve con su propio tick. Con eso la ventana del armado se fija al pedirlo, no al tickear.
    void cueMedia (int idx, bool immediate = false, double beatPosNow = -1.0);
    // ← / → : devuelve si HIZO algo. Con la sesión congelada (ningún vecino vivo) es false y la tecla no
    // se consume — sigue al host, como ya hacían 1-9/0 sobre un tile que no existe.
    bool stepMedia (int delta, bool immediate = false, double beatPosNow = -1.0);
    // Consultas de lo que está EN PANTALLA / EN LA MANO (read-only; las usa el shell de la app y los tests):
    // el path del item mostrado, y si el cue armado ya tiene su imagen decodificada esperando el compás.
    juce::String shownMediaPath() const noexcept { return shownPath; }
    bool         cuePrefetched()  const noexcept { return cueReady != nullptr; }
    bool         nextPrefetched() const noexcept { return seqNextReady != nullptr; }   // la SIGUIENTE, lista
    // El caché de decodes base (ronda 4 · R1): sólo lectura — cuántas fotos, cuánta RAM, hits/misses.
    const DecodedImageCache& decodedImages() const noexcept { return imageCache; }
    void removeMediaAt (int idx);            // ✕ / menú; el último → vuelve la imagen de fábrica
    // MEDIA FALTANTE (ronda 3): el archivo ya no está donde el proyecto lo dejó. El tile NO desaparece —
    // queda marcado y el reloj lo saltea — y desde su menú se relinkea: un archivo, o toda la carpeta de
    // una (todos los faltantes que compartían el directorio viejo, por nombre). Las versiones con `File`
    // hacen el trabajo (y son las que testeamos); las de un argumento abren el diálogo async.
    bool relinkMediaAt  (int idx, const juce::File& newFile);
    int  relinkFolderAt (int idx, const juce::File& newFolder);   // devuelve cuántos recuperó
    void chooseRelink       (int idx);       // menú del tile: "Relink…"
    void chooseRelinkFolder (int idx);       // menú del tile: "Relink folder…"
    void moveMedia (int from, int to);       // drag en la tira (AUTO canvas sigue al primero)
    // Soltar archivos/carpetas ENTRE dos tiles: se insertan EN esa posición (antes todo drop iba al final).
    // Sin secuencia todavía, cae al camino normal de ingesta (armar la sesión).
    void insertMedia (const juce::StringArray& files, int at);
    void rotateMediaAt (int idx);            // menú: rotar cualquier item (persiste por foto)
    void clearMedia();                       // vaciar la sesión
    void setMediaStripVisible (bool on);     // ▦ de la barra (persistido)
    bool isMediaStripVisible() const noexcept { return mediaStripOn; }
    bool mediaStripShown() const noexcept   { return mediaStrip.isVisible(); }   // visible de verdad (hay media, no inmersivo)

    // CANVAS FORMAT: AUTO (orientación del primer item) / FREE / 16:9 / 9:16 / 1:1 / 4:5 / 4:3. Es LAYOUT: la
    // vista Metal se centra con ese aspecto; el renderer hace FIT/FILL por target → goldens intactos.
    void         setCanvasFormat (CanvasFormat f);
    CanvasFormat canvasFormat() const noexcept   { return canvasFmt; }
    float        canvasAspect() const noexcept   { return resolvedAspect; }     // resuelto (0 = libre)
    juce::String canvasChipText() const;                                        // "AUTO 9:16" · "16:9" · "FREE"
    void         setFitMode (FitMode m);
    FitMode      fitMode() const noexcept        { return fitModeV; }
    juce::Rectangle<int> visualBounds() const    { return view.getBounds(); }   // el lienzo en pantalla (tests)
    const SupernovaView& visualView() const noexcept { return view; }           // read-only (tests del fundido)
    // El fundido que espera al PRIMER frame de un video (read-only; tests del fundido de video).
    double pendingVideoDissolveSeconds() const noexcept { return pendingVideoDissolve; }

    // CUE DE FOTOS POR MIDI (ronda 3): notas 72-87 = tiles 1..16, 88 = siguiente, 89 = anterior,
    // 90 = aleatoria. Por default el cue MIDI sigue la MISMA regla que la tira (en BEATS se arma y cae en
    // el compás; en SECONDS/KICK corta ya); "MIDI cue: now" lo hace siempre inmediato (persistido).
    void setMidiCueNow (bool on);
    bool midiCueImmediate() const noexcept { return midiCueNow; }

    // SEQ PRO: reloj SECONDS / BEATS / KICK · orden LOOP / SHUFFLE · transición CUT / BURST.
    void setSequenceClock (SeqClock c);
    void setSequenceOrder (SeqOrder o);
    void setSequenceBurst (bool on);
    void stepSequenceRate (int dir);         // − / + de la barra: segundos, beats o gap según el reloj

protected:
    void layoutBody (juce::Rectangle<int> body) override;
    void paintBody (juce::Graphics& g) override;   // letterbox del lienzo (negro + hairline)

private:
    void timerCallback() override;

    void chooseImage();                                    // botón IMG → FileChooser async (RF1; multi = secuencia)
    void ingestMedia (const juce::StringArray& files);     // el camino del drop/LOAD (1 = simple/agregar; 2+ = secuencia)
    static juce::StringArray expandFolders (const juce::StringArray& files);   // carpeta → sus fotos/videos por nombre
    void loadImageFile (const juce::File& file);           // decode en background → view.loadImage
    // FUNDIDO AUTOMÁTICO (2026-09-07): todo cambio de foto disuelve — no hay ajuste, es el motor. La
    // duración sale del RELOJ de la secuencia: min(0.7 s, 45% del intervalo), nunca menos de 0.1 s. Sin
    // secuencia (foto única, drag&drop, rotar) es el tope. Los DOS cortes declarados —la restauración de
    // una sesión al abrir (nada que fundir desde el gradiente de fábrica) y CLEAR— pasan 0 a mano.
    double dissolveSecondsNow() const;
    void rotateCurrentImage();                             // botón ⟳: endereza la foto/video vertical
    // Abre un video como fuente de color (false = no abrió). `dissolveSeconds` es cuánto tiene que durar la
    // disolución de su PRIMER frame sobre lo que está en pantalla — 0 = CORTE. Va como argumento OBLIGATORIO
    // porque el valor se ESTACIONA hasta que llegue ese frame: cuando lo seteaba el caller, dos caminos se
    // olvidaban y el fundido del video anterior (uno que se cerró sin llegar a mostrar nada) se le filtraba
    // al siguiente, que tenía que cortar (MEDIUM-1 del revisor del fundido).
    bool openVideo (const juce::File& file, double dissolveSeconds);
    // Muestra un item (imagen o video). `onFail` corre en el MESSAGE THREAD si el decode falla o el video
    // no abre: sin eso, un archivo ilegible dejaba la sesión "cargada" con la imagen de fábrica en pantalla.
    void showItem (const juce::File& file, int turns, double dissolveSeconds,
                   std::function<void()> onFail = {});
    // Decode (o giro desde el caché) en hilo de fondo; onDone SIEMPRE en el message thread. Ver PluginEditor.cpp.
    void decodeImageAsync (const juce::File& file, int quarterTurns,
                           std::function<void (std::shared_ptr<const LoadedImage>)> onDone);
    void videoTick();                                      // bombea frames del video → updateColors
    void demoteToSingleIfNeeded();                         // secuencia que quedó en 1 item → foto única real

    // PHOTO SEQUENCE (spec §D): el editor maneja el objeto del processor en el message thread.
    // (La UI del cluster SEQ vive en la TopBar, que pollea el estado a 30 Hz — sin updateSeqUi acá.)
    void hydrateSequence();                                // apvts.state("sequence") → objeto + arma la UI
    void hydratePerformanceState();                        // apvts.state → ccMap/LFOs/escenas (message thread, Phase B)
    void sequenceTick (double nowMs);                      // prefetch + switch (desde timerCallback)

    // MEDIA SESSION PRO (privado)
    void  refreshMediaStrip();                             // modelo → tira (+ pide miniaturas) + visibilidad + lienzo
    void  refreshMissingFlags();                           // filesystem → marcas de faltante de la secuencia
    void  showSequenceItem (int idx, double dissolveSeconds);   // muestra el item; si no decodifica, lo marca faltante
    void  updateMediaStripVisibility();                    // hay media && ▦ && no inmersivo
    void  unloadMedia();                                   // vuelve a la imagen de fábrica (sesión vacía)
    float sourceAspectForCanvas() const;                   // aspecto del PRIMER item (miniatura / imagen / video)
    void  resolveCanvas();                                 // AUTO → aspecto resuelto → relayout si cambió
    void  hydrateMediaSettings();                          // canvasFormat / fitMode / mediaStrip / srcAspect del state
    // La foto/video ÚNICO (sin secuencia) al state: props "singlePath" / "singleRot". Sin esto, cargar UNA
    // foto — el caso más común — no sobrevivía guardar el proyecto ni un preset, y el undo de sacarla o
    // rotarla era un checkpoint vacío (el snapshot del APVTS no la llevaba).
    void  syncSingleToState();
    // TODA escritura de la sesión al state pasa por acá: entre un `setStateInformation` del host y la
    // hidratación del editor (su próximo tick) el state es del HOST, no nuestro. Escribir en esa ventana
    // pisaba el proyecto recién cargado con la sesión vieja, y encima `hydrateSequence` lo daba por
    // "equivalente" y no reintentaba: el restore se perdía en silencio. La acción del usuario que caiga ahí
    // se pierde con la sesión vieja — eso es lo correcto.
    void  syncSequenceIfCurrent();
    void  applyMidiCue (const MidiCueMsg& m);              // un cue de la cola MIDI → cueMedia (msg thread)
    SeqTick seqTickNow (double nowMs) const;               // reloj de pared + beats + onsets para PhotoSequence

    SupernovaProcessor& proc;
    SupernovaView view;
    // FUNDIDO de un VIDEO: su primer frame entra por videoTick (no por decodeImageAsync), así que la
    // duración se estaciona acá entre showItem/openVideo y ese primer frame. Se consume una sola vez.
    double pendingVideoDissolve = 0.0;
    ControlStrip  controls;
    WorldBrowser  worldBrowser;           // grilla de mundos (Phase C)
    LfoPanel      lfoPanel;               // panel de LFOs sync (Phase B UI)
    MediaStrip    mediaStrip;             // la tira de miniaturas de la sesión (MEDIA SESSION PRO)
    MediaThumbCache thumbs;               // miniaturas en fondo (path#rot → Image + dims)
    // El decode BASE (sin la rotación manual) por path: girar con el ⟳ deja de re-leer el disco y de
    // re-correr Vision (ronda 4 · R1). Sólo el message thread lo toca → sin locks.
    DecodedImageCache imageCache;
    bool          mediaStripOn      = true;               // ▦ (persistido: "mediaStrip")
    bool          midiCueNow        = false;              // cue MIDI inmediato (persistido: "midiCueNow")
    CanvasFormat  canvasFmt         = CanvasFormat::Auto; // persistido: "canvasFormat"
    FitMode       fitModeV          = FitMode::Fit;       // persistido: "fitMode"
    float         resolvedAspect    = 0.0f;               // aspecto del lienzo en uso (0 = libre)
    float         knownSourceAspect = 0.0f;               // último aspecto del primer item (persistido: "srcAspect")
    juce::Rectangle<int> visualArea;                      // el área donde vive el lienzo (para el letterbox)
    bool          browserOpen = false;
    bool          lfoPanelOpen = false;
    std::thread   thumbThread;            // generación de thumbnails en background
    std::atomic<bool> thumbCancel { false };
    // fix 3 · cache de thumbnails: la clave es la IDENTIDAD de la imagen cargada. Sólo se regeneran los 36
    // cuando cambia la imagen (o la primera vez). thumbsValid se prende al COMPLETAR la generación.
    bool                 thumbsValid    = false;
    const LoadedImage*   thumbsForImage = nullptr;
    UndoStack<juce::ValueTree> history;   // deshacer/rehacer del estado completo (Phase C)
    std::unique_ptr<TopBar> topBar;       // la barra PRO (la misma de la app); oculta en app-mode
    bool mediaCanRotate = false;          // habilita el ⟳ de la barra (foto/video/secuencia cargada)
    void persistEditorSizeIfSettled();    // timer: tamaño → estado del DAW cuando el resize se asienta
    juce::Point<int> lastSeenEditorSize, lastSavedEditorSize;
    std::unique_ptr<juce::FileChooser> imageChooser;   // vivo mientras el diálogo async está abierto
    // Paths cuyo DECODE falló (el archivo está, pero no se puede leer: corrupto, truncado, formato raro).
    // Van aparte de existsAsFile porque el barrido del filesystem los "reviviría" en cada refresh y el
    // prefetch los reintentaría cada tick, a 30 Hz, para siempre.
    juce::StringArray decodeFailed;

    juce::String     seqPrefetchPath;                  // path en decode (vacío = nada en vuelo)
    std::shared_ptr<const LoadedImage> seqNextReady;   // la siguiente, decodificada y lista
    int              lastStateStamp = 0;               // detecta setStateInformation con editor abierto

    // Rotación de la foto ÚNICA (sin secuencia): archivo + cuartos de vuelta (el botón ⟳).
    juce::File       currentSingleFile;
    int              singleRot = 0;

    // Lo que está EN PANTALLA (path + cuartos de vuelta): un undo que no cambia el item visible no tiene por
    // qué re-decodificarlo (hipo visible + trabajo de más). markShown() lo registra en cada camino de carga.
    juce::String     shownPath;
    int              shownRot = 0;

    // CUE ARMADO (beat snap) — el decode arranca AL ARMAR, no al disparar: una foto de celular tarda
    // 100-400 ms entre JUCE, Vision y la máscara, y a 120 BPM el beat dura 500 ms; decodificar en el
    // disparo hacía que el "corte al compás" llegara a contratiempo. Un item de VIDEO no se pre-decodifica.
    juce::String                       cuePath;    // path del armado en decode (vacío = nada en vuelo)
    std::shared_ptr<const LoadedImage> cueReady;   // el armado, ya decodificado y listo para el corte
    void prefetchCue (int idx);                    // lanza el decode del armado (message thread)
    void cancelCuePrefetch() noexcept { cuePath.clear(); cueReady.reset(); }
    void markShown (const juce::File& f, int turns) noexcept { shownPath = f.getFullPathName(); shownRot = turns; }

    // VIDEO (spec §C): fuente de color viva. La geometría se fija con el PRIMER frame (loadImage), los
    // siguientes solo recolorean (updateColors). videoBuf = scratch del message thread.
    VideoSource          videoSource;
    bool                 videoGeomReady = false;
    std::vector<uint8_t> videoBuf;
    int                  videoW = 0, videoH = 0;

    // EXPORT (Phase C): retenemos la imagen actual + los params + un anillo de frames de análisis recientes
    // para que el export reproduzca el LOOK reactivo. El hilo de export toma COPIAS (sin estado compartido).
    std::shared_ptr<const LoadedImage> currentImage;
    ParticleParams                     lastPp;
    MorphSnapshot                      morphState {};   // base ease-ada del timer (30 Hz); los LFO se le
    int                                morphPresetIdx = 0;   // suman por CUADRO en renderFrameTick()
    // ANILLO DE ANÁLISIS del export: lo llena el TIMER del editor, un frame por tick — o sea a
    // kAnalysisRingHz, NO a los fps del clip. El export tiene que reproducirlo a ESTA tasa (si consume uno
    // por cuadro a 60 fps, la reactividad del MP4 corre al doble y se despega del audio muxeado).
    static constexpr int kAnalysisRingHz      = 30;   // = startTimerHz del editor (no deben divergir)
    // Tope del anillo: EL MISMO tramo que guarda el anillo de audio del processor. Si fuera más corto, el
    // clip repetiría cada kAnalysisRingSeconds y los 30 s de sonido nunca entrarían enteros (D-42 (b)).
    static constexpr int kAnalysisRingSeconds = kExportAudioRingSeconds;
    std::vector<AnalysisFrame>         recentFrames;   // anillo (message thread)
    std::thread                        exportThread;
    std::atomic<bool>                  exporting { false };
    std::atomic<bool>                  exportCancel { false };   // el dtor lo setea antes del join → no freeze

    bool          immersive = false;   // true → oculta la franja de knobs; las partículas llenan la ventana
    bool          chromeVisible = true; // false (app-mode) → oculta el HUD propio: la app pone su barra
    bool          appFsActive = false;  // fix 1: la app está en kiosk fullscreen → Esc sale
    bool          userImageLoaded = false;   // false → el HUD muestra el hint "IMG / arrastrá una imagen"

    // Preset morph (RF6, "sin corte seco"): el APVTS tiene el destino; el render recorre A→B. Estado en el editor.
    PresetMorph   morph;
    int           lastPresetIdx = -1;
    double        lastTickMs     = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SupernovaEditor)
};
}
