#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "params/ParameterIDs.h"
#include "ui/HaloRings.h"

namespace pid = halo::params::id;

static void setNorm (juce::AudioProcessorValueTreeState& s, const char* id, float v01)
{ if (auto* p = s.getParameter (id)) p->setValueNotifyingHost (v01); }

// ── SYNC = control CORE del sello (va en TODOS los plugins). En HALO controla su elemento rítmico natural:
//    el RATE de la ÓRBITA espacial (la trayectoria del halo alrededor de la cabeza). En SYNC, debugOrbitRateHz
//    debe usar la DIVISIÓN ELEGIDA (no una fija) → espeja el test de NÉBULA. Helpers de la tabla sync::divs[].
TEST_CASE ("HALO: en SYNC la orbita usa la division elegida (no fija a 4 bars)", "[diccionario][halo]")
{
    halo::HaloProcessor proc;
    proc.prepareToPlay (48000.0, 256);
    setNorm (proc.apvts, pid::ORBITSYNC, 1.0f);

    namespace sd = halo::params::sync;
    const float hz1bar  = sd::orbitRateHz (120.0, 0);   // idx0 = "1 bar"
    const float hz2bars = sd::orbitRateHz (120.0, 1);   // idx1 = "2 bars"
    REQUIRE (hz2bars == Catch::Approx (hz1bar * 0.5f).margin (0.001f));   // 2 bars = mitad de frecuencia

    // orbitDiv es un choice de kCount entradas; el valor normalizado del índice i es i/(kCount-1).
    const float denom = (float) (sd::kCount - 1);
    setNorm (proc.apvts, pid::ORBITDIV, 0.0f / denom);          // idx0 = "1 bar"
    REQUIRE (proc.debugOrbitRateHz (120.0) == Catch::Approx (hz1bar).margin (0.001f));
    setNorm (proc.apvts, pid::ORBITDIV, 1.0f / denom);          // idx1 = "2 bars"
    REQUIRE (proc.debugOrbitRateHz (120.0) == Catch::Approx (hz2bars).margin (0.001f));
}

// ── Honestidad del RATE (Joaquín pidió poder ELEGIR la velocidad de la órbita, lento DE VERDAD, y que el knob
//    SE SIENTA en todo su recorrido): en FREE la perilla RATE debe (a) llegar GENUINAMENTE lenta, (b) cambiar
//    la velocidad de forma monótona, y (c) NO tener ZONA MUERTA (el bug que cazó: el cuarto inferior del knob
//    no cambiaba nada por un skew power-law; el mapeo log lo arregla). En SYNC manda la división, no el knob.
TEST_CASE ("HALO: el RATE en FREE es lento de verdad y sin zona muerta (honesto)", "[diccionario][halo]")
{
    halo::HaloProcessor proc;
    proc.prepareToPlay (48000.0, 256);
    REQUIRE (proc.apvts.getParameter (pid::ORBITRATE) != nullptr);   // el control existe

    setNorm (proc.apvts, pid::ORBITSYNC, 0.0f);                      // FREE: la velocidad sale del knob RATE
    auto rateAt = [&] (float norm) { setNorm (proc.apvts, pid::ORBITRATE, norm);
                                     return proc.debugEffectiveOrbitRateHz (120.0); };

    // (a) Genuinamente LENTO: el piso ≤ 0.005 Hz (≥ 200 s/vuelta). "Lento de verdad", pedido de Joaquín.
    REQUIRE (rateAt (0.0f) <= 0.005f);
    // (b) Monótono + audible: más knob = más rápido (RATE no es inerte).
    REQUIRE (rateAt (0.8f) > rateAt (0.2f) * 1.5f);
    // (c) SIN ZONA MUERTA: el tramo lento TAMBIÉN responde — norm 0.25 claramente más rápido que norm 0.05
    //     (con el skew viejo ambos caían al piso → "el knob no cambia nada"; el mapeo log lo garantiza).
    REQUIRE (rateAt (0.25f) > rateAt (0.05f) * 1.4f);

    // (d) En SYNC la velocidad la fija la división, NO el knob FREE.
    setNorm (proc.apvts, pid::ORBITSYNC, 1.0f);
    namespace sd = halo::params::sync;
    setNorm (proc.apvts, pid::ORBITDIV, 0.0f);                       // idx0 = "1 bar"
    REQUIRE (proc.debugEffectiveOrbitRateHz (120.0) == Catch::Approx (sd::orbitRateHz (120.0, 0)).margin (0.001f));
}

// ── Los "circulitos" del visualizador (HaloRings) deben salir LENTO a RATE bajo y RÁPIDO a RATE alto — antes
//    corrían a un reloj FIJO (∝ Shimmer) que el knob no tocaba ("el knob no hace nada / todos rápidos", lo
//    cazó Joaquín mirando los anillos). Ahora la VELOCIDAD del halo la manda el RATE. Verificación por conteo
//    de nacimientos sobre N frames de animación.
TEST_CASE ("HALO circulitos: el RATE controla la velocidad de los anillos (no reloj fijo)", "[diccionario][halo]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    std::atomic<float> sh {0.44f}, dc {0.20f}, sz {1.0f}, tn {1.0f}, orb {0.0f}, mx {1.0f}, fz {0.0f}, lr {0.0f}, rate {0.0f};
    halo::ui::HaloRings rings (sh, dc, sz, tn, orb, mx, fz, lr, rate);

    rate.store (0.0f);  const int slow = rings.dbgSpawnsOverFrames (300);   // RATE mínimo (circulitos lentos)
    rate.store (1.0f);  const int fast = rings.dbgSpawnsOverFrames (300);   // RATE máximo (circulitos rápidos)
    INFO ("nacimientos en 300 frames: slow=" << slow << "  fast=" << fast);
    REQUIRE (fast > slow * 3);     // a más RATE, MUCHOS más anillos → el knob SÍ controla la velocidad
    REQUIRE (slow < 80);           // a RATE mínimo, pocos en 10 s → ya NO son ~17/s (circulitos lentos)
}

// ── Regla de layout CRÍTICA del Diccionario OVNI §4: el FREEZE NO debe pisar el IN PHASE (que vive en la
//    columna de utilidad, a la derecha). Verificación DETERMINÍSTICA por bounds (no por captura). Migrado del
//    test de no-solape de la tira SCALE/VOICING (que ya no existe) → ahora "FREEZE no pisa IN PHASE".
TEST_CASE ("HALO: el FREEZE NO se solapa con IN PHASE", "[diccionario][halo]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    halo::HaloProcessor proc;
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    ed->setSize (900, 600);   // fuerza el layout (layoutBody)
    auto* he = dynamic_cast<halo::HaloEditor*> (ed.get());
    REQUIRE (he != nullptr);
    const auto fz = he->dbgFreezeBounds();
    const auto ip = he->dbgInPhaseBounds();
    INFO ("freeze=" << fz.toString().toStdString() << "  inPhase=" << ip.toString().toStdString());
    REQUIRE_FALSE (fz.intersects (ip));     // no se pisan
    REQUIRE (fz.getRight() <= ip.getX());   // el FREEZE entero a la IZQUIERDA del IN PHASE
}

// ── El control SYNC (core del sello) tampoco debe pisar el IN PHASE (regresión del bug que Joaquín cazó en
//    NÉBULA: el SYNC montado sobre el IN PHASE). Verificación DETERMINÍSTICA por bounds (no por captura).
TEST_CASE ("HALO: el control SYNC NO se solapa con IN PHASE", "[diccionario][halo]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    halo::HaloProcessor proc;
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    ed->setSize (900, 600);   // fuerza el layout (layoutBody)
    auto* he = dynamic_cast<halo::HaloEditor*> (ed.get());
    REQUIRE (he != nullptr);
    const auto s  = he->dbgSyncBounds();
    const auto ip = he->dbgInPhaseBounds();
    INFO ("sync=" << s.toString().toStdString() << "  inPhase=" << ip.toString().toStdString());
    REQUIRE_FALSE (s.intersects (ip));     // no se pisan
    REQUIRE (s.getRight() <= ip.getX());   // SYNC entero a la IZQUIERDA del IN PHASE
}

// ── Honestidad de marca (base técnica §1): NO existen parámetros SCALE ni VOICING. La matemática del
//    feedback desmiente el "scale-aware" (sólo la octava es invariante bajo recursión); las voces son FIJAS
//    (octava + quinta) en el motor. Este test FALLA si alguien re-expone esos controles.
TEST_CASE ("HALO: NO expone SCALE ni VOICING (honestidad del feedback)", "[diccionario][halo]")
{
    halo::HaloProcessor proc;
    REQUIRE (proc.apvts.getParameter ("scale")   == nullptr);
    REQUIRE (proc.apvts.getParameter ("voicing") == nullptr);
    // Los 6 controles del rediseño "VASTEDAD" SÍ existen.
    for (const char* id : { pid::MIX, pid::SIZE, pid::DECAY, pid::SHIMMER, pid::TONE, pid::ORBIT, pid::FREEZE })
        REQUIRE (proc.apvts.getParameter (id) != nullptr);
}
