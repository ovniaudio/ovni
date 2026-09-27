// [telescope][tramo] — T6 de la 0.2: el tramo de la referencia de TONAL BALANCE (pedido de un usuario, como en
// Mastering The Mix). La tira muestra la forma de onda de la referencia; arrastrando se elige [desde, hasta) y
// la curva de la referencia pasa a ser la de ese tramo; con doble clic vuelve al archivo entero.
//
//   T6[bandas]  la curva de un tramo da las MISMAS bandas que telescope-measure para ese archivo y ese tramo:
//               una función, dos usos (D-100 §3). Y el tramo cambia la curva de verdad (si no, no probaría nada).
//   T6[estado]  el tramo se guarda en el estado del plugin y vuelve igual al reabrir la sesión; cargar otra
//               referencia lo borra.
//   T6[mouse]   arrastrar en la tira elige el tramo, un clic suelto no, y el doble clic vuelve al archivo entero.
//   T6[uisnap]  las fotos de la tira, en S/M/L (con OVNI_UISNAP_SCALES, también a 1,25 y 1,5).
//
// El archivo: 12 s de rosa, los primeros 6 planos y los últimos 6 con +12 dB arriba de 6,3 kHz. El tramo
// [6, 11) cae entero en la parte brillante, así que su curva se aparta de la del archivo entero en los agudos.
#include <catch2/catch_test_macros.hpp>
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "TestHelpers.h"
#include "TestShelf.h"
#include "TestSignals.h"
#include "TestWav.h"
#include "lenses/LensIds.h"
#include "lenses/TonalBalanceLens.h"
#include "MeasureCore.h"

using telescope::TonalBalanceLens;

namespace
{
constexpr double kSr = 48000.0;
constexpr double kFrom = 6.0, kTo = 11.0;

juce::File spanWav()
{
    auto a  = std::make_shared<telescope::test::Pink> (telescope::test::kPinkSeedA);
    auto b  = std::make_shared<telescope::test::Pink> (telescope::test::kPinkSeedB);
    auto sl = std::make_shared<telescope::test::HighShelf> (6300.0, 12.0, kSr);
    auto sr = std::make_shared<telescope::test::HighShelf> (6300.0, 12.0, kSr);
    const auto half = (juce::int64) (6.0 * kSr);
    return telescope::test::writeWav ("t6_tramo_rosa_brillante.wav", kSr, 2, (juce::int64) (12.0 * kSr),
                                      [=] (juce::int64 i)
                                      {
                                          float l = a->next(), r = b->next();
                                          const float fl = sl->process (l), fr = sr->process (r);   // el filtro corre siempre
                                          if (i >= half) { l = fl; r = fr; }
                                          return std::pair<float, float> { 0.25f * l, 0.25f * r };
                                      });
}

bool waitIdle (telescope::TelescopeProcessor& proc)
{
    return telescope::test::waitUntil ([&] { return ! proc.referenceBusy(); }, 60000);
}

// Las bandas relativas al integrado de la referencia cargada: el número que dibuja TONAL BALANCE.
std::array<float, 30> relBands (const telescope::FileAnalysis& a)
{
    std::array<float, 30> r {};
    for (int b = 0; b < 30; ++b) r[(size_t) b] = a.bandsDb[b] - a.integratedLufs;
    return r;
}

// telescope-measure (el núcleo de la herramienta, en el mismo proceso) sobre el mismo archivo y el mismo tramo.
juce::var measureJson (const juce::File& f, double from, double to)
{
    telescope::measure::Session session ({ "t6", "test" });
    const auto req = "{\"id\":\"t6\",\"file\":" + juce::JSON::toString (f.getFullPathName())
                   + ",\"from_s\":" + juce::String (from, 3) + ",\"to_s\":" + juce::String (to, 3) + "}";
    const auto line = session.handle (req.toStdString());
    return juce::JSON::parse (juce::String::fromUTF8 (line.c_str()));
}
}

// ============================================================================================ T6[bandas]
TEST_CASE ("telescope: T6 · la curva de un tramo son las bandas de telescope-measure", "[telescope][tramo]")
{
    const auto wav = spanWav();
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (kSr, 512);

    proc.loadReference (wav);
    REQUIRE (waitIdle (proc));
    const auto whole = relBands (proc.referenceAnalysis());
    REQUIRE (proc.referenceSpan().whole);

    proc.setReferenceRange (kFrom, kTo);
    REQUIRE (waitIdle (proc));
    const auto span = proc.referenceSpan();
    const auto a    = proc.referenceAnalysis();
    const auto part = relBands (a);
    REQUIRE (a.valid);
    REQUIRE_FALSE (span.whole);
    REQUIRE (span.fromS == kFrom);
    REQUIRE (span.toS == kTo);

    const auto json  = measureJson (wav, kFrom, kTo);
    const auto bands = json.getProperty ("bands_db_rel_integrated", juce::var());
    REQUIRE (bands.isArray());
    REQUIRE (bands.size() == 30);
    int same = 0, compared = 0;
    float worst = 0.0f, apart = 0.0f;
    for (int b = 0; b < 30; ++b)
    {
        apart = std::max (apart, std::abs (part[(size_t) b] - whole[(size_t) b]));
        if (bands[b].isVoid() || bands[b].isUndefined()) continue;   // null: BAND_BELOW_FLOOR
        ++compared;
        const float d = std::abs (part[(size_t) b] - (float) (double) bands[b]);
        worst = std::max (worst, d);
        if (d <= 0.0501f) ++same;   // la herramienta redondea a 0,1 dB
    }
    std::printf ("T6[bandas] tramo [%.2f, %.2f) s: %d de %d bandas iguales a telescope-measure (peor %.3f dB, "
                 "redondeo 0,1) · el tramo se aparta del archivo entero hasta %.2f dB\n",
                 span.fromS, span.toS, same, compared, worst, apart);
    REQUIRE (compared >= 25);
    REQUIRE (same == compared);
    REQUIRE (apart > 2.0f);   // el tramo cae en la parte brillante: la curva cambió de verdad

    // Y es lo que le llega al motor: la referencia del frame es la del tramo.
    telescope::test::pushExact (proc, 4800, kSr, [] (juce::AudioBuffer<float>&, int) {});
    telescope::test::waitDigested (proc, 4800, kSr);
    REQUIRE (telescope::test::waitUntil ([&] { return proc.reference().read().refValid; }, 10000));
    const auto f = proc.reference().read();
    int bit = 0, n = 0;
    for (int b = 0; b < 30; ++b)
        if (f.refBands[b] >= -90.0f) { ++n; bit += (f.refNorm[b] == a.bandsDb[b] - a.integratedLufs) ? 1 : 0; }
    std::printf ("T6[bandas] en el motor: %d de %d bandas iguales al bit a las del tramo\n", bit, n);
    REQUIRE (bit == n);
    proc.releaseResources();
}

// ============================================================================================ T6[estado]
TEST_CASE ("telescope: T6 · el tramo se guarda en el estado y vuelve igual", "[telescope][tramo]")
{
    const auto wav = spanWav();
    juce::MemoryBlock state;
    std::array<float, 30> before {};
    {
        telescope::TelescopeProcessor proc;
        proc.prepareToPlay (kSr, 512);
        proc.loadReference (wav);
        REQUIRE (waitIdle (proc));
        proc.setReferenceRange (kFrom, kTo);
        REQUIRE (waitIdle (proc));
        before = relBands (proc.referenceAnalysis());
        proc.getStateInformation (state);
        proc.releaseResources();
    }

    telescope::TelescopeProcessor proc2;
    proc2.prepareToPlay (kSr, 512);
    proc2.setStateInformation (state.getData(), (int) state.getSize());
    REQUIRE (waitIdle (proc2));
    double from = -1.0, to = -1.0;
    REQUIRE (proc2.referenceRange (from, to));
    const auto span  = proc2.referenceSpan();
    const auto after = relBands (proc2.referenceAnalysis());
    int bit = 0;
    for (int b = 0; b < 30; ++b) bit += (after[(size_t) b] == before[(size_t) b]) ? 1 : 0;
    std::printf ("T6[estado] al reabrir: tramo pedido [%.2f, %.2f) · medido [%.2f, %.2f) · %d de 30 bandas iguales al bit\n",
                 from, to, span.fromS, span.toS, bit);
    REQUIRE (from == kFrom);
    REQUIRE (to == kTo);
    REQUIRE_FALSE (span.whole);
    REQUIRE (bit == 30);

    // El tramo es de ESE archivo: cargar otra referencia lo borra.
    proc2.loadReference (wav);
    REQUIRE (waitIdle (proc2));
    REQUIRE_FALSE (proc2.referenceRange (from, to));
    REQUIRE (proc2.referenceSpan().whole);
    proc2.releaseResources();
}

// ============================================================================================ T6[mouse]
TEST_CASE ("telescope: T6 · arrastrar elige el tramo y el doble clic vuelve al archivo entero", "[telescope][tramo]")
{
    const auto wav = spanWav();
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (kSr, 512);
    TonalBalanceLens lens (proc);
    proc.setEnabledModules (lens.requiredModules() | telescope::kAlwaysOnModules);
    lens.setSize (1025, 702);

    lens.pumpFrames (2);
    REQUIRE_FALSE (lens.stripVisible());   // sin referencia, la lente queda como estaba

    proc.loadReference (wav);
    REQUIRE (waitIdle (proc));
    const auto whole = relBands (proc.referenceAnalysis());
    REQUIRE (telescope::test::waitUntil ([&] { lens.pumpFrames (1); return lens.waveformReady(); }, 30000));
    REQUIRE (lens.stripVisible());
    const auto r = lens.stripArea();
    std::printf ("T6[mouse] la tira: %d×%d en (%d, %d) · rótulo «%s» · ayuda «%s»\n", r.getWidth(), r.getHeight(),
                 r.getX(), r.getY(), lens.stripLabel().toRawUTF8(), lens.stripHint().toRawUTF8());
    REQUIRE (lens.stripLabel() == juce::String::fromUTF8 ("ref \xc2\xb7 0:00\xe2\x80\x93" "0:12"));

    // Un clic suelto no es un tramo.
    lens.stripBegin (r.getCentreX());
    lens.stripEnd (r.getCentreX() + 1);
    double from = 0.0, to = 0.0;
    REQUIRE_FALSE (proc.referenceRange (from, to));

    // Arrastrar de la mitad (6 s) a los 11/12 (11 s), pasando por el medio.
    const int x0 = r.getX() + r.getWidth() / 2;
    const int x1 = r.getX() + juce::roundToInt ((double) r.getWidth() * 11.0 / 12.0);
    lens.stripBegin (x0);
    lens.stripDrag ((x0 + x1) / 2);
    lens.stripDrag (x1);
    lens.stripEnd (x1);
    REQUIRE (proc.referenceRange (from, to));
    REQUIRE (waitIdle (proc));
    lens.pumpFrames (2);
    std::printf ("T6[mouse] arrastre %d → %d px: tramo [%.3f, %.3f) s · rótulo «%s»\n", x0, x1, from, to,
                 lens.stripLabel().toRawUTF8());
    REQUIRE (std::abs (from - 6.0) < 12.0 / r.getWidth());
    REQUIRE (std::abs (to - 11.0) < 12.0 / r.getWidth());
    REQUIRE (lens.stripLabel() == juce::String::fromUTF8 ("ref \xc2\xb7 0:06\xe2\x80\x93" "0:11"));
    REQUIRE_FALSE (proc.referenceSpan().whole);

    // El doble clic vuelve al archivo entero: la curva, al bit la de antes.
    lens.stripDoubleClick();
    REQUIRE_FALSE (proc.referenceRange (from, to));
    REQUIRE (waitIdle (proc));
    lens.pumpFrames (2);
    const auto back = relBands (proc.referenceAnalysis());
    int bit = 0;
    for (int b = 0; b < 30; ++b) bit += (back[(size_t) b] == whole[(size_t) b]) ? 1 : 0;
    std::printf ("T6[mouse] doble clic: archivo entero de nuevo · %d de 30 bandas iguales al bit · rótulo «%s»\n",
                 bit, lens.stripLabel().toRawUTF8());
    REQUIRE (proc.referenceSpan().whole);
    REQUIRE (bit == 30);
    REQUIRE (lens.stripLabel() == juce::String::fromUTF8 ("ref \xc2\xb7 0:00\xe2\x80\x93" "0:12"));

    // Quitar la referencia se lleva la tira.
    lens.pressControl (TonalBalanceLens::ctrlClear);
    lens.pumpFrames (2);
    REQUIRE_FALSE (lens.stripVisible());
    proc.releaseResources();
}

// ============================================================================================ T6[uisnap]
TEST_CASE ("telescope: T6 · snapshot de la tira de la referencia en S/M/L", "[telescope][uisnap]")
{
    const auto wav = spanWav();
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (kSr, 512);
    auto* lensParam = proc.apvts.getParameter ("lens");
    lensParam->setValueNotifyingHost (lensParam->convertTo0to1 ((float) (int) telescope::LensId::tonalBalance));
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto* tel = dynamic_cast<telescope::TelescopeEditor*> (ed.get());
    REQUIRE (tel != nullptr);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

    proc.loadReference (wav);
    REQUIRE (waitIdle (proc));
    proc.setReferenceRange (kFrom, kTo);
    REQUIRE (waitIdle (proc));
    auto* tonal = dynamic_cast<TonalBalanceLens*> (tel->activeLens());
    REQUIRE (tonal != nullptr);
    REQUIRE (telescope::test::waitUntil ([&] { tel->pumpLensFrames (1); return tonal->waveformReady(); }, 30000));

    // Un programa para que las dos curvas estén: 4 s de rosa, digeridos enteros.
    telescope::test::Pink a { telescope::test::kPinkSeedA }, b { telescope::test::kPinkSeedB };
    const auto pushed = telescope::test::pushExact (proc, 4 * (long long) kSr, kSr, [&] (juce::AudioBuffer<float>& buf, int k)
    {
        for (int i = 0; i < k; ++i) { buf.setSample (0, i, 0.25f * a.next()); buf.setSample (1, i, 0.25f * b.next()); }
    });
    telescope::test::waitDigested (proc, pushed, kSr);
    REQUIRE (telescope::test::waitUntil ([&] { const auto& f = proc.reference().read(); return f.refValid && f.liveValid; }, 10000));
    tel->pumpLensFrames (40);

    const struct { ovni::PluginEditorBase::Zoom zoom; const char* path; } shots[] = {
        { ovni::PluginEditorBase::Zoom::small,  "/tmp/ovni_telescope_tonal_tira_S.png" },
        { ovni::PluginEditorBase::Zoom::medium, "/tmp/ovni_telescope_tonal_tira_M.png" },
        { ovni::PluginEditorBase::Zoom::large,  "/tmp/ovni_telescope_tonal_tira_L.png" },
    };
    for (const auto& s : shots)
    {
        tel->applyZoom (s.zoom);
        tel->pumpLensFrames (10);
        std::printf ("T6[uisnap] %s · rótulo «%s» · ayuda «%s»\n", s.path, tonal->stripLabel().toRawUTF8(),
                     tonal->stripHint().toRawUTF8());
        REQUIRE (tonal->stripVisible());
        telescope::test::writePng (*tel, s.path);
    }
    ed.reset();
    proc.releaseResources();
}
