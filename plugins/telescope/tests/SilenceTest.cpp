// [telescope][silencio] — T4 de la 0.2 (D-113): lo que TELESCOPE muestra EN VIVO cuando no suena nada.
//
// Con el transporte parado el host manda ceros, y tres lentes decían cosas que confunden (veredicto 96, obs. 5):
//   T4[dyn]      DYNAMICS apilaba el silencio en el bin de abajo del histograma de short-term. Ahora cuenta
//                sólo los short-term sobre la compuerta absoluta de EBU R128 (−70 LUFS).
//   T4[tonal]    la curva viva de TONAL BALANCE se hundía: el promedio de potencia sumaba cuadros vacíos
//                y el integrado (que tiene compuerta) no bajaba. Ahora un cuadro bajo −70 LUFS no entra.
//   T4[verdict]  VERDICT decía «en vivo · N s» contando segundos de ceros. Ahora dice «esperando audio»
//                si en la ventana analizada no hubo audio sobre la compuerta.
// Cada uno con tres casos: silencio, señal, y silencio DESPUÉS de señal. El camino del ARCHIVO no cambia
// (lo cuida [golden]): esto es sólo el camino en vivo.
//   T4[compuerta] la loudness del cuadro que decide la compuerta de TONAL BALANCE coincide con el medidor.
//   F5[verdict]  el reparo de la F4 (D-122): con la ventana en silencio VERDICT decía «esperando audio» arriba, pero
//                abajo seguía el informe de ceros («3 chequeos dentro de rango · 1 para revisar», un ⚠ de Celular,
//                cinco tildes y «10 s analizados»). Ahora el motor no evalúa sin audio sobre la compuerta: el informe
//                sale vacío, la lista también, y el silencio no cuenta como analizado. Vale también por ARCHIVO
//                (es el mismo Verdict::evaluate): ahí cambia el INFORME de un archivo sin audio, no un número del
//                análisis — [golden] y telescope-measure no pasan por VERDICT.
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <vector>
#include "PluginProcessor.h"
#include "TestHelpers.h"
#include "TestSignals.h"
#include "TestWav.h"
#include "analysis/FileAnalysis.h"
#include "analysis/FileAnalyzer.h"
#include "analysis/ReferenceFrame.h"
#include "analysis/SecondHistory.h"
#include "analysis/modules/Loudness.h"
#include "analysis/modules/Reference.h"
#include "analysis/modules/Spectrum.h"
#include "analysis/modules/Verdict.h"
#include "lenses/VerdictLens.h"

using telescope::Loudness;
using telescope::Reference;
using telescope::ReferenceFrame;
using telescope::Spectrum;

namespace
{
constexpr double kSr = 48000.0;

// ---------------------------------------------------------------------------------------- el camino vivo
// Un processor de verdad, con los módulos que pida el caso. Todo se empuja en hops enteros y se espera a que
// el motor lo haya digerido entero (la regla de la F2b: nada de fotos de un motor a medio comer).
struct Live
{
    telescope::TelescopeProcessor proc;
    long long pushed = 0;
    double    phase  = 0.0;

    explicit Live (juce::uint32 modules)
    {
        proc.prepareToPlay (kSr, 512);
        proc.setEnabledModules (modules | telescope::kAlwaysOnModules);
    }
    ~Live() { proc.releaseResources(); }

    // Un seno de 1 kHz a `dbfs` de pico en los dos canales (dbfs <= -200: ceros).
    void push (double seconds, double dbfs)
    {
        const double a  = dbfs <= -200.0 ? 0.0 : std::pow (10.0, dbfs / 20.0);
        const double dp = 2.0 * juce::MathConstants<double>::pi * 1000.0 / kSr;
        pushWith (seconds, [&] (float& l, float& r) { l = r = (float) (a * std::sin (phase)); phase += dp; });
    }

    // Ruido rosa estéreo a `peak` (TONAL BALANCE necesita energía en todas las bandas).
    void pushPink (double seconds, float peak)
    {
        pushWith (seconds, [&] (float& l, float& r) { l = peak * pinkL.next(); r = peak * pinkR.next(); });
    }

    telescope::test::Pink pinkL { telescope::test::kPinkSeedA }, pinkR { telescope::test::kPinkSeedB };

    // Empuja en bloques de 512 y frena con el total ABSOLUTO desde el reset. `pushExact` cuenta desde el
    // principio de SU llamada, así que en un segundo empuje nunca frenaba: llenaba el bus de 8 s y se perdía
    // audio (lo vio este test: 1,1 s de 10 s de silencio nunca llegaron al motor).
    template <typename Gen>
    void pushWith (double seconds, Gen&& gen)
    {
        const auto n = (long long) std::llround (seconds * kSr);
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        for (long long done = 0; done < n;)
        {
            const int k = (int) juce::jmin ((long long) 512, n - done);
            buf.setSize (2, k, false, false, true);
            for (int i = 0; i < k; ++i)
            {
                float l = 0.0f, r = 0.0f;
                gen (l, r);
                buf.setSample (0, i, l);
                buf.setSample (1, i, r);
            }
            proc.processBlock (buf, midi);
            done += k;
            telescope::test::keepUp (proc, pushed + done, kSr);
        }
        pushed += n;
        telescope::test::waitDigested (proc, pushed, kSr);
    }

    std::vector<juce::uint32> histogram()
    {
        const auto& f = proc.analysis().read();
        return { f.histogram, f.histogram + 61 };
    }
};

juce::uint32 sum (const std::vector<juce::uint32>& h)
{
    juce::uint32 s = 0;
    for (const auto v : h) s += v;
    return s;
}

// ---------------------------------------------------------------------------------------- el módulo
// El lado vivo de TONAL BALANCE en chiquito (como LiveRun de ReferenceTest), con un promedio SIN compuerta
// colgado del mismo espectro: es el «antes» del arreglo, medido en la misma corrida.
struct TonalRun
{
    Spectrum  spectrum;
    Loudness  loudness;
    Reference reference;
    telescope::ThirdOctaveAverage ungated;   // el promedio de antes: todos los cuadros
    telescope::FrameSinkFanout    fanout;
    std::unique_ptr<telescope::test::Pink> a, b;

    TonalRun()
        : a (std::make_unique<telescope::test::Pink> (telescope::test::kPinkSeedA)),
          b (std::make_unique<telescope::test::Pink> (telescope::test::kPinkSeedB))
    {
        fanout.set (0, &reference);
        fanout.set (1, &ungated);
        spectrum.setFrameSink (&fanout);
        spectrum.applySettings (Spectrum::Settings{});
        spectrum.prepare (kSr);
        spectrum.reset();
        loudness.prepare (kSr);
        loudness.reset();
        reference.resetLive();
        ungated.reset();
    }

    // Ruido rosa estéreo a `peak` (0: ceros).
    void feed (double seconds, float peak)
    {
        const auto total = (long long) std::llround (seconds * kSr);
        std::vector<float> L (512), R (512);
        for (long long done = 0; done < total;)
        {
            const int k = (int) std::min ((long long) 512, total - done);
            for (int i = 0; i < k; ++i)
            {
                const float l = a->next(), r = b->next();   // se avanza igual con peak 0: la misma realización
                L[(size_t) i] = peak * l;
                R[(size_t) i] = peak * r;
            }
            spectrum.process (L.data(), R.data(), k);
            loudness.process (L.data(), R.data(), k);
            done += k;
        }
        const auto res = loudness.result();
        reference.setLiveLoudness (res.integrated, res.integratedValid);
    }

    ReferenceFrame frame() const
    {
        ReferenceFrame f;
        reference.fill (f);
        return f;
    }

    // La curva normalizada del promedio SIN compuerta (lo que dibujaba la lente antes), con el mismo integrado.
    void ungatedNorm (float* dst) const
    {
        ungated.bandsDb (dst);
        const auto res = loudness.result();
        for (int bnd = 0; bnd < ReferenceFrame::kNumBands; ++bnd) dst[bnd] -= res.integrated;
    }
};

// La mayor diferencia entre dos curvas normalizadas, en las bandas que las dos miden (≥ −90 dBFS crudo).
float maxDiff (const float* x, const float* y, const float* raw)
{
    float m = 0.0f;
    for (int bnd = 0; bnd < ReferenceFrame::kNumBands; ++bnd)
        if (raw[bnd] >= -90.0f) m = std::max (m, std::abs (x[bnd] - y[bnd]));
    return m;
}
}

// ============================================================================================ T4[dyn]
TEST_CASE ("telescope: T4 · DYNAMICS no apila el silencio en el histograma", "[telescope][silencio]")
{
    SECTION ("silencio")
    {
        Live live (0);
        live.push (10.0, -300.0);
        const auto h = live.histogram();
        std::printf ("T4[dyn] silencio 10 s: Σ bins en pantalla = %u (antes, el bin del borde se llevaba todos)\n", sum (h));
        REQUIRE (sum (h) == 0u);
    }

    SECTION ("señal")
    {
        // El módulo: con señal, el histograma de la lente es el de siempre, bin por bin.
        Loudness m;
        m.prepare (kSr);
        std::vector<float> L (4800), R (4800);
        double ph = 0.0;
        for (int hop = 0; hop < 100; ++hop)
        {
            for (int i = 0; i < 4800; ++i) { L[(size_t) i] = R[(size_t) i] = (float) (0.1 * std::sin (ph)); ph += 0.1309; }
            m.process (L.data(), R.data(), 4800);
        }
        const auto r = m.result();
        std::printf ("T4[dyn] señal 10 s: %lld short-term, %lld sobre la compuerta; los 61 bins iguales: %s\n",
                     r.shortTermHops, r.shortTermHopsGated, r.histogram == r.histogramGated ? "sí" : "NO");
        REQUIRE (r.shortTermHops == 71);
        REQUIRE (r.shortTermHopsGated == r.shortTermHops);
        REQUIRE (r.histogram == r.histogramGated);

        // Y el cableado: la lente lee ése.
        Live live (0);
        live.push (10.0, -20.0);
        const auto h = live.histogram();
        REQUIRE (sum (h) == 71u);
    }

    SECTION ("silencio después de señal")
    {
        Live live (0);
        live.push (10.0, -20.0);
        const auto h1 = live.histogram();
        live.push (10.0, -300.0);
        const auto h2 = live.histogram();
        live.push (5.0, -300.0);
        const auto h3 = live.histogram();

        // Lo que entra al cortar la señal son los short-term de la cola (la ventana de 3 s todavía lleva
        // señal): 29 hops, todos muy arriba de −70. El silencio de verdad no entra, y el bin de abajo queda vacío.
        std::printf ("T4[dyn] señal→silencio: Σ %u → %u → %u · bin del borde (≤ −60) %u → %u\n",
                     sum (h1), sum (h2), sum (h3), h1[0], h3[0]);
        REQUIRE (sum (h2) - sum (h1) <= 30u);
        REQUIRE (h3 == h2);
        REQUIRE (h3[0] == 0u);
    }
}

// ============================================================================================ T4[tonal]
TEST_CASE ("telescope: T4 · la curva viva de TONAL BALANCE no se hunde en silencio", "[telescope][silencio]")
{
    SECTION ("silencio")
    {
        TonalRun run;
        run.feed (10.0, 0.0f);
        const auto f = run.frame();
        std::printf ("T4[tonal] silencio 10 s: liveSeconds=%.3f liveValid=%d · cuadros afuera=%lld\n",
                     f.liveSeconds, (int) f.liveValid, run.reference.gatedOutFrames());
        REQUIRE (f.liveSeconds == 0.0f);
        REQUIRE_FALSE (f.liveValid);
        REQUIRE (run.reference.gatedOutFrames() > 0);
    }

    SECTION ("señal")
    {
        TonalRun run;
        run.feed (10.0, 0.3f);
        ReferenceFrame f = run.frame();
        float before[ReferenceFrame::kNumBands];
        run.ungated.bandsDb (before);
        int same = 0;
        for (int bnd = 0; bnd < ReferenceFrame::kNumBands; ++bnd) same += (f.liveBands[bnd] == before[bnd]) ? 1 : 0;
        std::printf ("T4[tonal] señal 10 s: cuadros afuera=%lld · %d de 30 bandas iguales AL BIT al promedio sin compuerta\n",
                     run.reference.gatedOutFrames(), same);
        REQUIRE (run.reference.gatedOutFrames() == 0);
        REQUIRE (same == ReferenceFrame::kNumBands);
        REQUIRE (f.liveValid);
    }

    SECTION ("silencio después de señal")
    {
        TonalRun run;
        run.feed (10.0, 0.3f);
        const auto f1 = run.frame();
        run.feed (10.0, 0.0f);
        const auto f2 = run.frame();

        float oldNorm[ReferenceFrame::kNumBands];
        run.ungatedNorm (oldNorm);
        const float moved   = maxDiff (f2.liveNorm, f1.liveNorm, f1.liveBands);
        const float sunkOld = maxDiff (oldNorm, f1.liveNorm, f1.liveBands);

        // Lo único que entra después de la señal son los cuadros del borde (llevan la parte que sonó): unos
        // pocos centésimos de segundo sobre 10 s. El integrado no se mueve (la compuerta del medidor).
        std::printf ("T4[tonal] señal→silencio: la curva se movió %.3f dB (sin compuerta: %.2f dB) · liveSeconds %.3f → %.3f · I %+.2f → %+.2f\n",
                     moved, sunkOld, f1.liveSeconds, f2.liveSeconds, f1.liveIntegrated, f2.liveIntegrated);
        // El integrado sí se mueve un poco: los bloques de 400 ms de la cola llevan la parte que sonó y pasan la
        // compuerta del medidor, como tiene que ser. Lo que no entra es el silencio.
        REQUIRE (f2.liveValid);
        REQUIRE (std::abs (f2.liveIntegrated - f1.liveIntegrated) < 0.1f);
        REQUIRE (f2.liveSeconds - f1.liveSeconds < 0.2f);
        REQUIRE (moved < 0.1f);
        REQUIRE (sunkOld > 2.5f);   // el «antes», medido: 10 s de ceros sobre 10 s de señal hunden ~3 dB
    }

    SECTION ("el camino vivo entero, en el processor")
    {
        // Lo mismo por el camino de verdad (la FFT fija del AnalysisThread). La cola pesa un poco más que en el
        // módulo (el integrado también se mueve con ella), así que el tope es 0.25 dB: un orden de magnitud
        // debajo de los ~3 dB que se hundía antes con la misma cuenta.
        Live live (telescope::kReference);
        live.pushPink (10.0, 0.3f);
        const auto f1 = live.proc.reference().read();
        live.pushPink (10.0, 0.0f);
        const auto f2 = live.proc.reference().read();
        const float moved = maxDiff (f2.liveNorm, f1.liveNorm, f1.liveBands);
        std::printf ("T4[tonal] processor: la curva se movió %.3f dB con 10 s de silencio · liveSeconds %.3f → %.3f\n",
                     moved, f1.liveSeconds, f2.liveSeconds);
        REQUIRE (f1.liveValid);
        REQUIRE (f2.liveValid);
        REQUIRE (f2.liveSeconds - f1.liveSeconds < 0.2f);
        REQUIRE (moved < 0.25f);
    }
}

// ============================================================================================ T4[compuerta]
// La loudness del cuadro (K en frecuencia, Parseval con la ventana) contra el medidor de BS.1770, sobre ruido
// rosa estacionario a tres niveles. Y la compuerta, a los dos lados de −70.
TEST_CASE ("telescope: T4 · la loudness del cuadro coincide con el medidor", "[telescope][silencio]")
{
    struct Probe : Spectrum::FrameSink
    {
        Reference* ref = nullptr;
        double sumLin = 0.0;
        int    n = 0;
        void spectrumFrameComputed (const Spectrum::FrameInfo& info) override
        {
            const double l = ref->frameLoudnessLufs (info);
            sumLin += std::pow (10.0, l / 10.0);
            ++n;
            ref->spectrumFrameComputed (info);
        }
    };

    for (const float peak : { 0.3f, 0.003f, 0.001f, 0.0003f })
    {
        TonalRun run;
        Probe probe;
        probe.ref = &run.reference;
        run.spectrum.setFrameSink (&probe);
        run.feed (8.0, peak);

        const double frameLufs = 10.0 * std::log10 (probe.sumLin / std::max (1, probe.n));
        const auto   meter     = run.loudness.result();
        std::printf ("T4[compuerta] rosa pico %.4f: medidor I=%+.2f LUFS · cuadros (media de potencia) %+.2f LUFS · "
                     "%lld de %d cuadros afuera\n",
                     peak, meter.integrated, frameLufs, run.reference.gatedOutFrames(), probe.n);
        if (meter.integratedValid)
            REQUIRE (std::abs (frameLufs - meter.integrated) < 0.5);

        // A los dos lados de la compuerta, con 5 dB de margen: todo adentro o todo afuera.
        if (frameLufs > -65.0) REQUIRE (run.reference.gatedOutFrames() == 0);
        if (frameLufs < -75.0) REQUIRE (run.reference.gatedOutFrames() == probe.n);
    }
}

// ============================================================================================ T4[verdict]
TEST_CASE ("telescope: T4 · VERDICT dice «esperando audio» si no hubo audio", "[telescope][silencio]")
{
    SECTION ("la regla, sobre filas")
    {
        std::vector<telescope::SecondRow> rows (5);
        for (auto& r : rows) { r.measuredModules = telescope::kLoudness; r.momentaryMax = telescope::kSilenceDb; }
        REQUIRE_FALSE (telescope::VerdictLens::heardAudio (rows.data(), (int) rows.size()));
        rows[3].momentaryMax = -70.0f;                       // justo en la compuerta: no pasa (como el integrado)
        REQUIRE_FALSE (telescope::VerdictLens::heardAudio (rows.data(), (int) rows.size()));
        rows[3].momentaryMax = -69.9f;
        REQUIRE (telescope::VerdictLens::heardAudio (rows.data(), (int) rows.size()));
        rows[3].measuredModules = 0;                         // una fila sin loudness no cuenta
        REQUIRE_FALSE (telescope::VerdictLens::heardAudio (rows.data(), (int) rows.size()));
    }

    const auto live = [] (const char* lang, const std::function<void (Live&, telescope::VerdictLens&)>& script)
    {
        Live l (0);
        telescope::VerdictLens lens (l.proc);
        l.proc.setEnabledModules (lens.requiredModules() | telescope::kAlwaysOnModules);
        l.proc.setVerdictLanguage (lang);
        lens.setSize (900, 600);
        script (l, lens);
    };
    const auto state = [] (telescope::VerdictLens& lens) { lens.pumpFrames (2); return lens.stateText(); };

    SECTION ("silencio, señal y silencio después de señal, en castellano y en inglés")
    {
        live ("es", [&] (Live& l, telescope::VerdictLens& lens)
        {
            l.push (4.0, -300.0);
            const auto s1 = state (lens);
            l.push (3.0, -20.0);
            const auto s2 = state (lens);
            l.push (4.0, -300.0);
            const auto s3 = state (lens);
            std::printf ("T4[verdict] es · silencio: «%s»\n                señal: «%s»\n                silencio después: «%s»\n",
                         s1.toRawUTF8(), s2.toRawUTF8(), s3.toRawUTF8());
            REQUIRE (s1.contains (juce::String::fromUTF8 ("esperando audio")));
            REQUIRE_FALSE (s1.contains (" s"));
            REQUIRE_FALSE (s2.contains ("esperando"));
            REQUIRE (s2.endsWith (" s"));
            REQUIRE_FALSE (s3.contains ("esperando"));   // la ventana tuvo audio: sigue contando
            REQUIRE (s3.endsWith (" s"));
        });
        live ("en", [&] (Live& l, telescope::VerdictLens& lens)
        {
            l.push (4.0, -300.0);
            const auto s1 = state (lens);
            std::printf ("T4[verdict] en · silencio: «%s»\n", s1.toRawUTF8());
            REQUIRE (s1.contains ("waiting for audio"));
        });
    }

    SECTION ("los seis idiomas tienen la frase")
    {
        for (const char* lang : { "en", "es", "pt", "fr", "de", "it" })
        {
            const auto t = telescope::Verdict::translate ("ui.waiting", lang);
            std::printf ("T4[verdict] %s: «%s»\n", lang, t.c_str());
            REQUIRE (t != "ui.waiting");
            if (std::string (lang) != "en") REQUIRE (t != telescope::Verdict::translate ("ui.waiting", "en"));
        }
    }
}

// ============================================================================================ F5[verdict]
namespace
{
// Lo que VERDICT tiene para decir, leído del informe y de la lente (lo mismo que se pinta).
struct VerdictLook
{
    bool heard = false;
    int  secondsTotal = 0, seconds = 0, findings = 0, strengths = 0, lines = 0;
    std::string headline;
    juce::String sub, state;
};

VerdictLook lookAt (telescope::VerdictLens& lens)
{
    lens.pumpFrames (2);
    const auto& rep = lens.report();
    VerdictLook v;
    v.heard        = rep.summary.heard;
    v.secondsTotal = rep.summary.secondsTotal;
    v.seconds      = rep.summary.seconds;
    v.findings     = (int) rep.findings.size();
    v.strengths    = (int) rep.strengths.size();
    v.lines        = lens.numLines();
    v.headline     = rep.summary.headline;
    v.sub          = lens.headSubText();
    v.state        = lens.stateText();
    return v;
}

void printLook (const char* what, const VerdictLook& v)
{
    std::printf ("F5[verdict] %-26s heard=%d filas=%d s=%d hallazgos=%d dentro=%d lineas=%d\n"
                 "            titular «%s» · sub «%s» · estado «%s»\n",
                 what, (int) v.heard, v.secondsTotal, v.seconds, v.findings, v.strengths, v.lines,
                 v.headline.c_str(), v.sub.toRawUTF8(), v.state.toRawUTF8());
}

// Sin audio: nada de lo que el reparo encontró en la foto de la F4.
void requireSilentReport (const VerdictLook& v)
{
    REQUIRE_FALSE (v.heard);
    REQUIRE (v.secondsTotal > 0);      // hubo filas: es silencio, no «todavía no hay segundos»
    REQUIRE (v.findings == 0);         // ni avisos (el ⚠ de Celular) ni las cajas con tilde (sección 2)
    REQUIRE (v.strengths == 0);        // ni «dentro de rango»
    REQUIRE (v.headline.empty());      // ni «3 chequeos dentro de rango · 1 para revisar»
    REQUIRE (v.seconds == 0);          // el silencio no cuenta como analizado
    REQUIRE (v.lines == 0);            // la lista, vacía: tampoco los títulos con su «nada»
    REQUIRE (v.sub.isEmpty());         // ni «N s analizados»
}

// Con audio en la ventana: el informe de siempre.
void requireReport (const VerdictLook& v)
{
    REQUIRE (v.heard);
    REQUIRE (v.findings >= telescope::devices::kNumDevices);   // las cajas de la sección 2, por lo menos
    REQUIRE_FALSE (v.headline.empty());
    REQUIRE (v.seconds > 0);
    REQUIRE (v.lines > 0);
    REQUIRE (v.sub.isNotEmpty());
}

// Un WAV estéreo de `seconds`, con rosa a `peak` hasta `signalUntil` y ceros después.
juce::File writeSilenceWav (const juce::String& name, double seconds, double signalUntil, float peak)
{
    auto pinkA = std::make_shared<telescope::test::Pink> (telescope::test::kPinkSeedA);
    auto pinkB = std::make_shared<telescope::test::Pink> (telescope::test::kPinkSeedB);
    return telescope::test::writeWav (name, kSr, 2, (juce::int64) std::llround (seconds * kSr),
                                      [=] (juce::int64 i)
                                      {
                                          const bool on = (double) i / kSr < signalUntil;
                                          const float l = pinkA->next(), r = pinkB->next();
                                          return std::pair<float, float> { on ? peak * l : 0.0f, on ? peak * r : 0.0f };
                                      });
}

telescope::VerdictReport evaluateFile (const juce::File& wav, const char* lang)
{
    telescope::FileAnalyzer fa;
    fa.start (wav);
    REQUIRE (telescope::test::waitUntil ([&] { return ! fa.busy(); }, 120000));
    const auto a = fa.result();
    REQUIRE (a.valid);
    telescope::Verdict::Inputs in;
    in.aggregates = telescope::Verdict::aggregatesFrom (a);
    in.rows       = a.secondRows.data();
    in.n          = (int) a.secondRows.size();
    in.language   = lang;
    return telescope::Verdict::evaluate (in);
}
}

TEST_CASE ("telescope: F5 · VERDICT en silencio muestra sólo «esperando audio»", "[telescope][silencio][f5]")
{
    SECTION ("en vivo: silencio, señal y silencio después de señal")
    {
        Live l (0);
        telescope::VerdictLens lens (l.proc);
        l.proc.setEnabledModules (lens.requiredModules() | telescope::kAlwaysOnModules);
        l.proc.setVerdictLanguage ("es");
        lens.setSize (900, 600);

        l.push (10.0, -300.0);                      // los 10 s de ceros de la foto del reparo
        const auto s1 = lookAt (lens);
        printLook ("silencio (10 s)", s1);
        requireSilentReport (s1);
        REQUIRE (s1.state.contains (juce::String::fromUTF8 ("esperando audio")));

        l.pushPink (4.0, 0.25f);
        const auto s2 = lookAt (lens);
        printLook ("señal (4 s)", s2);
        requireReport (s2);
        REQUIRE_FALSE (s2.state.contains ("esperando"));
        REQUIRE (s2.state.endsWith (" s"));
        REQUIRE (s2.sub.contains ("analizados"));

        l.push (4.0, -300.0);                       // la ventana tuvo audio: sigue como hoy
        const auto s3 = lookAt (lens);
        printLook ("silencio después (4 s)", s3);
        requireReport (s3);
        REQUIRE_FALSE (s3.state.contains ("esperando"));
        REQUIRE (s3.state.endsWith (" s"));
        REQUIRE (s3.seconds >= s2.seconds + 3);     // el silencio de después cuenta, como antes del arreglo
    }

    SECTION ("en vivo, en inglés")
    {
        Live l (0);
        telescope::VerdictLens lens (l.proc);
        l.proc.setEnabledModules (lens.requiredModules() | telescope::kAlwaysOnModules);
        l.proc.setVerdictLanguage ("en");
        lens.setSize (900, 600);
        l.push (10.0, -300.0);
        const auto s1 = lookAt (lens);
        printLook ("en · silencio (10 s)", s1);
        requireSilentReport (s1);
        REQUIRE (s1.state.contains ("waiting for audio"));
    }

    SECTION ("por archivo: silencio, señal, y señal con silencio después")
    {
        const auto silent = writeSilenceWav ("f5_verdict_silencio_10s.wav", 10.0, 0.0, 0.25f);
        const auto signal = writeSilenceWav ("f5_verdict_senal_10s.wav", 10.0, 10.0, 0.25f);
        const auto after  = writeSilenceWav ("f5_verdict_senal_4s_silencio_6s.wav", 10.0, 4.0, 0.25f);

        const auto r1 = evaluateFile (silent, "es");
        std::printf ("F5[verdict] archivo silencio: filas=%d heard=%d hallazgos=%d dentro=%d titular «%s»\n",
                     r1.summary.secondsTotal, (int) r1.summary.heard, (int) r1.findings.size(),
                     (int) r1.strengths.size(), r1.summary.headline.c_str());
        REQUIRE_FALSE (r1.summary.heard);
        REQUIRE (r1.summary.secondsTotal == 10);
        REQUIRE (r1.findings.empty());
        REQUIRE (r1.strengths.empty());
        REQUIRE (r1.summary.headline.empty());
        REQUIRE (r1.summary.seconds == 0);
        REQUIRE (r1.summary.text.empty());
        REQUIRE_FALSE (r1.footer.empty());          // el pie queda: «medición, no gusto…»

        for (const auto* w : { &signal, &after })
        {
            const auto r = evaluateFile (*w, "es");
            std::printf ("F5[verdict] archivo %s: filas=%d s=%d heard=%d hallazgos=%d titular «%s»\n",
                         w->getFileName().toRawUTF8(), r.summary.secondsTotal, r.summary.seconds,
                         (int) r.summary.heard, (int) r.findings.size(), r.summary.headline.c_str());
            REQUIRE (r.summary.heard);
            REQUIRE (r.findings.size() >= (size_t) telescope::devices::kNumDevices);
            REQUIRE_FALSE (r.summary.headline.empty());
            REQUIRE (r.summary.seconds == 10);      // el silencio de después también se cuenta, como hoy
        }

        // La lente en modo ARCHIVO dice por qué la lista quedó vacía.
        telescope::TelescopeProcessor proc;
        proc.prepareToPlay (kSr, 512);
        proc.setVerdictLanguage ("es");
        proc.loadVerdictFile (silent);
        REQUIRE (telescope::test::waitUntil ([&] { return ! proc.verdictFileBusy(); }, 120000));
        REQUIRE (proc.verdictMode() == telescope::TelescopeProcessor::verdictFile);
        telescope::VerdictLens lens (proc);
        lens.setSize (900, 600);
        const auto v = lookAt (lens);
        printLook ("archivo en la lente", v);
        requireSilentReport (v);
        REQUIRE (v.state.contains (juce::String::fromUTF8 ("sin audio sobre -70 LUFS")));
        proc.releaseResources();

        silent.deleteFile();
        signal.deleteFile();
        after.deleteFile();
    }

    SECTION ("la frase del archivo sin audio, en los seis idiomas")
    {
        for (const char* lang : { "en", "es", "pt", "fr", "de", "it" })
        {
            const auto t = telescope::Verdict::translate ("ui.silent", lang);
            std::printf ("F5[verdict] ui.silent %s: «%s»\n", lang, t.c_str());
            REQUIRE (t != "ui.silent");
            REQUIRE (t.find ("-70 LUFS") != std::string::npos);
            if (std::string (lang) != "en") REQUIRE (t != telescope::Verdict::translate ("ui.silent", "en"));
        }
    }
}
