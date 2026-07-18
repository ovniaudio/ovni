#pragma once
#include "template/PluginEditorBase.h"
#include "ui/SupernovaView.h"
#include "ui/ControlStrip.h"
#include "ui/WorldBrowser.h"
#include "ui/LfoPanel.h"
#include "presets/PresetMorph.h"
#include "presets/UndoStack.h"
#include "video/VideoSource.h"
#include "video/ExportPreset.h"
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
    float lfoModulation (const char* paramId, double beatPos) const;   // suma de LFOs sync que apuntan al param
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

protected:
    void layoutBody (juce::Rectangle<int> body) override;

private:
    void timerCallback() override;

    void chooseImage();                                    // botón IMG → FileChooser async (RF1; multi = secuencia)
    void loadImageFile (const juce::File& file);           // decode en background → view.loadImage
    void rotateCurrentImage();                             // botón ⟳: endereza la foto/video vertical
    void openVideo (const juce::File& file);               // abre un video como fuente de color
    void showItem (const juce::File& file, int turns);     // muestra un item de secuencia (imagen o video)
    void videoTick();                                      // bombea frames del video → updateColors
    void demoteToSingleIfNeeded();                         // secuencia que quedó en 1 item → foto única real

    // PHOTO SEQUENCE (spec §D): el editor maneja el objeto del processor en el message thread.
    // (La UI del cluster SEQ vive en la TopBar, que pollea el estado a 30 Hz — sin updateSeqUi acá.)
    void hydrateSequence();                                // apvts.state("sequence") → objeto + arma la UI
    void hydratePerformanceState();                        // apvts.state → ccMap/LFOs/escenas (message thread, Phase B)
    void sequenceTick (double nowMs);                      // prefetch + switch (desde timerCallback)

    SupernovaProcessor& proc;
    SupernovaView view;
    ControlStrip  controls;
    WorldBrowser  worldBrowser;           // grilla de mundos (Phase C)
    LfoPanel      lfoPanel;               // panel de LFOs sync (Phase B UI)
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

    juce::String     seqPrefetchPath;                  // path en decode (vacío = nada en vuelo)
    std::shared_ptr<const LoadedImage> seqNextReady;   // la siguiente, decodificada y lista
    int              lastStateStamp = 0;               // detecta setStateInformation con editor abierto

    // Rotación de la foto ÚNICA (sin secuencia): archivo + cuartos de vuelta (el botón ⟳).
    juce::File       currentSingleFile;
    int              singleRot = 0;

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
