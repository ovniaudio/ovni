// [telescope][fanout] — el fan-out del FrameSink reenvía a los CUATRO slots, cada frame, en orden fijo.
//
// El módulo Spectrum dispara UN solo sink. Desde el 51 ese lugar lo pelean varios consumidores
// (StereoBands, Reference, y desde el 55 la historia por segundo), y el fan-out es lo que hace que no
// tengan que pelearlo. Este test verifica lo único que hay que verificar de él: que TODOS reciben TODOS
// los frames —no sólo los emitidos— y siempre en el mismo orden.
#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <vector>
#include "TestSignals.h"
#include "analysis/modules/Reference.h"
#include "analysis/modules/Spectrum.h"

using telescope::FrameSinkFanout;
using telescope::Spectrum;
using telescope::test::Pink;

namespace
{
// Un sink que sólo cuenta y anota su turno: lo que importa es CUÁNTOS y EN QUÉ ORDEN.
struct CountingSink : Spectrum::FrameSink
{
    CountingSink (int idIn, std::vector<int>& logIn) : id (idIn), log (logIn) {}

    void spectrumFrameComputed (const Spectrum::FrameInfo& info) override
    {
        ++frames;
        if (info.emitted) ++emittedSeen;
        lastBins = info.numBins;
        log.push_back (id);
    }

    int  id = 0;
    long frames = 0, emittedSeen = 0;
    int  lastBins = 0;
    std::vector<int>& log;
};
}

TEST_CASE ("telescope: el fan-out del FrameSink reenvia a los cuatro slots", "[telescope][fanout]")
{
    constexpr double kSr = 48000.0;
    constexpr double kSeconds = 2.0;

    std::vector<int> order;
    FrameSinkFanout fan;
    REQUIRE (fan.empty());
    REQUIRE (fan.count() == 0);
    REQUIRE (FrameSinkFanout::kSlots == 4);

    CountingSink s0 { 0, order }, s1 { 1, order }, s2 { 2, order }, s3 { 3, order };
    fan.set (0, &s0);
    fan.set (1, &s1);
    fan.set (2, &s2);
    fan.set (3, &s3);
    REQUIRE_FALSE (fan.empty());
    REQUIRE (fan.count() == 4);
    REQUIRE (fan.get (2) == &s2);

    // Fuera de rango: se ignora, no escribe al lado (el worker lo llama en cada vuelta).
    fan.set (-1, &s0);
    fan.set (4, &s0);
    REQUIRE (fan.count() == 4);
    REQUIRE (fan.get (-1) == nullptr);
    REQUIRE (fan.get (4)  == nullptr);

    Spectrum sp;
    sp.setFrameSink (&fan);
    sp.applySettings (Spectrum::Settings{});
    sp.prepare (kSr);
    sp.reset();

    Pink a { telescope::test::kPinkSeedA }, b { telescope::test::kPinkSeedB };
    constexpr int kBlock = 337;   // un bloque feo a propósito: las posiciones de frame no dependen de él
    std::vector<float> L ((size_t) kBlock), R ((size_t) kBlock);
    const auto total = (long long) std::llround (kSeconds * kSr);
    for (long long done = 0; done < total; done += kBlock)
    {
        const int n = (int) std::min ((long long) kBlock, total - done);
        for (int i = 0; i < n; ++i) { L[(size_t) i] = 0.2f * a.next(); R[(size_t) i] = 0.2f * b.next(); }
        sp.process (L.data(), R.data(), n);
    }

    std::printf ("FANOUT %ld frames a cada uno de %d slots (%ld emitidos vistos)  ·  %d bins  ·  "
                 "%d eventos en total\n",
                 s0.frames, fan.count(), s0.emittedSeen, s0.lastBins, (int) order.size());

    // Todos reciben lo mismo, y son los CALCULADOS (no sólo los emitidos).
    REQUIRE (s0.frames > 50);
    REQUIRE (s1.frames == s0.frames);
    REQUIRE (s2.frames == s0.frames);
    REQUIRE (s3.frames == s0.frames);
    REQUIRE (s0.emittedSeen > 0);
    REQUIRE (s0.emittedSeen <= s0.frames);
    REQUIRE (s0.lastBins == 2049);
    REQUIRE (s3.lastBins == 2049);

    // Y siempre en el MISMO orden, 0 → 3: el determinismo no puede depender de en qué orden se engancharon.
    REQUIRE ((int) order.size() == (int) (4 * s0.frames));
    for (size_t i = 0; i < order.size(); ++i)
        REQUIRE (order[i] == (int) (i % 4));

    // Un slot que se apaga deja de recibir y los otros siguen igual.
    const long before = s1.frames;
    fan.set (1, nullptr);
    REQUIRE (fan.count() == 3);
    order.clear();
    for (int i = 0; i < kBlock; ++i) { L[(size_t) i] = 0.2f * a.next(); R[(size_t) i] = 0.2f * b.next(); }
    sp.process (L.data(), R.data(), kBlock);
    REQUIRE (s1.frames == before);
    REQUIRE (s0.frames == s2.frames);
    REQUIRE (s0.frames == s3.frames);
    for (const auto id : order) REQUIRE (id != 1);

    fan.clear();
    REQUIRE (fan.empty());
    REQUIRE (fan.count() == 0);
}
