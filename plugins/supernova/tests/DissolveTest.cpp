// [supernova][dissolve] — el FUNDIDO entre fotos: puro (sin JUCE ni GPU), testeable como PhotoSequence.
// El motor lo avanza con el dt del render (nunca reloj de pared: el export tiene que dar lo mismo que la
// pantalla). La regla de duración vive acá también, para que la pantalla, el export y los tests usen UNA.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <vector>
#include "render/Dissolve.h"
#include "image/PhotoSequence.h"   // SeqClock: el reloj del que sale la duración (Dissolve.h no lo incluye)

using Catch::Approx;
using supernova::Dissolve;
using supernova::dissolveSecondsFor;
using supernova::SeqClock;

TEST_CASE ("dissolve: en reposo no está activo y su mezcla es 0", "[supernova][dissolve]")
{
    Dissolve d;
    REQUIRE (! d.active());
    REQUIRE (d.mix01() == 0.0f);
    d.tick (1.0 / 60.0);          // avanzar sin haber arrancado no lo despierta
    REQUIRE (! d.active());
}

TEST_CASE ("dissolve: smoothstep 0→1 monótono, con derivada 0 en los dos bordes", "[supernova][dissolve]")
{
    Dissolve d;
    d.start (1.0);
    REQUIRE (d.active());
    REQUIRE (d.mix01() == 0.0f);

    // 100 pasos de 10 ms: la mezcla sube siempre, nunca retrocede, y termina en 1.
    float prev = d.mix01();
    std::vector<float> mixes;
    for (int i = 0; i < 100; ++i)
    {
        d.tick (0.01);
        const float m = d.mix01();
        REQUIRE (m >= prev);
        prev = m;
        mixes.push_back (m);
    }
    REQUIRE (d.mix01() == Approx (1.0f).margin (1e-6));
    REQUIRE (! d.active());       // llegó a destino: el fundido se apagó solo

    // Derivada ≈ 0 en los extremos (arranque y llegada suaves) y máxima en el medio: eso es el smoothstep.
    const float dStart = mixes[1] - mixes[0];
    const float dEnd   = mixes[99] - mixes[98];
    const float dMid   = mixes[50] - mixes[49];
    REQUIRE (dStart < dMid * 0.25f);
    REQUIRE (dEnd   < dMid * 0.25f);
}

TEST_CASE ("dissolve: un dt gigante satura en 1 sin pasarse", "[supernova][dissolve]")
{
    Dissolve d;
    d.start (0.5);
    d.tick (10.0);                // un hipo del sistema (o un breakpoint) no puede dejar mix > 1
    REQUIRE (d.mix01() == Approx (1.0f).margin (1e-6));
    REQUIRE (! d.active());
}

TEST_CASE ("dissolve: duración = min(0.7 s, 45% del intervalo del reloj), nunca menos de 0.1 s",
           "[supernova][dissolve]")
{
    // SECONDS: el intervalo manda hasta que el tope de 0.7 s lo corta.
    REQUIRE (dissolveSecondsFor (SeqClock::Seconds, 8.0, 4.0, 120.0, 1.0) == Approx (0.7));
    REQUIRE (dissolveSecondsFor (SeqClock::Seconds, 2.0, 4.0, 120.0, 1.0) == Approx (0.7));
    REQUIRE (dissolveSecondsFor (SeqClock::Seconds, 1.0, 4.0, 120.0, 1.0) == Approx (0.45));

    // KICK: con el gap mínimo (0.25 s) el fundido termina ANTES del próximo kick.
    REQUIRE (dissolveSecondsFor (SeqClock::Kick, 8.0, 4.0, 120.0, 0.25) == Approx (0.1125));

    // BEATS: el intervalo son los beats a la BPM del host.
    REQUIRE (dissolveSecondsFor (SeqClock::Beats, 8.0, 4.0, 120.0, 1.0) == Approx (0.7));   // 2 s
    REQUIRE (dissolveSecondsFor (SeqClock::Beats, 8.0, 1.0, 180.0, 1.0) == Approx (0.15));  // 1/3 s

    // BPM inválida (host sin transporte) → 120 de fallback: 1 beat = 0.5 s → 0.225.
    REQUIRE (dissolveSecondsFor (SeqClock::Beats, 8.0, 1.0, 0.0, 1.0) == Approx (0.225));

    // Piso duro: por más corto que sea el intervalo, nunca baja de 0.1 s (si no, es un corte otra vez).
    REQUIRE (dissolveSecondsFor (SeqClock::Seconds, 0.05, 4.0, 120.0, 1.0) == Approx (0.1));
}

TEST_CASE ("dissolve: sin secuencia (cambio a mano) el fundido dura el tope, 0.7 s", "[supernova][dissolve]")
{
    REQUIRE (supernova::kDissolveMaxSeconds == Approx (0.7));
    REQUIRE (supernova::dissolveSecondsDefault() == Approx (0.7));
}

TEST_CASE ("dissolve: retarget a mitad de camino arranca de 0 (la base ya viene mezclada, no salta)",
           "[supernova][dissolve]")
{
    Dissolve d;
    d.start (1.0);
    for (int i = 0; i < 50; ++i) d.tick (0.01);   // a mitad
    const float half = d.mix01();
    REQUIRE (half > 0.4f);
    REQUIRE (half < 0.6f);

    // Llega otra foto: el renderer hornea A = mix(A, B, half) y re-arranca. El helper vuelve a 0 — la
    // continuidad la da la base horneada, no un mix que arranque en `half` sobre una B nueva.
    d.start (0.7);
    REQUIRE (d.active());
    REQUIRE (d.mix01() == 0.0f);
    REQUIRE (d.seconds() == Approx (0.7));
}

TEST_CASE ("dissolve: una duración de 0 s es un CORTE (nunca queda activo)", "[supernova][dissolve]")
{
    Dissolve d;
    d.start (0.0);
    REQUIRE (! d.active());
    REQUIRE (d.mix01() == 0.0f);
}
