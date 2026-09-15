// [telescope][settings] — los settings de LENTE (los que no son parámetros: FFT, ventana, umbrales…).
// Viven como propiedades del ValueTree del APVTS, así que tienen que sobrevivir a
// getStateInformation/setStateInformation igual que los parámetros — un preset que guarda "ventana 1 s,
// osciloscopio sin trigger" y lo abre en 300 ms con trigger es un preset roto.
//
// Y la LENTE A DEMANDA (§4 del spec): el editor le pasa al motor la máscara de la lente visible, así lo
// que no se ve no se calcula. Con LOUDNESS arriba, el módulo Stereo NO corre.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <cstdio>
#include <memory>
#include <thread>
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "TestHelpers.h"
#include "TestSignals.h"
#include "analysis/modules/Cqt.h"
#include "analysis/modules/Spectrum.h"
#include "analysis/modules/Stereo.h"
#include "analysis/modules/StereoBands.h"
#include "lenses/LensIds.h"
#include "lenses/ScopeLens.h"
#include "lenses/Strings.h"
#include "ui/LensStrip.h"
#include "lenses/BandCorrelationLens.h"
#include "lenses/CqtLens.h"
#include "lenses/SpiralLens.h"
#include "lenses/LensReadout.h"
#include "lenses/SpectrogramLens.h"
#include "lenses/SpectrumLens.h"

namespace
{
// Selecciona la lente por el parámetro (como haría el host) y deja que el callAsync del editor corra.
void selectLens (telescope::TelescopeProcessor& proc, telescope::LensId id)
{
    auto* p = proc.apvts.getParameter ("lens");
    REQUIRE (p != nullptr);
    p->setValueNotifyingHost (p->convertTo0to1 ((float) (int) id));
    juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
}

// Ruido rosa de la casa por processBlock. `invertR` da vuelta el canal derecho (corr +1 → -1).
void pushPink (telescope::TelescopeProcessor& proc, double seconds, bool invertR, float peak)
{
    constexpr double sr = 48000.0;
    constexpr int    block = 512;
    telescope::test::Pink p { telescope::test::kPinkSeedA };
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    const auto total = (long long) std::llround (seconds * sr);
    for (long long done = 0; done < total; done += block)
    {
        const int k = (int) juce::jmin ((long long) block, total - done);
        buf.clear();
        for (int i = 0; i < k; ++i)
        {
            const float x = peak * p.next();
            buf.setSample (0, i, x);
            buf.setSample (1, i, invertR ? -x : x);
        }
        proc.processBlock (buf, midi);
    }
}
}

TEST_CASE ("telescope: los settings de lente sobreviven al round-trip de estado", "[telescope][settings]")
{
    juce::MemoryBlock saved;

    {
        telescope::TelescopeProcessor proc;
        // Todos distintos del default (300 / false / true / -1.0) para que un "no guardó nada" se note.
        proc.setStereoWindowMs (1000);
        proc.setScopePolar (true);
        proc.setScopeTrigger (false);
        proc.setClipThresholdDbtp (-2.5f);
        proc.getStateInformation (saved);
    }

    telescope::TelescopeProcessor proc;
    REQUIRE (proc.stereoWindowMs() == telescope::Stereo::kDefaultWindowMs);   // arranca en el default

    proc.setStateInformation (saved.getData(), (int) saved.getSize());

    REQUIRE (proc.stereoWindowMs() == 1000);
    REQUIRE (proc.scopePolar());
    REQUIRE_FALSE (proc.scopeTrigger());
    REQUIRE (proc.clipThresholdDbtp() == -2.5f);
}

TEST_CASE ("telescope: setStateInformation empuja los settings al motor, no sólo al árbol", "[telescope][settings]")
{
    juce::MemoryBlock saved;
    {
        telescope::TelescopeProcessor proc;
        proc.setStereoWindowMs (100);
        proc.getStateInformation (saved);
    }

    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    proc.setStateInformation (saved.getData(), (int) saved.getSize());
    proc.setEnabledModules (telescope::kStereo);

    // Se mide con la ventana que dice el ESTADO, no con la que quedó en el motor al construir.
    {
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        for (int b = 0; b < 100; ++b)   // ~1 s de continua: alcanza y sobra para cerrar hops
        {
            for (int i = 0; i < 512; ++i) { buf.setSample (0, i, 0.2f); buf.setSample (1, i, 0.2f); }
            proc.processBlock (buf, midi);
        }
    }
    REQUIRE (telescope::test::waitUntil ([&] { return proc.analysis().read().stereoWindowSec > 0.0f; }, 3000));
    REQUIRE (proc.analysis().read().stereoWindowSec == 0.1f);

    proc.releaseResources();
}

TEST_CASE ("telescope: lente a demanda — el módulo Stereo sólo corre con SCOPE arriba", "[telescope][settings]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (48000.0, 512);

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    REQUIRE (ed != nullptr);

    selectLens (proc, telescope::LensId::loudness);
    REQUIRE ((proc.enabledModules() & telescope::kLoudness) != 0u);
    REQUIRE ((proc.enabledModules() & telescope::kStereo)   == 0u);   // lo que no se ve, no se calcula

    selectLens (proc, telescope::LensId::scope);
    REQUIRE ((proc.enabledModules() & telescope::kStereo)   != 0u);

    selectLens (proc, telescope::LensId::dynamics);
    REQUIRE ((proc.enabledModules() & telescope::kLoudness) != 0u);   // DYNAMICS come del mismo medidor
    REQUIRE ((proc.enabledModules() & telescope::kStereo)   == 0u);   // y kStereo vuelve a apagarse

    ed.reset();
    proc.releaseResources();
}

// kLoudness SIEMPRE (decisión de la auditora del 50). El medidor integrado, el LRA, el histograma y el
// contador de clips ACUMULAN desde el reset: si mirar el espectro los apagara, el número que el usuario
// lee al volver tendría un agujero del tamaño del rato que estuvo en otra lente — y no habría forma de
// saberlo mirando la pantalla. Un medidor con agujeros invisibles es peor que no tener medidor.
// Cuesta dos biquads y un FIR de 48 taps por canal; la lente a demanda sigue valiendo para los módulos
// caros (kStereo, kSpectrum, kCqt, kField, kReference).
TEST_CASE ("telescope: kLoudness queda encendido con cualquier lente visible", "[telescope][settings]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (48000.0, 512);

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    REQUIRE (ed != nullptr);

    for (const auto id : { telescope::LensId::scope, telescope::LensId::spectrum,
                           telescope::LensId::spectrogram, telescope::LensId::loudness })
    {
        selectLens (proc, id);
        INFO ("lente " << telescope::lensName (id));
        REQUIRE ((proc.enabledModules() & telescope::kLoudness) != 0u);
    }

    // Y no es sólo la máscara: con SCOPE arriba durante 5 s el integrado SIGUE midiendo.
    selectLens (proc, telescope::LensId::scope);
    pushPink (proc, 5.0, false, 0.05f);
    REQUIRE (telescope::test::waitUntil ([&] { return proc.analysis().read().timeSeconds >= 4.85; }, 5000));

    const auto f = proc.analysis().read();
    std::printf ("SETTINGS[loudness con SCOPE] t=%.2f s  I=%.2f LUFS  valido=%d\n",
                 f.timeSeconds, f.loudness.integrated, (int) f.loudness.integratedValid);
    REQUIRE (f.loudness.integratedValid);
    REQUIRE (f.loudness.integrated > -70.0f);

    ed.reset();
    proc.releaseResources();
}

// Un módulo con ventana deslizante que se apaga y se vuelve a encender NO puede seguir sumando los hops de
// antes: el primer número tras volver mezclaría hasta 1 s de audio viejo con el nuevo, y el correlímetro
// diría +1 sobre una señal que ya es -1 (LOW del revisor del 49). Se limpia en el flanco de subida.
TEST_CASE ("telescope: al reactivar SCOPE la ventana de estéreo arranca limpia", "[telescope][settings]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    proc.setStereoWindowMs (1000);   // la ventana MÁS LARGA: 10 hops viejos para arrastrar si no se limpia

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    REQUIRE (ed != nullptr);

    // (a) SCOPE con L = R: la ventana se llena de hops con corr = +1.
    selectLens (proc, telescope::LensId::scope);
    pushPink (proc, 2.0, false, 0.2f);
    REQUIRE (telescope::test::waitUntil ([&] { return proc.analysis().read().corr > 0.99f; }, 4000));

    // (b) dos segundos en otra lente (kStereo apagado) con la señal YA dada vuelta.
    selectLens (proc, telescope::LensId::loudness);
    const double tB = proc.analysis().read().timeSeconds;
    pushPink (proc, 2.0, true, 0.2f);
    REQUIRE (telescope::test::waitUntil ([&] { return proc.analysis().read().timeSeconds >= tB + 1.95; }, 4000));

    // (c) de vuelta a SCOPE: el PRIMER hop publicado ya tiene que decir -1.
    selectLens (proc, telescope::LensId::scope);
    const double tC = proc.analysis().read().timeSeconds;
    pushPink (proc, 0.1, true, 0.2f);   // exactamente UN hop
    REQUIRE (telescope::test::waitUntil ([&] { return proc.analysis().read().timeSeconds > tC + 0.05; }, 4000));

    const auto f = proc.analysis().read();
    std::printf ("SETTINGS[reactivar SCOPE] corr del primer hop = %+.4f  (ventana efectiva %.2f s)\n",
                 f.corr, f.stereoWindowSec);
    REQUIRE (f.corr < -0.99f);            // sin el fix daría ~+0.8 (1 hop nuevo contra 9 viejos)
    REQUIRE (f.stereoWindowSec <= 0.11f); // y la ventana EFECTIVA dice la verdad: un solo hop

    ed.reset();
    proc.releaseResources();
}

// Los once settings del módulo Spectrum viajan juntos y persisten con el estado. Se guardan TODOS
// distintos del default: si el round-trip perdiera uno solo, se nota.
TEST_CASE ("telescope: los settings de SPECTRUM sobreviven al round-trip de estado", "[telescope][settings]")
{
    telescope::Spectrum::Settings wanted;
    wanted.fftOrder          = 15;
    wanted.window            = telescope::Spectrum::kaiser9;
    wanted.overlapIndex      = 2;                                  // 87.5 %
    wanted.channel           = telescope::Spectrum::side;
    wanted.slopeDbPerOct     = 4.5f;
    wanted.avgMode           = telescope::Spectrum::avgInfinite;
    wanted.avgSeconds        = 7.5f;
    wanted.peakHold          = false;
    wanted.holdDecayDbPerSec = 30.0f;
    wanted.bandsMode         = telescope::Spectrum::bandsBark;
    wanted.rangeDbIndex      = 2;                                  // 120 dB
    wanted.historySecIndex   = 2;                                  // 60 s de espectrograma

    juce::MemoryBlock saved;
    {
        telescope::TelescopeProcessor proc;
        proc.setSpectrumSettings (wanted);
        proc.getStateInformation (saved);
    }

    telescope::TelescopeProcessor proc;
    REQUIRE (proc.spectrumSettings() == telescope::Spectrum::Settings{});   // arranca en los defaults

    proc.setStateInformation (saved.getData(), (int) saved.getSize());
    REQUIRE (proc.spectrumSettings() == wanted);

    // Y el motor tiene que estar corriendo ESO, no lo que quedó al construir.
    proc.prepareToPlay (48000.0, 512);
    proc.setEnabledModules (telescope::kSpectrum);
    {
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        for (int b = 0; b < 200; ++b)      // ~2.1 s: con FFT de 32 768 hacen falta al menos 0.7 s
        {
            for (int i = 0; i < 512; ++i) { buf.setSample (0, i, 0.2f); buf.setSample (1, i, -0.2f); }
            proc.processBlock (buf, midi);
        }
    }
    REQUIRE (telescope::test::waitUntil ([&] { return proc.spectrum().read().fftSize == 32768; }, 4000));
    std::printf ("SETTINGS[spectrum] fftSize=%d  bins=%d  canal=%d  (guardado y recuperado)\n",
                 proc.spectrum().read().fftSize, proc.spectrum().read().numBins,
                 proc.spectrum().read().channelMode);
    REQUIRE (proc.spectrum().read().numBins == 16385);
    REQUIRE (proc.spectrum().read().channelMode == telescope::Spectrum::side);

    proc.releaseResources();
}

// Lente a demanda para el módulo CARO: kSpectrum sólo con SPECTRUM (o SPECTROGRAM) a la vista.
TEST_CASE ("telescope: lente a demanda — kSpectrum sólo con SPECTRUM arriba", "[telescope][settings]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (48000.0, 512);

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    REQUIRE (ed != nullptr);

    selectLens (proc, telescope::LensId::loudness);
    REQUIRE ((proc.enabledModules() & telescope::kSpectrum) == 0u);

    selectLens (proc, telescope::LensId::spectrum);
    REQUIRE ((proc.enabledModules() & telescope::kSpectrum) != 0u);
    REQUIRE ((proc.enabledModules() & telescope::kLoudness) != 0u);   // el medidor no se apaga nunca
    REQUIRE ((proc.enabledModules() & telescope::kStereo)   == 0u);

    selectLens (proc, telescope::LensId::spectrogram);
    REQUIRE ((proc.enabledModules() & telescope::kSpectrum) != 0u);   // SPECTROGRAM come del mismo módulo

    selectLens (proc, telescope::LensId::scope);
    REQUIRE ((proc.enabledModules() & telescope::kSpectrum) == 0u);

    ed.reset();
    proc.releaseResources();
}

// Los settings del ESPECTROGRAMA (historia) viajan con los del espectro y cambian el anillo del motor.
TEST_CASE ("telescope: la historia del espectrograma llega al anillo del motor", "[telescope][settings]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    proc.setEnabledModules (telescope::kSpectrum);

    for (int idx = 0; idx < telescope::SpectrogramRing::kNumHistoryOptions; ++idx)
    {
        auto st = proc.spectrumSettings();
        st.historySecIndex = idx;
        proc.setSpectrumSettings (st);

        pushPink (proc, 0.4, false, 0.2f);
        const int wantSec = telescope::SpectrogramRing::kHistoryOptions[idx];
        REQUIRE (telescope::test::waitUntil ([&] { return proc.spectrogram().historySeconds() == wantSec; }, 4000));

        const auto& ring = proc.spectrogram();
        std::printf ("SETTINGS[espectrograma] historia %2d s -> %d columnas a %.3f col/s (%.2f s)\n",
                     wantSec, ring.capacity(), ring.columnsPerSecond(),
                     (double) ring.capacity() / ring.columnsPerSecond());
        REQUIRE (ring.capacity() == (int) std::llround (ring.columnsPerSecond() * (double) wantSec));
    }

    proc.releaseResources();
}

// LA LECTURA DE NOTA. cents = 1200·log2(f / f_nota), A4 = 440 Hz, numeración científica (C4 = do central).
// Los valores esperados se derivan acá con la fórmula, no se copian de lo que devolvió la función.
TEST_CASE ("telescope: la lectura de SPECTRUM dice frecuencia, nota y cents", "[telescope][settings]")
{
    const auto expectCents = [] (double f, double fNote) { return (int) std::lround (1200.0 * std::log2 (f / fNote)); };

    // A4 exacta, C4 (do central, 261.626 Hz) y el bin 85 de una FFT de 4 096 a 48 k (996.09375 Hz).
    {
        const auto n = telescope::SpectrumLens::noteFor (440.0);
        REQUIRE (n.name == "A4");
        REQUIRE (n.cents == 0);
    }
    {
        const double c4 = 440.0 * std::pow (2.0, -9.0 / 12.0);
        const auto n = telescope::SpectrumLens::noteFor (c4);
        std::printf ("SETTINGS[nota] C4 = %.4f Hz -> %s %+d cents\n", c4, n.name.toRawUTF8(), n.cents);
        REQUIRE (n.name == "C4");
        REQUIRE (n.cents == 0);
    }
    {
        const double f     = 85.0 * 48000.0 / 4096.0;              // 996.09375 Hz
        const double fB5   = 440.0 * std::pow (2.0, 14.0 / 12.0);  // 987.7666 Hz
        const auto   n     = telescope::SpectrumLens::noteFor (f);
        std::printf ("SETTINGS[nota] %.5f Hz -> %s %+d cents  (B5 = %.4f Hz, formula da %+d)\n",
                     f, n.name.toRawUTF8(), n.cents, fB5, expectCents (f, fB5));
        REQUIRE (n.name == "B5");
        REQUIRE (n.cents == expectCents (f, fB5));
    }

    // Y la lectura completa sobre la lente: se busca la columna del pico y se pide lo que hay ahí.
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    proc.setEnabledModules (telescope::kSpectrum);
    {
        auto st = proc.spectrumSettings();
        st.channel       = telescope::Spectrum::left;
        st.slopeDbPerOct = 0.0f;
        st.bandsMode     = telescope::Spectrum::bandsFft;
        proc.setSpectrumSettings (st);
    }

    const double freq = 85.0 * 48000.0 / 4096.0;
    const float  amp  = std::pow (10.0f, -20.0f / 20.0f);
    {
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        long long n = 0;
        for (int b = 0; b < (int) (2.0 * 48000.0 / 512.0); ++b)
        {
            for (int i = 0; i < 512; ++i, ++n)
            {
                const auto v = (float) ((double) amp * std::sin (2.0 * juce::MathConstants<double>::pi
                                                                 * freq * (double) n / 48000.0));
                buf.setSample (0, i, v);
                buf.setSample (1, i, v);
            }
            proc.processBlock (buf, midi);
        }
    }
    // Hay que esperar a que el motor digiera los 2 s ENTEROS, no a los primeros frames: el promediado
    // exponencial por default tiene τ = 0.5 s, así que a los 0.2 s el número todavía va en camino
    // (medido: -24.3 dB en vez de -20.4). Es la misma trampa que el reduced-motion de LOUDNESS.
    REQUIRE (telescope::test::waitUntil ([&] { return proc.analysis().read().timeSeconds >= 1.9
                                                   && proc.spectrum().read().frameIndex > 40u; }, 4000));

    telescope::SpectrumLens lens (proc);
    lens.setSize (1025, 702);
    lens.pumpFrames (3);

    telescope::SpectrumLens::Readout best;
    for (int x = 0; x < 1025; ++x)
    {
        const auto r = lens.readoutAtX (x);
        if (r.valid && (! best.valid || r.db > best.db)) best = r;
    }
    REQUIRE (best.valid);
    std::printf ("SETTINGS[lectura SPECTRUM] columna del pico: %.1f Hz  %s %+d cents  %.2f dB\n",
                 best.freqHz, best.note.name.toRawUTF8(), best.note.cents, best.db);
    REQUIRE (std::abs (best.freqHz / freq - 1.0) < 0.01);   // la columna de píxel cae sobre el tono
    REQUIRE (best.note.name == "B5");
    REQUIRE (std::abs (best.db + 20.0f) < 1.0f);

    proc.releaseResources();
}

// ========================================================================================================
// FIXES DEL 50 (revisor + auditora). Cada uno con su test, y cada test rojo antes del arreglo.
// ========================================================================================================

// (1a, HIGH) Al CERRAR la ventana, el motor tiene que volver a los módulos siempre-activos. Sin esto, un
// usuario que cierra el plugin con SPECTRUM/SPECTROGRAM/SCOPE a la vista deja el módulo caro corriendo
// para siempre (FFT de hasta orden 15 a ~375 frames/s) sin nadie mirando — que es exactamente lo contrario
// de "lente a demanda", y empeora con cada módulo nuevo.
//
// Y con DOS editores vivos (algunos hosts abren dos ventanas del mismo plugin), cerrar uno NO puede
// apagarle el módulo al otro: el processor cuenta editores y sólo resetea cuando llega a cero.
TEST_CASE ("telescope: al cerrar el editor el motor vuelve a los módulos siempre-activos", "[telescope][settings]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (48000.0, 512);

    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        REQUIRE (ed != nullptr);
        selectLens (proc, telescope::LensId::spectrum);
        REQUIRE ((proc.enabledModules() & telescope::kSpectrum) != 0u);
    }
    std::printf ("SETTINGS[cerrar editor] mascara tras cerrar = 0x%02x  (siempre-activos = 0x%02x)\n",
                 (unsigned) proc.enabledModules(), (unsigned) telescope::kAlwaysOnModules);
    REQUIRE (proc.enabledModules() == telescope::kAlwaysOnModules);

    // Y al volver a abrir, la lente persistida (`lens` sigue en SPECTRUM) reenciende su módulo.
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        REQUIRE (ed != nullptr);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
        REQUIRE ((proc.enabledModules() & telescope::kSpectrum) != 0u);
    }
    REQUIRE (proc.enabledModules() == telescope::kAlwaysOnModules);

    // DOS editores vivos: cerrar uno no apaga lo que el otro está mirando.
    {
        std::unique_ptr<juce::AudioProcessorEditor> a (proc.createEditor());
        std::unique_ptr<juce::AudioProcessorEditor> b (proc.createEditor());
        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
        REQUIRE ((proc.enabledModules() & telescope::kSpectrum) != 0u);

        a.reset();
        REQUIRE ((proc.enabledModules() & telescope::kSpectrum) != 0u);   // el otro sigue mirando

        b.reset();
        REQUIRE (proc.enabledModules() == telescope::kAlwaysOnModules);
    }

    proc.releaseResources();
}

// (1d, LOW) La cajita de lectura que sigue al cursor NUNCA se sale del área de dibujo. Con la lente
// angosta (o un texto largo) el ancho pedido supera el plot y el cálculo viejo devolvía una x a la
// IZQUIERDA del plot (jlimit con el límite inferior mayor que el superior).
TEST_CASE ("telescope: la caja de lectura nunca se sale del area de dibujo", "[telescope][settings]")
{
    const juce::Rectangle<int> plot { 40, 10, 200, 300 };
    int worstLeft = 0, worstRight = 0;

    for (const int textWidth : { 0, 20, 150, 199, 200, 201, 400, 4000 })
        for (const int cursorX : { 40, 41, 120, 238, 239 })
        {
            const auto box = telescope::readoutBoxFor (plot, cursorX, textWidth);
            worstLeft  = juce::jmin (worstLeft,  box.getX() - plot.getX());
            worstRight = juce::jmax (worstRight, box.getRight() - plot.getRight());
            REQUIRE (box.getX() >= plot.getX());
            REQUIRE (box.getRight() <= plot.getRight());
            REQUIRE (box.getWidth() >= 0);
        }

    std::printf ("SETTINGS[caja de lectura] desborde izquierdo peor = %d px  derecho peor = %d px  (criterio 0)\n",
                 worstLeft, worstRight);
}

// (1e, LOW) La lectura de dB del ESPECTROGRAMA sale del DATO (el byte del anillo), no del COLOR del píxel.
// La paleta de 256 entradas tiene DIEZ PARES de entradas con el mismo ARGB (dos índices consecutivos que
// redondean al mismo color de 8 bits), así que buscar "el color más parecido" devuelve siempre el índice
// más bajo del par: en esos diez niveles la lectura mentía un escalón entero (rango/255 = 0.353 dB con
// rango 90). Los cuatro niveles de acá son cuatro de esos diez, elegidos a propósito.
TEST_CASE ("telescope: la lectura del ESPECTROGRAMA sale del dato, no del color", "[telescope][settings]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    proc.setEnabledModules (telescope::kSpectrum | telescope::kLoudness);
    {
        auto st = proc.spectrumSettings();
        st.channel         = telescope::Spectrum::left;
        st.historySecIndex = 0;    // 10 s → una columna del anillo por píxel
        st.rangeDbIndex    = 1;    // 90 dB
        proc.setSpectrumSettings (st);
    }

    telescope::SpectrogramLens lens (proc);
    lens.setSize (1025, 702);
    juce::Image img (juce::Image::ARGB, 1025, 702, true);

    const double freq = 85.0 * 48000.0 / 4096.0;   // 996.09375 Hz: centro de bin exacto, sin scalloping
    long long n = 0;
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;

    for (const int wantByte : { 201, 194, 165, 137 })
    {
        // El nivel que cae EXACTAMENTE en el centro del escalón `wantByte` del mapeo de la columna.
        const double db  = (double) wantByte / 255.0 * 90.0 - 90.0;
        const float  amp = (float) std::pow (10.0, db / 20.0);
        const auto before = proc.spectrogram().writeIndex();

        for (int blk = 0; blk < (int) (1.5 * 48000.0 / 512.0); ++blk)
        {
            for (int i = 0; i < 512; ++i, ++n)
            {
                const auto v = (float) ((double) amp * std::sin (2.0 * juce::MathConstants<double>::pi
                                                                 * freq * (double) n / 48000.0));
                buf.setSample (0, i, v);
                buf.setSample (1, i, v);
            }
            proc.processBlock (buf, midi);
            if (blk % 20 == 19)
                REQUIRE (telescope::test::waitUntil (
                    [&] { return proc.spectrogram().writeIndex() > before; }, 3000));
        }
        REQUIRE (telescope::test::waitUntil (
            [&] { return proc.spectrogram().writeIndex() > before + 40; }, 5000));

        lens.pumpFrames (1);
        { juce::Graphics g (img); lens.paintEntireComponent (g, false); }

        // La columna de píxel MÁS NUEVA (la más a la derecha con lectura válida) y su pico.
        int px = -1;
        for (int x = lens.getWidth() - 1; x >= 0 && px < 0; --x)
            if (lens.readoutAt ({ x, lens.getHeight() / 2 }).valid) px = x;
        REQUIRE (px > 0);

        float peak = -1000.0f;
        for (int y = 0; y < lens.getHeight(); ++y)
        {
            const auto r = lens.readoutAt ({ px, y });
            if (r.valid) peak = juce::jmax (peak, r.db);
        }

        std::printf ("SETTINGS[lectura espectrograma] byte %d -> tono %.4f dB, leido %.4f dB  (error %.4f dB)\n",
                     wantByte, db, peak, (double) peak - db);
        REQUIRE (std::abs ((double) peak - db) < 0.02);
    }

    proc.releaseResources();
}

// ========================================================================================================
// PROMPT 51 · el estéreo POR BANDA visto desde el processor (el módulo tiene su propia batería, [bands]).
// ========================================================================================================

TEST_CASE ("telescope: los settings del estereo por banda sobreviven al round-trip", "[telescope][settings]")
{
    juce::MemoryBlock saved;
    {
        telescope::TelescopeProcessor proc;
        REQUIRE (proc.bandsWindowIndex() == telescope::StereoBands::kDefaultWindowIndex);
        REQUIRE (proc.bandsRow() == 0);
        proc.setBandsWindowIndex (2);   // 3 s
        proc.setBandsRow (2);           // BALANCE
        proc.getStateInformation (saved);
    }

    telescope::TelescopeProcessor proc;
    proc.setStateInformation (saved.getData(), (int) saved.getSize());
    REQUIRE (proc.bandsWindowIndex() == 2);
    REQUIRE (proc.bandsRow() == 2);
    REQUIRE (proc.bandsWindowSec() == telescope::StereoBands::kWindowSecOptions[2]);

    // Y el motor tiene que estar corriendo ESO, no lo que quedó al construir.
    proc.prepareToPlay (48000.0, 512);
    proc.setEnabledModules (telescope::kSpectrum | telescope::kStereoBands);
    pushPink (proc, 4.0, false, 0.2f);
    REQUIRE (telescope::test::waitUntil ([&] { return proc.analysis().read().bandsWindowSec > 2.5f; }, 6000));

    std::printf ("SETTINGS[bandas] ventana guardada %.1f s -> efectiva %.3f s  ·  fila secundaria = %d\n",
                 proc.bandsWindowSec(), proc.analysis().read().bandsWindowSec, proc.bandsRow());
    REQUIRE (proc.analysis().read().bandsWindowSec <= 3.1f);

    proc.releaseResources();
}

// Los 30×4 números llegan al AnalysisFrame, y dicen lo que tienen que decir: con L = −R toda banda con
// energía está fuera de fase y se cancela al monoficar.
TEST_CASE ("telescope: el AnalysisFrame trae los 30 numeros por banda", "[telescope][settings]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    proc.setEnabledModules (telescope::kSpectrum | telescope::kStereoBands);
    proc.setBandsWindowIndex (1);   // 1 s

    pushPink (proc, 4.0, true, 0.2f);   // L = -R
    REQUIRE (telescope::test::waitUntil ([&] { return proc.analysis().read().bandsValid > 20; }, 6000));

    const auto f = proc.analysis().read();
    int checked = 0;
    for (int b = 0; b < telescope::StereoBands::kNumBands; ++b)
    {
        REQUIRE (std::isfinite (f.bandCorr[b]));
        REQUIRE (std::isfinite (f.bandWidth[b]));
        REQUIRE (std::isfinite (f.bandBalanceDb[b]));
        REQUIRE (std::isfinite (f.bandMonoLossDb[b]));
        if (f.bandCorr[b] == 0.0f) continue;      // banda sin bins o sin energía: no hay medición
        INFO ("banda " << telescope::kThirdOctaveHz[b] << " Hz");
        REQUIRE (f.bandCorr[b] < -0.99f);
        REQUIRE (f.bandMonoLossDb[b] <= -60.0f);
        ++checked;
    }
    std::printf ("SETTINGS[bandas en el frame] %d bandas con medicion (bandsValid=%d), ventana %.3f s  ·  "
                 "corr de 1 kHz = %+.4f\n", checked, f.bandsValid, f.bandsWindowSec,
                 f.bandCorr[16]);
    REQUIRE (checked >= 20);
    REQUIRE (f.bandsValid >= checked);

    proc.releaseResources();
}

// Lente a demanda para el módulo del estéreo por banda: kStereoBands sólo con la lente 9 (BAND
// CORRELATION) o la 10 (STEREO SPECTROGRAM) a la vista. Es el módulo que más cuesta de los que hay: le
// suma hasta dos transformadas por frame al espectro, más un anillo de sumas por bin.
TEST_CASE ("telescope: lente a demanda — kStereoBands solo con las lentes de fase arriba", "[telescope][settings]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (48000.0, 512);

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    REQUIRE (ed != nullptr);

    selectLens (proc, telescope::LensId::spectrum);
    REQUIRE ((proc.enabledModules() & telescope::kSpectrum)     != 0u);
    REQUIRE ((proc.enabledModules() & telescope::kStereoBands)  == 0u);

    selectLens (proc, telescope::LensId::bandCorrelation);
    REQUIRE ((proc.enabledModules() & telescope::kStereoBands)  != 0u);
    REQUIRE ((proc.enabledModules() & telescope::kSpectrum)     != 0u);   // come de la misma STFT
    REQUIRE ((proc.enabledModules() & telescope::kLoudness)     != 0u);   // el medidor no se apaga nunca

    selectLens (proc, telescope::LensId::loudness);
    REQUIRE ((proc.enabledModules() & telescope::kStereoBands)  == 0u);

    selectLens (proc, telescope::LensId::bandCorrelation);
    REQUIRE ((proc.enabledModules() & telescope::kStereoBands)  != 0u);

    // Y al cerrar la ventana se apaga, como todo lo demás (fix 1a).
    ed.reset();
    std::printf ("SETTINGS[bandas a demanda] mascara tras cerrar = 0x%02x\n", (unsigned) proc.enabledModules());
    REQUIRE (proc.enabledModules() == telescope::kAlwaysOnModules);

    proc.releaseResources();
}

// La lectura de BAND CORRELATION y su resumen: sobre la señal mixta (graves mono + agudos fuera de fase)
// el resumen tiene que NOMBRAR una banda aguda, que es exactamente el trabajo de la lente.
TEST_CASE ("telescope: BAND CORRELATION lee la banda bajo el cursor y resume la peor", "[telescope][settings]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    proc.setEnabledModules (telescope::kSpectrum | telescope::kStereoBands | telescope::kLoudness);
    proc.setBandsWindowIndex (1);   // 1 s

    // La señal mixta de la casa: graves MONO + agudos FUERA DE FASE (ver TestSignals.h).
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

    telescope::BandCorrelationLens lens (proc);
    lens.setSize (1025, 702);
    lens.pumpFrames (3);

    // El resumen nombra la banda MÁS fuera de fase y la de mayor pérdida al monoficar.
    const auto sum = lens.summary();
    REQUIRE (sum.valid);
    const auto text = lens.summaryText();
    std::printf ("SETTINGS[resumen BAND CORRELATION] \"%s\"  (peor fase = banda %d, peor mono = banda %d)\n",
                 text.toRawUTF8(), sum.worstPhaseBand, sum.worstMonoBand);
    REQUIRE (text.isNotEmpty());
    REQUIRE (sum.worstPhaseBand >= 0);
    // La banda que nombra tiene que estar en la parte FUERA DE FASE de la señal (todo lo que está por
    // encima del seno grave), nunca en los graves mono. Cuál exactamente no está definido cuando muchas
    // empatan en -1.00: el resumen es una referencia, no un veredicto — VERDICT (lente 13) es la que
    // concluye, y ahí la desempata la energía.
    REQUIRE (telescope::kThirdOctaveHz[sum.worstPhaseBand] > 160.0);
    REQUIRE (sum.worstCorr < -0.9f);

    // Y la lectura al pasar el cursor devuelve la banda que hay debajo, con sus bins.
    bool sawLow = false, sawHigh = false;
    for (int x = 0; x < lens.getWidth(); ++x)
    {
        const auto r = lens.readoutAt ({ x, lens.getHeight() / 3 });
        if (! r.valid) continue;
        REQUIRE (r.band >= 0);
        REQUIRE (r.band < telescope::StereoBands::kNumBands);
        REQUIRE (r.centreHz == telescope::kThirdOctaveHz[r.band]);
        if (r.centreHz <= 160.0 && r.bins > 0 && r.corr != 0.0f) { REQUIRE (r.corr > 0.9f);  sawLow = true; }
        if (r.centreHz >= 4000.0 && r.bins > 0)                  { REQUIRE (r.corr < -0.9f); sawHigh = true; }
    }
    std::printf ("SETTINGS[lectura BAND CORRELATION] bandas graves en fase leidas = %d  ·  agudas fuera de fase = %d\n",
                 (int) sawLow, (int) sawHigh);
    REQUIRE (sawLow);
    REQUIRE (sawHigh);

    proc.releaseResources();
}

// ============================================================================ CQT (prompt 52)
// Los settings del constant-Q (canal analizado y suavizado del cromagrama) viven en el ValueTree como el
// resto de los settings de lente, así que tienen que sobrevivir al round-trip Y llegar al motor.
TEST_CASE ("telescope: los settings del CQT sobreviven al round-trip de estado", "[telescope][settings]")
{
    telescope::Cqt::Settings wanted;
    wanted.channel        = telescope::Cqt::right;   // distinto del default (mid)
    wanted.chromaSecIndex = 2;                       // 5 s, distinto del default (2 s)

    juce::MemoryBlock saved;
    {
        telescope::TelescopeProcessor proc;
        proc.setCqtSettings (wanted);
        proc.getStateInformation (saved);
    }

    telescope::TelescopeProcessor proc;
    REQUIRE (proc.cqtSettings().channel == telescope::Cqt::Settings{}.channel);   // arranca en el default

    proc.setStateInformation (saved.getData(), (int) saved.getSize());
    std::printf ("SETTINGS[cqt] canal=%d  suavizado=%.1f s  (guardado y recuperado)\n",
                 proc.cqtSettings().channel, proc.cqtSettings().chromaSeconds());
    REQUIRE (proc.cqtSettings().channel == telescope::Cqt::right);
    REQUIRE (proc.cqtSettings().chromaSecIndex == 2);

    // El decaimiento del peak hold NO es un setting propio: es el MISMO de SPECTRUM, y tiene que seguirlo.
    auto sp = proc.spectrumSettings();
    sp.holdDecayDbPerSec = 30.0f;
    proc.setSpectrumSettings (sp);
    REQUIRE (proc.cqtSettings().holdDecayDbPerSec == 30.0f);
}

// Lente a demanda: kCqt sólo con CQT (o SPIRAL) a la vista. Es el módulo que construye ~229 kernels de
// 2^16 la primera vez que se le pide un frame: encenderlo sin que nadie lo mire sería pagar eso de gusto.
TEST_CASE ("telescope: lente a demanda — kCqt solo con las lentes musicales arriba", "[telescope][settings]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (48000.0, 512);

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    REQUIRE (ed != nullptr);

    selectLens (proc, telescope::LensId::loudness);
    REQUIRE ((proc.enabledModules() & telescope::kCqt) == 0u);

    selectLens (proc, telescope::LensId::cqt);
    REQUIRE ((proc.enabledModules() & telescope::kCqt) != 0u);
    REQUIRE ((proc.enabledModules() & telescope::kLoudness) != 0u);   // el medidor no se apaga nunca
    REQUIRE ((proc.enabledModules() & telescope::kSpectrum) == 0u);   // y el CQT no arrastra al FFT
    REQUIRE ((proc.enabledModules() & telescope::kStereo)   == 0u);

    selectLens (proc, telescope::LensId::spectrum);
    REQUIRE ((proc.enabledModules() & telescope::kCqt) == 0u);

    ed.reset();
    proc.releaseResources();
}

// LA LECTURA DE LA LENTE CQT: nota, octava, cents y dB. Los cents salen de la POSICIÓN del cursor (no del
// índice del bin, que sólo podría dar 0 o 50), y el dB del bin que está debajo.
TEST_CASE ("telescope: la lectura de CQT dice nota, cents y dB del bin", "[telescope][settings]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    proc.setEnabledModules (telescope::kCqt | telescope::kLoudness);

    // Un La 440 a -20 dBFS: la lectura sobre su barra tiene que decir A4, 0 cents y -20 dB.
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    long long n = 0;
    for (int b = 0; b < 300; ++b)   // ~3.2 s
    {
        for (int i = 0; i < 512; ++i, ++n)
        {
            const auto v = (float) (0.1 * std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * (double) n / 48000.0));
            buf.setSample (0, i, v);
            buf.setSample (1, i, v);
        }
        proc.processBlock (buf, midi);
    }
    REQUIRE (telescope::test::waitUntil ([&] { return proc.cqt().read().frameIndex > 20u; }, 8000));

    telescope::CqtLens lens (proc);
    lens.setSize (1025, 702);
    lens.pumpFrames (5);

    // El centro del bin de A4 (96): su borde izquierdo más medio bin.
    const int x = juce::roundToInt (0.5f * (lens.xForBinEdge (96) + lens.xForBinEdge (97)));
    const auto r = lens.readoutAtX (x);

    // La tolerancia se DERIVA de la geometría: un bin son 1200/B = 50 cents y mide
    // (ancho del plot / numBins) píxeles, así que un píxel vale 50·numBins/ancho cents. El cursor es un
    // entero, así que su error es medio píxel.
    const float anchoBin = lens.xForBinEdge (97) - lens.xForBinEdge (96);
    const double centsPorPixel = (1200.0 / (double) telescope::Cqt::kBinsPerOctave) / (double) anchoBin;
    const int tolerancia = (int) std::ceil (0.5 * centsPorPixel) + 1;

    std::printf ("SETTINGS[cqt lectura] x=%d  ->  bin %d  %s %+d cents  %.2f Hz  %.2f dB   "
                 "(bin = %.2f px = %.1f cents/px, tolerancia %d)\n",
                 x, r.bin, r.note.name.toRawUTF8(), r.note.cents, r.freqHz, r.db,
                 anchoBin, centsPorPixel, tolerancia);

    REQUIRE (r.valid);
    REQUIRE (r.bin == 96);
    REQUIRE (r.note.name == "A4");
    REQUIRE (std::abs (r.note.cents) <= tolerancia);   // parado en el CENTRO de la barra de A4
    REQUIRE (std::abs (r.db + 20.0f) < 0.3f);

    // Fuera del área de dibujo no hay lectura (y no se inventa una).
    REQUIRE_FALSE (lens.readoutAtX (2).valid);

    // Y el eje es LINEAL EN BINS: dos bins consecutivos ocupan lo mismo en las dos puntas del eje.
    const float anchoGrave = lens.xForBinEdge (1) - lens.xForBinEdge (0);
    const float anchoAgudo = lens.xForBinEdge (200) - lens.xForBinEdge (199);
    std::printf ("SETTINGS[cqt eje] ancho del bin 0 = %.3f px  ·  del bin 199 = %.3f px\n",
                 anchoGrave, anchoAgudo);
    REQUIRE (std::abs (anchoGrave - anchoAgudo) < 0.01f);

    proc.releaseResources();
}

// EL MAPEO DE SPIRAL, puro: sólo depende del bin y de los bins por octava, así que se testea sin lente,
// sin señal y sin tamaño. Es la definición que sostiene todo el dibujo — si esto se corre, las notas
// iguales dejan de alinearse y la lente pierde su única razón de existir.
TEST_CASE ("telescope: en SPIRAL el angulo es la nota y el radio la octava", "[telescope][settings]")
{
    constexpr int B = telescope::Cqt::kBinsPerOctave;

    // Los cinco La del eje: mismo ángulo (0.75 de vuelta = 9 semitonos = La), octava distinta.
    for (int oct = 0; oct <= 4; ++oct)
    {
        const int bin = oct * B;
        const auto p = telescope::SpiralLens::positionFor (bin, B);
        std::printf ("SETTINGS[spiral] bin %3d  ->  vueltas %.4f  octava %d  clase %.4f  angulo %.1f grados\n",
                     bin, p.turns, p.octave, p.classFraction,
                     p.angleRad * 180.0f / juce::MathConstants<float>::pi);
        INFO ("bin " << bin);
        REQUIRE (p.octave == oct);
        REQUIRE_THAT (p.classFraction, Catch::Matchers::WithinAbs (0.75f, 1.0e-5f));   // La = 9/12
        REQUIRE_THAT (p.turns, Catch::Matchers::WithinAbs (0.75 + (double) oct, 1.0e-9));
    }

    // Los dos casos que pide el prompt, explícitos.
    REQUIRE (telescope::SpiralLens::positionFor (96, B).octave == 4);    // A4
    REQUIRE (telescope::SpiralLens::positionFor (0, B).octave == 0);     // A0

    // Do arriba: la clase 0 está en el ángulo 0 (las 12 en punto) y cada semitono avanza 1/12 de vuelta.
    const int binC4 = 78;                                    // 27.5·2^(78/24) = C4
    const auto c4 = telescope::SpiralLens::positionFor (binC4, B);
    REQUIRE (c4.octave == 4);
    REQUIRE_THAT (c4.classFraction, Catch::Matchers::WithinAbs (0.0f, 1.0e-5f));
    REQUIRE_THAT (c4.angleRad, Catch::Matchers::WithinAbs (0.0f, 1.0e-5f));

    // Y una vuelta entera son exactamente B bins: las notas iguales quedan en el MISMO ángulo.
    for (int bin = 0; bin + B < 200; ++bin)
        REQUIRE_THAT (telescope::SpiralLens::positionFor (bin, B).classFraction,
                      Catch::Matchers::WithinAbs (telescope::SpiralLens::positionFor (bin + B, B).classFraction,
                                                  1.0e-5f));
}

// Los botones de SPIRAL escriben los MISMOS settings que los de CQT (son la misma medición mirada de dos
// maneras): que cada lente tuviera su canal sería garantizar que algún día muestren cosas distintas.
TEST_CASE ("telescope: los botones de SPIRAL mueven los settings del CQT", "[telescope][settings]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (48000.0, 512);

    telescope::SpiralLens lens (proc);
    lens.setSize (1025, 702);

    const int canalAntes = proc.cqtSettings().channel;
    lens.cycleControl (telescope::SpiralLens::ctrlChannel);
    const int canalDespues = proc.cqtSettings().channel;

    const int cromaAntes = proc.cqtSettings().chromaSecIndex;
    lens.cycleControl (telescope::SpiralLens::ctrlChroma);
    const int cromaDespues = proc.cqtSettings().chromaSecIndex;

    std::printf ("SETTINGS[spiral botones] canal %d -> %d  ·  croma %d -> %d\n",
                 canalAntes, canalDespues, cromaAntes, cromaDespues);
    REQUIRE (canalDespues == (canalAntes + 1) % (int) telescope::Cqt::kNumChannels);
    REQUIRE (cromaDespues == (cromaAntes + 1) % telescope::Cqt::kNumChromaSecOptions);

    // Y vuelven al mismo lugar dando la vuelta completa (round-trip por los botones).
    for (int i = 1; i < (int) telescope::Cqt::kNumChannels; ++i)
        lens.cycleControl (telescope::SpiralLens::ctrlChannel);
    for (int i = 1; i < telescope::Cqt::kNumChromaSecOptions; ++i)
        lens.cycleControl (telescope::SpiralLens::ctrlChroma);
    REQUIRE (proc.cqtSettings().channel == canalAntes);
    REQUIRE (proc.cqtSettings().chromaSecIndex == cromaAntes);

    proc.releaseResources();
}

// Lente a demanda: SPIRAL pide el mismo módulo que CQT y ningún otro.
TEST_CASE ("telescope: lente a demanda — SPIRAL enciende kCqt y nada mas", "[telescope][settings]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    REQUIRE (ed != nullptr);

    selectLens (proc, telescope::LensId::spiral);
    REQUIRE ((proc.enabledModules() & telescope::kCqt) != 0u);
    REQUIRE ((proc.enabledModules() & telescope::kLoudness) != 0u);
    REQUIRE ((proc.enabledModules() & telescope::kSpectrum) == 0u);
    REQUIRE ((proc.enabledModules() & telescope::kStereo) == 0u);
    REQUIRE ((proc.enabledModules() & telescope::kStereoBands) == 0u);

    selectLens (proc, telescope::LensId::loudness);
    REQUIRE ((proc.enabledModules() & telescope::kCqt) == 0u);

    ed.reset();
    proc.releaseResources();
}

// ========================================================================================================
// 56b · LAS TRES PROPIEDADES DEL ÁRBOL SOBREVIVEN AL ESTADO, Y EL IDIOMA REPINTA
//
// `language`, `scopeMode` y `hemisphereDecayDbPerSec` no son parámetros del APVTS: viven en el ValueTree
// como propiedades sueltas, así que el round-trip por getStateInformation/setStateInformation NO lo
// garantiza el framework — lo garantiza que el árbol entero se serialice. `language` tenía test (55);
// las otras dos entraron en el 56 sin ninguno, y un ajuste que se pierde al cerrar la sesión es un bug
// que sólo aparece en manos del usuario.
//
// La segunda mitad es lo que hace útil al selector: que cambiar el idioma REPINTE. La lente no cachea el
// idioma —lo lee del árbol cuando pinta— pero su repaint está suspendido mientras el dibujo no cambia
// (VisualizerBase pausa en reposo), así que sin el listener del editor el cambio no se vería hasta que
// algo más pidiera un repintado.
// ========================================================================================================
TEST_CASE ("telescope: idioma, modo de SCOPE y decaimiento del hemisferio sobreviven al estado",
           "[telescope][settings][i18n]")
{
    juce::MemoryBlock state;
    {
        telescope::TelescopeProcessor proc;
        proc.prepareToPlay (48000.0, 512);
        telescope::strings::setLanguage (proc.apvts.state, "de");
        proc.apvts.state.setProperty (telescope::ScopeLens::kScopeModeProperty, 2, nullptr);
        proc.apvts.state.setProperty (telescope::ScopeLens::kHemiDecayProperty, 3, nullptr);
        proc.getStateInformation (state);
        proc.releaseResources();
    }

    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    proc.setStateInformation (state.getData(), (int) state.getSize());

    const auto lang = telescope::strings::languageOf (proc.apvts.state);
    const int  mode = (int) proc.apvts.state.getProperty (telescope::ScopeLens::kScopeModeProperty, -1);
    const int  hemi = (int) proc.apvts.state.getProperty (telescope::ScopeLens::kHemiDecayProperty, -1);
    std::printf ("SETTINGS[round-trip] language=\"%s\"  scopeMode=%d  hemisphereDecayDbPerSec=%d\n",
                 lang.toRawUTF8(), mode, hemi);
    REQUIRE (lang == "de");
    REQUIRE (mode == 2);
    REQUIRE (hemi == 3);

    // Un idioma que esta versión no tiene no deja la pantalla muda: cae al default de D-50.
    proc.apvts.state.setProperty (telescope::strings::kLanguageProperty, "zz", nullptr);
    REQUIRE (telescope::strings::languageOf (proc.apvts.state) == "en");
    telescope::strings::setLanguage (proc.apvts.state, "en");

    // ---- y que el cambio de idioma llegue a la PANTALLA ----
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto* tel = dynamic_cast<telescope::TelescopeEditor*> (ed.get());
    REQUIRE (tel != nullptr);
    tel->setSize (980, 620);

    const auto shoot = [&tel]
    {
        tel->pumpLensFrames (3);
        juce::Image img (juce::Image::ARGB, tel->getWidth(), tel->getHeight(), true);
        juce::Graphics g (img);
        tel->paintEntireComponent (g, true);
        return img;
    };

    const auto en = shoot();
    telescope::strings::setLanguage (proc.apvts.state, "es");
    juce::MessageManager::getInstance()->runDispatchLoopUntil (30);
    const auto es = shoot();

    // La TIRA (izquierda) y la LENTE (el resto) cambian las DOS. Se cuentan por separado a propósito: con
    // un solo conteo global, que sólo repintara una de las dos pasaría igual.
    const int split = 190;   // ancho de la tira (150 de diseño) más el margen, en píxeles del editor
    int stripDiff = 0, lensDiff = 0;
    for (int y = 0; y < en.getHeight(); ++y)
        for (int x = 0; x < en.getWidth(); ++x)
            if (en.getPixelAt (x, y) != es.getPixelAt (x, y))
                (x < split ? stripDiff : lensDiff) += 1;

    std::printf ("SETTINGS[idioma] en -> es: %d px distintos en la tira, %d en la lente visible\n",
                 stripDiff, lensDiff);
    REQUIRE (stripDiff > 200);
    REQUIRE (lensDiff  > 200);

    proc.releaseResources();
}

// ========================================================================================================
// ===== 56c ===== EL IDIOMA LLEGA AL EDITOR DESDE CUALQUIER HILO, Y TAMBIÉN CUANDO EL HOST RESTAURA
//
// Dos agujeros que el 56b dejó abiertos y que `SETTINGS[round-trip]` no podía ver porque prueba el
// PROCESSOR, no el editor:
//
//   1 · `juce::ValueTree::Listener` notifica de forma SÍNCRONA, en el hilo que hizo el `setProperty`. El
//       editor colgaba `applyLanguage()` —que toca la tira y pide repaint— directo de ese callback, sin
//       pasar por el message thread. `parameterChanged`, dos funciones más arriba, sí lo hacía. Un preset
//       cambiado desde el hilo de automatización del host tocaba componentes desde ese hilo.
//
//   2 · `apvts.replaceState (tree)` hace `state = newState` (JUCE 8.0.13,
//       juce_AudioProcessorValueTreeState.cpp:400), y `ValueTree::operator=` avisa por
//       `valueTreeRedirected`, NO por `valueTreePropertyChanged`. El editor sólo escuchaba lo segundo, así
//       que con la ventana abierta un idioma restaurado por el host no llegaba nunca a la pantalla.
//
// El primero no se puede ver mirando píxeles: una pantalla correcta pintada desde el hilo equivocado se
// ve igual acá y explota en el host del usuario. Por eso el editor deja dicho en qué hilo aplicó.
// ========================================================================================================
TEST_CASE ("telescope: el idioma cambiado desde otro hilo se aplica en el message thread",
           "[telescope][settings][i18n]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    telescope::strings::setLanguage (proc.apvts.state, "en");

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto* tel = dynamic_cast<telescope::TelescopeEditor*> (ed.get());
    REQUIRE (tel != nullptr);
    tel->setSize (980, 620);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (30);

    const int before = tel->languageApplyCount();
    REQUIRE (tel->stripLanguage() == "en");

    // El cambio viene de OTRO hilo, como lo haría la automatización o la carga de un preset del host.
    std::thread ([&proc] { telescope::strings::setLanguage (proc.apvts.state, "es"); }).join();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (120);

    std::printf ("SETTINGS[hilo] applyLanguage: %d -> %d veces  ·  ultima en el message thread: %s  ·  "
                 "la tira dice \"%s\"\n",
                 before, tel->languageApplyCount(),
                 tel->lastApplyWasOnMessageThread() ? "si" : "NO",
                 telescope::strings::endonymOf (tel->stripLanguage()).toRawUTF8());

    REQUIRE (tel->languageApplyCount() > before);
    REQUIRE (tel->lastApplyWasOnMessageThread());
    REQUIRE (telescope::strings::endonymOf (tel->stripLanguage()) == juce::String::fromUTF8 ("Espa\xc3\xb1ol"));

    proc.releaseResources();
}

TEST_CASE ("telescope: el idioma que restaura el host llega al editor abierto",
           "[telescope][settings][i18n]")
{
    juce::MemoryBlock saved;
    {
        telescope::TelescopeProcessor tmp;
        tmp.prepareToPlay (48000.0, 512);
        telescope::strings::setLanguage (tmp.apvts.state, "de");
        tmp.getStateInformation (saved);
        tmp.releaseResources();
    }

    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    telescope::strings::setLanguage (proc.apvts.state, "en");

    // La ventana YA está abierta cuando el host restaura: es el caso que se rompía.
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto* tel = dynamic_cast<telescope::TelescopeEditor*> (ed.get());
    REQUIRE (tel != nullptr);
    tel->setSize (980, 620);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (30);
    REQUIRE (tel->stripLanguage() == "en");

    const auto shoot = [&tel]
    {
        tel->pumpLensFrames (3);
        juce::Image img (juce::Image::ARGB, tel->getWidth(), tel->getHeight(), true);
        juce::Graphics g (img);
        tel->paintEntireComponent (g, true);
        return img;
    };
    const auto en = shoot();

    proc.setStateInformation (saved.getData(), (int) saved.getSize());
    juce::MessageManager::getInstance()->runDispatchLoopUntil (120);
    const auto de = shoot();

    int stripDiff = 0, lensDiff = 0;
    constexpr int kSplit = 190;   // el ancho de la tira más el margen, en píxeles del editor
    for (int y = 0; y < en.getHeight(); ++y)
        for (int x = 0; x < en.getWidth(); ++x)
            if (en.getPixelAt (x, y) != de.getPixelAt (x, y))
                (x < kSplit ? stripDiff : lensDiff) += 1;

    std::printf ("SETTINGS[restaurado] el arbol dice \"%s\", la tira dice \"%s\"  ·  %d px distintos en la "
                 "tira, %d en la lente visible\n",
                 telescope::strings::languageOf (proc.apvts.state).toRawUTF8(),
                 telescope::strings::endonymOf (tel->stripLanguage()).toRawUTF8(), stripDiff, lensDiff);

    REQUIRE (telescope::strings::languageOf (proc.apvts.state) == "de");
    REQUIRE (telescope::strings::endonymOf (tel->stripLanguage()) == "Deutsch");
    REQUIRE (stripDiff > 200);
    REQUIRE (lensDiff  > 200);

    proc.releaseResources();
}

// ========================================================================================================
// ===== 56c ===== LA TIRA APRIETA DONDE DIBUJA
//
// `paint()` repartía las trece filas con `jmax (kNumLenses, alto - kLanguageRowH)` y `rowAt()` —el que
// decide qué lente se eligió con el clic— con `jmax (0, alto - kLanguageRowH)`. Dos bases distintas para
// la misma división: donde no coinciden, el usuario aprieta una lente y se abre otra. En S/M/L las dos
// dan el mismo número y por eso nadie lo vio; con la tira apretada el desfase es total.
//
// Ahora las dos salen de `rowBounds()`, y esto lo comprueba en los tres tamaños de verdad —los que da el
// editor, no unos inventados— más un alto degenerado que es donde las dos cuentas se separaban.
// ========================================================================================================
namespace
{
telescope::LensStrip* findStrip (juce::Component& c)
{
    for (auto* child : c.getChildren())
    {
        if (auto* s = dynamic_cast<telescope::LensStrip*> (child)) return s;
        if (auto* s = findStrip (*child)) return s;
    }
    return nullptr;
}
}

TEST_CASE ("telescope: la tira de lentes elige la fila que dibuja", "[telescope][settings]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (48000.0, 512);

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto* tel = dynamic_cast<telescope::TelescopeEditor*> (ed.get());
    REQUIRE (tel != nullptr);

    const struct { ovni::PluginEditorBase::Zoom zoom; const char* name; } zooms[] = {
        { ovni::PluginEditorBase::Zoom::small,  "S" },
        { ovni::PluginEditorBase::Zoom::medium, "M" },
        { ovni::PluginEditorBase::Zoom::large,  "L" },
    };

    // EXHAUSTIVO sobre la altura de la lista: para CADA y entero, la fila que elige `rowAt` tiene que ser
    // la fila que `rowBounds` dibuja ahí. La fila esperada sale de la geometría de dibujo, no de una
    // fórmula copiada — si el test se escribiera la cuenta, mediría su propia copia.
    const auto comprobar = [] (telescope::LensStrip& strip, const char* etiqueta)
    {
        const auto filaDibujadaEn = [&strip] (int y)
        {
            for (int i = 0; i < telescope::kNumLenses; ++i)
            {
                const auto r = strip.rowBounds (i);
                if ((float) y >= r.getY() && (float) y < r.getBottom()) return i;
            }
            return -1;
        };

        int malas = 0, puntos = 0;
        for (int y = 0; y < strip.listHeight(); ++y)
        {
            ++puntos;
            if (strip.rowAt (y) != filaDibujadaEn (y)) ++malas;
        }

        std::printf ("STRIP[filas] %-6s tira %3dx%-4d  lista %3d px  ·  fila 0 = [%.1f, %.1f)  ·  "
                     "%d de %d filas de pixeles eligen otra lente  %s\n",
                     etiqueta, strip.getWidth(), strip.getHeight(), strip.listHeight(),
                     strip.rowBounds (0).getY(), strip.rowBounds (0).getBottom(),
                     malas, puntos, malas == 0 ? "OK" : "MAL");
        return malas;
    };

    for (const auto& z : zooms)
    {
        tel->applyZoom (z.zoom);
        auto* strip = findStrip (*tel);
        REQUIRE (strip != nullptr);
        REQUIRE (strip->getHeight() > 0);
        CHECK (comprobar (*strip, z.name) == 0);
    }

    // Los tres zooms dan la MISMA tira de 546: `applyZoom` aplica una AffineTransform sobre el editor
    // entero, no vuelve a repartir los hijos, así que la geometría lógica de la tira no cambia. Queda
    // dicho para que el log no se lea como tres tamaños distintos que casualmente coinciden.
    //
    // Lo que sí barre alturas de verdad es esto: por debajo de kNumLenses + kLanguageRowH = 39 las dos
    // cuentas se separaban (una con piso en 13, la otra en 0) y `rowAt` devolvía cualquier cosa.
    for (const int alto : { 30, 38, 39, 40, 100, 546, 900 })
    {
        telescope::LensStrip suelta;
        suelta.setSize (150, alto);
        CHECK (comprobar (suelta, (juce::String (alto) + "px").toRawUTF8()) == 0);
    }

    proc.releaseResources();
}
