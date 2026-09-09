// [supernova][mediasession] — integración del editor (MEDIA SESSION PRO): la sesión como modelo (mediaItems),
// la tira aparece con media, el lienzo se letterboxea al formato (AUTO sigue al PRIMER item), cue ← →,
// reordenar, sacar (la última → fábrica), ▦ y persistencia en el state. Sin GPU: el layout es JUCE puro.
// [.uisnap]: vuelca el editor con la tira y el lienzo 9:16 a PNG para el ojo.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "image/PhotoSequence.h"
#include "image/CanvasFormat.h"

using Catch::Approx;

namespace
{
juce::File writePng (int w, int h, juce::Colour c)
{
    juce::Image img (juce::Image::RGB, w, h, true);
    img.clear (img.getBounds(), c);
    // una diagonal para que la miniatura no sea un bloque liso
    juce::Graphics g (img);
    g.setColour (c.contrasting (0.6f));
    g.drawLine (0.0f, 0.0f, (float) w, (float) h, (float) juce::jmax (2, w / 20));
    juce::File f = juce::File::createTempFile ("png");
    juce::FileOutputStream os (f);
    juce::PNGImageFormat png;
    png.writeImageToStream (img, os);
    os.flush();
    return f;
}

void pump (int ms)
{
    const double end = juce::Time::getMillisecondCounterHiRes() + ms;
    while (juce::Time::getMillisecondCounterHiRes() < end)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
}

float aspectOf (juce::Rectangle<int> r) { return r.getHeight() > 0 ? (float) r.getWidth() / (float) r.getHeight() : 0.0f; }
}

TEST_CASE ("mediasession: sesión de 2 fotos → tira visible, lienzo AUTO 9:16 por la vertical primera, formatos, cue, reordenar, sacar",
           "[supernova][mediasession][media]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File a = writePng (300, 600, juce::Colours::orange);    // VERTICAL (0.5)
    const juce::File b = writePng (600, 300, juce::Colours::teal);      // horizontal (2.0)

    supernova::SupernovaProcessor proc;
    supernova::PhotoSequence seq;
    seq.setFiles ({ a.getFullPathName(), b.getFullPathName() });
    proc.apvts.state.appendChild (seq.toValueTree(), nullptr);
    proc.apvts.state.setProperty ("srcAspect", 0.5, nullptr);   // lo que un proyecto guardado trae consigo

    auto holder = std::unique_ptr<juce::AudioProcessorEditor> (proc.createEditor());
    auto* ed = dynamic_cast<supernova::SupernovaEditor*> (holder.get());
    REQUIRE (ed != nullptr);
    ed->setBounds (0, 0, 1100, 760);

    // ---- modelo ----
    REQUIRE (ed->mediaItems().size() == 2);
    REQUIRE (ed->mediaItems()[0].path == a.getFullPathName());
    REQUIRE (! ed->mediaItems()[0].isVideo);
    REQUIRE (ed->currentMediaIndex() == 0);
    REQUIRE (ed->mediaStripShown());

    // ---- lienzo AUTO: vertical primera → 9:16 (sin esperar la miniatura: srcAspect persistido) ----
    REQUIRE (ed->canvasFormat() == supernova::CanvasFormat::Auto);
    REQUIRE (ed->canvasAspect() == Approx (9.0f / 16.0f));
    const auto vb = ed->visualBounds();
    REQUIRE (aspectOf (vb) == Approx (9.0f / 16.0f).margin (0.01f));
    REQUIRE (ed->canvasChipText() == "AUTO 9:16");

    // FREE → llena el área; 16:9 explícito → 16:9; persistido en el state
    ed->setCanvasFormat (supernova::CanvasFormat::Free);
    REQUIRE (ed->canvasAspect() == 0.0f);
    REQUIRE (ed->visualBounds().getWidth() > vb.getWidth());
    REQUIRE (ed->canvasChipText() == "FREE");
    ed->setCanvasFormat (supernova::CanvasFormat::Wide16x9);
    REQUIRE (aspectOf (ed->visualBounds()) == Approx (16.0f / 9.0f).margin (0.01f));
    REQUIRE ((int) proc.apvts.state.getProperty ("canvasFormat") == (int) supernova::CanvasFormat::Wide16x9);
    ed->setFitMode (supernova::FitMode::Fill);
    REQUIRE ((int) proc.apvts.state.getProperty ("fitMode") == 1);
    ed->setFitMode (supernova::FitMode::Fit);

    // ---- cue ← → ----
    ed->stepMedia (+1);
    REQUIRE (ed->currentMediaIndex() == 1);
    ed->stepMedia (-1);
    REQUIRE (ed->currentMediaIndex() == 0);
    ed->cueMedia (1);
    REQUIRE (proc.photoSequence().currentIndex() == 1);
    REQUIRE (proc.apvts.state.getChildWithName ("sequence").getProperty ("index").equals (1));   // persistido

    // ---- reordenar: b al inicio; la actual (b) sigue actual; AUTO pasa a seguir a b (horizontal → 16:9) ----
    ed->setCanvasFormat (supernova::CanvasFormat::Auto);
    ed->moveMedia (1, 0);
    REQUIRE (ed->mediaItems()[0].path == b.getFullPathName());
    REQUIRE (proc.photoSequence().currentPath() == b.getFullPathName());
    REQUIRE (ed->currentMediaIndex() == 0);
    for (int i = 0; i < 150 && std::abs (ed->canvasAspect() - 16.0f / 9.0f) > 1.0e-3f; ++i) pump (20);   // llega la miniatura
    REQUIRE (ed->canvasAspect() == Approx (16.0f / 9.0f));
    REQUIRE (ed->canvasChipText() == "AUTO 16:9");
    REQUIRE ((double) proc.apvts.state.getProperty ("srcAspect") == Approx (2.0).margin (0.01));

    // ---- sacar: queda 1 (foto única, la tira sigue) → sacar la última: fábrica, tira oculta, AUTO = FREE ----
    ed->removeMediaAt (1);
    REQUIRE (ed->mediaItems().size() == 1);
    REQUIRE (ed->mediaStripShown());
    REQUIRE (! proc.photoSequence().active());
    ed->removeMediaAt (0);
    REQUIRE (ed->mediaItems().empty());
    REQUIRE (! ed->mediaStripShown());
    REQUIRE (ed->canvasAspect() == 0.0f);
    REQUIRE (! proc.apvts.state.getChildWithName ("sequence").isValid());

    // ---- ▦ ----
    ed->setMediaStripVisible (false);
    REQUIRE (! ed->isMediaStripVisible());
    REQUIRE (! (bool) proc.apvts.state.getProperty ("mediaStrip"));

    holder.reset();
    a.deleteFile(); b.deleteFile();
}

TEST_CASE ("mediasession: reloj / orden / burst desde el editor persisten en el child sequence",
           "[supernova][mediasession][media]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File a = writePng (64, 64, juce::Colours::red);
    const juce::File b = writePng (64, 64, juce::Colours::blue);
    supernova::SupernovaProcessor proc;
    supernova::PhotoSequence seq;
    seq.setFiles ({ a.getFullPathName(), b.getFullPathName() });
    proc.apvts.state.appendChild (seq.toValueTree(), nullptr);
    auto holder = std::unique_ptr<juce::AudioProcessorEditor> (proc.createEditor());
    auto* ed = dynamic_cast<supernova::SupernovaEditor*> (holder.get());
    REQUIRE (ed != nullptr);

    ed->setSequenceClock (supernova::SeqClock::Beats);
    ed->stepSequenceRate (+1);                                   // 4 → 8 beats
    ed->setSequenceOrder (supernova::SeqOrder::Shuffle);
    ed->setSequenceBurst (true);
    const auto vt = proc.apvts.state.getChildWithName ("sequence");
    REQUIRE ((int) vt.getProperty ("clock") == (int) supernova::SeqClock::Beats);
    REQUIRE ((double) vt.getProperty ("beats") == 8.0);
    REQUIRE ((int) vt.getProperty ("order") == (int) supernova::SeqOrder::Shuffle);
    REQUIRE ((bool) vt.getProperty ("burst"));

    ed->setSequenceClock (supernova::SeqClock::Kick);
    ed->stepSequenceRate (-1);                                   // gap 1.0 → 0.75
    REQUIRE ((double) proc.apvts.state.getChildWithName ("sequence").getProperty ("kickGap") == Approx (0.75));

    holder.reset();
    a.deleteFile(); b.deleteFile();
}

// [.uisnap] — el editor con la TIRA y el lienzo AUTO 9:16 (3 fotos: vertical, horizontal, cuadrada) →
// /tmp/snv-media-session.png (2×) + la tira sola a /tmp/snv-media-strip.png. La vista Metal no sale en el
// snapshot JUCE (capa nativa): se ve el letterbox negro con hairline donde vive el lienzo.
TEST_CASE ("uisnap: editor con tira de media + lienzo 9:16 → /tmp/snv-media-session*.png", "[supernova][.uisnap]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File a = writePng (600, 1200, juce::Colours::orange);
    const juce::File b = writePng (1200, 600, juce::Colours::teal);
    const juce::File c = writePng (800, 800, juce::Colours::mediumpurple);

    supernova::SupernovaProcessor proc;
    supernova::PhotoSequence seq;
    seq.setFiles ({ a.getFullPathName(), b.getFullPathName(), c.getFullPathName() });
    proc.apvts.state.appendChild (seq.toValueTree(), nullptr);
    auto holder = std::unique_ptr<juce::AudioProcessorEditor> (proc.createEditor());
    auto* ed = dynamic_cast<supernova::SupernovaEditor*> (holder.get());
    REQUIRE (ed != nullptr);
    ed->setBounds (0, 0, 1100, 760);
    for (int i = 0; i < 150 && std::abs (ed->canvasAspect() - 9.0f / 16.0f) > 1.0e-3f; ++i) pump (20);
    pump (600);   // que lleguen las 3 miniaturas

    auto dump = [] (juce::Component& comp, const juce::String& path, float scale)
    {
        const auto img = comp.createComponentSnapshot (comp.getLocalBounds(), true, scale);
        REQUIRE (img.isValid());
        juce::File f (path);
        f.deleteFile();
        juce::FileOutputStream os (f);
        REQUIRE (os.openedOk());
        juce::PNGImageFormat png;
        REQUIRE (png.writeImageToStream (img, os));
    };
    dump (*ed, "/tmp/snv-media-session.png", 2.0f);
    ed->setCanvasFormat (supernova::CanvasFormat::Free);
    dump (*ed, "/tmp/snv-media-session-free.png", 2.0f);

    holder.reset();
    a.deleteFile(); b.deleteFile(); c.deleteFile();
}

// MEDIA SESSION PRO ronda 2 · T2 — DESHACER las operaciones de la sesión. Antes el undo (UndoStack del APVTS
// completo) cubría knobs/RANDOM/CLEAR pero NO sacar/reordenar/rotar/vaciar: sacar una foto por error era
// una puerta de una vía. Ahora cada operación destructiva captura estado ANTES, y el undo RE-HIDRATA la
// secuencia (no alcanza con restaurar el ValueTree: el objeto vivo del processor tiene que seguirlo).
TEST_CASE ("mediasession: Cmd+Z deshace SACAR y REORDENAR de la sesión (y el redo los rehace)",
           "[supernova][mediasession][media]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File a = writePng (64, 64, juce::Colours::red);
    const juce::File b = writePng (64, 64, juce::Colours::green);
    const juce::File c = writePng (64, 64, juce::Colours::blue);
    const juce::StringArray original { a.getFullPathName(), b.getFullPathName(), c.getFullPathName() };

    supernova::SupernovaProcessor proc;
    supernova::PhotoSequence seq;
    seq.setFiles (original);
    proc.apvts.state.appendChild (seq.toValueTree(), nullptr);

    auto holder = std::unique_ptr<juce::AudioProcessorEditor> (proc.createEditor());
    auto* ed = dynamic_cast<supernova::SupernovaEditor*> (holder.get());
    REQUIRE (ed != nullptr);
    ed->setBounds (0, 0, 1100, 760);

    auto paths = [&]
    {
        juce::StringArray out;
        for (const auto& m : ed->mediaItems()) out.add (m.path);
        return out;
    };
    REQUIRE (paths() == original);

    // ---- sacar → deshacer: vuelven las 3, en el orden original ----
    ed->removeMediaAt (1);
    REQUIRE (ed->mediaItems().size() == 2);
    REQUIRE (ed->doUndo());
    REQUIRE (paths() == original);
    REQUIRE (proc.photoSequence().size() == 3);

    // ---- reordenar → deshacer → rehacer ----
    ed->moveMedia (2, 0);
    const juce::StringArray moved { c.getFullPathName(), a.getFullPathName(), b.getFullPathName() };
    REQUIRE (paths() == moved);
    REQUIRE (ed->doUndo());
    REQUIRE (paths() == original);
    REQUIRE (ed->doRedo());
    REQUIRE (paths() == moved);

    // ---- vaciar la sesión → deshacer: vuelve entera ----
    ed->clearMedia();
    REQUIRE (ed->mediaItems().empty());
    REQUIRE (ed->doUndo());
    REQUIRE (paths() == moved);
    REQUIRE (ed->mediaStripShown());

    // ---- rotar → deshacer: vuelve la rotación ----
    const int rot0 = ed->mediaItems()[1].rot;
    ed->rotateMediaAt (1);
    REQUIRE (ed->mediaItems()[1].rot == (rot0 + 1) % 4);
    REQUIRE (ed->doUndo());
    REQUIRE (ed->mediaItems()[1].rot == rot0);

    // ---- cuear NO es una edición: no gasta undo (el siguiente undo sigue siendo el de la rotación) ----
    ed->cueMedia (2);
    REQUIRE (ed->currentMediaIndex() == 2);
    REQUIRE (ed->doRedo());                                  // el redo pendiente sigue siendo el de rotar
    REQUIRE (ed->mediaItems()[1].rot == (rot0 + 1) % 4);

    holder.reset();
    a.deleteFile(); b.deleteFile(); c.deleteFile();
}

// Un undo que NO toca la media (un knob, RANDOM) deja la sesión y la foto actual EXACTAMENTE donde estaban:
// re-hidratar no debe re-mostrar (ni re-decodificar) la misma foto.
TEST_CASE ("mediasession: un undo que no toca la media deja la sesión y el item actual intactos",
           "[supernova][mediasession][media]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File a = writePng (64, 64, juce::Colours::red);
    const juce::File b = writePng (64, 64, juce::Colours::green);

    supernova::SupernovaProcessor proc;
    supernova::PhotoSequence seq;
    seq.setFiles ({ a.getFullPathName(), b.getFullPathName() });
    proc.apvts.state.appendChild (seq.toValueTree(), nullptr);
    auto holder = std::unique_ptr<juce::AudioProcessorEditor> (proc.createEditor());
    auto* ed = dynamic_cast<supernova::SupernovaEditor*> (holder.get());
    REQUIRE (ed != nullptr);
    ed->cueMedia (1);
    REQUIRE (ed->currentMediaIndex() == 1);

    ed->captureUndoState();                                  // el patrón de RANDOM/CLEAR/preset
    ed->clearCanvas();
    REQUIRE (ed->doUndo());
    REQUIRE (ed->mediaItems().size() == 2);
    REQUIRE (ed->currentMediaIndex() == 1);                  // la foto actual no se movió
    REQUIRE (proc.photoSequence().currentPath() == b.getFullPathName());

    holder.reset();
    a.deleteFile(); b.deleteFile();
}

namespace
{
// Playhead de host para manejar el BeatClock desde el test (mismo patrón que ProcessorIntegrationTest).
struct MockPlayHead : juce::AudioPlayHead
{
    double bpm = 120.0, ppq = 0.0; bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setBpm (bpm);
        p.setPpqPosition (ppq);
        p.setIsPlaying (playing);
        return p;
    }
};
}

// MEDIA SESSION PRO ronda 2 · T3 — CUE AL COMPÁS. Con el reloj en BEATS, clickear un tile no corta al
// instante: espera al próximo límite de ventana (beat snap de Resolume). Shift corta YA. En SECONDS/KICK
// todo sigue como antes (inmediato).
TEST_CASE ("mediasession: en BEATS el cue espera el próximo compás; con Shift corta al instante",
           "[supernova][mediasession][media]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File a = writePng (64, 64, juce::Colours::red);
    const juce::File b = writePng (64, 64, juce::Colours::green);
    const juce::File c = writePng (64, 64, juce::Colours::blue);

    supernova::SupernovaProcessor proc;
    supernova::PhotoSequence seq;
    seq.setFiles ({ a.getFullPathName(), b.getFullPathName(), c.getFullPathName() });
    proc.apvts.state.appendChild (seq.toValueTree(), nullptr);

    MockPlayHead ph;
    ph.ppq = 1.0;
    proc.prepareToPlay (48000.0, 512);
    proc.setPlayHead (&ph);
    juce::AudioBuffer<float> buf (2, 512); buf.clear();
    juce::MidiBuffer midi;
    proc.processBlock (buf, midi);
    REQUIRE (proc.phaseInBeats() == Approx (1.0));

    auto holder = std::unique_ptr<juce::AudioProcessorEditor> (proc.createEditor());
    auto* ed = dynamic_cast<supernova::SupernovaEditor*> (holder.get());
    REQUIRE (ed != nullptr);
    ed->setBounds (0, 0, 1100, 760);
    ed->setSequenceClock (supernova::SeqClock::Beats);       // ventana por defecto: 4 beats (un compás)
    pump (60);

    // ---- armado: el cue NO corta, queda pendiente ----
    ed->cueMedia (2);
    REQUIRE (ed->currentMediaIndex() == 0);
    REQUIRE (proc.photoSequence().pendingCue() == 2);
    pump (100);                                              // sigue dentro de la ventana [0,4)
    REQUIRE (ed->currentMediaIndex() == 0);

    // ---- cruza el compás: dispara ----
    ph.ppq = 4.5;
    proc.processBlock (buf, midi);
    for (int i = 0; i < 40 && ed->currentMediaIndex() != 2; ++i) pump (20);
    REQUIRE (ed->currentMediaIndex() == 2);
    REQUIRE (proc.photoSequence().pendingCue() == -1);

    // ---- Shift: corta YA aunque el reloj sea BEATS ----
    ed->cueMedia (1, true);
    REQUIRE (ed->currentMediaIndex() == 1);
    REQUIRE (proc.photoSequence().pendingCue() == -1);

    // ---- SECONDS: inmediato como siempre ----
    ed->setSequenceClock (supernova::SeqClock::Seconds);
    ed->cueMedia (0);
    REQUIRE (ed->currentMediaIndex() == 0);
    REQUIRE (proc.photoSequence().pendingCue() == -1);

    holder.reset();
    proc.setPlayHead (nullptr);
    a.deleteFile(); b.deleteFile(); c.deleteFile();
}

// MEDIA SESSION PRO ronda 2 · T4 — TECLAS 1..9 y 0 cuean el tile N (0 = el décimo), como los decks de
// cualquier software de VJ. Si ese tile no existe (o no hay secuencia) la tecla NO se consume: sigue al host.
TEST_CASE ("mediasession: las teclas 1..9 y 0 cuean el tile N; sin ese tile la tecla sigue al host",
           "[supernova][mediasession][media]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    juce::Array<juce::File> files;
    juce::StringArray paths;
    for (int i = 0; i < 10; ++i)
    {
        files.add (writePng (64, 64, juce::Colour::fromHSV ((float) i / 10.0f, 0.7f, 0.8f, 1.0f)));
        paths.add (files[i].getFullPathName());
    }

    supernova::SupernovaProcessor proc;
    supernova::PhotoSequence seq;
    seq.setFiles (paths);
    proc.apvts.state.appendChild (seq.toValueTree(), nullptr);
    auto holder = std::unique_ptr<juce::AudioProcessorEditor> (proc.createEditor());
    auto* ed = dynamic_cast<supernova::SupernovaEditor*> (holder.get());
    REQUIRE (ed != nullptr);
    ed->setBounds (0, 0, 1100, 760);
    REQUIRE (ed->mediaItems().size() == 10);

    REQUIRE (ed->keyPressed (juce::KeyPress ('3')));
    REQUIRE (ed->currentMediaIndex() == 2);
    REQUIRE (ed->keyPressed (juce::KeyPress ('9')));
    REQUIRE (ed->currentMediaIndex() == 8);
    REQUIRE (ed->keyPressed (juce::KeyPress ('0')));      // 0 = el décimo
    REQUIRE (ed->currentMediaIndex() == 9);
    REQUIRE (ed->keyPressed (juce::KeyPress ('1')));
    REQUIRE (ed->currentMediaIndex() == 0);

    // Cmd+1 es del host (presets del DAW): no se roba.
    REQUIRE (! ed->keyPressed (juce::KeyPress ('2', juce::ModifierKeys::commandModifier, 0)));
    REQUIRE (ed->currentMediaIndex() == 0);

    // Sin ese tile: la tecla no se consume.
    for (int i = 9; i >= 3; --i) ed->removeMediaAt (i);
    REQUIRE (ed->mediaItems().size() == 3);
    REQUIRE (! ed->keyPressed (juce::KeyPress ('9')));
    REQUIRE (! ed->keyPressed (juce::KeyPress ('0')));
    REQUIRE (ed->currentMediaIndex() == 0);

    // Sin secuencia tampoco.
    ed->clearMedia();
    REQUIRE (! ed->keyPressed (juce::KeyPress ('1')));

    holder.reset();
    for (auto& f : files) f.deleteFile();
}

// En BEATS, la tecla respeta el beat snap (arma) y con Shift corta al instante.
TEST_CASE ("mediasession: en BEATS la tecla 2 arma el cue y Shift+2 corta ya", "[supernova][mediasession][media]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File a = writePng (64, 64, juce::Colours::red);
    const juce::File b = writePng (64, 64, juce::Colours::green);
    supernova::SupernovaProcessor proc;
    supernova::PhotoSequence seq;
    seq.setFiles ({ a.getFullPathName(), b.getFullPathName() });
    proc.apvts.state.appendChild (seq.toValueTree(), nullptr);
    auto holder = std::unique_ptr<juce::AudioProcessorEditor> (proc.createEditor());
    auto* ed = dynamic_cast<supernova::SupernovaEditor*> (holder.get());
    REQUIRE (ed != nullptr);
    ed->setSequenceClock (supernova::SeqClock::Beats);

    REQUIRE (ed->keyPressed (juce::KeyPress ('2')));
    REQUIRE (ed->currentMediaIndex() == 0);                       // armado, todavía no cortó
    REQUIRE (proc.photoSequence().pendingCue() == 1);
    REQUIRE (ed->keyPressed (juce::KeyPress ('2', juce::ModifierKeys::shiftModifier, 0)));
    REQUIRE (ed->currentMediaIndex() == 1);                       // Shift corta ya
    REQUIRE (proc.photoSequence().pendingCue() == -1);

    holder.reset();
    a.deleteFile(); b.deleteFile();
}

// ============================ RONDA 2b · F1 — la foto/video ÚNICO se guarda con el proyecto ============================
// Hasta acá `currentSingleFile`/`singleRot` vivían SÓLO en el editor: cargabas UNA foto (el caso más común),
// guardabas el proyecto del DAW o un preset, reabrías → imagen de fábrica. Y por lo mismo el undo de sacar /
// rotar esa foto era un checkpoint vacío. Ahora van como props `singlePath` / `singleRot` del state.

TEST_CASE ("mediasession: un state con singlePath restaura la foto única al abrir el editor",
           "[supernova][mediasession][media]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File a = writePng (300, 600, juce::Colours::orange);

    supernova::SupernovaProcessor proc;
    proc.apvts.state.setProperty ("singlePath", a.getFullPathName(), nullptr);
    proc.apvts.state.setProperty ("singleRot", 1, nullptr);

    auto holder = std::unique_ptr<juce::AudioProcessorEditor> (proc.createEditor());
    auto* ed = dynamic_cast<supernova::SupernovaEditor*> (holder.get());
    REQUIRE (ed != nullptr);
    ed->setBounds (0, 0, 1100, 760);

    REQUIRE (ed->mediaItems().size() == 1);
    REQUIRE (ed->mediaItems()[0].path == a.getFullPathName());
    REQUIRE (ed->mediaItems()[0].rot == 1);                      // la rotación manual vuelve con ella
    REQUIRE (ed->currentMediaIndex() == 0);
    REQUIRE (ed->mediaStripShown());

    holder.reset();
    a.deleteFile();
}

TEST_CASE ("mediasession: cargar una foto sola la persiste, y rotarla / sacarla se deshace",
           "[supernova][mediasession][media]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File a = writePng (240, 240, juce::Colours::hotpink);

    supernova::SupernovaProcessor proc;
    auto holder = std::unique_ptr<juce::AudioProcessorEditor> (proc.createEditor());
    auto* ed = dynamic_cast<supernova::SupernovaEditor*> (holder.get());
    REQUIRE (ed != nullptr);
    ed->setBounds (0, 0, 1100, 760);
    REQUIRE (ed->mediaItems().empty());

    ed->filesDropped ({ a.getFullPathName() }, 0, 0);             // el camino real del usuario
    pump (200);
    REQUIRE (ed->mediaItems().size() == 1);
    REQUIRE (proc.apvts.state.getProperty ("singlePath").toString() == a.getFullPathName());
    REQUIRE ((int) proc.apvts.state.getProperty ("singleRot", 0) == 0);

    // rotar → deshacer
    ed->rotateMediaAt (0);
    REQUIRE ((int) proc.apvts.state.getProperty ("singleRot") == 1);
    REQUIRE (ed->doUndo());
    REQUIRE ((int) proc.apvts.state.getProperty ("singleRot") == 0);
    REQUIRE (ed->mediaItems()[0].rot == 0);

    // sacar → deshacer: la foto VUELVE (antes el checkpoint estaba vacío y Cmd+Z no hacía nada)
    ed->removeMediaAt (0);
    REQUIRE (ed->mediaItems().empty());
    REQUIRE (! proc.apvts.state.hasProperty ("singlePath"));
    REQUIRE (ed->doUndo());
    REQUIRE (ed->mediaItems().size() == 1);
    REQUIRE (ed->mediaItems()[0].path == a.getFullPathName());
    REQUIRE (ed->mediaStripShown());

    holder.reset();
    a.deleteFile();
}

TEST_CASE ("mediasession: la foto única sobrevive el round-trip real get/setStateInformation",
           "[supernova][mediasession][media]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File a = writePng (240, 120, juce::Colours::seagreen);

    juce::MemoryBlock mb;
    {
        supernova::SupernovaProcessor proc;
        auto holder = std::unique_ptr<juce::AudioProcessorEditor> (proc.createEditor());
        auto* ed = dynamic_cast<supernova::SupernovaEditor*> (holder.get());
        REQUIRE (ed != nullptr);
        ed->filesDropped ({ a.getFullPathName() }, 0, 0);
        pump (200);
        REQUIRE (ed->mediaItems().size() == 1);
        proc.getStateInformation (mb);                            // lo que guarda el DAW / el preset
        holder.reset();
    }
    REQUIRE (mb.getSize() > 0);

    supernova::SupernovaProcessor proc2;                          // otra sesión: proyecto reabierto
    proc2.setStateInformation (mb.getData(), (int) mb.getSize());
    auto holder2 = std::unique_ptr<juce::AudioProcessorEditor> (proc2.createEditor());
    auto* ed2 = dynamic_cast<supernova::SupernovaEditor*> (holder2.get());
    REQUIRE (ed2 != nullptr);
    ed2->setBounds (0, 0, 1100, 760);
    REQUIRE (ed2->mediaItems().size() == 1);
    REQUIRE (ed2->mediaItems()[0].path == a.getFullPathName());

    // Y con el editor YA ABIERTO (el host carga un proyecto/preset en pantalla): el sello de estado hace
    // que el editor re-hidrate en su próximo tick.
    supernova::SupernovaProcessor proc3;
    auto holder3 = std::unique_ptr<juce::AudioProcessorEditor> (proc3.createEditor());
    auto* ed3 = dynamic_cast<supernova::SupernovaEditor*> (holder3.get());
    REQUIRE (ed3 != nullptr);
    ed3->setBounds (0, 0, 1100, 760);
    REQUIRE (ed3->mediaItems().empty());
    proc3.setStateInformation (mb.getData(), (int) mb.getSize());
    for (int i = 0; i < 50 && ed3->mediaItems().empty(); ++i) pump (20);
    REQUIRE (ed3->mediaItems().size() == 1);
    REQUIRE (ed3->mediaItems()[0].path == a.getFullPathName());
    REQUIRE (ed3->mediaStripShown());

    holder3.reset();
    holder2.reset();
    a.deleteFile();
}

TEST_CASE ("mediasession: un state VIEJO (sin las props) o con la foto borrada abre en fábrica sin crashear",
           "[supernova][mediasession][media]")
{
    juce::ScopedJuceInitialiser_GUI gui;

    {   // state viejo: ni singlePath ni sequence
        supernova::SupernovaProcessor proc;
        auto holder = std::unique_ptr<juce::AudioProcessorEditor> (proc.createEditor());
        auto* ed = dynamic_cast<supernova::SupernovaEditor*> (holder.get());
        REQUIRE (ed != nullptr);
        REQUIRE (ed->mediaItems().empty());
        REQUIRE (! ed->mediaStripShown());
        holder.reset();
    }
    {   // la foto ÚNICA que el proyecto recuerda ya no existe: sin tira donde marcarla, vuelve la fábrica
        const juce::File gone = writePng (64, 64, juce::Colours::grey);
        const juce::String path = gone.getFullPathName();
        gone.deleteFile();
        supernova::SupernovaProcessor proc;
        proc.apvts.state.setProperty ("singlePath", path, nullptr);
        auto holder = std::unique_ptr<juce::AudioProcessorEditor> (proc.createEditor());
        auto* ed = dynamic_cast<supernova::SupernovaEditor*> (holder.get());
        REQUIRE (ed != nullptr);
        REQUIRE (ed->mediaItems().empty());
        holder.reset();
    }
}

// ============================ RONDA 2b · F2 — el cue armado PRE-DECODIFICA ============================
// El beat snap no sirve si el corte cae 300 ms después del compás: al disparar había que decodificar la foto
// (JUCE + Vision + máscara). Ahora el decode arranca AL ARMAR, y al cruzar el compás la imagen ya está.
TEST_CASE ("mediasession: el cue armado se pre-decodifica y el corte cae EN el compás",
           "[supernova][mediasession][media]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File a = writePng (128, 128, juce::Colours::red);
    const juce::File b = writePng (128, 128, juce::Colours::green);
    const juce::File c = writePng (128, 128, juce::Colours::blue);

    supernova::SupernovaProcessor proc;
    supernova::PhotoSequence seq;
    seq.setFiles ({ a.getFullPathName(), b.getFullPathName(), c.getFullPathName() });
    proc.apvts.state.appendChild (seq.toValueTree(), nullptr);

    MockPlayHead ph;
    ph.ppq = 1.0;
    proc.prepareToPlay (48000.0, 512);
    proc.setPlayHead (&ph);
    juce::AudioBuffer<float> buf (2, 512); buf.clear();
    juce::MidiBuffer midi;
    proc.processBlock (buf, midi);

    auto holder = std::unique_ptr<juce::AudioProcessorEditor> (proc.createEditor());
    auto* ed = dynamic_cast<supernova::SupernovaEditor*> (holder.get());
    REQUIRE (ed != nullptr);
    ed->setBounds (0, 0, 1100, 760);
    ed->setSequenceClock (supernova::SeqClock::Beats);
    pump (60);

    REQUIRE (! ed->cuePrefetched());
    ed->cueMedia (2);
    REQUIRE (proc.photoSequence().pendingCue() == 2);
    for (int i = 0; i < 100 && ! ed->cuePrefetched(); ++i) pump (20);
    REQUIRE (ed->cuePrefetched());                       // el decode TERMINÓ antes del compás
    REQUIRE (ed->currentMediaIndex() == 0);              // y todavía no cortó

    ph.ppq = 4.5;                                        // cruza el compás
    proc.processBlock (buf, midi);
    for (int i = 0; i < 40 && ed->currentMediaIndex() != 2; ++i) pump (20);
    REQUIRE (ed->currentMediaIndex() == 2);
    REQUIRE (ed->shownMediaPath() == c.getFullPathName());
    REQUIRE (! ed->cuePrefetched());                     // el corte lo consumió

    holder.reset();
    proc.setPlayHead (nullptr);
    a.deleteFile(); b.deleteFile(); c.deleteFile();
}

// Salir de BEATS con un cue armado lo soltaría en el limbo: el tile quedaría punteado para siempre (en
// SECONDS/KICK no hay ventana de beats que cruzar). Cambiar de reloj lo cancela.
TEST_CASE ("mediasession: cambiar el reloj a SECONDS cancela el cue armado y su pre-decode",
           "[supernova][mediasession][media]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File a = writePng (128, 128, juce::Colours::red);
    const juce::File b = writePng (128, 128, juce::Colours::green);
    supernova::SupernovaProcessor proc;
    supernova::PhotoSequence seq;
    seq.setFiles ({ a.getFullPathName(), b.getFullPathName() });
    proc.apvts.state.appendChild (seq.toValueTree(), nullptr);
    auto holder = std::unique_ptr<juce::AudioProcessorEditor> (proc.createEditor());
    auto* ed = dynamic_cast<supernova::SupernovaEditor*> (holder.get());
    REQUIRE (ed != nullptr);
    ed->setSequenceClock (supernova::SeqClock::Beats);

    ed->cueMedia (1);
    for (int i = 0; i < 100 && ! ed->cuePrefetched(); ++i) pump (20);
    REQUIRE (proc.photoSequence().pendingCue() == 1);
    REQUIRE (ed->cuePrefetched());

    ed->setSequenceClock (supernova::SeqClock::Seconds);
    REQUIRE (proc.photoSequence().pendingCue() == -1);
    REQUIRE (! ed->cuePrefetched());
    REQUIRE (ed->currentMediaIndex() == 0);           // cancelar no es cortar

    holder.reset();
    a.deleteFile(); b.deleteFile();
}

TEST_CASE ("mediasession: sacar el item armado descarta el cue y su pre-decode", "[supernova][mediasession][media]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File a = writePng (128, 128, juce::Colours::red);
    const juce::File b = writePng (128, 128, juce::Colours::green);
    const juce::File c = writePng (128, 128, juce::Colours::blue);

    supernova::SupernovaProcessor proc;
    supernova::PhotoSequence seq;
    seq.setFiles ({ a.getFullPathName(), b.getFullPathName(), c.getFullPathName() });
    proc.apvts.state.appendChild (seq.toValueTree(), nullptr);
    auto holder = std::unique_ptr<juce::AudioProcessorEditor> (proc.createEditor());
    auto* ed = dynamic_cast<supernova::SupernovaEditor*> (holder.get());
    REQUIRE (ed != nullptr);
    ed->setSequenceClock (supernova::SeqClock::Beats);

    ed->cueMedia (2);
    for (int i = 0; i < 100 && ! ed->cuePrefetched(); ++i) pump (20);
    REQUIRE (ed->cuePrefetched());

    ed->removeMediaAt (2);                               // el item armado se fue
    REQUIRE (proc.photoSequence().pendingCue() == -1);
    REQUIRE (! ed->cuePrefetched());
    REQUIRE (ed->currentMediaIndex() == 0);

    holder.reset();
    a.deleteFile(); b.deleteFile(); c.deleteFile();
}

// ==================== RONDA 3 · A — CUE DE FOTOS POR MIDI (el instrumento en vivo) ====================
// En inmersivo o fullscreen la tira no está en pantalla y sólo quedan ← → y Space: el VJ dispara las fotos
// desde un pad. Camino REAL: nota → MidiMapper (audio thread) → MidiCueQueue (SPSC) → timer del editor
// (message thread, el único que puede tocar la PhotoSequence) → cueMedia.
namespace
{
// Manda una nota por el camino de verdad (processBlock) y deja que el timer del editor drene la cola.
void sendNote (supernova::SupernovaProcessor& proc, juce::AudioBuffer<float>& buf, int note, int vel = 100)
{
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) vel), 0);
    proc.processBlock (buf, midi);
}
}

TEST_CASE ("mediasession: la nota 73 cuea el tile 2; 88/89 = siguiente/anterior; 90 = otra distinta",
           "[supernova][mediasession][media][midi]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File a = writePng (64, 64, juce::Colours::red);
    const juce::File b = writePng (64, 64, juce::Colours::green);
    const juce::File c = writePng (64, 64, juce::Colours::blue);

    supernova::SupernovaProcessor proc;
    supernova::PhotoSequence seq;
    seq.setFiles ({ a.getFullPathName(), b.getFullPathName(), c.getFullPathName() });
    proc.apvts.state.appendChild (seq.toValueTree(), nullptr);
    proc.prepareToPlay (48000.0, 512);
    juce::AudioBuffer<float> buf (2, 512); buf.clear();

    auto holder = std::unique_ptr<juce::AudioProcessorEditor> (proc.createEditor());
    auto* ed = dynamic_cast<supernova::SupernovaEditor*> (holder.get());
    REQUIRE (ed != nullptr);
    ed->setBounds (0, 0, 1100, 760);
    REQUIRE (ed->currentMediaIndex() == 0);

    // 73 = tile 2 (el rango arranca en 72)
    sendNote (proc, buf, 73);
    for (int i = 0; i < 40 && ed->currentMediaIndex() != 1; ++i) pump (20);
    REQUIRE (ed->currentMediaIndex() == 1);

    // 88 = siguiente
    sendNote (proc, buf, 88);
    for (int i = 0; i < 40 && ed->currentMediaIndex() != 2; ++i) pump (20);
    REQUIRE (ed->currentMediaIndex() == 2);

    // 89 = anterior
    sendNote (proc, buf, 89);
    for (int i = 0; i < 40 && ed->currentMediaIndex() != 1; ++i) pump (20);
    REQUIRE (ed->currentMediaIndex() == 1);

    // 90 = aleatoria: otra, nunca la que está en pantalla
    sendNote (proc, buf, 90);
    for (int i = 0; i < 40 && ed->currentMediaIndex() == 1; ++i) pump (20);
    REQUIRE (ed->currentMediaIndex() != 1);
    REQUIRE (ed->currentMediaIndex() >= 0);

    // Un tile que no existe (la 87 = tile 16, con 3 fotos cargadas) no hace nada.
    const int before = ed->currentMediaIndex();
    sendNote (proc, buf, 87);
    pump (120);
    REQUIRE (ed->currentMediaIndex() == before);

    holder.reset();
    a.deleteFile(); b.deleteFile(); c.deleteFile();
}

TEST_CASE ("mediasession: en BEATS el cue por MIDI se arma y cae en el compás; con 'MIDI cue: now' corta ya",
           "[supernova][mediasession][media][midi]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File a = writePng (64, 64, juce::Colours::red);
    const juce::File b = writePng (64, 64, juce::Colours::green);
    const juce::File c = writePng (64, 64, juce::Colours::blue);

    supernova::SupernovaProcessor proc;
    supernova::PhotoSequence seq;
    seq.setFiles ({ a.getFullPathName(), b.getFullPathName(), c.getFullPathName() });
    proc.apvts.state.appendChild (seq.toValueTree(), nullptr);

    MockPlayHead ph;
    ph.ppq = 1.0;
    proc.prepareToPlay (48000.0, 512);
    proc.setPlayHead (&ph);
    juce::AudioBuffer<float> buf (2, 512); buf.clear();
    juce::MidiBuffer empty;
    proc.processBlock (buf, empty);

    auto holder = std::unique_ptr<juce::AudioProcessorEditor> (proc.createEditor());
    auto* ed = dynamic_cast<supernova::SupernovaEditor*> (holder.get());
    REQUIRE (ed != nullptr);
    ed->setBounds (0, 0, 1100, 760);
    ed->setSequenceClock (supernova::SeqClock::Beats);
    REQUIRE (! ed->midiCueImmediate());          // default: el cue MIDI sigue la misma regla que la tira
    pump (60);

    // ---- default (on the bar): la nota ARMA, no corta ----
    sendNote (proc, buf, 74);                    // tile 3
    for (int i = 0; i < 40 && proc.photoSequence().pendingCue() != 2; ++i) pump (20);
    REQUIRE (proc.photoSequence().pendingCue() == 2);
    pump (100);                                  // el tick registra la ventana de armado (como con el mouse)
    REQUIRE (ed->currentMediaIndex() == 0);      // sigue dentro de la ventana [0,4)

    ph.ppq = 4.5;                                 // cruza el compás
    proc.processBlock (buf, empty);
    for (int i = 0; i < 40 && ed->currentMediaIndex() != 2; ++i) pump (20);
    REQUIRE (ed->currentMediaIndex() == 2);
    REQUIRE (proc.photoSequence().pendingCue() == -1);

    // ---- "MIDI cue: now": corta al instante aunque el reloj sea BEATS ----
    ed->setMidiCueNow (true);
    REQUIRE (ed->midiCueImmediate());
    REQUIRE ((bool) proc.apvts.state.getProperty ("midiCueNow", false));   // persistido con el proyecto
    sendNote (proc, buf, 72);                    // tile 1
    for (int i = 0; i < 40 && ed->currentMediaIndex() != 0; ++i) pump (20);
    REQUIRE (ed->currentMediaIndex() == 0);
    REQUIRE (proc.photoSequence().pendingCue() == -1);

    holder.reset();
    proc.setPlayHead (nullptr);
    a.deleteFile(); b.deleteFile(); c.deleteFile();
}

TEST_CASE ("mediasession: sin secuencia activa (foto única) el cue por MIDI no hace nada",
           "[supernova][mediasession][media][midi]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File a = writePng (64, 64, juce::Colours::red);

    supernova::SupernovaProcessor proc;
    proc.apvts.state.setProperty ("singlePath", a.getFullPathName(), nullptr);
    proc.prepareToPlay (48000.0, 512);
    juce::AudioBuffer<float> buf (2, 512); buf.clear();

    auto holder = std::unique_ptr<juce::AudioProcessorEditor> (proc.createEditor());
    auto* ed = dynamic_cast<supernova::SupernovaEditor*> (holder.get());
    REQUIRE (ed != nullptr);
    ed->setBounds (0, 0, 1100, 760);
    REQUIRE (ed->mediaItems().size() == 1);

    sendNote (proc, buf, 88);      // "siguiente" sin secuencia: no hay a dónde ir
    sendNote (proc, buf, 73);
    sendNote (proc, buf, 90);
    pump (150);
    REQUIRE (ed->mediaItems().size() == 1);
    REQUIRE (ed->currentMediaIndex() == 0);
    REQUIRE (ed->shownMediaPath() == a.getFullPathName());

    holder.reset();
    a.deleteFile();
}

// ==================== RONDA 3 · B — MEDIA FALTANTE (el tile se queda, marcado) ====================
// Antes: movías la carpeta, reabrías el proyecto y las fotos habían DESAPARECIDO sin explicación (el
// restore podaba los paths muertos). Ahora el tile se queda marcado como faltante, el reloj lo saltea y
// se relinkea — archivo por archivo o toda la carpeta de una, como Premiere o Resolume.
namespace
{
juce::File tempDir (const juce::String& name)
{
    auto d = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (name);
    d.deleteRecursively();
    d.createDirectory();
    return d;
}

// PNG dentro de una carpeta concreta (para probar el relink de carpeta por nombre de archivo).
juce::File writePngIn (const juce::File& dir, const juce::String& name, juce::Colour c)
{
    juce::Image img (juce::Image::RGB, 64, 64, true);
    img.clear (img.getBounds(), c);
    auto f = dir.getChildFile (name);
    juce::FileOutputStream os (f);
    juce::PNGImageFormat png;
    png.writeImageToStream (img, os);
    os.flush();
    return f;
}
}

TEST_CASE ("mediasession: un archivo borrado NO desaparece de la sesión: queda marcado y el reloj lo saltea",
           "[supernova][mediasession][media][missing]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File a = writePng (64, 64, juce::Colours::red);
    const juce::File b = writePng (64, 64, juce::Colours::green);
    const juce::File c = writePng (64, 64, juce::Colours::blue);

    supernova::SupernovaProcessor proc;
    supernova::PhotoSequence seq;
    seq.setFiles ({ a.getFullPathName(), b.getFullPathName(), c.getFullPathName() });
    proc.apvts.state.appendChild (seq.toValueTree(), nullptr);

    b.deleteFile();     // el usuario movió/borró la del medio entre dos sesiones

    auto holder = std::unique_ptr<juce::AudioProcessorEditor> (proc.createEditor());
    auto* ed = dynamic_cast<supernova::SupernovaEditor*> (holder.get());
    REQUIRE (ed != nullptr);
    ed->setBounds (0, 0, 1100, 760);

    const auto items = ed->mediaItems();
    REQUIRE (items.size() == 3);                       // los TRES tiles siguen ahí
    REQUIRE (! items[0].missing);
    REQUIRE (items[1].missing);                        // el del medio, marcado
    REQUIRE (! items[2].missing);
    REQUIRE (items[1].path == b.getFullPathName());    // con su path viejo, para poder relinkearlo
    REQUIRE (ed->mediaStripShown());
    REQUIRE (proc.photoSequence().nextIndex() == 2);   // el reloj saltea el faltante

    holder.reset();
    a.deleteFile(); c.deleteFile();
}

TEST_CASE ("mediasession: relinkear un item faltante lo recupera (y vuelve a entrar en el reloj)",
           "[supernova][mediasession][media][missing]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File a = writePng (64, 64, juce::Colours::red);
    const juce::File b = writePng (64, 64, juce::Colours::green);
    const juce::File c = writePng (64, 64, juce::Colours::blue);

    supernova::SupernovaProcessor proc;
    supernova::PhotoSequence seq;
    seq.setFiles ({ a.getFullPathName(), b.getFullPathName(), c.getFullPathName() });
    proc.apvts.state.appendChild (seq.toValueTree(), nullptr);
    b.deleteFile();

    auto holder = std::unique_ptr<juce::AudioProcessorEditor> (proc.createEditor());
    auto* ed = dynamic_cast<supernova::SupernovaEditor*> (holder.get());
    REQUIRE (ed != nullptr);
    ed->setBounds (0, 0, 1100, 760);
    REQUIRE (ed->mediaItems()[1].missing);
    REQUIRE (proc.photoSequence().nextIndex() == 2);

    const juce::File found = writePng (64, 64, juce::Colours::green);   // apareció en otro lado
    REQUIRE (ed->relinkMediaAt (1, found));
    REQUIRE (! ed->mediaItems()[1].missing);
    REQUIRE (ed->mediaItems()[1].path == found.getFullPathName());
    REQUIRE (proc.photoSequence().nextIndex() == 1);                    // vuelve al reloj
    // y viaja con el proyecto
    REQUIRE (proc.apvts.state.getChildWithName ("sequence").getChild (1).getProperty ("path").toString()
             == found.getFullPathName());

    REQUIRE (! ed->relinkMediaAt (1, juce::File ("/tmp/no-existe-jamas.png")));   // un path muerto no relinkea

    holder.reset();
    a.deleteFile(); c.deleteFile(); found.deleteFile();
}

TEST_CASE ("mediasession: relinkear la CARPETA recupera de una todos los faltantes que vivían ahí",
           "[supernova][mediasession][media][missing]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    const auto oldDir = tempDir ("snv-relink-old");
    const auto newDir = tempDir ("snv-relink-new");
    const juce::File x = writePngIn (oldDir, "uno.png", juce::Colours::red);
    const juce::File y = writePngIn (oldDir, "dos.png", juce::Colours::green);
    const juce::File z = writePng (64, 64, juce::Colours::blue);        // este vive en otro lado y no se toca

    supernova::SupernovaProcessor proc;
    supernova::PhotoSequence seq;
    seq.setFiles ({ x.getFullPathName(), y.getFullPathName(), z.getFullPathName() });
    proc.apvts.state.appendChild (seq.toValueTree(), nullptr);

    // La carpeta se mudó: los dos archivos están, con el mismo nombre, en otro directorio.
    writePngIn (newDir, "uno.png", juce::Colours::red);
    writePngIn (newDir, "dos.png", juce::Colours::green);
    oldDir.deleteRecursively();

    auto holder = std::unique_ptr<juce::AudioProcessorEditor> (proc.createEditor());
    auto* ed = dynamic_cast<supernova::SupernovaEditor*> (holder.get());
    REQUIRE (ed != nullptr);
    ed->setBounds (0, 0, 1100, 760);
    REQUIRE (ed->mediaItems()[0].missing);
    REQUIRE (ed->mediaItems()[1].missing);
    REQUIRE (! ed->mediaItems()[2].missing);

    REQUIRE (ed->relinkFolderAt (0, newDir) == 2);        // los DOS de esa carpeta, de una
    const auto items = ed->mediaItems();
    REQUIRE (! items[0].missing);
    REQUIRE (! items[1].missing);
    REQUIRE (items[0].path == newDir.getChildFile ("uno.png").getFullPathName());
    REQUIRE (items[1].path == newDir.getChildFile ("dos.png").getFullPathName());
    REQUIRE (items[2].path == z.getFullPathName());       // el que no faltaba, intacto

    holder.reset();
    newDir.deleteRecursively();
    z.deleteFile();
}

// ==================== RONDA 3 · C — soltar archivos ENTRE dos tiles ====================
TEST_CASE ("mediasession: insertMedia mete las fotos en la posición pedida sin mover la que se ve",
           "[supernova][mediasession][media]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File a = writePng (64, 64, juce::Colours::red);
    const juce::File b = writePng (64, 64, juce::Colours::green);
    const juce::File c = writePng (64, 64, juce::Colours::blue);
    const juce::File nueva = writePng (64, 64, juce::Colours::yellow);

    supernova::SupernovaProcessor proc;
    supernova::PhotoSequence seq;
    seq.setFiles ({ a.getFullPathName(), b.getFullPathName(), c.getFullPathName() });
    proc.apvts.state.appendChild (seq.toValueTree(), nullptr);

    auto holder = std::unique_ptr<juce::AudioProcessorEditor> (proc.createEditor());
    auto* ed = dynamic_cast<supernova::SupernovaEditor*> (holder.get());
    REQUIRE (ed != nullptr);
    ed->setBounds (0, 0, 1100, 760);
    REQUIRE (ed->mediaItems().size() == 3);
    REQUIRE (ed->currentMediaIndex() == 0);

    ed->insertMedia ({ nueva.getFullPathName() }, 1);
    const auto items = ed->mediaItems();
    REQUIRE (items.size() == 4);
    REQUIRE (items[0].path == a.getFullPathName());
    REQUIRE (items[1].path == nueva.getFullPathName());   // entró entre la 1 y la 2
    REQUIRE (items[2].path == b.getFullPathName());
    REQUIRE (items[3].path == c.getFullPathName());
    REQUIRE (ed->currentMediaIndex() == 0);               // la que se ve no se movió
    // viaja con el proyecto
    REQUIRE (proc.apvts.state.getChildWithName ("sequence").getNumChildren() == 4);
    // y se deshace
    REQUIRE (ed->doUndo());
    REQUIRE (ed->mediaItems().size() == 3);

    holder.reset();
    a.deleteFile(); b.deleteFile(); c.deleteFile(); nueva.deleteFile();
}

TEST_CASE ("mediasession: soltar una CARPETA entre dos tiles inserta sus fotos ahí, en orden de nombre",
           "[supernova][mediasession][media]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    const auto dir = tempDir ("snv-insert-folder");
    writePngIn (dir, "1-uno.png", juce::Colours::red);
    writePngIn (dir, "2-dos.png", juce::Colours::green);
    dir.getChildFile ("leeme.txt").replaceWithText ("no soy media");

    const juce::File a = writePng (64, 64, juce::Colours::orange);
    const juce::File b = writePng (64, 64, juce::Colours::teal);

    supernova::SupernovaProcessor proc;
    supernova::PhotoSequence seq;
    seq.setFiles ({ a.getFullPathName(), b.getFullPathName() });
    proc.apvts.state.appendChild (seq.toValueTree(), nullptr);

    auto holder = std::unique_ptr<juce::AudioProcessorEditor> (proc.createEditor());
    auto* ed = dynamic_cast<supernova::SupernovaEditor*> (holder.get());
    REQUIRE (ed != nullptr);
    ed->setBounds (0, 0, 1100, 760);

    ed->insertMedia ({ dir.getFullPathName() }, 1);
    const auto items = ed->mediaItems();
    REQUIRE (items.size() == 4);                          // 2 + las 2 fotos de la carpeta (el .txt no)
    REQUIRE (items[1].path == dir.getChildFile ("1-uno.png").getFullPathName());
    REQUIRE (items[2].path == dir.getChildFile ("2-dos.png").getFullPathName());
    REQUIRE (items[3].path == b.getFullPathName());

    holder.reset();
    dir.deleteRecursively();
    a.deleteFile(); b.deleteFile();
}

TEST_CASE ("mediasession: sin secuencia todavía, soltar sobre la tira sigue el camino normal de ingesta",
           "[supernova][mediasession][media]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File a = writePng (64, 64, juce::Colours::red);
    const juce::File b = writePng (64, 64, juce::Colours::green);

    supernova::SupernovaProcessor proc;
    proc.apvts.state.setProperty ("singlePath", a.getFullPathName(), nullptr);
    auto holder = std::unique_ptr<juce::AudioProcessorEditor> (proc.createEditor());
    auto* ed = dynamic_cast<supernova::SupernovaEditor*> (holder.get());
    REQUIRE (ed != nullptr);
    ed->setBounds (0, 0, 1100, 760);
    REQUIRE (ed->mediaItems().size() == 1);

    ed->insertMedia ({ b.getFullPathName() }, 0);         // una foto sola + otra = se arma la secuencia
    REQUIRE (ed->mediaItems().size() == 2);
    REQUIRE (proc.photoSequence().active());

    holder.reset();
    a.deleteFile(); b.deleteFile();
}

// ==================== RONDA 3 · D2 — el cue armado al "siguiente" natural no se decodifica dos veces ====================
// Armar el cue lanza `prefetchCue`; si ese item es ADEMÁS el que sigue naturalmente, `sequenceTick` lanzaba
// su propio prefetch de la MISMA foto (dos decodes con Vision + máscara, 100-400 ms cada uno). Ahora se
// reusa. El test lo prueba en serio: una vez pre-decodificado el cue, BORRAMOS el archivo — si el tick
// lanzara su propio decode fallaría y el item quedaría marcado como faltante.
TEST_CASE ("mediasession: el cue armado al siguiente natural se REUSA como prefetch (un solo decode)",
           "[supernova][mediasession][media]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File a = writePng (128, 128, juce::Colours::red);
    const juce::File b = writePng (128, 128, juce::Colours::green);
    const juce::File c = writePng (128, 128, juce::Colours::blue);

    supernova::SupernovaProcessor proc;
    supernova::PhotoSequence seq;
    seq.setFiles ({ a.getFullPathName(), b.getFullPathName(), c.getFullPathName() });
    seq.setPlaying (false);                       // pausada: el tick no prefetchea hasta que la soltemos
    proc.apvts.state.appendChild (seq.toValueTree(), nullptr);

    auto holder = std::unique_ptr<juce::AudioProcessorEditor> (proc.createEditor());
    auto* ed = dynamic_cast<supernova::SupernovaEditor*> (holder.get());
    REQUIRE (ed != nullptr);
    ed->setBounds (0, 0, 1100, 760);
    ed->setSequenceClock (supernova::SeqClock::Beats);
    REQUIRE (! proc.photoSequence().playing());
    REQUIRE (proc.photoSequence().nextIndex() == 1);       // el siguiente natural es el 1

    ed->cueMedia (1);                                       // armamos JUSTO ese
    REQUIRE (proc.photoSequence().pendingCue() == 1);
    for (int i = 0; i < 100 && ! ed->cuePrefetched(); ++i) pump (20);
    REQUIRE (ed->cuePrefetched());
    REQUIRE (! ed->nextPrefetched());

    b.deleteFile();                                         // a partir de acá, decodificarla otra vez FALLA
    ed->toggleSequencePlayback();                           // ahora sí, el tick prefetchea
    for (int i = 0; i < 60 && ! ed->nextPrefetched(); ++i) pump (20);

    REQUIRE (ed->nextPrefetched());                         // la tiene: reusó la del cue
    REQUIRE (! ed->mediaItems()[1].missing);                // y no intentó (ni falló) un segundo decode

    holder.reset();
    a.deleteFile(); c.deleteFile();
}

// ==================== RONDA 3 · D1 — un archivo ilegible no deja una sesión fantasma ====================
// El state decía "hay una foto/video cargado", el archivo EXISTÍA pero no se podía leer (corrupto, sin GPU
// para el video) y la sesión quedaba "cargada" — un tile en la tira, userImageLoaded en true — con la
// imagen de FÁBRICA en pantalla. Ahora se vuelve a fábrica de verdad. (Mismo guard que ingestMedia.)
TEST_CASE ("mediasession: una foto única que no decodifica vuelve a fábrica (sin tile fantasma)",
           "[supernova][mediasession][media]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    juce::File rota = juce::File::createTempFile ("png");
    rota.replaceWithText ("esto no es un PNG");     // existe, pesa, y no decodifica

    supernova::SupernovaProcessor proc;
    proc.apvts.state.setProperty ("singlePath", rota.getFullPathName(), nullptr);

    auto holder = std::unique_ptr<juce::AudioProcessorEditor> (proc.createEditor());
    auto* ed = dynamic_cast<supernova::SupernovaEditor*> (holder.get());
    REQUIRE (ed != nullptr);
    ed->setBounds (0, 0, 1100, 760);

    for (int i = 0; i < 100 && ! ed->mediaItems().empty(); ++i) pump (20);   // el decode falla en background
    REQUIRE (ed->mediaItems().empty());             // sesión vacía: la fábrica es la fábrica
    REQUIRE (! ed->mediaStripShown());
    REQUIRE (ed->currentMediaIndex() == -1);
    REQUIRE (! proc.apvts.state.hasProperty ("singlePath"));   // y el state deja de prometerla

    holder.reset();
    rota.deleteFile();
}

TEST_CASE ("mediasession: un video único que no abre tampoco deja la sesión cargada",
           "[supernova][mediasession][media]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    juce::File roto = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("snv-video-roto.mp4");
    roto.deleteFile();
    roto.replaceWithText ("esto no es un mp4");

    supernova::SupernovaProcessor proc;
    proc.apvts.state.setProperty ("singlePath", roto.getFullPathName(), nullptr);

    auto holder = std::unique_ptr<juce::AudioProcessorEditor> (proc.createEditor());
    auto* ed = dynamic_cast<supernova::SupernovaEditor*> (holder.get());
    REQUIRE (ed != nullptr);
    ed->setBounds (0, 0, 1100, 760);
    pump (60);
    REQUIRE (ed->mediaItems().empty());
    REQUIRE (! ed->mediaStripShown());

    holder.reset();
    roto.deleteFile();
}

// D5 — deshacer la rotación de un VIDEO único. Lo que se puede observar desde afuera es el contrato: la
// rotación va y vuelve con el undo y la sesión queda entera. Que el video ya NO se reabra (antes se perdía
// el punto de reproducción y el primer frame) no se puede afirmar sin instrumentar el VideoSource sólo
// para el test, así que esto es la red de seguridad del camino, no la prueba del "sin reabrir".
TEST_CASE ("mediasession: rotar un video único y deshacerlo deja la rotación y la sesión donde estaban",
           "[supernova][mediasession][media][video]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File asset = juce::File (__FILE__).getParentDirectory().getChildFile ("assets/tiny.mp4");
    if (! asset.existsAsFile()) { SUCCEED ("asset tiny.mp4 ausente - test saltado"); return; }

    supernova::SupernovaProcessor proc;
    proc.apvts.state.setProperty ("singlePath", asset.getFullPathName(), nullptr);
    proc.apvts.state.setProperty ("singleRot", 0, nullptr);

    auto holder = std::unique_ptr<juce::AudioProcessorEditor> (proc.createEditor());
    auto* ed = dynamic_cast<supernova::SupernovaEditor*> (holder.get());
    REQUIRE (ed != nullptr);
    ed->setBounds (0, 0, 1100, 760);
    pump (120);
    REQUIRE (ed->mediaItems().size() == 1);

    ed->rotateMediaAt (0);
    pump (60);
    REQUIRE ((int) proc.apvts.state.getProperty ("singleRot") == 1);

    REQUIRE (ed->doUndo());
    pump (120);
    REQUIRE ((int) proc.apvts.state.getProperty ("singleRot") == 0);
    REQUIRE (ed->mediaItems().size() == 1);                       // la sesión sigue entera
    REQUIRE (ed->shownMediaPath() == asset.getFullPathName());

    holder.reset();
}

// ================== RONDA 3b · F1 — ningún camino de cue DIRECTO cae en un faltante ==================
// La ronda 3 hizo que el RELOJ saltee los faltantes, pero cuear a mano no miraba la marca: click en el
// tile, teclas 1-9/0, ← →, notas 72-87 y 88/89/90 hacían `jumpTo` a un archivo que no está. La pantalla se
// quedaba con la foto anterior y la tira marcaba como ACTUAL el tile con el glifo `!` (modelo y vista
// desincronizados). Ahora los caminos directos respetan `isMissing()`.
TEST_CASE ("mediasession: cuear un tile faltante (click, tecla, nota 73) no mueve el índice ni la pantalla",
           "[supernova][mediasession][media][missing]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File a = writePng (64, 64, juce::Colours::red);
    const juce::File b = writePng (64, 64, juce::Colours::green);
    const juce::File c = writePng (64, 64, juce::Colours::blue);

    supernova::SupernovaProcessor proc;
    supernova::PhotoSequence seq;
    seq.setFiles ({ a.getFullPathName(), b.getFullPathName(), c.getFullPathName() });
    proc.apvts.state.appendChild (seq.toValueTree(), nullptr);
    proc.prepareToPlay (48000.0, 512);
    juce::AudioBuffer<float> buf (2, 512); buf.clear();

    b.deleteFile();     // la del medio se movió entre dos sesiones

    auto holder = std::unique_ptr<juce::AudioProcessorEditor> (proc.createEditor());
    auto* ed = dynamic_cast<supernova::SupernovaEditor*> (holder.get());
    REQUIRE (ed != nullptr);
    ed->setBounds (0, 0, 1100, 760);
    pump (150);
    REQUIRE (ed->mediaItems()[1].missing);
    REQUIRE (ed->currentMediaIndex() == 0);
    REQUIRE (ed->shownMediaPath() == a.getFullPathName());

    // click en el tile (el camino del mouse y del menú "Cue now")
    ed->cueMedia (1);
    REQUIRE (ed->currentMediaIndex() == 0);
    REQUIRE (ed->shownMediaPath() == a.getFullPathName());
    ed->cueMedia (1, true);                          // Shift = "cortar ya": tampoco
    REQUIRE (ed->currentMediaIndex() == 0);

    // la tecla 2 no se consume: si el tile no se puede mostrar, la tecla sigue al host
    REQUIRE (! ed->keyPressed (juce::KeyPress ('2')));
    REQUIRE (ed->currentMediaIndex() == 0);

    // y la nota 73 (tile 2) por el camino de verdad: cola MIDI → timer del editor
    sendNote (proc, buf, 73);
    pump (150);
    REQUIRE (ed->currentMediaIndex() == 0);
    REQUIRE (ed->shownMediaPath() == a.getFullPathName());

    // el tile que SÍ está sigue cueándose igual
    REQUIRE (ed->keyPressed (juce::KeyPress ('3')));
    REQUIRE (ed->currentMediaIndex() == 2);

    // RONDA 3c · F10 — sin vecino vivo, ← / → no consumen la tecla: sigue al host, como ya hacían 1-9/0.
    ed->removeMediaAt (0);                    // queda [faltante, la que se ve]: no hay a dónde caminar
    REQUIRE (ed->mediaItems().size() == 2);
    REQUIRE (ed->mediaItems()[0].missing);
    REQUIRE (ed->currentMediaIndex() == 1);
    REQUIRE (! ed->keyPressed (juce::KeyPress (juce::KeyPress::rightKey)));
    REQUIRE (! ed->keyPressed (juce::KeyPress (juce::KeyPress::leftKey)));
    REQUIRE (ed->currentMediaIndex() == 1);

    holder.reset();
    a.deleteFile(); c.deleteFile();
}

TEST_CASE ("mediasession: 88/89 saltean el faltante y 90 nunca cae en él",
           "[supernova][mediasession][media][missing][midi]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    juce::Array<juce::File> files;
    juce::StringArray paths;
    for (int i = 0; i < 4; ++i)
    {
        files.add (writePng (64, 64, juce::Colour::fromHSV ((float) i / 4.0f, 0.7f, 0.8f, 1.0f)));
        paths.add (files[i].getFullPathName());
    }

    supernova::SupernovaProcessor proc;
    supernova::PhotoSequence seq;
    seq.setFiles (paths);
    proc.apvts.state.appendChild (seq.toValueTree(), nullptr);
    proc.prepareToPlay (48000.0, 512);
    juce::AudioBuffer<float> buf (2, 512); buf.clear();

    files.getReference (1).deleteFile();             // el tile 2 falta

    auto holder = std::unique_ptr<juce::AudioProcessorEditor> (proc.createEditor());
    auto* ed = dynamic_cast<supernova::SupernovaEditor*> (holder.get());
    REQUIRE (ed != nullptr);
    ed->setBounds (0, 0, 1100, 760);
    pump (150);
    REQUIRE (ed->mediaItems()[1].missing);
    REQUIRE (ed->currentMediaIndex() == 0);

    // 88 = siguiente: el vecino VIVO (el 2), no el faltante
    sendNote (proc, buf, 88);
    for (int i = 0; i < 40 && ed->currentMediaIndex() != 2; ++i) pump (20);
    REQUIRE (ed->currentMediaIndex() == 2);

    // 89 = anterior: hacia atrás también saltea el faltante (2 → 0, no 2 → 1)
    sendNote (proc, buf, 89);
    for (int i = 0; i < 40 && ed->currentMediaIndex() != 0; ++i) pump (20);
    REQUIRE (ed->currentMediaIndex() == 0);

    // 90 = aleatoria: cae en los vivos (2 o 3), jamás en el faltante
    for (int k = 0; k < 8; ++k)
    {
        sendNote (proc, buf, 90);
        pump (120);
        REQUIRE (ed->currentMediaIndex() != 1);
        REQUIRE (! ed->mediaItems()[(size_t) ed->currentMediaIndex()].missing);
    }

    // RONDA 3c · F8 — bajo SHUFFLE el pulgar sigue caminando por LA TIRA: 88 y 89 son espejo, así que
    // 88 seguido de 89 vuelve a donde estabas. El sorteo es del reloj automático, no de la tecla.
    ed->cueMedia (0, true);
    REQUIRE (ed->currentMediaIndex() == 0);
    ed->setSequenceOrder (supernova::SeqOrder::Shuffle);
    auto& sq = proc.photoSequence();
    for (uint32_t seed = 1; seed < 512 && sq.nextIndex() != 3; ++seed) sq.setSeed (seed);
    REQUIRE (sq.nextIndex() == 3);               // el reloj automático sortearía la 4ª…

    sendNote (proc, buf, 88);                    // …pero 88 va al vecino VIVO (el 3º, salteando el faltante)
    for (int i = 0; i < 40 && ed->currentMediaIndex() != 2; ++i) pump (20);
    REQUIRE (ed->currentMediaIndex() == 2);
    sendNote (proc, buf, 89);
    for (int i = 0; i < 40 && ed->currentMediaIndex() != 0; ++i) pump (20);
    REQUIRE (ed->currentMediaIndex() == 0);      // volver es volver

    holder.reset();
    for (auto& f : files) f.deleteFile();
}

// ================== RONDA 3b · F2 — el cue por MIDI se ancla en la nota, no en el tick ==================
// El armado fijaba su ventana en el PRIMER tick del editor con beatPos, y para el MIDI eran DOS ticks (el
// drenaje de la cola corría después de `sequenceTick`). Una nota tocada justo antes del downbeat perdía el
// compás que el VJ estaba anticipando y cortaba un compás entero tarde.
TEST_CASE ("mediasession: nota MIDI armada en 3.99 dispara en el tick de 4.01, no un compás después",
           "[supernova][mediasession][media][midi]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File a = writePng (64, 64, juce::Colours::red);
    const juce::File b = writePng (64, 64, juce::Colours::green);
    const juce::File c = writePng (64, 64, juce::Colours::blue);

    supernova::SupernovaProcessor proc;
    supernova::PhotoSequence seq;
    seq.setFiles ({ a.getFullPathName(), b.getFullPathName(), c.getFullPathName() });
    proc.apvts.state.appendChild (seq.toValueTree(), nullptr);

    MockPlayHead ph;
    ph.ppq = 0.0;
    proc.prepareToPlay (48000.0, 512);
    proc.setPlayHead (&ph);
    juce::AudioBuffer<float> buf (2, 512); buf.clear();
    juce::MidiBuffer empty;
    proc.processBlock (buf, empty);

    auto holder = std::unique_ptr<juce::AudioProcessorEditor> (proc.createEditor());
    auto* ed = dynamic_cast<supernova::SupernovaEditor*> (holder.get());
    REQUIRE (ed != nullptr);
    ed->setBounds (0, 0, 1100, 760);
    ed->setSequenceClock (supernova::SeqClock::Beats);
    pump (60);
    REQUIRE (ed->currentMediaIndex() == 0);

    // El reloj llega a 3.99 y la nota entra en ESE bloque (el pad toca anticipando el downbeat).
    ph.ppq = 3.99;
    proc.processBlock (buf, empty);
    REQUIRE (proc.phaseInBeats() == Approx (3.99));
    sendNote (proc, buf, 74);                    // tile 3, estampado con beatPos 3.99

    // El compás cruza ANTES de que el editor tickee (33 ms de ventana en la vida real).
    ph.ppq = 4.01;
    proc.processBlock (buf, empty);
    for (int i = 0; i < 40 && ed->currentMediaIndex() != 2; ++i) pump (20);
    REQUIRE (ed->currentMediaIndex() == 2);      // cayó EN el compás que el VJ anticipaba
    REQUIRE (proc.photoSequence().pendingCue() == -1);

    holder.reset();
    proc.setPlayHead (nullptr);
    a.deleteFile(); b.deleteFile(); c.deleteFile();
}

// ================== RONDA 3b · F4 — `decodeFailed` se limpia al sacar y al vaciar ==================
// `decodeFailed` es la lista de paths que EXISTEN pero no decodifican (corruptos, truncados): sin ella, el
// barrido de `existsAsFile` los "revivía" y el prefetch los reintentaba a 30 Hz para siempre. Pero no se
// limpiaba al sacar el item ni al vaciar la sesión: el path quedaba envenenado para toda la vida del editor,
// así que volver a cargar el MISMO archivo ya arreglado lo mostraba faltante.
namespace
{
// Un ".png" que no es un PNG: existe, pesa, y el decode falla.
juce::File writeBrokenPng()
{
    juce::File f = juce::File::createTempFile ("png");
    f.replaceWithText ("esto no es un PNG");
    return f;
}
}

TEST_CASE ("mediasession: sacar un item roto y volver a cargarlo arreglado no lo marca faltante",
           "[supernova][mediasession][media][missing]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File a = writePng (64, 64, juce::Colours::red);
    const juce::File c = writePng (64, 64, juce::Colours::blue);
    juce::File broken = writeBrokenPng();

    supernova::SupernovaProcessor proc;
    supernova::PhotoSequence seq;
    seq.setFiles ({ a.getFullPathName(), broken.getFullPathName(), c.getFullPathName() });
    proc.apvts.state.appendChild (seq.toValueTree(), nullptr);

    auto holder = std::unique_ptr<juce::AudioProcessorEditor> (proc.createEditor());
    auto* ed = dynamic_cast<supernova::SupernovaEditor*> (holder.get());
    REQUIRE (ed != nullptr);
    ed->setBounds (0, 0, 1100, 760);

    // el prefetch del "siguiente" lo intenta, falla y lo marca (el archivo está, pero no decodifica)
    for (int i = 0; i < 60 && ! ed->mediaItems()[1].missing; ++i) pump (20);
    REQUIRE (ed->mediaItems()[1].missing);

    // ---- SACAR el item roto y volver a cargarlo, ya arreglado, en el MISMO path ----
    ed->removeMediaAt (1);
    REQUIRE (ed->mediaItems().size() == 2);
    broken.deleteFile();
    const juce::File fixed = writePngIn (broken.getParentDirectory(), broken.getFileName(), juce::Colours::green);
    REQUIRE (fixed.getFullPathName() == broken.getFullPathName());

    ed->insertMedia ({ fixed.getFullPathName() }, 1);
    pump (60);
    const auto items = ed->mediaItems();
    REQUIRE (items.size() == 3);
    REQUIRE (items[1].path == fixed.getFullPathName());
    REQUIRE (! items[1].missing);                     // el path ya no está envenenado

    // ---- VACIAR la sesión también limpia la lista ----
    juce::File broken2 = writeBrokenPng();
    ed->clearMedia();
    ed->insertMedia ({ a.getFullPathName(), broken2.getFullPathName() }, 0);   // sin secuencia → ingesta normal
    for (int i = 0; i < 60 && ! ed->mediaItems()[1].missing; ++i) pump (20);
    REQUIRE (ed->mediaItems()[1].missing);

    ed->clearMedia();
    broken2.deleteFile();
    const juce::File fixed2 = writePngIn (broken2.getParentDirectory(), broken2.getFileName(), juce::Colours::yellow);
    ed->insertMedia ({ a.getFullPathName(), fixed2.getFullPathName() }, 0);
    pump (60);
    REQUIRE (ed->mediaItems().size() == 2);
    REQUIRE (! ed->mediaItems()[1].missing);

    holder.reset();
    a.deleteFile(); c.deleteFile(); fixed.deleteFile(); fixed2.deleteFile();
}

// ================== RONDA 3b · F5 — el salto a la primera viva se PERSISTE ==================
// Al hidratar, si la foto que el proyecto dejó en pantalla ya no está, la sesión se abre en la primera que
// SÍ está (`jumpTo`). Pero el índice nuevo no volvía al state: el proyecto seguía apuntando al muerto y
// cada reapertura repetía el salto (y un `getStateInformation` guardaba un índice que no era el que se veía).
TEST_CASE ("mediasession: al hidratar con la foto actual faltante, el salto a la primera viva se persiste",
           "[supernova][mediasession][media][missing]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File a = writePng (64, 64, juce::Colours::red);
    const juce::File b = writePng (64, 64, juce::Colours::green);
    const juce::File c = writePng (64, 64, juce::Colours::blue);

    supernova::SupernovaProcessor proc;
    supernova::PhotoSequence seq;
    seq.setFiles ({ a.getFullPathName(), b.getFullPathName(), c.getFullPathName() });
    seq.jumpTo (2);                                   // el proyecto se guardó mirando la tercera
    proc.apvts.state.appendChild (seq.toValueTree(), nullptr);
    REQUIRE ((int) proc.apvts.state.getChildWithName ("sequence").getProperty ("index") == 2);

    c.deleteFile();                                   // …y esa tercera ya no está

    auto holder = std::unique_ptr<juce::AudioProcessorEditor> (proc.createEditor());
    auto* ed = dynamic_cast<supernova::SupernovaEditor*> (holder.get());
    REQUIRE (ed != nullptr);
    ed->setBounds (0, 0, 1100, 760);
    pump (60);

    REQUIRE (ed->currentMediaIndex() == 0);           // abre en la primera que existe
    REQUIRE (ed->mediaItems().size() == 3);           // el faltante sigue en la sesión, marcado
    REQUIRE (ed->mediaItems()[2].missing);
    REQUIRE ((int) proc.apvts.state.getChildWithName ("sequence").getProperty ("index") == 0);

    holder.reset();
    a.deleteFile(); b.deleteFile();
}

// ================== RONDA 3c · F7 — un restore del host no se pierde por una escritura del editor ========
// F2 movió el drenaje de la cola MIDI ANTES de `sequenceTick` y con eso destapó una carrera: si el host
// llama `setStateInformation` (sello++ y child "sequence" nuevo en el state) y ANTES del próximo tick entra
// una nota de cue, el drenaje corre primero → `cueMedia` opera sobre la PhotoSequence VIEJA y termina en
// `syncSequenceToState()`, que PISA el child recién restaurado con la sesión vieja. Recién después
// `sequenceTick` ve el sello, `hydrateSequence` compara el state (ya pisado) contra la secuencia viva, da
// "equivalente" y toma el atajo: el proyecto que el host acaba de cargar se pierde en silencio, y
// `lastStateStamp` queda al día (no hay reintento). Vale para CUALQUIER escritura del editor en esa
// ventana — el MIDI sólo la volvió frecuente por diseño.
namespace
{
// El blob que un host le pasa a setStateInformation: un proyecto REAL con esta sesión adentro.
juce::MemoryBlock stateBlobWith (const juce::StringArray& paths)
{
    supernova::SupernovaProcessor p;
    supernova::PhotoSequence s;
    s.setFiles (paths);
    p.apvts.state.appendChild (s.toValueTree(), nullptr);
    juce::MemoryBlock blob;
    p.getStateInformation (blob);
    return blob;
}

juce::StringArray statePaths (supernova::SupernovaProcessor& proc)
{
    juce::StringArray out;
    for (const auto& c : proc.apvts.state.getChildWithName ("sequence"))
        out.add (c.getProperty ("path").toString());
    return out;
}

juce::StringArray itemPaths (const supernova::SupernovaEditor& ed)
{
    juce::StringArray out;
    for (const auto& m : ed.mediaItems()) out.add (m.path);
    return out;
}
}

TEST_CASE ("mediasession: un restore del host con una nota de cue en el mismo tick NO se pierde",
           "[supernova][mediasession][media][midi][restore]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File a1 = writePng (64, 64, juce::Colours::red);      // sesión A: la que está en pantalla
    const juce::File a2 = writePng (64, 64, juce::Colours::green);
    const juce::File a3 = writePng (64, 64, juce::Colours::blue);
    const juce::File b1 = writePng (64, 64, juce::Colours::yellow);   // sesión B: la que carga el host
    const juce::File b2 = writePng (64, 64, juce::Colours::magenta);
    const juce::StringArray bPaths { b1.getFullPathName(), b2.getFullPathName() };
    const auto blobB = stateBlobWith (bPaths);

    supernova::SupernovaProcessor proc;
    supernova::PhotoSequence seq;
    seq.setFiles ({ a1.getFullPathName(), a2.getFullPathName(), a3.getFullPathName() });
    proc.apvts.state.appendChild (seq.toValueTree(), nullptr);
    proc.prepareToPlay (48000.0, 512);
    juce::AudioBuffer<float> buf (2, 512); buf.clear();

    auto holder = std::unique_ptr<juce::AudioProcessorEditor> (proc.createEditor());
    auto* ed = dynamic_cast<supernova::SupernovaEditor*> (holder.get());
    REQUIRE (ed != nullptr);
    ed->setBounds (0, 0, 1100, 760);
    pump (100);
    REQUIRE (ed->mediaItems().size() == 3);          // en pantalla, la sesión A

    // El host carga otro proyecto… y el VJ toca el pad antes de que el editor tickee (33 ms de ventana).
    proc.setStateInformation (blobB.getData(), (int) blobB.getSize());
    sendNote (proc, buf, 88);                        // "siguiente"
    for (int i = 0; i < 60 && ed->mediaItems().size() != 2; ++i) pump (20);
    for (int i = 0; i < 40 && ed->currentMediaIndex() != 1; ++i) pump (20);   // el cue espera UN tick

    REQUIRE (itemPaths (*ed) == bPaths);             // manda el host: la sesión es la B
    REQUIRE (statePaths (proc) == bPaths);           // …y el child del state jamás fue pisado con la A
    REQUIRE (ed->currentMediaIndex() == 1);          // el cue tampoco se perdió: cayó sobre la sesión FRESCA

    holder.reset();
    a1.deleteFile(); a2.deleteFile(); a3.deleteFile(); b1.deleteFile(); b2.deleteFile();
}

TEST_CASE ("mediasession: un click en la tira en la ventana de un restore no pisa el state del host",
           "[supernova][mediasession][media][restore]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File a1 = writePng (64, 64, juce::Colours::red);
    const juce::File a2 = writePng (64, 64, juce::Colours::green);
    const juce::File a3 = writePng (64, 64, juce::Colours::blue);
    const juce::File b1 = writePng (64, 64, juce::Colours::yellow);
    const juce::File b2 = writePng (64, 64, juce::Colours::magenta);
    const juce::StringArray bPaths { b1.getFullPathName(), b2.getFullPathName() };
    const auto blobB = stateBlobWith (bPaths);

    supernova::SupernovaProcessor proc;
    supernova::PhotoSequence seq;
    seq.setFiles ({ a1.getFullPathName(), a2.getFullPathName(), a3.getFullPathName() });
    proc.apvts.state.appendChild (seq.toValueTree(), nullptr);

    auto holder = std::unique_ptr<juce::AudioProcessorEditor> (proc.createEditor());
    auto* ed = dynamic_cast<supernova::SupernovaEditor*> (holder.get());
    REQUIRE (ed != nullptr);
    ed->setBounds (0, 0, 1100, 760);
    pump (100);
    REQUIRE (ed->mediaItems().size() == 3);

    // La clase general de la carrera: el mouse (o una tecla, o el menú) en la MISMA ventana. La acción se
    // pierde con la sesión vieja — eso está bien; lo que no puede pasar es que pise el state del host.
    proc.setStateInformation (blobB.getData(), (int) blobB.getSize());
    ed->cueMedia (1);
    for (int i = 0; i < 60 && ed->mediaItems().size() != 2; ++i) pump (20);

    REQUIRE (itemPaths (*ed) == bPaths);
    REQUIRE (statePaths (proc) == bPaths);

    holder.reset();
    a1.deleteFile(); a2.deleteFile(); a3.deleteFile(); b1.deleteFile(); b2.deleteFile();
}

// ================== RONDA 3c · F11 — la nota 90 con todo lo demás faltante ==================
// `randomOtherIndex()` sortea sólo entre los VIVOS que no son el actual y, sin ninguno, devuelve el actual;
// `applyMidiCue` no cuea cuando el sorteo cae en la foto que ya está en pantalla. Eso estaba probado a nivel
// PhotoSequence pero no por el camino de verdad (cola MIDI → timer del editor). Hallazgo L2 de la 3b.
TEST_CASE ("mediasession: la nota 90 con todo lo demás faltante no hace nada",
           "[supernova][mediasession][media][missing][midi]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File a = writePng (64, 64, juce::Colours::red);
    const juce::File b = writePng (64, 64, juce::Colours::green);
    const juce::File c = writePng (64, 64, juce::Colours::blue);

    supernova::SupernovaProcessor proc;
    supernova::PhotoSequence seq;
    seq.setFiles ({ a.getFullPathName(), b.getFullPathName(), c.getFullPathName() });
    proc.apvts.state.appendChild (seq.toValueTree(), nullptr);
    proc.prepareToPlay (48000.0, 512);
    juce::AudioBuffer<float> buf (2, 512); buf.clear();

    b.deleteFile();      // la sesión se queda con UNA sola foto viva: la que está en pantalla
    c.deleteFile();

    auto holder = std::unique_ptr<juce::AudioProcessorEditor> (proc.createEditor());
    auto* ed = dynamic_cast<supernova::SupernovaEditor*> (holder.get());
    REQUIRE (ed != nullptr);
    ed->setBounds (0, 0, 1100, 760);
    pump (150);
    REQUIRE (ed->mediaItems().size() == 3);
    REQUIRE (ed->mediaItems()[1].missing);
    REQUIRE (ed->mediaItems()[2].missing);
    REQUIRE (ed->currentMediaIndex() == 0);
    REQUIRE (ed->shownMediaPath() == a.getFullPathName());

    for (int k = 0; k < 5; ++k)      // "otra cualquiera" cuando no hay ninguna otra: silencio, no un corte
    {
        sendNote (proc, buf, 90);
        pump (120);
        REQUIRE (ed->currentMediaIndex() == 0);
        REQUIRE (ed->shownMediaPath() == a.getFullPathName());
        REQUIRE (proc.photoSequence().pendingCue() == -1);   // ni siquiera queda un tile punteado
    }

    holder.reset();
    a.deleteFile();
}

// ================== RONDA 4 · F12 — la compuerta del restore, también en las preferencias de UI ==========
// Hermano de F7. `syncSingleToState()` y `syncSequenceIfCurrent()` ya callan cuando hay un
// `setStateInformation` pendiente (el sello no coincide), pero las CUATRO preferencias de UI escribían el
// state a pelo: `setMidiCueNow`, `setMediaStripVisible`, `setCanvasFormat` y `setFitMode`. Un click en la
// ventana de 33 ms entre el restore del host y el próximo tick pisaba la preferencia recién restaurada… y
// como `hydrateMediaSettings()` relee el state SIN comparar, se quedaba con lo pisado: no se autocorrige.
TEST_CASE ("mediasession: un toggle de UI en la ventana de un restore no pisa la preferencia del host",
           "[supernova][mediasession][media][restore]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File a1 = writePng (64, 64, juce::Colours::red);
    const juce::File a2 = writePng (64, 64, juce::Colours::green);
    const juce::File b1 = writePng (64, 64, juce::Colours::yellow);
    const juce::File b2 = writePng (64, 64, juce::Colours::magenta);

    // El proyecto B trae SUS preferencias de lienzo/tira/MIDI, distintas de los defaults y de las de A.
    juce::MemoryBlock blobB;
    {
        supernova::SupernovaProcessor p;
        supernova::PhotoSequence s;
        s.setFiles ({ b1.getFullPathName(), b2.getFullPathName() });
        p.apvts.state.appendChild (s.toValueTree(), nullptr);
        p.apvts.state.setProperty ("fitMode",      1, nullptr);   // FILL (default = FIT)
        p.apvts.state.setProperty ("canvasFormat", (int) supernova::CanvasFormat::Wide16x9, nullptr);
        p.apvts.state.setProperty ("mediaStrip",   false, nullptr);   // tira apagada (default = true)
        p.apvts.state.setProperty ("midiCueNow",   true,  nullptr);   // "MIDI cue: now" (default = false)
        p.getStateInformation (blobB);
    }

    supernova::SupernovaProcessor proc;
    supernova::PhotoSequence seq;
    seq.setFiles ({ a1.getFullPathName(), a2.getFullPathName() });
    proc.apvts.state.appendChild (seq.toValueTree(), nullptr);

    auto holder = std::unique_ptr<juce::AudioProcessorEditor> (proc.createEditor());
    auto* ed = dynamic_cast<supernova::SupernovaEditor*> (holder.get());
    REQUIRE (ed != nullptr);
    ed->setBounds (0, 0, 1100, 760);
    pump (100);
    REQUIRE (ed->mediaItems().size() == 2);
    REQUIRE (ed->fitMode() == supernova::FitMode::Fit);          // el proyecto A: todo en default
    REQUIRE (ed->canvasFormat() == supernova::CanvasFormat::Auto);

    // El host carga el proyecto B… y el VJ toca las cuatro preferencias antes de que el editor tickee.
    proc.setStateInformation (blobB.getData(), (int) blobB.getSize());
    ed->setFitMode (supernova::FitMode::Fit);
    ed->setCanvasFormat (supernova::CanvasFormat::Square1x1);
    ed->setMediaStripVisible (true);
    ed->setMidiCueNow (false);
    for (int i = 0; i < 60 && ed->mediaItems().size() != 2; ++i) pump (20);
    pump (120);

    // Manda el host: ni el state ni la UI se quedaron con lo del click.
    REQUIRE ((int) proc.apvts.state.getProperty ("fitMode") == 1);
    REQUIRE ((int) proc.apvts.state.getProperty ("canvasFormat") == (int) supernova::CanvasFormat::Wide16x9);
    REQUIRE ((bool) proc.apvts.state.getProperty ("mediaStrip") == false);
    REQUIRE ((bool) proc.apvts.state.getProperty ("midiCueNow") == true);
    REQUIRE (ed->fitMode() == supernova::FitMode::Fill);
    REQUIRE (ed->canvasFormat() == supernova::CanvasFormat::Wide16x9);
    REQUIRE (! ed->isMediaStripVisible());
    REQUIRE (ed->midiCueImmediate());

    holder.reset();
    a1.deleteFile(); a2.deleteFile(); b1.deleteFile(); b2.deleteFile();
}

// ================== RONDA 4 · R1 — girar es instantáneo: el decode base se cachea ========================
// "si yo doy vuelta la imagen tarda en ponerse ahí en el preview" (Joaquín, 3-sep). Cada cuarto de vuelta
// re-leía el archivo, lo re-decodificaba entero y re-corría las DOS pasadas de Vision. Ahora se decodifica
// SIEMPRE la base (rotación 0), se cachea por path y la rotación se deriva permutando píxeles y grillas.
// `decodedImages().misses()` cuenta los decodes de verdad (un miss = un decode base lanzado).
TEST_CASE ("mediasession: rotar cuatro veces la misma foto decodifica UNA sola vez",
           "[supernova][mediasession][media][imgcache]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File a = writePng (240, 160, juce::Colours::orange);

    supernova::SupernovaProcessor proc;
    auto holder = std::unique_ptr<juce::AudioProcessorEditor> (proc.createEditor());
    auto* ed = dynamic_cast<supernova::SupernovaEditor*> (holder.get());
    REQUIRE (ed != nullptr);
    ed->setBounds (0, 0, 1100, 760);
    ed->filesDropped ({ a.getFullPathName() }, 0, 0);
    for (int i = 0; i < 60 && ed->decodedImages().count() == 0; ++i) pump (20);

    REQUIRE (ed->decodedImages().count() == 1);
    const int decodesTrasCargar = ed->decodedImages().misses();
    REQUIRE (decodesTrasCargar == 1);                  // la carga: UN decode

    for (int k = 0; k < 4; ++k)                        // cuatro toques del ⟳ = vuelta completa
    {
        ed->rotateMedia();
        pump (150);
    }
    REQUIRE (ed->mediaItems()[0].rot == 0);            // volvió al derecho
    REQUIRE (ed->decodedImages().misses() == decodesTrasCargar);   // …sin un solo decode más
    REQUIRE (ed->decodedImages().hits() >= 4);         // los cuatro giros salieron del caché

    holder.reset();
    a.deleteFile();
}

TEST_CASE ("mediasession: rotar un item que NO está en pantalla no decodifica nada",
           "[supernova][mediasession][media][imgcache]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File a = writePng (200, 140, juce::Colours::red);
    const juce::File b = writePng (200, 140, juce::Colours::green);

    supernova::SupernovaProcessor proc;
    supernova::PhotoSequence seq;
    seq.setFiles ({ a.getFullPathName(), b.getFullPathName() });
    proc.apvts.state.appendChild (seq.toValueTree(), nullptr);

    auto holder = std::unique_ptr<juce::AudioProcessorEditor> (proc.createEditor());
    auto* ed = dynamic_cast<supernova::SupernovaEditor*> (holder.get());
    REQUIRE (ed != nullptr);
    ed->setBounds (0, 0, 1100, 760);
    // que la sesión se asiente: la ACTUAL decodificada + la SIGUIENTE prefetcheada (si no, el prefetch
    // normal llegaría en medio de la medición y se le achacaría al giro).
    for (int i = 0; i < 120 && ed->decodedImages().count() < 2; ++i) pump (20);
    REQUIRE (ed->decodedImages().count() == 2);
    const int antes = ed->decodedImages().misses();
    REQUIRE (ed->currentMediaIndex() == 0);

    ed->rotateMediaAt (1);                              // el OTRO tile: sólo cambia `rots` y su miniatura
    pump (250);                                         // (el prefetch se re-lanza con la rotación nueva)
    REQUIRE (ed->mediaItems()[1].rot == 1);
    REQUIRE (ed->mediaItems()[0].rot == 0);
    REQUIRE (ed->decodedImages().misses() == antes);    // cero decodes: el giro salió del caché

    holder.reset();
    a.deleteFile(); b.deleteFile();
}

TEST_CASE ("mediasession: sacar, relinkear y vaciar sueltan el decode cacheado",
           "[supernova][mediasession][media][imgcache]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File a = writePng (160, 120, juce::Colours::red);
    const juce::File b = writePng (160, 120, juce::Colours::green);
    const juce::File c = writePng (160, 120, juce::Colours::blue);
    const juce::File nuevo = writePng (160, 120, juce::Colours::yellow);

    supernova::SupernovaProcessor proc;
    supernova::PhotoSequence seq;
    seq.setFiles ({ a.getFullPathName(), b.getFullPathName(), c.getFullPathName() });
    proc.apvts.state.appendChild (seq.toValueTree(), nullptr);

    auto holder = std::unique_ptr<juce::AudioProcessorEditor> (proc.createEditor());
    auto* ed = dynamic_cast<supernova::SupernovaEditor*> (holder.get());
    REQUIRE (ed != nullptr);
    ed->setBounds (0, 0, 1100, 760);
    for (int i = 0; i < 120 && ed->decodedImages().count() < 2; ++i) pump (20);   // la actual + el prefetch
    REQUIRE (ed->decodedImages().contains (a.getFullPathName()));
    REQUIRE (ed->decodedImages().contains (b.getFullPathName()));

    ed->removeMediaAt (1);                       // sacar un item suelta SU entrada, no las otras
    pump (150);
    REQUIRE (! ed->decodedImages().contains (b.getFullPathName()));
    REQUIRE (ed->decodedImages().contains (a.getFullPathName()));

    // relinkear el item EN PANTALLA: su base cacheada es de un archivo que ya no es el suyo.
    REQUIRE (ed->relinkMediaAt (0, nuevo));
    pump (250);
    REQUIRE (ed->mediaItems()[0].path == nuevo.getFullPathName());
    REQUIRE (! ed->decodedImages().contains (a.getFullPathName()));
    REQUIRE (ed->decodedImages().contains (nuevo.getFullPathName()));

    ed->clearMedia();                            // vaciar la sesión devuelve TODA la RAM
    pump (150);
    REQUIRE (ed->decodedImages().count() == 0);
    REQUIRE (ed->decodedImages().bytes() == 0);

    holder.reset();
    a.deleteFile(); b.deleteFile(); c.deleteFile(); nuevo.deleteFile();
}

// [mediasession][dissolve] — FUNDIDO AUTOMÁTICO: todo cambio de foto disuelve, y la duración sale del RELOJ
// de la secuencia (min(0.7 s, 45% del intervalo), piso 0.1 s). El editor se la pasa a la vista JUNTO con la
// imagen; la vista se la da al renderer antes del upload. Sin GPU la vista no rinde nada, pero el valor que
// PIDE el editor se registra igual (lastDissolveSeconds) — que es exactamente lo que este test verifica.
TEST_CASE ("mediasession: la duración del fundido sale del reloj — SECONDS 8 s pide 0,7 y KICK 0,25 pide 0,1125",
           "[supernova][mediasession][dissolve]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File a = writePng (200, 200, juce::Colours::orange);
    const juce::File b = writePng (200, 200, juce::Colours::teal);

    supernova::SupernovaProcessor proc;
    supernova::PhotoSequence seq;
    seq.setFiles ({ a.getFullPathName(), b.getFullPathName() });
    seq.setIntervalSeconds (8.0);                       // el default de la casa
    proc.apvts.state.appendChild (seq.toValueTree(), nullptr);

    auto holder = std::unique_ptr<juce::AudioProcessorEditor> (proc.createEditor());
    auto* ed = dynamic_cast<supernova::SupernovaEditor*> (holder.get());
    REQUIRE (ed != nullptr);
    ed->setBounds (0, 0, 1100, 760);
    pump (250);

    // 1. La RESTAURACIÓN de la sesión al abrir el editor NO funde: no hay foto desde la cual disolver,
    //    sólo el gradiente de fábrica. Pide 0 = corte.
    REQUIRE (ed->visualView().lastDissolveSeconds() == Approx (0.0));

    // 2. Un cue a mano con el reloj en SECONDS 8 s: 45% de 8 son 3,6 → lo corta el tope de 0,7.
    ed->cueMedia (1);
    pump (250);
    REQUIRE (ed->currentMediaIndex() == 1);
    REQUIRE (ed->visualView().lastDissolveSeconds() == Approx (0.7));

    // 3. Con el reloj en KICK y el gap MÍNIMO (0,25 s) el fundido dura 0,1125 s: cierra ANTES del próximo
    //    kick, así una ráfaga nunca se pisa consigo misma.
    ed->setSequenceClock (supernova::SeqClock::Kick);
    proc.photoSequence().setKickGapSeconds (0.25);
    ed->cueMedia (0);
    pump (250);
    REQUIRE (ed->currentMediaIndex() == 0);
    REQUIRE (ed->visualView().lastDissolveSeconds() == Approx (0.1125));

    // 4. "Cortar YA" (Shift / MIDI cue: now) es no esperar al COMPÁS, no cortar en seco: sigue fundiendo.
    ed->setSequenceClock (supernova::SeqClock::Seconds);
    ed->cueMedia (1, /*immediate*/ true);
    pump (250);
    REQUIRE (ed->visualView().lastDissolveSeconds() == Approx (0.7));

    // 5. CLEAR es un corte declarado (el lienzo aparece YA): pide 0.
    ed->clearMedia();
    pump (250);
    REQUIRE (ed->visualView().lastDissolveSeconds() == Approx (0.0));

    holder.reset();
    a.deleteFile(); b.deleteFile();
}

// ================== MEDIUM-1 del revisor del 44 — el fundido de un video no queda estacionado ==============
// `pendingVideoDissolve` se estaciona entre `openVideo()` y el PRIMER frame decodificado (que llega por
// `videoTick`, no por `decodeImageAsync`). Si ese video se cierra ANTES de entregar su primer frame — CLEAR,
// o el decode que tarda — el valor quedaba parado en el miembro del editor y se lo comía el SIGUIENTE video,
// que tenía que CORTAR. El interleaving del revisor, sin threads: todo en el message thread.
TEST_CASE ("mediasession: un video cerrado antes de su primer frame no le deja el fundido al siguiente",
           "[supernova][mediasession][media][video][dissolve]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File asset = juce::File (__FILE__).getParentDirectory().getChildFile ("assets/tiny.mp4");
    if (! asset.existsAsFile()) { SUCCEED ("asset tiny.mp4 ausente - test saltado"); return; }

    supernova::SupernovaProcessor proc;
    supernova::PhotoSequence seq;
    const juce::File foto = writePng (128, 128, juce::Colours::orange);
    seq.setFiles ({ foto.getFullPathName(), asset.getFullPathName() });
    proc.apvts.state.appendChild (seq.toValueTree(), nullptr);

    auto holder = std::unique_ptr<juce::AudioProcessorEditor> (proc.createEditor());
    auto* ed = dynamic_cast<supernova::SupernovaEditor*> (holder.get());
    REQUIRE (ed != nullptr);
    ed->setBounds (0, 0, 1100, 760);
    pump (120);

    // 1) El reloj cuea el ITEM DE VIDEO: su primer frame tiene que entrar FUNDIENDO, así que la duración
    //    queda estacionada esperándolo.
    ed->cueMedia (1, true);
    const double estacionado = ed->pendingVideoDissolveSeconds();   // SIN pump: el primer frame no llegó
    INFO ("fundido estacionado tras cuear el video: " << estacionado);
    REQUIRE (estacionado > 0.0);      // si no hay nada estacionado, el test no está midiendo el bug

    // 2) CLEAR antes de que llegue ese primer frame. Vaciar la sesión es un CORTE declarado: no puede quedar
    //    nada esperando fundir.
    ed->clearMedia();
    CHECK (ed->pendingVideoDissolveSeconds() == 0.0);

    // 3) Se suelta un video NUEVO sobre la sesión ya vacía. Ese camino abre el video directo y también tiene
    //    que ser un CORTE — antes se comía el valor del paso 1 y disolvía sobre la imagen de fábrica.
    ed->filesDropped ({ asset.getFullPathName() }, 0, 0);
    pump (60);
    CHECK (ed->pendingVideoDissolveSeconds() == 0.0);

    // 4) Y restaurar una sesión guardada tampoco funde (misma regla que reabrir un proyecto).
    holder.reset();
    supernova::SupernovaProcessor proc2;
    proc2.apvts.state.setProperty ("singlePath", asset.getFullPathName(), nullptr);
    proc2.apvts.state.setProperty ("singleRot", 0, nullptr);
    auto holder2 = std::unique_ptr<juce::AudioProcessorEditor> (proc2.createEditor());
    auto* ed2 = dynamic_cast<supernova::SupernovaEditor*> (holder2.get());
    REQUIRE (ed2 != nullptr);
    ed2->setBounds (0, 0, 1100, 760);
    pump (60);
    CHECK (ed2->pendingVideoDissolveSeconds() == 0.0);

    holder2.reset();
    foto.deleteFile();
}
