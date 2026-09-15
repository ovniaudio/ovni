// [telescope][uisnap] — captura el editor REAL (header del sello + tira de lentes + lente LOUDNESS) a PNG
// en los tres tamaños del sello, con 5 s de seno adentro para que los números no salgan vacíos.
//   ./OvniTelescopeTests "[uisnap]"  →  /tmp/ovni_telescope_loudness_{S,M,L}.png
// Patrón de plugins/_probe/tests/SnapshotTest.cpp. Headless: el ScopedJuceInitialiser_GUI lo da TestMain.
#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <cstdio>
#include <memory>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "lenses/Look.h"
#include "TestHelpers.h"
#include "TestSignals.h"
#include "lenses/LensIds.h"
#include "lenses/FieldLens.h"
#include "lenses/SpectrogramLens.h"
#include "lenses/SpiralLens.h"
#include "lenses/WaterfallLens.h"
#include "lenses/ScopeLens.h"
#include "analysis/ScopeFrame.h"
#include "analysis/SpectrogramRing.h"
#include "analysis/SpectrumFrame.h"
#include "analysis/modules/Cqt.h"
#include "lenses/NoteName.h"
#include "analysis/modules/StereoBands.h"

namespace
{
// LA SEÑAL MUSICAL de las lentes 6 y 7: una tríada de Do mayor con el bajo en C2 y repetida arriba, así
// que las MISMAS clases de nota aparecen en varias octavas. Es lo que hace legibles las dos fotos: en CQT
// se ven tres barras por octava en las mismas posiciones del teclado, y en SPIRAL las notas iguales quedan
// ALINEADAS sobre el mismo radio (que es toda la razón de ser de esa lente).
//
//   Do en cuatro octavas (C2 C3 C4 C5) · Mi y Sol en dos (E4 G4 E5 G5)
std::vector<double> majorTriadHz()
{
    std::vector<double> hz;
    for (const int midi : { 36, 48, 60, 64, 67, 72, 76, 79 })
        hz.push_back (440.0 * std::pow (2.0, (double) (midi - 69) / 12.0));
    return hz;
}

void pushTriad (telescope::TelescopeProcessor& proc, double seconds, double amp)
{
    const auto hz = majorTriadHz();
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    long long n = 0;
    const auto total = (long long) std::llround (seconds * 48000.0);

    for (long long done = 0; done < total; done += 512)
    {
        const int k = (int) juce::jmin ((long long) 512, total - done);
        buf.clear();
        for (int i = 0; i < k; ++i, ++n)
        {
            double v = 0.0;
            for (const double f : hz)
                v += amp * std::sin (2.0 * juce::MathConstants<double>::pi * f * (double) n / 48000.0);
            buf.setSample (0, i, (float) v);
            buf.setSample (1, i, (float) v);
        }
        proc.processBlock (buf, midi);

        const double pushed = (double) n / 48000.0;
        if (pushed - proc.analysis().read().timeSeconds > 2.0)
            REQUIRE (telescope::test::waitUntil (
                [&] { return pushed - proc.analysis().read().timeSeconds <= 1.0; }, 8000));
    }
}

// La captura a PNG vive en TestHelpers.h, en UNA sola copia (LOW de los tres revisores).
using telescope::test::writePng;
}

TEST_CASE ("telescope: snapshot del editor con la lente LOUDNESS en S/M/L", "[telescope][uisnap]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (48000.0, 512);

    // Un objetivo elegido para que se vea el delta, y 5 s de seno a -14 dBFS para llenar los números.
    proc.apvts.getParameter ("target")->setValueNotifyingHost (
        proc.apvts.getParameter ("target")->convertTo0to1 (1.0f));   // Spotify

    const float peak = std::pow (10.0f, -14.0f / 20.0f);
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    long long n = 0;
    for (int b = 0; b < (int) (5.0 * 48000.0 / 512.0); ++b)
    {
        for (int i = 0; i < 512; ++i, ++n)
        {
            const auto v = (float) (peak * std::sin (2.0 * juce::MathConstants<double>::pi * 997.0 * (double) n / 48000.0));
            buf.setSample (0, i, v);
            buf.setSample (1, i, v * 0.85f);   // un poco de desbalance: el medidor no es simétrico
        }
        proc.processBlock (buf, midi);
    }
    // Que el motor digiera los ~5 s antes de la foto (espera por condición, no por reloj).
    // 4.85 y no 4.9: `analysedSeconds` acumula 0.1 por hop y 0.1 no es exacto en binario — a los 49 hops
    // vale 4.899999999999999. Comparar contra el borde exacto sería un test que falla por aritmética.
    REQUIRE (telescope::test::waitUntil ([&] { return proc.analysis().read().timeSeconds >= 4.85; }, 3000));

    // La foto tiene que tener NÚMEROS adentro, no sólo el tamaño correcto.
    {
        const auto f = proc.analysis().read();
        std::printf ("UISNAP loudness: M=%.2f  S=%.2f  I=%.2f LUFS (valido=%d)  TP=%.2f dBTP\n",
                     f.loudness.momentary, f.loudness.shortTerm, f.loudness.integrated,
                     (int) f.loudness.integratedValid, f.loudness.truePeakMax);
        REQUIRE (f.loudness.integratedValid);
        REQUIRE (f.loudness.integrated > -30.0f);
        REQUIRE (f.loudness.truePeakMax > -20.0f);
    }

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    REQUIRE (ed != nullptr);

    auto* tel = dynamic_cast<telescope::TelescopeEditor*> (ed.get());
    REQUIRE (tel != nullptr);
    tel->pumpLensFrames (40);   // que la lente llegue a sus valores antes de la foto

    const struct { ovni::PluginEditorBase::Zoom zoom; const char* path; } shots[] = {
        { ovni::PluginEditorBase::Zoom::small,  "/tmp/ovni_telescope_loudness_S.png" },
        { ovni::PluginEditorBase::Zoom::medium, "/tmp/ovni_telescope_loudness_M.png" },
        { ovni::PluginEditorBase::Zoom::large,  "/tmp/ovni_telescope_loudness_L.png" },
    };

    for (const auto& s : shots)
    {
        tel->applyZoom (s.zoom);        // aplica sin persistir (no toca la preferencia global del sello)
        tel->pumpLensFrames (5);
        writePng (*tel, s.path);
    }

    // 56b (D-50): LA MISMA LENTE EN CASTELLANO. El default es inglés desde el 56; esta foto es la
    // prueba de que el idioma llega a los rótulos de una lente que hasta ayer los tenía a mano.
    telescope::strings::setLanguage (proc.apvts.state, "es");
    tel->applyZoom (ovni::PluginEditorBase::Zoom::medium);
    tel->pumpLensFrames (5);
    writePng (*tel, "/tmp/ovni_telescope_loudness_es_M.png");
    std::printf ("UISNAP loudness es: \"%s\"  ·  en: \"%s\"\n",
                 telescope::strings::get (telescope::strings::Key::integrated, "es").toRawUTF8(),
                 telescope::strings::get (telescope::strings::Key::integrated, "en").toRawUTF8());
    telescope::strings::setLanguage (proc.apvts.state, "en");

    proc.releaseResources();
}

// ========================================================================================================
// ===== 57c · EL MEDIDOR MIENTRAS LA VENTANA SE LLENA =====
//
// «Que no haya retraso al cargar el medidor» (Joaquín, 12-sep). A los 0.5 s el MOMENTARY ya tiene su
// ventana entera (400 ms) y el SHORT-TERM todavía no (necesita 3 s): antes eso eran tres segundos de
// "--.-" y una barra vacía. Ahora el short-term muestra su PARCIAL —la misma cuenta sobre los hops que
// hay— con el relleno al 55 % y un contorno fino, y el número apagado. La foto tiene las dos cosas al
// lado: una barra llena y una parcial, para que la diferencia se vea de un vistazo.
TEST_CASE ("telescope: snapshot de LOUDNESS con la ventana de short-term a medio llenar",
           "[telescope][uisnap]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (48000.0, 512);

    const float peak = std::pow (10.0f, -14.0f / 20.0f);
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    long long n = 0;
    for (int b = 0; b < (int) (0.55 * 48000.0 / 512.0); ++b)
    {
        for (int i = 0; i < 512; ++i, ++n)
        {
            const auto v = (float) (peak * std::sin (2.0 * juce::MathConstants<double>::pi * 997.0
                                                     * (double) n / 48000.0));
            buf.setSample (0, i, v);
            buf.setSample (1, i, v * 0.85f);
        }
        proc.processBlock (buf, midi);
    }
    REQUIRE (telescope::test::waitUntil ([&] { return proc.analysis().read().timeSeconds >= 0.45; }, 3000));

    const auto f = proc.analysis().read();
    std::printf ("UISNAP loudness parcial a %.2f s: momentary oficial %+.1f (parcial %+.2f)  ·  "
                 "short-term oficial %+.1f (parcial %+.2f)  ·  TP L %+.1f  R %+.1f dBTP\n",
                 f.timeSeconds, f.loudness.momentary, f.momentaryPartial,
                 f.loudness.shortTerm, f.shortTermPartial, f.truePeakHopL, f.truePeakHopR);

    // Lo que la foto tiene que estar mostrando: el momentary YA es oficial y el short-term todavía no,
    // pero su parcial existe y es un número de verdad.
    REQUIRE (f.loudness.momentary > -30.0f);
    REQUIRE (f.loudness.shortTerm <= -100.0f);
    REQUIRE (f.shortTermPartial   > -30.0f);
    REQUIRE (f.truePeakHopL > f.truePeakHopR);   // el desbalance de 0.85 se ve por canal

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    REQUIRE (ed != nullptr);
    auto* tel = dynamic_cast<telescope::TelescopeEditor*> (ed.get());
    REQUIRE (tel != nullptr);
    tel->applyZoom (ovni::PluginEditorBase::Zoom::medium);
    tel->pumpLensFrames (6);        // pocos: la idea es fotografiar el estado parcial, no esperarlo
    writePng (*tel, "/tmp/ovni_telescope_loudness_parcial_M.png");

    proc.releaseResources();
}


// SCOPE con ruido rosa estéreo INDEPENDIENTE: el goniómetro tiene que salir como una nube redonda (que es
// justo lo que dibuja una señal decorrelacionada), no como una línea.
//
// EL EDITOR VA PRIMERO (MEDIUM del revisor del 50). Empujar los 3 s ANTES de crear el editor medía el
// audio con kStereo APAGADO (la lente es la que lo enciende): que la foto saliera igual dependía de que
// quedaran muestras sin drenar en el bus cuando el módulo se prendía — o sea, de la suerte. Con el editor
// arriba primero, la nube del goniómetro es la de la señal que se empujó, siempre.
TEST_CASE ("telescope: snapshot del editor con la lente SCOPE en S/M/L", "[telescope][uisnap]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (48000.0, 512);

    auto* lensParam = proc.apvts.getParameter ("lens");
    REQUIRE (lensParam != nullptr);
    lensParam->setValueNotifyingHost (lensParam->convertTo0to1 ((float) (int) telescope::LensId::scope));

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    REQUIRE (ed != nullptr);
    auto* tel = dynamic_cast<telescope::TelescopeEditor*> (ed.get());
    REQUIRE (tel != nullptr);

    juce::MessageManager::getInstance()->runDispatchLoopUntil (100);   // que el editor cambie de lente
    REQUIRE ((proc.enabledModules() & telescope::kStereo) != 0u);

    const float peak = std::pow (10.0f, -20.0f / 20.0f);
    telescope::test::Pink a { telescope::test::kPinkSeedA }, b { telescope::test::kPinkSeedB };
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    for (int blk = 0; blk < (int) (3.0 * 48000.0 / 512.0); ++blk)
    {
        for (int i = 0; i < 512; ++i)
        {
            buf.setSample (0, i, peak * a.next());
            buf.setSample (1, i, peak * b.next());
        }
        proc.processBlock (buf, midi);
    }
    REQUIRE (telescope::test::waitUntil ([&] { return proc.analysis().read().timeSeconds >= 2.85; }, 3000));

    // La foto tiene que tener DATO adentro, no sólo el tamaño correcto: el goniómetro dibujando el tope
    // de puntos y un correlímetro finito y cercano a 0 (dos fuentes independientes).
    const auto scope = proc.scope().read();
    const auto frame = proc.analysis().read();
    std::printf ("UISNAP scope: xyCount=%d  oscCount=%d  corr=%+.4f  width=%.4f\n",
                 scope.xyCount, scope.oscCount, frame.corr, frame.width);
    REQUIRE (scope.xyCount == telescope::ScopeFrame::kMaxXy);
    REQUIRE (scope.oscCount > 0);
    REQUIRE (std::isfinite (frame.corr));
    REQUIRE (std::abs (frame.corr) < 0.5f);

    tel->pumpLensFrames (40);   // que la estela del goniómetro se llene antes de la foto

    const struct { ovni::PluginEditorBase::Zoom zoom; const char* path; } shots[] = {
        { ovni::PluginEditorBase::Zoom::small,  "/tmp/ovni_telescope_scope_S.png" },
        { ovni::PluginEditorBase::Zoom::medium, "/tmp/ovni_telescope_scope_M.png" },
        { ovni::PluginEditorBase::Zoom::large,  "/tmp/ovni_telescope_scope_L.png" },
    };
    for (const auto& s : shots)
    {
        tel->applyZoom (s.zoom);
        tel->pumpLensFrames (10);
        writePng (*tel, s.path);
    }

    proc.releaseResources();
}

// DYNAMICS con la señal de RÁFAGAS: tienen que verse las marcas rojas en la línea de tiempo y el contador
// en 10. Un snapshot con el contador en cero no probaría nada de la lente.
TEST_CASE ("telescope: snapshot del editor con la lente DYNAMICS en S/M/L", "[telescope][uisnap]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    proc.setClipThresholdDbtp (-1.0f);

    auto* lensParam = proc.apvts.getParameter ("lens");
    REQUIRE (lensParam != nullptr);
    lensParam->setValueNotifyingHost (lensParam->convertTo0to1 ((float) (int) telescope::LensId::dynamics));

    // 10 ráfagas de 50 ms a -0.5 dBFS, una por segundo, sobre un lecho de ruido rosa a -40 dBFS: la misma
    // señal de [dyn]. Con freno, para no desbordar el bus (ver la nota de DynamicsTest.cpp).
    telescope::test::Pink bed { telescope::test::kPinkSeedA };
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    long long n = 0;
    const long long total = (long long) (10.0 * 48000.0);
    for (long long done = 0; done < total; done += 512)
    {
        const int k = (int) juce::jmin ((long long) 512, total - done);
        buf.clear();
        for (int i = 0; i < k; ++i, ++n)
        {
            const long long inSecond = n % 48000;
            float v = 0.01f * bed.next();
            const long long j = inSecond - 9600;
            if (j >= 0 && j < 2400)
            {
                double env = 1.0;
                if (j < 48)              env = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * (double) j / 48.0);
                else if (j >= 2400 - 48) env = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * (double) (2400 - j) / 48.0);
                v += (float) (std::pow (10.0, -0.5 / 20.0) * env
                              * std::sin (2.0 * juce::MathConstants<double>::pi * 997.0 * (double) n / 48000.0));
            }
            buf.setSample (0, i, v);
            buf.setSample (1, i, v);
        }
        proc.processBlock (buf, midi);

        const double pushedSec = (double) (done + k) / 48000.0;
        if (pushedSec - proc.analysis().read().timeSeconds > 2.0)
            REQUIRE (telescope::test::waitUntil (
                [&] { return pushedSec - proc.analysis().read().timeSeconds <= 1.0; }, 5000));
    }

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    REQUIRE (ed != nullptr);
    auto* tel = dynamic_cast<telescope::TelescopeEditor*> (ed.get());
    REQUIRE (tel != nullptr);

    juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
    REQUIRE (telescope::test::waitUntil ([&] { return proc.analysis().read().timeSeconds >= 9.95; }, 5000));
    REQUIRE (proc.analysis().read().clipEvents == 10u);

    tel->pumpLensFrames (40);

    const struct { ovni::PluginEditorBase::Zoom zoom; const char* path; } shots[] = {
        { ovni::PluginEditorBase::Zoom::small,  "/tmp/ovni_telescope_dynamics_S.png" },
        { ovni::PluginEditorBase::Zoom::medium, "/tmp/ovni_telescope_dynamics_M.png" },
        { ovni::PluginEditorBase::Zoom::large,  "/tmp/ovni_telescope_dynamics_L.png" },
    };
    for (const auto& s : shots)
    {
        tel->applyZoom (s.zoom);
        tel->pumpLensFrames (10);
        writePng (*tel, s.path);
    }

    proc.releaseResources();
}

// SPECTRUM con ruido rosa estéreo (L y R independientes): en modo FFT con slope 3 el rosa se ve PLANO,
// que es justo lo que hay que poder verificar de un vistazo en la foto. Y una segunda foto en modo
// ⅓ de octava, que es un dibujo completamente distinto de los mismos datos.
TEST_CASE ("telescope: snapshot del editor con la lente SPECTRUM en S/M/L", "[telescope][uisnap]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (48000.0, 512);

    auto* lensParam = proc.apvts.getParameter ("lens");
    REQUIRE (lensParam != nullptr);
    lensParam->setValueNotifyingHost (lensParam->convertTo0to1 ((float) (int) telescope::LensId::spectrum));

    {
        auto st = proc.spectrumSettings();
        st.channel       = telescope::Spectrum::leftRight;
        st.slopeDbPerOct = 3.0f;
        st.bandsMode     = telescope::Spectrum::bandsFft;
        st.avgMode       = telescope::Spectrum::avgExp;
        proc.setSpectrumSettings (st);
    }

    // El EDITOR primero: es el que le pide kSpectrum al motor (lente a demanda). Empujar audio antes de
    // que la lente esté arriba lo mediría con el módulo apagado y la foto saldría vacía.
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    REQUIRE (ed != nullptr);
    auto* tel = dynamic_cast<telescope::TelescopeEditor*> (ed.get());
    REQUIRE (tel != nullptr);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
    REQUIRE ((proc.enabledModules() & telescope::kSpectrum) != 0u);

    const float peak = std::pow (10.0f, -14.0f / 20.0f);
    telescope::test::Pink a { telescope::test::kPinkSeedA }, b { telescope::test::kPinkSeedB };
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    for (int blk = 0; blk < (int) (3.0 * 48000.0 / 512.0); ++blk)
    {
        for (int i = 0; i < 512; ++i)
        {
            buf.setSample (0, i, peak * a.next());
            buf.setSample (1, i, peak * b.next() * 0.7f);   // R un poco más bajo: dos curvas distinguibles
        }
        proc.processBlock (buf, midi);
    }
    REQUIRE (telescope::test::waitUntil ([&] { return proc.spectrum().read().frameIndex > 20u; }, 5000));

    // Que haya ESPECTRO en la foto: los dos espectros de L+R, con energía real en el medio del rango.
    {
        const auto& f = proc.spectrum().read();
        float best = telescope::SpectrumFrame::kFloorDb;
        for (int k = 0; k < f.numBins; ++k) best = juce::jmax (best, f.magDb[0][k]);
        std::printf ("UISNAP spectrum: %d bins x %d espectros  ·  bin mas fuerte de L = %.2f dB\n",
                     f.numBins, telescope::spectrumNumSpectra (f.channelMode), best);
        REQUIRE (f.numBins == 2049);
        REQUIRE (telescope::spectrumNumSpectra (f.channelMode) == 2);
        REQUIRE (best > -80.0f);
    }

    tel->pumpLensFrames (40);

    const struct { ovni::PluginEditorBase::Zoom zoom; const char* path; } shots[] = {
        { ovni::PluginEditorBase::Zoom::small,  "/tmp/ovni_telescope_spectrum_S.png" },
        { ovni::PluginEditorBase::Zoom::medium, "/tmp/ovni_telescope_spectrum_M.png" },
        { ovni::PluginEditorBase::Zoom::large,  "/tmp/ovni_telescope_spectrum_L.png" },
    };
    for (const auto& s : shots)
    {
        tel->applyZoom (s.zoom);
        tel->pumpLensFrames (10);
        writePng (*tel, s.path);
    }

    // Los MISMOS datos en modo ⅓ de octava: barras del ancho real de cada banda.
    {
        auto st = proc.spectrumSettings();
        st.bandsMode = telescope::Spectrum::bandsThird;
        proc.setSpectrumSettings (st);
    }
    tel->applyZoom (ovni::PluginEditorBase::Zoom::medium);
    tel->pumpLensFrames (10);
    writePng (*tel, "/tmp/ovni_telescope_spectrum_bands_M.png");

    // Y la tira de nueve controles en castellano: VENTANA / SOLAPE / CANAL / BANDAS / PROMEDIO / RANGO
    // eran, hasta el 56, literales; ahora salen de la tabla como el resto (D-50).
    {
        auto st = proc.spectrumSettings();
        st.bandsMode = telescope::Spectrum::bandsFft;
        proc.setSpectrumSettings (st);
    }
    telescope::strings::setLanguage (proc.apvts.state, "es");
    tel->pumpLensFrames (10);
    writePng (*tel, "/tmp/ovni_telescope_spectrum_es_M.png");
    telescope::strings::setLanguage (proc.apvts.state, "en");

    proc.releaseResources();
}

// SPECTROGRAM con un BARRIDO logarítmico: en la foto tiene que verse la diagonal. Un espectrograma con
// ruido rosa sería un rectángulo lindo que no prueba que el eje de tiempo ande.
TEST_CASE ("telescope: snapshot del editor con la lente SPECTROGRAM en S/M/L", "[telescope][uisnap]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (48000.0, 512);

    auto* lensParam = proc.apvts.getParameter ("lens");
    REQUIRE (lensParam != nullptr);
    lensParam->setValueNotifyingHost (lensParam->convertTo0to1 ((float) (int) telescope::LensId::spectrogram));

    {
        auto st = proc.spectrumSettings();
        st.channel         = telescope::Spectrum::mid;
        st.historySecIndex = 0;      // 10 s: el barrido entra entero
        st.rangeDbIndex    = 1;      // 90 dB
        proc.setSpectrumSettings (st);
    }

    // El EDITOR primero (ver la nota de la lente SPECTRUM): sin él, kSpectrum está apagado y el
    // espectrograma se quedaría sin columnas.
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    REQUIRE (ed != nullptr);
    auto* tel = dynamic_cast<telescope::TelescopeEditor*> (ed.get());
    REQUIRE (tel != nullptr);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
    REQUIRE ((proc.enabledModules() & telescope::kSpectrum) != 0u);

    // Tres barridos log de 60 Hz a 16 kHz, 4 s cada uno, con freno para no desbordar el bus.
    constexpr double kSweepSec = 4.0, f0 = 60.0, f1 = 16000.0;
    const double lnK = std::log (f1 / f0);
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    const long long total = (long long) (3.0 * kSweepSec * 48000.0);   // 12 s: llena los 10 s de historia
    for (long long done = 0; done < total; done += 512)
    {
        const int k = (int) juce::jmin ((long long) 512, total - done);
        buf.clear();
        for (int i = 0; i < k; ++i)
        {
            const double t  = std::fmod ((double) (done + i) / 48000.0, kSweepSec);
            const double ph = 2.0 * juce::MathConstants<double>::pi * f0 * kSweepSec
                                  * (std::exp (lnK * t / kSweepSec) - 1.0) / lnK;
            const auto v = (float) (0.4 * std::sin (ph));
            buf.setSample (0, i, v);
            buf.setSample (1, i, v);
        }
        proc.processBlock (buf, midi);

        const double pushedSec = (double) (done + k) / 48000.0;
        if (pushedSec - proc.analysis().read().timeSeconds > 2.0)
            REQUIRE (telescope::test::waitUntil (
                [&] { return pushedSec - proc.analysis().read().timeSeconds <= 1.0; }, 5000));
    }

    REQUIRE (telescope::test::waitUntil ([&] { return proc.spectrogram().count() >= proc.spectrogram().capacity(); },
                                         6000));
    // Y que las columnas tengan DATO: la última columna del anillo con energía de verdad (el barrido).
    {
        juce::uint8 col[telescope::SpectrogramRing::kRows];
        REQUIRE (proc.spectrogram().copyColumn (proc.spectrogram().writeIndex() - 1, col));
        int worst = 0;
        for (const auto v : col) worst = juce::jmax (worst, (int) v);
        std::printf ("UISNAP spectrogram: %d columnas de %d, %.2f col/s  ·  pico de la ultima columna = %d/255\n",
                     proc.spectrogram().count(), proc.spectrogram().capacity(),
                     proc.spectrogram().columnsPerSecond(), worst);
        REQUIRE (worst > 64);
    }

    tel->pumpLensFrames (5);

    const struct { ovni::PluginEditorBase::Zoom zoom; const char* path; } shots[] = {
        { ovni::PluginEditorBase::Zoom::small,  "/tmp/ovni_telescope_spectrogram_S.png" },
        { ovni::PluginEditorBase::Zoom::medium, "/tmp/ovni_telescope_spectrogram_M.png" },
        { ovni::PluginEditorBase::Zoom::large,  "/tmp/ovni_telescope_spectrogram_L.png" },
    };
    for (const auto& s : shots)
    {
        tel->applyZoom (s.zoom);
        tel->pumpLensFrames (3);
        writePng (*tel, s.path);
    }

    proc.releaseResources();
}

// BAND CORRELATION con la señal que la lente existe para mostrar: graves MONO (seno de 80 Hz en L = R) y
// agudos FUERA DE FASE (ruido rosa pasa-altos de 2 kHz con R = −L). En la foto tiene que verse el corte
// en la mitad del eje: barras arriba a la izquierda, barras abajo a la derecha. Una foto con ruido rosa
// normal sería una fila de barritas al medio que no prueba nada.
TEST_CASE ("telescope: snapshot del editor con la lente BAND CORRELATION en S/M/L", "[telescope][uisnap]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    proc.setBandsWindowIndex (1);   // 1 s

    auto* lensParam = proc.apvts.getParameter ("lens");
    REQUIRE (lensParam != nullptr);
    lensParam->setValueNotifyingHost (lensParam->convertTo0to1 ((float) (int) telescope::LensId::bandCorrelation));

    // El EDITOR primero: es el que enciende kStereoBands (lente a demanda).
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    REQUIRE (ed != nullptr);
    auto* tel = dynamic_cast<telescope::TelescopeEditor*> (ed.get());
    REQUIRE (tel != nullptr);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
    REQUIRE ((proc.enabledModules() & telescope::kStereoBands) != 0u);

    telescope::test::MonoLowPhaseHigh sig { 48000.0 };
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    long long n = 0;
    for (int blk = 0; blk < (int) (6.0 * 48000.0 / 512.0); ++blk)
    {
        for (int i = 0; i < 512; ++i, ++n)
        {
            const auto v = sig.next();
            buf.setSample (0, i, v.first);
            buf.setSample (1, i, v.second);
        }
        proc.processBlock (buf, midi);
        const double pushed = (double) n / 48000.0;
        if (pushed - proc.analysis().read().timeSeconds > 2.0)
            REQUIRE (telescope::test::waitUntil (
                [&] { return pushed - proc.analysis().read().timeSeconds <= 1.0; }, 5000));
    }
    REQUIRE (telescope::test::waitUntil ([&] { return proc.analysis().read().bandsWindowSec > 0.9f; }, 6000));

    // Que la foto tenga el CORTE adentro, no sólo el tamaño correcto.
    {
        const auto f = proc.analysis().read();
        int low = 0, high = 0;
        for (int b = 0; b < telescope::StereoBands::kNumBands; ++b)
        {
            if (f.bandCorr[b] == 0.0f) continue;
            if (telescope::kThirdOctaveHz[b] <= 160.0  && f.bandCorr[b] >  0.9f) ++low;
            if (telescope::kThirdOctaveHz[b] >= 2500.0 && f.bandCorr[b] < -0.9f) ++high;
        }
        std::printf ("UISNAP bandas: %d bandas graves en fase  ·  %d agudas fuera de fase  ·  ventana %.2f s\n",
                     low, high, f.bandsWindowSec);
        REQUIRE (low >= 3);
        REQUIRE (high >= 8);
    }

    tel->pumpLensFrames (20);

    const struct { ovni::PluginEditorBase::Zoom zoom; const char* path; } shots[] = {
        { ovni::PluginEditorBase::Zoom::small,  "/tmp/ovni_telescope_bands_S.png" },
        { ovni::PluginEditorBase::Zoom::medium, "/tmp/ovni_telescope_bands_M.png" },
        { ovni::PluginEditorBase::Zoom::large,  "/tmp/ovni_telescope_bands_L.png" },
    };
    for (const auto& s : shots)
    {
        tel->applyZoom (s.zoom);
        tel->pumpLensFrames (5);
        writePng (*tel, s.path);
    }

    proc.releaseResources();
}

// STEREO SPECTROGRAM con dos cosas en la misma foto: primero la señal MIXTA (graves mono arriba, agudos
// fuera de fase abajo — o sea blanco abajo y rojo arriba en el eje de frecuencia) y después un BARRIDO
// MONO, que tiene que verse como una línea BLANCA subiendo sobre el fondo. Un espectrograma de ruido rosa
// sería un rectángulo lindo que no probaría ni el eje de tiempo ni el mapeo de color.
TEST_CASE ("telescope: snapshot del editor con la lente STEREO SPECTROGRAM en S/M/L", "[telescope][uisnap]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    proc.setBandsWindowIndex (0);   // 0.3 s: que el barrido no arrastre la fase del tramo anterior

    auto* lensParam = proc.apvts.getParameter ("lens");
    REQUIRE (lensParam != nullptr);
    lensParam->setValueNotifyingHost (lensParam->convertTo0to1 ((float) (int) telescope::LensId::stereoSpectrogram));

    {
        auto st = proc.spectrumSettings();
        st.historySecIndex = 0;      // 10 s: los dos tramos entran enteros
        st.rangeDbIndex    = 1;      // 90 dB
        proc.setSpectrumSettings (st);
    }

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    REQUIRE (ed != nullptr);
    auto* tel = dynamic_cast<telescope::TelescopeEditor*> (ed.get());
    REQUIRE (tel != nullptr);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
    REQUIRE ((proc.enabledModules() & telescope::kStereoBands) != 0u);

    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    const auto drain = [&] (double pushedSec)
    {
        if (pushedSec - proc.analysis().read().timeSeconds > 2.0)
            REQUIRE (telescope::test::waitUntil (
                [&] { return pushedSec - proc.analysis().read().timeSeconds <= 1.0; }, 5000));
    };

    // (a) 8 s de la señal mixta (con los 4 del barrido son 12 s: la historia de 10 s entra llena).
    telescope::test::MonoLowPhaseHigh sig { 48000.0 };
    long long n = 0;
    for (int blk = 0; blk < (int) (8.0 * 48000.0 / 512.0); ++blk)
    {
        for (int i = 0; i < 512; ++i, ++n)
        {
            const auto v = sig.next();
            buf.setSample (0, i, v.first);
            buf.setSample (1, i, v.second);
        }
        proc.processBlock (buf, midi);
        drain ((double) n / 48000.0);
    }

    // (b) 4 s de barrido log MONO de 60 Hz a 16 kHz: la línea blanca.
    constexpr double kSweepSec = 4.0, f0 = 60.0, f1 = 16000.0;
    const double lnK = std::log (f1 / f0);
    for (long long done = 0; done < (long long) (kSweepSec * 48000.0); done += 512)
    {
        const int k = (int) juce::jmin ((long long) 512, (long long) (kSweepSec * 48000.0) - done);
        buf.clear();
        for (int i = 0; i < k; ++i, ++n)
        {
            const double t  = (double) (done + i) / 48000.0;
            const double ph = 2.0 * juce::MathConstants<double>::pi * f0 * kSweepSec
                                  * (std::exp (lnK * t / kSweepSec) - 1.0) / lnK;
            const auto v = (float) (0.4 * std::sin (ph));
            buf.setSample (0, i, v);
            buf.setSample (1, i, v);
        }
        proc.processBlock (buf, midi);
        drain ((double) n / 48000.0);
    }

    REQUIRE (telescope::test::waitUntil (
        [&] { return proc.stereoSpectrogram().count() >= proc.stereoSpectrogram().capacity(); }, 8000));

    // Que la última columna tenga el barrido MONO adentro: una fila con energía y coherencia de mono.
    {
        juce::uint8 col[telescope::StereoSpectrogramRing::kCellBytes];
        REQUIRE (proc.stereoSpectrogram().copyColumn (proc.stereoSpectrogram().writeIndex() - 1, col));
        int bestRow = -1, bestEnergy = 0;
        for (int row = 0; row < telescope::StereoSpectrogramRing::kRows; ++row)
            if ((int) col[2 * row + 1] > bestEnergy) { bestEnergy = (int) col[2 * row + 1]; bestRow = row; }

        std::printf ("UISNAP stereo spectrogram: %d columnas de %d  ·  pico de la ultima columna: fila %d "
                     "(%.0f Hz) energia %d coherencia %d\n",
                     proc.stereoSpectrogram().count(), proc.stereoSpectrogram().capacity(), bestRow,
                     telescope::StereoSpectrogramRing::rowFrequency (juce::jmax (0, bestRow)),
                     bestEnergy, bestRow >= 0 ? (int) col[2 * bestRow] : -1);
        REQUIRE (bestRow >= 0);
        REQUIRE (bestEnergy > 100);
        REQUIRE ((int) col[2 * bestRow] >= 250);     // el barrido es MONO: blanco
    }

    tel->pumpLensFrames (5);

    const struct { ovni::PluginEditorBase::Zoom zoom; const char* path; } shots[] = {
        { ovni::PluginEditorBase::Zoom::small,  "/tmp/ovni_telescope_stereo_spectrogram_S.png" },
        { ovni::PluginEditorBase::Zoom::medium, "/tmp/ovni_telescope_stereo_spectrogram_M.png" },
        { ovni::PluginEditorBase::Zoom::large,  "/tmp/ovni_telescope_stereo_spectrogram_L.png" },
    };
    for (const auto& s : shots)
    {
        tel->applyZoom (s.zoom);
        tel->pumpLensFrames (3);
        writePng (*tel, s.path);
    }

    proc.releaseResources();
}

// CQT con la tríada de Do mayor y el bajo en C2. En la foto tienen que verse las barras PARADAS SOBRE SUS
// TECLAS (el teclado está dibujado abajo, a escala), el Do repetido en cuatro octavas a la misma altura,
// y el cromagrama con exactamente tres clases arriba: Do, Mi y Sol.
TEST_CASE ("telescope: snapshot del editor con la lente CQT en S/M/L", "[telescope][uisnap]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (48000.0, 512);

    auto* lensParam = proc.apvts.getParameter ("lens");
    REQUIRE (lensParam != nullptr);
    lensParam->setValueNotifyingHost (lensParam->convertTo0to1 ((float) (int) telescope::LensId::cqt));

    // El EDITOR primero: es el que enciende kCqt (lente a demanda). Empujar audio antes lo mediría con el
    // módulo apagado y la foto saldría vacía.
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    REQUIRE (ed != nullptr);
    auto* tel = dynamic_cast<telescope::TelescopeEditor*> (ed.get());
    REQUIRE (tel != nullptr);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
    REQUIRE ((proc.enabledModules() & telescope::kCqt) != 0u);

    pushTriad (proc, 6.0, 0.08);
    REQUIRE (telescope::test::waitUntil ([&] { return proc.cqt().read().frameIndex > 40u; }, 10000));

    // Que la foto tenga MÚSICA adentro, no sólo el tamaño correcto: las tres clases arriba y el resto
    // abajo, y la tonalidad estimada con sus dos números.
    {
        const auto& f = proc.cqt().read();
        // El Do está en CUATRO octavas y el Mi y el Sol en dos, así que la relación esperada es 2:1:1 —
        // y eso es lo que se ve en la foto: la barra de Do al tope y las otras dos a media altura. Las
        // nueve clases restantes tienen que quedar en el ruido.
        int fuertes = 0, debiles = 0;
        for (int c = 0; c < 12; ++c) { if (f.chroma[c] >= 0.4f) ++fuertes; if (f.chroma[c] < 0.2f) ++debiles; }

        std::printf ("UISNAP cqt: %d bins  ·  cromagrama Do=%.2f Mi=%.2f Sol=%.2f (%d clases >= 0.4, "
                     "%d por debajo de 0.2)  ·  tonalidad %s confianza %.2f (%.0f %% del tiempo)  ·  "
                     "latencia del grave %.3f s\n",
                     f.numBins, f.chroma[0], f.chroma[4], f.chroma[7], fuertes, debiles,
                     telescope::keyLabel (f.keyTonic, f.keyMode, "es").toRawUTF8(), f.keyConfidence,
                     100.0f * f.keyTimeFraction, f.lowestBinLatencySec);

        REQUIRE (f.numBins == 229);
        REQUIRE (fuertes == 3);            // exactamente Do, Mi y Sol
        REQUIRE (debiles == 9);            // y las otras nueve, abajo
        REQUIRE (f.chroma[0] == 1.0f);     // Do es el máximo: cuatro octavas contra dos
        REQUIRE (f.chroma[4] >= 0.4f);     // Mi
        REQUIRE (f.chroma[7] >= 0.4f);     // Sol
        REQUIRE (f.keyTonic == 0);
        REQUIRE (f.keyMode == telescope::Cqt::major);
        REQUIRE (f.keyConfidence > 0.7f);
    }

    tel->pumpLensFrames (20);

    const struct { ovni::PluginEditorBase::Zoom zoom; const char* path; } shots[] = {
        { ovni::PluginEditorBase::Zoom::small,  "/tmp/ovni_telescope_cqt_S.png" },
        { ovni::PluginEditorBase::Zoom::medium, "/tmp/ovni_telescope_cqt_M.png" },
        { ovni::PluginEditorBase::Zoom::large,  "/tmp/ovni_telescope_cqt_L.png" },
    };
    for (const auto& sh : shots)
    {
        tel->applyZoom (sh.zoom);
        tel->pumpLensFrames (5);
        writePng (*tel, sh.path);
    }

    proc.releaseResources();
}

// SPIRAL con la MISMA señal que CQT, que es todo el punto: el Do está en cuatro octavas, así que en la
// foto tiene que verse la LÍNEA DE DO — cuatro púas alineadas sobre el mismo radio, una por vuelta. Si el
// mapeo estuviera corrido, esa alineación no existiría y la lente no diría nada que la 6 no diga mejor.
TEST_CASE ("telescope: snapshot del editor con la lente SPIRAL en S/M/L", "[telescope][uisnap]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (48000.0, 512);

    auto* lensParam = proc.apvts.getParameter ("lens");
    REQUIRE (lensParam != nullptr);
    lensParam->setValueNotifyingHost (lensParam->convertTo0to1 ((float) (int) telescope::LensId::spiral));

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    REQUIRE (ed != nullptr);
    auto* tel = dynamic_cast<telescope::TelescopeEditor*> (ed.get());
    REQUIRE (tel != nullptr);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
    REQUIRE ((proc.enabledModules() & telescope::kCqt) != 0u);

    pushTriad (proc, 6.0, 0.08);
    REQUIRE (telescope::test::waitUntil ([&] { return proc.cqt().read().frameIndex > 40u; }, 10000));

    // Que la ALINEACIÓN esté en el dato antes de sacar la foto: los cuatro bins de Do (C2 C3 C4 C5) tienen
    // que estar los cuatro fuertes, y los cuatro en el mismo ángulo del mapeo.
    {
        const auto& f = proc.cqt().read();
        const int binC2 = (int) std::lround (24.0 * std::log2 (65.40639132514966 / 27.5));
        int fuertes = 0;
        float claseC = -1.0f;
        for (int oct = 0; oct < 4; ++oct)
        {
            const int bin = binC2 + oct * telescope::Cqt::kBinsPerOctave;
            const auto p = telescope::SpiralLens::positionFor (bin, telescope::Cqt::kBinsPerOctave);
            if (f.magDb[bin] > -40.0f) ++fuertes;
            if (claseC < 0.0f) claseC = p.classFraction;
            REQUIRE (std::abs (p.classFraction - claseC) < 1.0e-5f);   // el mismo ángulo, siempre
            REQUIRE (p.octave == 2 + oct);
        }
        std::printf ("UISNAP spiral: %d de 4 Do fuertes (bins %d, %d, %d, %d)  ·  clase de Do = %.3f de "
                     "vuelta  ·  tonalidad %s confianza %.2f\n",
                     fuertes, binC2, binC2 + 24, binC2 + 48, binC2 + 72, claseC,
                     telescope::keyLabel (f.keyTonic, f.keyMode, "es").toRawUTF8(), f.keyConfidence);
        REQUIRE (fuertes == 4);
        REQUIRE_THAT (claseC, Catch::Matchers::WithinAbs (0.0f, 1.0e-5f));   // Do arriba
        REQUIRE (f.keyTonic == 0);
    }

    tel->pumpLensFrames (20);

    const struct { ovni::PluginEditorBase::Zoom zoom; const char* path; } shots[] = {
        { ovni::PluginEditorBase::Zoom::small,  "/tmp/ovni_telescope_spiral_S.png" },
        { ovni::PluginEditorBase::Zoom::medium, "/tmp/ovni_telescope_spiral_M.png" },
        { ovni::PluginEditorBase::Zoom::large,  "/tmp/ovni_telescope_spiral_L.png" },
    };
    for (const auto& sh : shots)
    {
        tel->applyZoom (sh.zoom);
        tel->pumpLensFrames (5);
        writePng (*tel, sh.path);
    }

    proc.releaseResources();
}

// ========================================================================================================
// ===== 56: EL HEMISFERIO —desde el 57c, POLAR LEVEL— · las tres fotos =====
//
// La vista que pidió Joaquín, con las TRES señales que la explican:
//
//   mono        un lóbulo vertical y nada más — si el dibujo estuviera girado, se vería acá
//   rosa indep. un abanico abierto y simétrico — dos fuentes sin relación no apuntan a ningún lado
//   mezcla      bajo mono + pad ancho + hi-hat a la derecha: la foto que se compara con un Insight
//
// Cada snapshot AFIRMA su dato antes de sacar la foto: una imagen que sale bien pero mide mal sería
// exactamente la clase de captura que engaña a quien la mira.
namespace
{
// La mezcla sintética. No es "ruido": son tres elementos con roles distintos, que es lo que hace que el
// dibujo tenga algo que decir. El pad va DECORRELACIONADO (dos rosas distintos) porque un pad ancho de
// verdad lo está; un pad hecho con el mismo ruido invertido daría un lóbulo falso abajo.
void pushMix (telescope::TelescopeProcessor& proc, double seconds)
{
    telescope::test::Pink padL { telescope::test::kPinkSeedA }, padR { telescope::test::kPinkSeedB };
    telescope::test::Pink hat  { telescope::test::kPinkSeedA + 7u };
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    long long n = 0;
    const auto total = (long long) std::llround (seconds * 48000.0);

    for (long long done = 0; done < total; done += 512)
    {
        const int k = (int) juce::jmin ((long long) 512, total - done);
        buf.clear();
        for (int i = 0; i < k; ++i, ++n)
        {
            const double t = (double) n / 48000.0;
            // bajo MONO a 55 Hz: tiene que quedar clavado en el eje vertical
            const auto bass = (float) (0.30 * std::sin (2.0 * juce::MathConstants<double>::pi * 55.0 * t));
            // pad ANCHO: dos ruidos rosas independientes, filtrados a lo grueso por su propia amplitud
            const auto pl = 0.10f * padL.next();
            const auto pr = 0.10f * padR.next();
            // hi-hat cada 250 ms, paneado a la DERECHA con ley de potencia constante a 67.5°
            const double phase = std::fmod (t, 0.25);
            const auto env = phase < 0.02 ? (float) std::exp (-phase * 180.0) : 0.0f;
            const auto h = 0.55f * env * hat.next();
            const float hl = h * 0.3827f, hr = h * 0.9239f;   // cos/sin de 67.5°

            buf.setSample (0, i, bass + pl + hl);
            buf.setSample (1, i, bass + pr + hr);
        }
        proc.processBlock (buf, midi);
    }
}
}

TEST_CASE ("telescope: snapshot de POLAR LEVEL con mono, ruido independiente y un pad",
           "[telescope][uisnap]")
{
    struct Shot { const char* name; const char* path; int kind; };   // 0 mono · 1 indep · 2 pad
    // 57c — el modo se llama POLAR LEVEL (D-34) y las fotos se llaman como el modo. La tercera es el PAD
    // de `pushMix` (grave mono + pad ancho + hi-hat a la derecha): la señal que muestra las dos capas —
    // el relleno promediado y el contorno de pico— haciendo cosas distintas.
    const Shot shots[] = {
        { "mono",     "/tmp/ovni_telescope_scope_polarlevel_mono_M.png",  0 },
        { "indep",    "/tmp/ovni_telescope_scope_polarlevel_indep_M.png", 1 },
        { "pad",      "/tmp/ovni_telescope_scope_polarlevel_pad_M.png",   2 },
    };

    for (const auto& s : shots)
    {
        telescope::TelescopeProcessor proc;
        proc.prepareToPlay (48000.0, 512);

        // El modo vive en el ValueTree (setting de VISTA, ver ScopeLens.h). Se fija ANTES de crear el
        // editor: así la capa estática se hornea ya con la retícula del hemisferio.
        proc.apvts.state.setProperty (telescope::ScopeLens::kScopeModeProperty,
                                      (int) telescope::ScopeLens::Mode::hemisphere, nullptr);

        auto* lensParam = proc.apvts.getParameter ("lens");
        REQUIRE (lensParam != nullptr);
        lensParam->setValueNotifyingHost (lensParam->convertTo0to1 ((float) (int) telescope::LensId::scope));

        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        REQUIRE (ed != nullptr);
        auto* tel = dynamic_cast<telescope::TelescopeEditor*> (ed.get());
        REQUIRE (tel != nullptr);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);

        if (s.kind == 2)
        {
            pushMix (proc, 3.0);
        }
        else
        {
            const float peak = std::pow (10.0f, -20.0f / 20.0f);
            telescope::test::Pink a { telescope::test::kPinkSeedA }, b { telescope::test::kPinkSeedB };
            juce::AudioBuffer<float> buf (2, 512);
            juce::MidiBuffer midi;
            for (int blk = 0; blk < (int) (3.0 * 48000.0 / 512.0); ++blk)
            {
                for (int i = 0; i < 512; ++i)
                {
                    const auto x = peak * a.next();
                    buf.setSample (0, i, x);
                    buf.setSample (1, i, s.kind == 0 ? x : peak * b.next());
                }
                proc.processBlock (buf, midi);
            }
        }
        REQUIRE (telescope::test::waitUntil ([&] { return proc.analysis().read().timeSeconds >= 2.85; }, 4000));

        // ---- lo que la foto TIENE que estar mostrando ----
        const auto sc = proc.scope().read();
        int occupied = 0, peakBin = 0;
        float peakDb = telescope::ScopeFrame::kHemiFloorDb;
        for (int i = 0; i < telescope::ScopeFrame::kHemiBins; ++i)
        {
            const auto v = sc.envelope[(size_t) i];
            if (v > telescope::ScopeFrame::kHemiFloorDb) ++occupied;
            if (v > peakDb) { peakDb = v; peakBin = i; }
        }
        std::printf ("UISNAP polarlevel[%s]: %d/360 grados con energia  ·  pico en %d deg (%.2f dB)  ·  corr=%+.3f\n",
                     s.name, occupied, peakBin, peakDb, proc.analysis().read().corr);

        if (s.kind == 0)
        {
            CHECK (std::abs (peakBin - 90) <= 2);   // mono: clavado arriba
            CHECK (occupied <= 4);                  // y en un lóbulo, no repartido
        }
        else if (s.kind == 1)
        {
            CHECK (occupied > 120);                 // independiente: abanico ancho
        }
        else
        {
            // La mezcla: el grave manda y es mono, así que el PICO queda arriba; y el hi-hat a la derecha
            // tiene que haber dejado energía pasando los 120° (entre mono y "sólo R").
            CHECK (std::abs (peakBin - 90) <= 12);
            float rightSide = telescope::ScopeFrame::kHemiFloorDb;
            for (int i = 120; i <= 175; ++i) rightSide = juce::jmax (rightSide, sc.envelope[(size_t) i]);
            std::printf ("UISNAP polarlevel[pad]: maximo del sector 120-175 deg (hi-hat a la derecha) = %.2f dB\n",
                         rightSide);
            CHECK (rightSide > telescope::ScopeFrame::kHemiFloorDb + 15.0f);
        }

        tel->applyZoom (ovni::PluginEditorBase::Zoom::medium);
        tel->pumpLensFrames (25);        // que la envolvente acumule su memoria antes de la foto
        writePng (*tel, s.path);

        if (s.kind == 2)                 // la mezcla, además, en los tres tamaños del sello
        {
            for (const auto z : { ovni::PluginEditorBase::Zoom::small, ovni::PluginEditorBase::Zoom::large })
            {
                tel->applyZoom (z);
                tel->pumpLensFrames (10);
                writePng (*tel, z == ovni::PluginEditorBase::Zoom::small
                                    ? "/tmp/ovni_telescope_scope_polarlevel_S.png"
                                    : "/tmp/ovni_telescope_scope_polarlevel_L.png");
            }
        }
        proc.releaseResources();
    }
}

// ========================================================================================================
// ===== 56: LA HOJA DE CONTACTO Y EL ANTES / DESPUÉS =====
//
// Las dos imágenes que Joaquín va a mirar para dar el GO: las doce lentes juntas en una grilla, y cada
// lente al lado de cómo se veía en el commit anterior a la pasada.
//
// LOS RÓTULOS SALEN DE UNA CONSTANTE, no escritos en el dibujo: en el 56b la hoja seguía diciendo
// "ANTES (04039ae) / DESPUES (56)" cuando la base ya era otra, y una comparación mal rotulada es peor
// que no tenerla — el que la mira saca conclusiones sobre el commit equivocado. Al empezar una pasada
// se cambian estas dos líneas junto con build/before/.
//
// SE ARMAN AL FINAL DE LA CORRIDA, con un listener de Catch2, y no en un TEST_CASE. Motivo: Catch2 no
// tiene dependencias entre casos, así que un caso "hoja de contacto" correría en el orden en que quedó
// enlazado —probablemente ANTES de que las lentes de FieldTest / WaterfallTest / TonalBalanceTest hayan
// sacado sus fotos— y compondría una hoja con las capturas de la corrida ANTERIOR. Un listener corre
// cuando ya terminó todo, siempre.
//
// Y sólo compone si las doce fotos son DE ESTA CORRIDA (más nuevas que el arranque del proceso): así
// `$EXE "[budget]"` no arma una hoja de contacto con imágenes viejas de /tmp y la hace pasar por nueva.
namespace
{
// De qué a qué compara el antes/después. Se cambian AL EMPEZAR una pasada, junto con build/before/.
constexpr const char* kBaseLabel = "cff1ec1";   // el commit del que salió build/before/
constexpr const char* kPassLabel = "56b";       // la pasada que se está mirando

struct Sheet { const char* name; const char* file; };

// LAS TRECE LENTES en el orden de la tira, más POLAR LEVEL al lado de SCOPE (56b): es un modo de la
// lente 8, no una lente propia, pero es el que más cambió en el 56 y en el 56b y no se veía en la hoja.
// Catorce paneles en 4 × 4, con dos huecos al final.
const Sheet kSheet[] = {
    { "LOUDNESS",           "ovni_telescope_loudness_M.png" },
    { "DYNAMICS",           "ovni_telescope_dynamics_M.png" },
    { "SPECTRUM",           "ovni_telescope_spectrum_M.png" },
    { "SPECTROGRAM",        "ovni_telescope_spectrogram_M.png" },
    { "WATERFALL",          "ovni_telescope_waterfall_M.png" },
    { "CQT",                "ovni_telescope_cqt_M.png" },
    { "SPIRAL",             "ovni_telescope_spiral_M.png" },
    { "SCOPE",              "ovni_telescope_scope_M.png" },
    { "SCOPE · POLAR LEVEL", "ovni_telescope_scope_polarlevel_pad_M.png" },
    { "BAND CORRELATION",   "ovni_telescope_bands_M.png" },
    { "STEREO SPECTROGRAM", "ovni_telescope_stereo_spectrogram_M.png" },
    { "FIELD",              "ovni_telescope_field_M.png" },
    { "TONAL BALANCE",      "ovni_telescope_tonal_M.png" },
    { "VERDICT",            "ovni_telescope_verdict_M.png" },
};
constexpr int kSheetCount = (int) (sizeof (kSheet) / sizeof (kSheet[0]));

// `build/before/` guardado desde kBaseLabel ANTES de tocar nada. Se resuelve desde el ejecutable (que vive
// en build/plugins/telescope/tests/…/Release/) y no desde el directorio de trabajo, que no se sabe cuál es.
juce::File beforeDir()
{
    auto d = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    for (int i = 0; i < 8 && d.exists(); ++i)
    {
        d = d.getParentDirectory();
        if (d.getFileName() == "build") return d.getChildFile ("before");
    }
    return {};
}

void writeSheet (const juce::Image& img, const juce::String& path)
{
    juce::File f (path);
    f.deleteFile();
    juce::FileOutputStream os (f);
    if (! os.openedOk()) return;
    juce::PNGImageFormat png;
    if (png.writeImageToStream (img, os)) { os.flush(); std::printf ("UISNAP %s\n", path.toRawUTF8()); }
}

void label (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& text, juce::Colour c)
{
    g.setColour (ovni::ui::theme::bg0.withAlpha (0.82f));
    g.fillRect (r);
    g.setColour (c);
    g.setFont (ovni::ui::fonts::label ((float) juce::jmin (22, r.getHeight() - 8)));
    g.drawText (text, r.reduced (10, 0), juce::Justification::centredLeft, false);
}

class SheetListener : public Catch::EventListenerBase
{
public:
    using Catch::EventListenerBase::EventListenerBase;

    void testRunEnded (const Catch::TestRunStats&) override
    {
        juce::Array<juce::Image> shots;
        for (const auto& s : kSheet)
        {
            const juce::File f ("/tmp/" + juce::String (s.file));
            // Sólo cuenta si la escribió ESTA corrida.
            if (! f.existsAsFile() || f.getLastModificationTime() < started)
                return;
            shots.add (juce::ImageFileFormat::loadFrom (f));
            if (! shots.getLast().isValid()) return;
        }

        buildContact (shots);
        buildBeforeAfter (shots);
    }

private:
    // 4 columnas × 4 filas, cada celda con el nombre de su lente. 57b: las fotos vienen a 2× (Retina) y
    // SE DEJAN A 2×, sin reducir. Hasta el 57 se bajaban a la mitad "para que la hoja entre en algo que
    // se pueda abrir", y esa reducción es exactamente lo que escondió el defecto que Joaquín encontró en
    // su DAW: una caché a 1× estirada al doble se ve perfecta cuando se la mira a la mitad. La hoja pesa
    // más y hay que mirarla al 100 %; es el precio de que la hoja diga la verdad.
    static void buildContact (const juce::Array<juce::Image>& shots)
    {
        constexpr int kCols = 4, kRows = 4, kPad = 14, kCap = 34;
        const int cw = shots[0].getWidth(), ch = shots[0].getHeight();

        juce::Image sheet (juce::Image::ARGB, kCols * cw + (kCols + 1) * kPad,
                           kRows * (ch + kCap) + (kRows + 1) * kPad, true);
        juce::Graphics g (sheet);
        g.fillAll (ovni::ui::theme::bg0);

        for (int i = 0; i < kSheetCount; ++i)
        {
            const int col = i % kCols, row = i / kCols;
            const int x = kPad + col * (cw + kPad), y = kPad + row * (ch + kCap + kPad);
            g.drawImage (shots[i], juce::Rectangle<float> ((float) x, (float) (y + kCap),
                                                           (float) cw, (float) ch),
                         juce::RectanglePlacement::stretchToFit);
            g.setColour (ovni::ui::theme::lineSoft);
            g.drawRect (x, y + kCap, cw, ch, 1);
            label (g, { x, y, cw, kCap }, juce::String (i + 1) + " · " + kSheet[i].name,
                   ovni::ui::theme::green);
        }
        writeSheet (sheet, "/tmp/ovni_telescope_contact_M.png");
    }

    // Una fila por lente: la captura de la base a la izquierda, la de ahora a la derecha. Es la
    // comparación que hace discutible una decisión de diseño en vez de opinable.
    static void buildBeforeAfter (const juce::Array<juce::Image>& shots)
    {
        const auto dir = beforeDir();
        if (! dir.isDirectory())
        {
            std::printf ("UISNAP antes/despues: no esta build/before/ (se guarda ANTES de tocar nada) — se omite\n");
            return;
        }

        juce::Array<juce::Image> before;
        for (const auto& s : kSheet)
        {
            const auto f = dir.getChildFile (s.file);
            before.add (f.existsAsFile() ? juce::ImageFileFormat::loadFrom (f) : juce::Image());
        }

        constexpr int kPad = 14, kCap = 34;
        const int cw = shots[0].getWidth() / 3, ch = shots[0].getHeight() / 3;
        juce::Image sheet (juce::Image::ARGB, 2 * cw + 3 * kPad,
                           kSheetCount * (ch + kCap) + (kSheetCount + 1) * kPad, true);
        juce::Graphics g (sheet);
        g.fillAll (ovni::ui::theme::bg0);

        for (int i = 0; i < kSheetCount; ++i)
        {
            const int y = kPad + i * (ch + kCap + kPad);
            const juce::Rectangle<float> lhs ((float) kPad, (float) (y + kCap), (float) cw, (float) ch);
            const juce::Rectangle<float> rhs ((float) (2 * kPad + cw), (float) (y + kCap), (float) cw, (float) ch);

            if (before[i].isValid())
                g.drawImage (before[i], lhs, juce::RectanglePlacement::stretchToFit);
            g.drawImage (shots[i], rhs, juce::RectanglePlacement::stretchToFit);

            g.setColour (ovni::ui::theme::lineSoft);
            g.drawRect (lhs, 1.0f);
            g.drawRect (rhs, 1.0f);
            label (g, { kPad, y, cw, kCap },
                   juce::String (kSheet[i].name) + juce::String::fromUTF8 ("  \xc2\xb7  ANTES (")
                       + kBaseLabel + ")",
                   ovni::ui::theme::mut);
            label (g, { 2 * kPad + cw, y, cw, kCap },
                   juce::String (kSheet[i].name) + juce::String::fromUTF8 ("  \xc2\xb7  DESPUES (")
                       + kPassLabel + ")",
                   ovni::ui::theme::green);
        }
        writeSheet (sheet, "/tmp/ovni_telescope_antes_despues.png");
    }

    static inline const juce::Time started = juce::Time::getCurrentTime();
};
}

CATCH_REGISTER_LISTENER (SheetListener)

// ========================================================================================================
// 56b · look::physicalScale() SOBRE UN paint() DE VERDAD (M2 del revisor del 56)
//
// `physicalScale` lee del `Graphics` cuánto mide un píxel lógico en píxeles FÍSICOS. Es de lo que depende
// `snap1px`, o sea toda la nitidez de la rejilla en Retina — y no tenía test: `VISUAL[snap]` le pasaba la
// escala 2.0 a mano, así que si `physicalScale` devolviera 1.0 siempre, la rejilla saldría sucia en Retina
// y toda la suite seguiría verde.
//
// Se mide donde importa: adentro del paint de un editor COMPLETO, con la transformación de escala puesta
// como la pone el host, no sobre un Graphics fabricado.
// ========================================================================================================
namespace
{
// Un componente que, cuando lo pintan, anota qué escala física vio.
class ScaleProbe : public juce::Component
{
public:
    void paint (juce::Graphics& g) override { seen = telescope::look::physicalScale (g); ++paints; }
    float seen = -1.0f;
    int   paints = 0;
};
}

TEST_CASE ("telescope: physicalScale ve la escala real adentro de un paint", "[telescope][visual]")
{
    // 1 · sin transformación: 1 píxel lógico = 1 físico.
    {
        ScaleProbe probe;
        probe.setSize (100, 60);
        juce::Image img (juce::Image::ARGB, 100, 60, true);
        juce::Graphics g (img);
        probe.paintEntireComponent (g, false);
        std::printf ("VISUAL[scale] sin transformacion -> %.3f\n", probe.seen);
        REQUIRE (probe.paints == 1);
        REQUIRE (std::abs (probe.seen - 1.0f) < 0.01f);
    }

    // 2 · a escala 2, como en Retina: la MISMA llamada tiene que devolver 2.
    {
        ScaleProbe probe;
        probe.setSize (100, 60);
        juce::Image img (juce::Image::ARGB, 200, 120, true);
        juce::Graphics g (img);
        g.addTransform (juce::AffineTransform::scale (2.0f));
        probe.paintEntireComponent (g, false);
        std::printf ("VISUAL[scale] a escala 2 -> %.3f\n", probe.seen);
        REQUIRE (std::abs (probe.seen - 2.0f) < 0.01f);
    }

    // 3 · Y EN EL EDITOR DE VERDAD, que es lo que el revisor pedía: un TelescopeEditor entero pintado a
    // escala 2. La sonda va adentro del árbol de componentes, así que ve exactamente la transformación
    // acumulada que ven las lentes cuando dibujan.
    {
        telescope::TelescopeProcessor proc;
        proc.prepareToPlay (48000.0, 512);
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        auto* tel = dynamic_cast<telescope::TelescopeEditor*> (ed.get());
        REQUIRE (tel != nullptr);
        tel->setSize (980, 620);

        ScaleProbe probe;
        probe.setBounds (10, 10, 60, 30);
        tel->addAndMakeVisible (probe);

        juce::Image img (juce::Image::ARGB, tel->getWidth() * 2, tel->getHeight() * 2, true);
        juce::Graphics g (img);
        g.addTransform (juce::AffineTransform::scale (2.0f));
        tel->paintEntireComponent (g, true);

        std::printf ("VISUAL[scale] dentro del editor a escala 2 -> %.3f  (%d paints)\n",
                     probe.seen, probe.paints);
        REQUIRE (probe.paints >= 1);
        REQUIRE (std::abs (probe.seen - 2.0f) < 0.01f);

        tel->removeChildComponent (&probe);
        proc.releaseResources();
    }
}

// ========================================================================================================
// ===== 57b · LA HOJA DE PALETAS — la que decide D-34 =====
//
// Desde el 57b las cuatro lentes en las que el color codifica NIVEL comparten una rampa elegible (ver
// lenses/Palettes.h). El default que quedó puesto —`inferno`— es PROVISORIO: la decisión es de Joaquín y
// se toma mirando, no leyendo. Esta hoja es lo que hay que mirar: las tres lentes que más cambian con la
// rampa (SPECTROGRAM, WATERFALL, FIELD) por las cuatro rampas, con la misma señal y el mismo instante.
//
// A 2× y sin reducir, como la hoja de contacto y por el mismo motivo.
TEST_CASE ("telescope: hoja de paletas para elegir la rampa", "[telescope][uisnap]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    proc.setEnabledModules (telescope::kSpectrum | telescope::kStereoBands | telescope::kField
                            | telescope::kLoudness);
    proc.setBandsWindowIndex (0);
    proc.setFieldDecayIndex (2);
    {
        auto st = proc.spectrumSettings();
        st.historySecIndex = 0;
        proc.setSpectrumSettings (st);
    }

    // Un barrido sobre ruido rosa: el barrido deja una diagonal que hace ver los escalones de la rampa, y
    // el rosa llena el resto para que se vea cómo se comporta en el piso.
    {
        telescope::test::Pink a { telescope::test::kPinkSeedA }, b { telescope::test::kPinkSeedB };
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        double phase = 0.0;
        long long n = 0;
        const int blocks = (int) (12.0 * 48000.0 / 512.0);
        for (int blk = 0; blk < blocks; ++blk)
        {
            for (int i = 0; i < 512; ++i, ++n)
            {
                const double t  = (double) n / 48000.0;
                const double hz = 40.0 * std::pow (400.0, std::fmod (t, 6.0) / 6.0);
                phase += 2.0 * juce::MathConstants<double>::pi * hz / 48000.0;
                const float sweep = 0.28f * (float) std::sin (phase);
                buf.setSample (0, i, sweep + 0.10f * a.next());
                buf.setSample (1, i, sweep + 0.10f * b.next());
            }
            proc.processBlock (buf, midi);
            const double pushed = (double) n / 48000.0;
            if (pushed - proc.analysis().read().timeSeconds > 2.0)
                REQUIRE (telescope::test::waitUntil (
                    [&] { return pushed - proc.analysis().read().timeSeconds <= 1.0; }, 20000));
        }
    }
    REQUIRE (telescope::test::waitUntil ([&] { return proc.spectrogram().count() > 300; }, 20000));
    REQUIRE (telescope::test::waitStable ([&] { return proc.analysis().read().timeSeconds; }, 250, 20000));

    constexpr int kW = 980, kH = 620;          // el tamaño M, el mismo de la hoja de contacto
    constexpr float kScale = 2.0f;

    const auto shoot = [&] (int lens) -> juce::Image
    {
        std::unique_ptr<telescope::Lens> l;
        if (lens == 0)      l = std::make_unique<telescope::SpectrogramLens> (proc);
        else if (lens == 1) l = std::make_unique<telescope::WaterfallLens> (proc);
        else                l = std::make_unique<telescope::FieldLens> (proc);
        l->setSize (kW, kH);

        juce::Image img (juce::Image::ARGB, (int) (kW * kScale), (int) (kH * kScale), true);
        for (int i = 0; i < 8; ++i)
        {
            l->pumpFrames (1);
            juce::Graphics g (img);
            g.addTransform (juce::AffineTransform::scale (kScale));
            l->paintEntireComponent (g, false);
        }
        return img;
    };

    constexpr int kPad = 14, kCap = 34;
    const int cw = (int) (kW * kScale), ch = (int) (kH * kScale);
    juce::Image sheet (juce::Image::ARGB, 3 * cw + 4 * kPad,
                       telescope::look::kNumPalettes * (ch + kCap) + (telescope::look::kNumPalettes + 1) * kPad,
                       true);
    juce::Graphics sg (sheet);
    sg.fillAll (ovni::ui::theme::bg0);

    const char* lensNames[3] = { "SPECTROGRAM", "WATERFALL", "FIELD" };
    for (int pal = 0; pal < telescope::look::kNumPalettes; ++pal)
    {
        proc.setPaletteIndex (pal);
        for (int lens = 0; lens < 3; ++lens)
        {
            const auto img = shoot (lens);
            const int x = kPad + lens * (cw + kPad);
            const int y = kPad + pal * (ch + kCap + kPad);
            sg.drawImageAt (img, x, y + kCap);
            sg.setColour (ovni::ui::theme::lineSoft);
            sg.drawRect (x, y + kCap, cw, ch, 1);
            label (sg, { x, y, cw, kCap },
                   juce::String (telescope::look::paletteName (telescope::look::paletteFromIndex (pal)))
                       + "  ·  " + lensNames[lens],
                   ovni::ui::theme::green);
        }
    }
    proc.setPaletteIndex (telescope::TelescopeProcessor::kDefaultPaletteIndex);

    writeSheet (sheet, "/tmp/ovni_telescope_paletas_M.png");
    std::printf ("UISNAP paletas: %d rampas x 3 lentes a %d%% (%d x %d px)\n",
                 telescope::look::kNumPalettes, (int) (kScale * 100.0f), sheet.getWidth(), sheet.getHeight());

    proc.releaseResources();
}
