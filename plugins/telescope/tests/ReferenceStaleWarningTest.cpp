// [telescope][ref][stale] — el aviso y el error de la referencia son de la referencia QUE ESTÁ CARGADA.
//
// LOW 3 del revisor del 54. `FileAnalyzer::result()` guarda su resultado hasta el próximo análisis, y
// `referenceWarning()` lo devolvía tal cual. Consecuencia: cargar un archivo de más de dos canales dejaba
// el aviso ("se miden los dos primeros") en pantalla PARA SIEMPRE — incluso después de QUITAR la
// referencia, cuando ya no había ningún archivo del que advertir nada. Un cartel verdadero sobre algo que
// dejó de existir es un cartel falso.
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdio>
#include <utility>
#include "PluginProcessor.h"
#include "TestHelpers.h"
#include "TestSignals.h"
#include "TestWav.h"

using telescope::test::Pink;

TEST_CASE ("telescope: el aviso de la referencia se limpia al quitarla", "[telescope][ref][stale]")
{
    constexpr double kSr = 48000.0;
    const float peak = std::pow (10.0f, -12.0f / 20.0f);

    Pink a { telescope::test::kPinkSeedA }, b { telescope::test::kPinkSeedB };
    const auto gen = [&] (juce::int64) { return std::pair<float, float> { peak * a.next(), peak * b.next() }; };

    // Seis canales: dispara el aviso "se miden los dos primeros (L y R)" de FileAnalyzer.
    const auto multi = telescope::test::writeWav ("ref_stale_6ch.wav", kSr, 6, (juce::int64) (2.0 * kSr), gen);
    Pink c { telescope::test::kPinkSeedA }, d { telescope::test::kPinkSeedB };
    const auto gen2 = [&] (juce::int64) { return std::pair<float, float> { peak * c.next(), peak * d.next() }; };
    const auto stereo = telescope::test::writeWav ("ref_stale_2ch.wav", kSr, 2, (juce::int64) (2.0 * kSr), gen2);

    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (kSr, 512);

    REQUIRE (proc.referenceWarning().isEmpty());

    // ---- 1 · el archivo de 6 canales avisa ----
    proc.loadReference (multi);
    REQUIRE (telescope::test::waitUntil ([&] { return ! proc.referenceBusy(); }, 60000));
    const auto warn = proc.referenceWarning();
    std::printf ("REF[stale] 6 canales -> aviso = \"%s\"\n", warn.toRawUTF8());
    REQUIRE (warn.isNotEmpty());
    REQUIRE (proc.referenceError().isEmpty());

    // ---- 2 · QUITARLA lo limpia (era el bug: quedaba para siempre) ----
    proc.clearReference();
    std::printf ("REF[stale] tras QUITAR   -> aviso = \"%s\"  (esperado vacio)\n",
                 proc.referenceWarning().toRawUTF8());
    REQUIRE (proc.referenceWarning().isEmpty());
    REQUIRE (proc.referenceError().isEmpty());

    // ---- 3 · y cargar una referencia sana tampoco lo revive ----
    proc.loadReference (stereo);
    REQUIRE (telescope::test::waitUntil ([&] { return ! proc.referenceBusy(); }, 60000));
    std::printf ("REF[stale] estereo sano  -> aviso = \"%s\"  (esperado vacio)\n",
                 proc.referenceWarning().toRawUTF8());
    REQUIRE (proc.referenceWarning().isEmpty());
    REQUIRE (proc.referenceError().isEmpty());

    // ---- 4 · un archivo ILEGIBLE deja error mientras está cargado, y nada al quitarlo ----
    auto notAudio = telescope::test::tempDir().getChildFile ("ref_stale_no_audio.txt");
    notAudio.replaceWithText ("esto no es un wav");
    proc.loadReference (notAudio);
    REQUIRE (telescope::test::waitUntil ([&] { return ! proc.referenceBusy(); }, 60000));
    std::printf ("REF[stale] no-audio      -> error = \"%s\"\n", proc.referenceError().toRawUTF8());
    REQUIRE (proc.referenceError().isNotEmpty());

    proc.clearReference();
    REQUIRE (proc.referenceError().isEmpty());

    proc.releaseResources();
    multi.deleteFile();
    stereo.deleteFile();
    notAudio.deleteFile();
}
