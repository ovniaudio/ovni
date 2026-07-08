// DiccionarioTest.cpp — [diccionario][dust]: las reglas del Diccionario OVNI sobre el editor REAL.
//
//   · Layout §4/§5: SYNC no pisa IN PHASE; DUCK vive DENTRO de la columna de utilidad sin pisar
//     IN/OUT ni el meter; rail y utilidad son regiones DISJUNTAS; el campo no pisa a nadie.
//     Verificación DETERMINÍSTICA por bounds (no por captura).
//   · Drag del ORIGIN 1:1: screenToField(fieldToScreen(p)) == p (±1px) — el drag invierte EXACTO
//     el mapeo del dibujo (bug real de PULSAR: la cruz se movía ~1/3 del mouse).
//   · El RATE controla la cadencia de burbujas del campo (patrón dbgSpawnsOverFrames de HALO).
//   · Convención de signo medida (no a ojo): ORIGIN a pantalla-DERECHA suena canal-DERECHO
//     (BAL_dB < 0 con el mecanismo HRIR encendido).
//   · Honestidad: los CORTES de la curaduría (TONE/DAMP, órbita del ORIGIN) NO existen como params.
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdio>
#include "template/tests/OvniTestHarness.h"
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "params/ParameterIDs.h"
#include "ui/DustField.h"

namespace pid = dust::params::id;

// ── Layout: regiones disjuntas (Diccionario §4, verificación §5 por bounds) ───────────────────────
TEST_CASE ("DUST: SYNC/DUCK/rail/campo NO pisan la utilidad (bounds disjuntos)", "[diccionario][dust]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    dust::DustProcessor proc;
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    REQUIRE (ed != nullptr);
    ed->setSize (960, 580);   // fuerza el layout (layoutBody) al tamaño base
    auto* de = dynamic_cast<dust::DustEditor*> (ed.get());
    REQUIRE (de != nullptr);

    const auto sync  = de->dbgSyncBounds();
    const auto ip    = de->dbgInPhaseBounds();
    const auto duckB = de->dbgDuckBounds();
    const auto inB   = de->dbgInBounds();
    const auto met   = de->dbgMeterBounds();
    const auto rail   = de->dbgRailBounds();
    const auto macros = de->dbgMacrosBounds();
    const auto util   = de->dbgUtilBounds();
    const auto fieldB = de->dbgFieldBounds();

    INFO ("sync=" << sync.toString().toStdString() << "  inPhase=" << ip.toString().toStdString()
          << "  duck=" << duckB.toString().toStdString() << "  util=" << util.toString().toStdString());

    // SYNC (en el rail) jamás toca el IN PHASE (columna de utilidad).
    REQUIRE_FALSE (sync.intersects (ip));
    REQUIRE (sync.getRight() <= ip.getX());   // SYNC entero a la IZQUIERDA del IN PHASE

    // DUCK se mudó al RAIL DE MACROS (es una macro, como en AURORA/HORIZON): NO vive en la utilidad,
    // queda BAJO el campo (en el rail inferior), alineado con las otras macros.
    REQUIRE_FALSE (util.intersects (duckB));        // ya NO está en la columna de utilidad
    REQUIRE (fieldB.getBottom() <= duckB.getY());   // DUCK en el rail inferior, bajo el campo
    REQUIRE_FALSE (duckB.intersects (fieldB));

    // Utilidad (orden HALO, igual que el resto del sello): meter ARRIBA → IN/OUT → IN PHASE (disjuntos).
    REQUIRE_FALSE (inB.intersects (met));
    REQUIRE_FALSE (inB.intersects (ip));
    REQUIRE (met.getBottom() <= inB.getY());        // meter ARRIBA de IN/OUT
    REQUIRE (inB.getBottom() <= ip.getY());         // IN/OUT ARRIBA del IN PHASE

    // Rail vs utilidad: regiones DISJUNTAS por construcción (la unión del rail queda a la izquierda
    // de la columna de utilidad).
    REQUIRE_FALSE (rail.intersects (util));

    // El campo no pisa a nadie. El rail de DUST es de DOS regiones (SYNC izquierda + macros abajo):
    // se verifica el no-solape del campo contra CADA región por separado (la unión rail engloba el
    // campo por bounding-box, que es artefacto de caja y no solape real — por eso no se usa acá).
    REQUIRE (sync.getRight()    <= fieldB.getX());   // SYNC (rail izquierdo) entero a la IZQUIERDA del campo
    REQUIRE (fieldB.getBottom() <= macros.getY());   // campo entero ARRIBA del rail de macros (inferior)
    REQUIRE_FALSE (fieldB.intersects (macros));
    REQUIRE_FALSE (fieldB.intersects (util));
    REQUIRE_FALSE (fieldB.isEmpty());              // el campo existe y domina (≈60% del cuerpo)
    REQUIRE (fieldB.getWidth() * fieldB.getHeight() > (960 * 580) / 5);
}

// ── Drag del ORIGIN 1:1: el mapeo del drag invierte EXACTAMENTE el del dibujo ─────────────────────
TEST_CASE ("DUST: drag del ORIGIN 1:1 (screenToField invierte fieldToScreen, ±1px)", "[diccionario][dust]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    std::atomic<float> a0 {0.35f}, a1 {0.55f}, a2 {0.4f}, a3 {0.6f}, a4 {0.25f}, a5 {0.0f},
                       ax {0.0f}, ay {0.35f}, aw {0.0f};
    dust::ui::BubbleEventFifo fifo;
    dust::ui::DustField field (a0, a1, a2, a3, a4, a5, ax, ay, aw, fifo, nullptr, nullptr);
    field.setBounds (0, 0, 640, 360);   // tamaño realista del campo (fuerza Rx≠Ry: mapeo elíptico)

    const juce::Point<float> pts[5] = {
        { 0.0f, 0.0f }, { 0.5f, 0.35f }, { -1.0f, -1.0f }, { 1.0f, -0.4f }, { -0.3f, 1.0f } };

    // Tolerancia ±1px traducida a coords de campo por eje (el campo es elíptico: Rx≠Ry).
    const float tolX = 1.0f / (640.0f * 0.5f - 16.0f);
    const float tolY = 1.0f / (360.0f * 0.5f - 16.0f);

    for (const auto& p : pts)
    {
        const auto s  = field.dbgFieldToScreen (p);
        const auto rt = field.dbgScreenToField (s);
        INFO ("p=(" << p.x << "," << p.y << ")  screen=(" << s.x << "," << s.y
              << ")  roundtrip=(" << rt.x << "," << rt.y << ")");
        REQUIRE (std::abs (rt.x - p.x) <= tolX);
        REQUIRE (std::abs (rt.y - p.y) <= tolY);

        // Y la vuelta en px: el marcador queda BAJO el cursor (±1px) — drag 1:1 de verdad.
        const auto s2 = field.dbgFieldToScreen (rt);
        REQUIRE (std::abs (s2.x - s.x) <= 1.0f);
        REQUIRE (std::abs (s2.y - s.y) <= 1.0f);
    }

    // Convención de pantalla: campo x+ = DERECHA de pantalla, y+ = FRENTE = ARRIBA.
    const auto centre = field.dbgFieldToScreen ({ 0.0f, 0.0f });
    REQUIRE (field.dbgFieldToScreen ({ 1.0f, 0.0f }).x > centre.x);
    REQUIRE (field.dbgFieldToScreen ({ 0.0f, 1.0f }).y < centre.y);
}

// ── El RATE controla la cadencia de nacimientos del campo (ninguna macro sin efecto visual) ───────
TEST_CASE ("DUST campo: el RATE controla la cadencia de burbujas (no reloj fijo)", "[diccionario][dust]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    std::atomic<float> mix {0.35f}, rate {0.55f}, dens {0.40f}, spr {0.60f}, vid {0.25f},
                       dgr {0.0f}, ox {0.0f}, oy {0.35f}, wet {0.0f};
    dust::ui::BubbleEventFifo fifo;
    dust::ui::DustField field (mix, rate, dens, spr, vid, dgr, ox, oy, wet, fifo, nullptr, nullptr);
    field.setBounds (0, 0, 640, 360);

    rate.store (1.0f);  const int slow = field.dbgSpawnsOverFrames (300);   // rateNorm 1 = 2 s entre ecos
    rate.store (0.0f);  const int fast = field.dbgSpawnsOverFrames (300);   // rateNorm 0 = 20 ms
    INFO ("nacimientos en 300 frames (10 s): slow=" << slow << "  fast=" << fast);
    REQUIRE (fast > slow * 3);   // a RATE rápido nacen MUCHAS más burbujas → el knob mueve el campo
    REQUIRE (slow < 40);         // a 2 s entre ecos, pocas en 10 s (la grilla lenta se VE lenta)
    REQUIRE (slow >= 1);         // pero el campo nunca está muerto (piso de vida)
}

// ── Teclado: la superficie estrella (ORIGIN) es operable con foco + flechas (interaction-grammar) ─
TEST_CASE ("DUST campo: las flechas mueven el ORIGIN (operable por teclado)", "[diccionario][dust]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    dust::DustProcessor proc;

    std::atomic<float> mix {0.35f}, rate {0.55f}, dens {0.40f}, spr {0.60f}, vid {0.25f},
                       dgr {0.0f}, ox {0.0f}, oy {0.35f}, wet {0.0f};
    dust::ui::BubbleEventFifo fifo;
    dust::ui::DustField field (mix, rate, dens, spr, vid, dgr, ox, oy, wet, fifo,
                               proc.apvts.getParameter (pid::ORIGINX),
                               proc.apvts.getParameter (pid::ORIGINY));
    field.setBounds (0, 0, 640, 360);

    REQUIRE (field.getWantsKeyboardFocus());   // el campo PIDE foco (no sólo se alcanza con mouse)

    auto* px = proc.apvts.getParameter (pid::ORIGINX);
    auto* py = proc.apvts.getParameter (pid::ORIGINY);
    REQUIRE (px != nullptr);
    REQUIRE (py != nullptr);

    const float x0 = px->convertFrom0to1 (px->getValue());
    const float y0 = py->convertFrom0to1 (py->getValue());

    REQUIRE (field.dbgKeyPressed (juce::KeyPress (juce::KeyPress::rightKey)));
    REQUIRE (px->convertFrom0to1 (px->getValue()) > x0 + 0.01f);    // → derecha: x sube

    REQUIRE (field.dbgKeyPressed (juce::KeyPress (juce::KeyPress::downKey)));
    REQUIRE (py->convertFrom0to1 (py->getValue()) < y0 - 0.01f);    // ↓: y baja (y+ = frente)

    // Shift = fino (paso ÷ fineDiv, mismo divisor que el OvniKnob).
    const float x1 = px->convertFrom0to1 (px->getValue());
    REQUIRE (field.dbgKeyPressed (juce::KeyPress (juce::KeyPress::leftKey,
                                                  juce::ModifierKeys::shiftModifier, 0)));
    const float dxFine = x1 - px->convertFrom0to1 (px->getValue());
    REQUIRE (dxFine > 0.0f);
    REQUIRE (dxFine < 0.02f);   // 0.05/5 = 0.01: claramente menor que el paso grueso

    // Una tecla NO-flecha no se traga (sigue su curso hacia el host).
    REQUIRE_FALSE (field.dbgKeyPressed (juce::KeyPress ('a')));
}

// ── Signo L/R MEDIDO (physics §4): ORIGIN a pantalla-derecha suena canal-DERECHO ──────────────────
TEST_CASE ("DUST: ORIGIN a la derecha suena canal derecho (BAL_dB medido, mecanismo ON)",
           "[diccionario][dust]")
{
    using namespace ovni::test;
    auto measureBal = [] (float originXVal)
    {
        dust::DustProcessor proc;
        const double SR = 48000.0; const int N = 512;
        setParam (proc, pid::MIX, 1.0f);       // wet pleno: medimos el campo de burbujas
        setParam (proc, pid::DENSITY, 0.5f);
        setParam (proc, pid::SPREAD, 0.0f);    // todo apilado EN el origen → dirección pura
        setParam (proc, pid::VIDA, 0.0f);
        // originX -1..1 → normalizado (x+1)/2; originY = 0 (puro costado).
        setParam (proc, pid::ORIGINX, (originXVal + 1.0f) * 0.5f);
        setParam (proc, pid::ORIGINY, 0.5f);
        proc.prepareToPlay (SR, N);

        Pink pink;
        StereoImageMeter meter;
        for (int blk = 0; blk < 500; ++blk)
        {
            juce::AudioBuffer<float> buf (2, N);
            juce::MidiBuffer midi;
            for (int i = 0; i < N; ++i)
            {
                const float x = pink.next();
                buf.setSample (0, i, x); buf.setSample (1, i, x);
            }
            proc.processBlock (buf, midi);
            if (blk >= 120) meter.addBlock (buf.getReadPointer (0), buf.getReadPointer (1), N);
        }
        return meter.finish();
    };

    const auto right = measureBal (+1.0f);   // pantalla-derecha
    const auto left  = measureBal (-1.0f);   // pantalla-izquierda
    std::printf ("ORIGIN[derecha  ] BAL_dB=%+.2f\nORIGIN[izquierda] BAL_dB=%+.2f\n",
                 right.balDb, left.balDb);
    REQUIRE (right.rms > 1.0e-5);            // el mecanismo sonó de verdad
    REQUIRE (left.rms  > 1.0e-5);
    REQUIRE (right.balDb < -1.0);            // derecha de pantalla → R domina (BAL = L/R en dB, negativo)
    REQUIRE (left.balDb  > +1.0);            // espejo: izquierda → L domina
}

// ── Honestidad: los CORTES de la curaduría no existen (este test FALLA si alguien los re-expone) ──
TEST_CASE ("DUST: NO expone TONE/DAMP ni orbita del ORIGIN (cortes de curaduria)", "[diccionario][dust]")
{
    dust::DustProcessor proc;
    for (const char* cut : { "tone", "damp", "tonedamp", "originOrbit", "orbit", "orbitSync" })
        REQUIRE (proc.apvts.getParameter (cut) == nullptr);
    // Las macros curadas SÍ existen.
    for (const char* id : { pid::MIX, pid::RATE, pid::RATESYNC, pid::RATEDIV, pid::DENSITY,
                            pid::SPREAD, pid::VIDA, pid::DUCK, pid::ORIGINX, pid::ORIGINY })
        REQUIRE (proc.apvts.getParameter (id) != nullptr);
}
