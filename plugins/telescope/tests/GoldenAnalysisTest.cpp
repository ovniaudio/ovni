// [telescope][golden] — LA LÍNEA DE BASE DEL MOTOR, AL BIT (prompt 101, F3 de TELESCOPE 0.2, hito 1).
//
// POR QUÉ EXISTE. La F3 saca el motor (`source/analysis/**`) a una biblioteca propia, `ovni_analysis`, para
// que la use también la herramienta `telescope-measure` (D-100). Mover la compilación de un motor de un
// target a otro puede cambiar un número sin que ningún test de tolerancia lo vea: otra bandera de compilación,
// otro orden de enlace, una FMA de más. La promesa es más fuerte que "parecido": LOS NÚMEROS DEL PLUGIN NO
// CAMBIAN, AL BIT. Este test la sostiene.
//
// QUÉ HACE. Genera un juego FIJO de señales deterministas (TestSignals.h, TestWav.h), las pasa por el
// FileAnalyzer —el mismo camino que usa TONAL BALANCE para cargar una referencia— y vuelca el resultado
// ENTERO a un texto canónico: integrado, LRA, true peak, máximos, las 30 bandas, las series de 10 Hz y de
// 1 Hz, los agregados de VERDICT y cada fila por segundo (con su correlación de banda ancha y por banda).
// Los float van en hexadecimal (el bit) y, al lado, en decimal (para el que lee el diff).
//
// El vuelco se compara BYTE A BYTE contra `tests/golden/<señal>.txt`. Esos archivos se generaron con el
// código de 74d65a2 (la F1, antes de tocar nada) y se commitean. Si un día difieren, la primera línea
// distinta sale en el mensaje y el vuelco nuevo queda al lado, en el temporal de los tests, para el diff.
//
// REGENERARLOS es una decisión, no un arreglo: `TELESCOPE_GOLDEN_WRITE=1 OvniTelescopeTests "[golden]"`
// los reescribe. Sólo se hace cuando un cambio del motor es A PROPÓSITO, y el commit lo dice.
//
// EL CONTROL + VIVE ADENTRO. GOLDEN[control] genera la misma señal con UNA muestra cambiada y exige que el
// vuelco difiera del dorado. Un test dorado que no se pone rojo cuando cambia el audio no mide nada.
//
// Sólo corre en la Mac: en Windows la aritmética de punto flotante (libm, FMA) no es la misma y el dorado
// es de esta plataforma. La comparación Mac contra Windows es de telescope-measure (hito 5), no de acá.
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <vector>
#include "TestHelpers.h"
#include "TestSignals.h"
#include "TestWav.h"
#include "analysis/FileAnalyzer.h"

using telescope::FileAnalysis;
using telescope::FileAnalyzer;
using telescope::test::Pink;

namespace
{
using Gen = std::function<std::pair<float, float> (juce::int64)>;

struct GoldenSignal
{
    const char* name;       // también el nombre del dorado y del WAV
    double      sr;
    int         channels;   // 1 = el archivo es mono (TestWav escribe sólo L)
    double      seconds;
    int         bits;       // 24 = entero; 32 = float (lo único que deja pasar de 0 dBFS)
    std::function<Gen()> make;   // una fábrica: cada corrida arranca el generador desde cero
};

constexpr double kTwoPi = 6.283185307179586476925286766559;

Gen sine (double hz, double sr, float peak, float phase = 0.0f)
{
    return [=] (juce::int64 i)
    {
        const auto v = (float) ((double) peak * std::sin (kTwoPi * hz * (double) i / sr + (double) phase));
        return std::pair<float, float> { v, v };
    };
}

Gen pinkStereo (float peak)
{
    auto a = std::make_shared<Pink> (telescope::test::kPinkSeedA);
    auto b = std::make_shared<Pink> (telescope::test::kPinkSeedB);
    return [=] (juce::int64) { return std::pair<float, float> { peak * a->next(), peak * b->next() }; };
}

Gen pinkMono (float peak)
{
    auto a = std::make_shared<Pink> (telescope::test::kPinkSeedA);
    return [=] (juce::int64) { const float v = peak * a->next(); return std::pair<float, float> { v, v }; };
}

Gen pinkAntiphase (float peak)
{
    auto a = std::make_shared<Pink> (telescope::test::kPinkSeedA);
    return [=] (juce::int64) { const float v = peak * a->next(); return std::pair<float, float> { v, -v }; };
}

Gen mixed (double sr)
{
    auto m = std::make_shared<telescope::test::MonoLowPhaseHigh> (sr);
    return [=] (juce::int64) { const auto v = m->next(); return std::pair<float, float> { 0.5f * v.first, 0.5f * v.second }; };
}

// EL JUEGO FIJO. Cubre lo que pide el hito 1: un tono, ruido con semilla fija, silencio, mono, estéreo, algo
// muy corto, algo pasado de 0 dBFS, a 44.1, 48 y 96 kHz. Agregar una señal es agregar un dorado; cambiar una
// que ya está es regenerar el suyo, y eso se dice en el commit.
const std::vector<GoldenSignal>& signals()
{
    static const std::vector<GoldenSignal> s {
        // un tono: 1 kHz a −18 dBFS de pico, L = R
        { "tone1k-48k-stereo",  48000.0, 2, 12.0, 24, [] { return sine (1000.0, 48000.0, 0.125893f); } },
        { "tone1k-44k-mono",    44100.0, 1, 10.0, 24, [] { return sine (1000.0, 44100.0, 0.125893f); } },
        { "tone100-96k-stereo", 96000.0, 2,  6.0, 24, [] { return sine (100.0,  96000.0, 0.25f); } },
        // ruido con semilla fija: rosa, dos semillas (estéreo independiente), una (mono) y fuera de fase
        { "pink-44k-stereo",    44100.0, 2, 15.0, 24, [] { return pinkStereo (0.5f); } },
        { "pink-48k-stereo",    48000.0, 2, 11.5, 24, [] { return pinkStereo (0.25f); } },
        { "pink-96k-mono",      96000.0, 1,  8.0, 24, [] { return pinkMono (0.5f); } },
        { "pink-48k-antiphase", 48000.0, 2, 10.0, 24, [] { return pinkAntiphase (0.3f); } },
        // la señal mixta de la casa: graves mono y agudos fuera de fase
        { "mixed-96k-stereo",   96000.0, 2,  7.0, 24, [] { return mixed (96000.0); } },
        // silencio
        { "silence-48k-stereo", 48000.0, 2,  5.0, 24, [] { return Gen ([] (juce::int64) { return std::pair<float, float> { 0.0f, 0.0f }; }); } },
        // muy corto: 250 ms (menos que una ventana momentánea) y 50 ms (menos que un hop de 100 ms)
        { "short250ms-48k",     48000.0, 2, 0.25, 24, [] { return pinkStereo (0.5f); } },
        { "short50ms-44k",      44100.0, 2, 0.05, 24, [] { return pinkStereo (0.5f); } },
        // pasado de 0 dBFS: en float a +6 dBFS de pico (el WAV entero no lo deja pasar)…
        { "over0-48k-float",    48000.0, 2,  6.0, 32, [] { return sine (997.0, 48000.0, 1.9952623f); } },
        // …y en entero, el pico entre muestras: fs/4 a 45° deja las muestras en ±1 y el true peak en +3 dBTP
        { "isp-44k-stereo",     44100.0, 2,  5.0, 24, [] { return sine (11025.0, 44100.0, 1.41421356f, 0.785398163f); } },
    };
    return s;
}

// ---- el vuelco canónico ----
juce::String hex (float v)
{
    juce::uint32 bits;
    std::memcpy (&bits, &v, sizeof (bits));
    char buf[16];
    std::snprintf (buf, sizeof (buf), "%08x", (unsigned) bits);
    return buf;
}

juce::String num (float v)
{
    char buf[48];
    std::snprintf (buf, sizeof (buf), "%s %.6f", hex (v).toRawUTF8(), (double) v);
    return buf;
}

juce::String dump (const GoldenSignal& sig, const FileAnalysis& a)
{
    juce::String s;
    const auto line = [&s] (const juce::String& k, const juce::String& v) { s << k << "=" << v << "\n"; };

    line ("signal", sig.name);
    line ("ok", juce::String ((int) a.ok));
    line ("valid", juce::String ((int) a.valid));
    line ("error", a.error);
    line ("warning", a.warning);
    line ("name", a.name);
    line ("sr", juce::String (a.sr, 3));
    line ("channels", juce::String (a.channels));
    line ("seconds", juce::String (a.seconds, 9));
    line ("integrated_lufs", num (a.integratedLufs));
    line ("lra_lu", num (a.lra));
    line ("true_peak_dbtp", num (a.truePeakDbtp));
    line ("momentary_max", num (a.momentaryMax));
    line ("short_term_max", num (a.shortTermMax));
    for (int b = 0; b < FileAnalysis::kNumBands; ++b)
        line ("band" + juce::String (b).paddedLeft ('0', 2) + "_" + juce::String (telescope::kThirdOctaveHz[b], 0) + "hz",
              num (a.bandsDb[b]));

    // La serie de 10 Hz, un segundo por línea; la de 1 Hz, un valor por línea.
    line ("short_term_history", juce::String ((int) a.shortTermHistory.size()));
    for (size_t i = 0; i < a.shortTermHistory.size(); i += 10)
    {
        juce::String row;
        for (size_t j = i; j < std::min (a.shortTermHistory.size(), i + 10); ++j)
            row << (j > i ? "," : "") << hex (a.shortTermHistory[j]);
        line ("st" + juce::String ((int) (i / 10)), row);
    }
    line ("true_peak_per_second", juce::String ((int) a.truePeakPerSecond.size()));
    for (size_t i = 0; i < a.truePeakPerSecond.size(); ++i)
        line ("tp" + juce::String ((int) i), num (a.truePeakPerSecond[i]));

    line ("clip_events", juce::String ((int) a.clipEvents));
    line ("dc_l", num (a.dcL));
    line ("dc_r", num (a.dcR));
    line ("key", juce::String (a.keyTonic) + "," + juce::String (a.keyMode) + "," + hex (a.keyConfidence)
                 + "," + hex (a.keyTimeFraction));

    // Cada fila por segundo: su correlación de banda ancha aparte (el campo que pide el hito, legible) y la
    // fila entera con la huella que ya usa el motor (FileAnalysis::secondFingerprint).
    line ("rows", juce::String ((int) a.secondRows.size()));
    for (size_t i = 0; i < a.secondRows.size(); ++i)
    {
        const auto& r = a.secondRows[i];
        line ("row" + juce::String ((int) i) + ".corr", num (r.corr));
        line ("row" + juce::String ((int) i), FileAnalysis::secondFingerprint (r, hex));
    }
    return s;
}

juce::File goldenDir()
{
    return juce::File (TELESCOPE_GOLDEN_DIR);
}

// El WAV de la señal, con `mutateAt` ≥ 0 cambiando UNA muestra (L y R) en +0.25: el control +.
juce::File writeSignal (const GoldenSignal& sig, juce::int64 mutateAt = -1)
{
    auto gen = sig.make();
    const auto frames = (juce::int64) std::llround (sig.seconds * sig.sr);
    // El mutado se escribe con EL MISMO nombre: el nombre entra al vuelco, y un control que se pone rojo por el
    // nombre del archivo no probaría nada sobre el audio.
    const juce::String file = juce::String ("f3golden-") + sig.name + ".wav";
    return telescope::test::writeWav (file, sig.sr, sig.channels, frames,
                                      [&] (juce::int64 i)
                                      {
                                          auto v = gen (i);
                                          if (i == mutateAt) { v.first += 0.25f; v.second += 0.25f; }
                                          return v;
                                      },
                                      sig.bits);
}

FileAnalysis analyseBlocking (const juce::File& f)
{
    FileAnalyzer fa;
    fa.start (f);
    REQUIRE (telescope::test::waitUntil ([&] { return ! fa.busy(); }, 60000));
    return fa.result();
}

// Primera línea distinta, para el mensaje. -1 si son iguales.
int firstDiff (const juce::String& a, const juce::String& b, juce::String& la, juce::String& lb)
{
    juce::StringArray x, y;
    x.addLines (a);
    y.addLines (b);
    for (int i = 0; i < juce::jmax (x.size(), y.size()); ++i)
        if (x[i] != y[i]) { la = x[i]; lb = y[i]; return i + 1; }
    return a == b ? -1 : juce::jmax (x.size(), y.size());
}
}

TEST_CASE ("telescope: GOLDEN el motor da los mismos numeros al bit que en 74d65a2", "[telescope][golden]")
{
    const bool write = juce::SystemStats::getEnvironmentVariable ("TELESCOPE_GOLDEN_WRITE", {}) == "1";
    int iguales = 0;

    for (const auto& sig : signals())
    {
        const auto wav = writeSignal (sig);
        const auto a = analyseBlocking (wav);
        const auto got = dump (sig, a);
        const auto golden = goldenDir().getChildFile (juce::String (sig.name) + ".txt");

        if (write)
        {
            goldenDir().createDirectory();
            REQUIRE (golden.replaceWithText (got, false, false, "\n"));
            WARN ("GOLDEN escrito: " << golden.getFullPathName());
            continue;
        }

        INFO ("señal " << sig.name << " · dorado " << golden.getFullPathName());
        REQUIRE (golden.existsAsFile());
        const auto want = golden.loadFileAsString();

        juce::String lw, lg;
        const int diff = firstDiff (want, got, lw, lg);
        if (diff >= 0)
        {
            const auto out = telescope::test::tempDir().getChildFile (juce::String ("f3golden-") + sig.name + ".actual.txt");
            out.replaceWithText (got, false, false, "\n");
            FAIL_CHECK ("GOLDEN " << sig.name << " difiere en la línea " << diff << "\n  dorado: " << lw
                                  << "\n  ahora:  " << lg << "\n  vuelco entero: " << out.getFullPathName());
        }
        else
        {
            ++iguales;
        }
        wav.deleteFile();
    }

    if (! write)
        CHECK (iguales == (int) signals().size());
}

TEST_CASE ("telescope: GOLDEN[control] una muestra cambiada cambia el vuelco", "[telescope][golden]")
{
    // El tono de 48 k, con la muestra del segundo 5 cambiada. Una sola muestra en 576 000 por canal.
    const auto& sig = signals().front();
    const auto golden = goldenDir().getChildFile (juce::String (sig.name) + ".txt");
    REQUIRE (golden.existsAsFile());

    const auto wav = writeSignal (sig, (juce::int64) (5.0 * sig.sr));
    const auto got = dump (sig, analyseBlocking (wav));
    wav.deleteFile();

    juce::String lw, lg;
    const int diff = firstDiff (golden.loadFileAsString(), got, lw, lg);
    INFO ("primera línea distinta: " << diff << " · dorado: " << lw << " · mutado: " << lg);
    CHECK (diff >= 0);
    CHECK_FALSE (lw.startsWith ("name="));   // la diferencia tiene que ser del audio
}
