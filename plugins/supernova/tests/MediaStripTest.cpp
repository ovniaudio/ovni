// [supernova][mediastrip][media] — MediaStrip (la tira de miniaturas): geometría de tiles, hit-test, y las
// acciones por click (cue / + / ✕) con eventos de mouse sintéticos. JUCE puro, sin GPU.
#include <catch2/catch_test_macros.hpp>
#include <juce_gui_basics/juce_gui_basics.h>
#include "ui/MediaStrip.h"

using supernova::MediaStrip;

namespace
{
std::vector<MediaStrip::Item> threeItems()
{
    return { { "/tmp/a.png", 0, false }, { "/tmp/b.jpg", 1, false }, { "/tmp/c.mp4", 0, true } };
}

juce::MouseEvent eventAt (juce::Component& c, juce::Point<int> p, juce::ModifierKeys mods = {})
{
    auto src = juce::Desktop::getInstance().getMainMouseSource();
    const auto now = juce::Time::getCurrentTime();
    return juce::MouseEvent (src, p.toFloat(), mods, juce::MouseInputSource::defaultPressure,
                             juce::MouseInputSource::defaultOrientation, juce::MouseInputSource::defaultRotation,
                             juce::MouseInputSource::defaultTiltX, juce::MouseInputSource::defaultTiltY,
                             &c, &c, now, p.toFloat(), now, 1, false);
}

void click (MediaStrip& s, juce::Point<int> p)
{
    s.mouseDown (eventAt (s, p, juce::ModifierKeys::leftButtonModifier));
    s.mouseUp   (eventAt (s, p, juce::ModifierKeys::leftButtonModifier));
}
}

TEST_CASE ("mediastrip: los tiles van en orden con el tile + al final; el bloque izquierdo no es tile",
           "[supernova][mediastrip][media]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    MediaStrip s;
    s.setSize (1000, MediaStrip::kHeight);
    s.setItems (threeItems());
    REQUIRE (s.count() == 3);

    const auto t0 = s.tileBounds (0), t1 = s.tileBounds (1), t2 = s.tileBounds (2), tp = s.tileBounds (3);
    REQUIRE (t0.getX() >= MediaStrip::kInfoW);
    REQUIRE (t1.getX() == t0.getX() + MediaStrip::kTile + MediaStrip::kGap);
    REQUIRE (t2.getX() == t1.getX() + MediaStrip::kTile + MediaStrip::kGap);
    REQUIRE (tp.getX() == t2.getX() + MediaStrip::kTile + MediaStrip::kGap);   // el "+"
    REQUIRE (t0.getWidth() == MediaStrip::kTile);
    REQUIRE (t0.getHeight() == MediaStrip::kTile);
    REQUIRE (t0.getY() + t0.getHeight() <= MediaStrip::kHeight);

    REQUIRE (s.tileAt (t1.getCentre()) == 1);
    REQUIRE (s.tileAt (tp.getCentre()) == 3);              // count() = el "+"
    REQUIRE (s.tileAt ({ 20, 30 }) == -1);                 // bloque izquierdo
    REQUIRE (s.tileAt ({ tp.getRight() + 200, 30 }) == -1);
    REQUIRE (s.tileBounds (9).isEmpty());
    REQUIRE (s.isOnRemoveHotspot (1, { t1.getRight() - 4, t1.getY() + 4 }));
    REQUIRE (! s.isOnRemoveHotspot (1, t1.getCentre()));
}

TEST_CASE ("mediastrip: click = cue · click en + = agregar · click en ✕ = sacar", "[supernova][mediastrip][media]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    MediaStrip s;
    s.setSize (1000, MediaStrip::kHeight);
    s.setItems (threeItems());

    int picked = -1, removed = -1, added = 0, moved = 0;
    s.onPick   = [&] (int i, bool) { picked = i; };
    s.onRemove = [&] (int i) { removed = i; };
    s.onAdd    = [&] { ++added; };
    s.onMove   = [&] (int, int) { ++moved; };

    click (s, s.tileBounds (2).getCentre());
    REQUIRE (picked == 2);
    click (s, s.tileBounds (3).getCentre());
    REQUIRE (added == 1);
    const auto t1 = s.tileBounds (1);
    click (s, { t1.getRight() - 4, t1.getY() + 4 });
    REQUIRE (removed == 1);
    REQUIRE (moved == 0);                                   // un click no reordena
    click (s, { 20, 30 });                                  // bloque izquierdo: nada
    REQUIRE ((picked == 2 && added == 1 && removed == 1));

    s.setCurrent (2);
    REQUIRE (s.current() == 2);
    s.setProgress (0.5f);                                   // no crashea sin thumbSource
    s.setItems ({});                                        // vaciar → el actual se resetea
    REQUIRE (s.current() == -1);
}

TEST_CASE ("mediastrip: drag de un tile reordena (from → to) con el hueco cerrado", "[supernova][mediastrip][media]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    MediaStrip s;
    s.setSize (1000, MediaStrip::kHeight);
    s.setItems (threeItems());
    int from = -1, to = -1;
    s.onMove = [&] (int f, int t) { from = f; to = t; };

    const auto t0 = s.tileBounds (0), t2 = s.tileBounds (2);
    auto src = juce::Desktop::getInstance().getMainMouseSource();
    const auto now = juce::Time::getCurrentTime();
    const juce::Point<int> start = t0.getCentre();
    s.mouseDown (eventAt (s, start, juce::ModifierKeys::leftButtonModifier));
    // arrastrar más allá del umbral hasta después del tile 2 (inserción en el slot 3 → to = 2)
    const juce::Point<int> end (t2.getRight() + 2, t2.getCentreY());
    juce::MouseEvent drag (src, end.toFloat(), juce::ModifierKeys::leftButtonModifier, juce::MouseInputSource::defaultPressure,
                           juce::MouseInputSource::defaultOrientation, juce::MouseInputSource::defaultRotation,
                           juce::MouseInputSource::defaultTiltX, juce::MouseInputSource::defaultTiltY,
                           &s, &s, now, start.toFloat(), now, 1, true);
    s.mouseDrag (drag);
    s.mouseUp (drag);
    REQUIRE (from == 0);
    REQUIRE (to == 2);
}

// MEDIA SESSION PRO ronda 2 · T3 — el tile ARMADO (cue esperando el compás) se marca con borde punteado.
TEST_CASE ("mediastrip: setPendingCue marca el tile armado y setItems lo limpia", "[supernova][mediastrip][media]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    MediaStrip s;
    s.setSize (1000, MediaStrip::kHeight);
    s.setItems (threeItems());
    REQUIRE (s.pendingCue() == -1);

    s.setPendingCue (2);
    REQUIRE (s.pendingCue() == 2);
    s.setPendingCue (-1);
    REQUIRE (s.pendingCue() == -1);

    s.setPendingCue (1);
    s.setItems (threeItems());                 // la sesión cambió: el armado ya no significa nada
    REQUIRE (s.pendingCue() == -1);

    s.setPendingCue (9);                       // fuera de rango: no arma
    REQUIRE (s.pendingCue() == -1);

    // el tooltip del tile armado explica la espera y el atajo para cortar ya
    s.setPendingCue (2);
    s.mouseMove (eventAt (s, s.tileBounds (2).getCentre()));
    REQUIRE (s.getTooltip().contains ("armed"));
    REQUIRE (s.getTooltip().contains ("Shift"));
    s.mouseMove (eventAt (s, s.tileBounds (1).getCentre()));
    REQUIRE (s.getTooltip().startsWith ("Cue "));
}

// El click pasa el estado de SHIFT: en BEATS el cue espera el compás, con Shift corta YA.
TEST_CASE ("mediastrip: el click informa si venía con Shift (cue inmediato)", "[supernova][mediastrip][media]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    MediaStrip s;
    s.setSize (1000, MediaStrip::kHeight);
    s.setItems (threeItems());
    int picked = -1; bool wasImmediate = false;
    s.onPick = [&] (int i, bool now) { picked = i; wasImmediate = now; };

    click (s, s.tileBounds (1).getCentre());
    REQUIRE (picked == 1);
    REQUIRE (! wasImmediate);

    const auto p = s.tileBounds (2).getCentre();
    const auto mods = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier);
    s.mouseDown (eventAt (s, p, mods));
    s.mouseUp   (eventAt (s, p, mods));
    REQUIRE (picked == 2);
    REQUIRE (wasImmediate);
}

// [.uisnap] — la tira con un CUE ARMADO (borde punteado en el tile 2, actual el 0) → /tmp/snv-media-strip-cue.png
// para mirar con el ojo que el "armado" se distingue del "actual" y del hover.
TEST_CASE ("uisnap: tira con cue armado → /tmp/snv-media-strip-cue.png", "[supernova][.uisnap]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    MediaStrip s;
    s.setSize (620, MediaStrip::kHeight);
    s.setItems (threeItems());
    s.setCurrent (0);
    s.setProgress (0.42f);
    s.setPendingCue (2);
    REQUIRE (s.pendingCue() == 2);

    const auto img = s.createComponentSnapshot (s.getLocalBounds(), true, 2.0f);
    REQUIRE (img.isValid());
    juce::File f ("/tmp/snv-media-strip-cue.png");
    f.deleteFile();
    juce::FileOutputStream os (f);
    REQUIRE (os.openedOk());
    juce::PNGImageFormat png;
    REQUIRE (png.writeImageToStream (img, os));
}

// ==================== RONDA 3 · B — el tile FALTANTE (media offline) ====================
// El archivo se movió: el tile NO desaparece. Se queda marcado, con su nombre apagado, y el tooltip dice
// cómo recuperarlo (click derecho → Relink). Que el reloj lo saltee es cosa de la PhotoSequence.
TEST_CASE ("mediastrip: un item faltante se marca y su tooltip explica cómo relinkearlo",
           "[supernova][mediastrip][media][missing]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    MediaStrip s;
    s.setSize (1000, MediaStrip::kHeight);
    std::vector<MediaStrip::Item> items = threeItems();
    items[1].missing = true;
    s.setItems (items);
    s.setCurrent (0);

    REQUIRE (s.itemAt (1) != nullptr);
    REQUIRE (s.itemAt (1)->missing);
    REQUIRE (! s.itemAt (0)->missing);

    // hover sobre el faltante → el tooltip ofrece el relink; sobre uno normal, el cue de siempre
    s.mouseMove (eventAt (s, s.tileBounds (1).getCentre()));
    REQUIRE (s.getTooltip().contains ("Missing"));
    REQUIRE (s.getTooltip().contains ("relink"));
    s.mouseMove (eventAt (s, s.tileBounds (0).getCentre()));
    REQUIRE (s.getTooltip().startsWith ("Cue "));

    // pintar con un faltante no explota (el glifo ! reemplaza a la miniatura)
    juce::Image img (juce::Image::ARGB, 1000, MediaStrip::kHeight, true);
    juce::Graphics g (img);
    s.paint (g);
}

// [.uisnap] — la tira con un tile FALTANTE (glifo ! sobre fondo de aviso) al lado de los normales, para el
// ojo de Joaquín: se tiene que leer de un vistazo cuál es el hueco de la sesión.
TEST_CASE ("uisnap: tira con un item faltante → /tmp/snv-media-strip-missing.png", "[supernova][.uisnap]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    MediaStrip s;
    s.setSize (620, MediaStrip::kHeight);
    std::vector<MediaStrip::Item> items = threeItems();
    items[1].missing = true;
    s.setItems (items);
    s.setCurrent (0);
    s.setProgress (0.42f);

    const auto img = s.createComponentSnapshot (s.getLocalBounds(), true, 2.0f);
    REQUIRE (img.isValid());
    juce::File f ("/tmp/snv-media-strip-missing.png");
    f.deleteFile();
    juce::FileOutputStream os (f);
    REQUIRE (os.openedOk());
    juce::PNGImageFormat png;
    REQUIRE (png.writeImageToStream (img, os));
}

// ==================== RONDA 3 · C — soltar archivos ENTRE dos tiles ====================
// La tira es su propio FileDragAndDropTarget: soltar sobre ella inserta EN esa posición (JUCE entrega el
// drop al target más profundo que diga que le interesa, así que el editor no se lo lleva).
TEST_CASE ("mediastrip: soltar archivos entre dos tiles avisa la posición de inserción",
           "[supernova][mediastrip][media]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    MediaStrip s;
    s.setSize (1000, MediaStrip::kHeight);
    s.setItems (threeItems());

    juce::StringArray gotFiles; int gotAt = -99;
    s.onInsertFiles = [&] (const juce::StringArray& f, int at) { gotFiles = f; gotAt = at; };

    // Le interesan imágenes, videos y carpetas; nada más.
    REQUIRE (s.isInterestedInFileDrag ({ "/tmp/foto.png" }));
    REQUIRE (s.isInterestedInFileDrag ({ "/tmp/clip.mp4" }));
    REQUIRE (s.isInterestedInFileDrag ({ juce::File::getSpecialLocation (juce::File::tempDirectory).getFullPathName() }));
    REQUIRE (! s.isInterestedInFileDrag ({ "/tmp/notas.txt" }));

    // x en el borde entre el tile 0 y el 1 → insertar en 1
    const int between01 = s.tileBounds (1).getX() - MediaStrip::kGap / 2;
    s.filesDropped ({ "/tmp/nueva.png" }, between01, MediaStrip::kHeight / 2);
    REQUIRE (gotAt == 1);
    REQUIRE (gotFiles.size() == 1);
    REQUIRE (gotFiles[0] == "/tmp/nueva.png");

    // soltar pasado el último tile → al final
    s.filesDropped ({ "/tmp/otra.png" }, s.tileBounds (2).getRight() + MediaStrip::kGap, 10);
    REQUIRE (gotAt == 3);

    // el indicador de inserción se enciende con el drag y se apaga al salir
    s.fileDragMove ({ "/tmp/nueva.png" }, between01, 10);
    REQUIRE (s.fileDropIndex() == 1);
    s.fileDragExit ({ "/tmp/nueva.png" });
    REQUIRE (s.fileDropIndex() == -1);
}

// ================== RONDA 3b · F1 — click en un tile FALTANTE ==================
// Cuear un archivo que no está no muestra nada: la pantalla se queda con la foto anterior y la tira marca
// como actual un tile con el glifo `!`. Lo útil en ese click es lo que el usuario vino a hacer — encontrar
// el archivo — así que abre el menú (Relink…) en vez de disparar el cue. El ✕ sigue sacándolo.
TEST_CASE ("mediastrip: click en un tile faltante no dispara onPick", "[supernova][mediastrip][media][missing]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    MediaStrip s;
    s.setSize (1000, MediaStrip::kHeight);
    std::vector<MediaStrip::Item> items = threeItems();
    items[1].missing = true;
    s.setItems (items);

    int picked = -1, removed = -1;
    s.onPick   = [&] (int i, bool) { picked = i; };
    s.onRemove = [&] (int i) { removed = i; };

    click (s, s.tileBounds (1).getCentre());
    REQUIRE (picked == -1);                       // no se cuea a un archivo que no está
    juce::PopupMenu::dismissAllActiveMenus();     // (el click abrió el menú del relink)

    // Shift-click (el "cortar YA") tampoco.
    s.mouseDown (eventAt (s, s.tileBounds (1).getCentre(),
                          juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier));
    s.mouseUp   (eventAt (s, s.tileBounds (1).getCentre(),
                          juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier));
    REQUIRE (picked == -1);
    juce::PopupMenu::dismissAllActiveMenus();

    // un tile normal sigue cueando, y el ✕ del faltante sigue sacándolo
    click (s, s.tileBounds (0).getCentre());
    REQUIRE (picked == 0);
    const auto t1 = s.tileBounds (1);
    click (s, { t1.getRight() - 4, t1.getY() + 4 });
    REQUIRE (removed == 1);
}
