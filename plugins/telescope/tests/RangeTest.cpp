// [telescope][range] — el tramo (prompt 101, F3 de TELESCOPE 0.2, hito 4; contrato D-100 §3).
//
// UNA función de la biblioteca mide [from_s, to_s) en segundos del archivo: RangeAnalyzer::measure
// (analysis/RangeAnalysis.h). La usa telescope-measure, la usa FileAnalyzer para el archivo entero, y la va a
// usar la tira de T6 en la F4.
//
//   RANGE[corte]     medir [a, b) de un archivo da los mismos números, al bit, que medir un archivo cortado en
//                    esas muestras. Control: el corte corrido UNA muestra cambia el resultado.
//   RANGE[reuso]     un motor que ya midió otra cosa (otra SR, otro tramo) da los mismos bits que uno nuevo.
//                    Control: audios distintos, huellas distintas.
//   RANGE[recorte]   lo que se pasa del archivo se recorta y avisa, y mide lo mismo que el archivo entero.
//                    Control: un tramo adentro del archivo no avisa.
//   RANGE[vacio]     un tramo vacío es una negativa, no una medición de cero segundos. Control: una sola
//                    muestra ya es un tramo, y se mide.
//   RANGE[muestras]  segundos → muestras con llround (medios lejos del cero), y `seconds_measured` es lo que
//                    entró de verdad. Control: un poco menos del medio redondea para abajo.
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <vector>
#include "MeasureCore.h"
#include "TestHelpers.h"
#include "TestSignals.h"
#include "TestWav.h"
#include "analysis/RangeAnalysis.h"

using telescope::RangeAnalyzer;
using telescope::RangeMeasurement;
using telescope::test::Pink;

namespace
{
const telescope::measure::EngineInfo kEngine { "0.1.0", "0123456789ab" };

// La señal entera en memoria: así el archivo y su corte salen de LAS MISMAS muestras.
std::vector<std::pair<float, float>> pinkSamples (double sr, double seconds, float peak)
{
    Pink a { telescope::test::kPinkSeedA }, b { telescope::test::kPinkSeedB };
    std::vector<std::pair<float, float>> v ((size_t) std::llround (sr * seconds));
    // Con un nivel que cambia cada 2 s: que el LRA y las filas por segundo tengan algo que decir.
    for (size_t i = 0; i < v.size(); ++i)
    {
        const float g = ((i / (size_t) (2 * sr)) % 2 == 0) ? peak : 0.3f * peak;
        v[i] = { g * a.next(), g * b.next() };
    }
    return v;
}

juce::File writeSamples (const juce::String& name, double sr, const std::vector<std::pair<float, float>>& s,
                         juce::int64 from, juce::int64 to)
{
    return telescope::test::writeWav ("f3range-" + name + ".wav", sr, 2, to - from,
                                      [&] (juce::int64 i) { return s[(size_t) (from + i)]; });
}

// La huella de la MEDICIÓN: todo el FileAnalysis salvo lo que es del archivo (nombre, ruta y duración), más las
// tres sumas del estéreo de banda ancha. Dos mediciones del mismo audio tienen que dar la misma huella.
juce::String measurementPrint (const RangeMeasurement& m)
{
    auto a = m.analysis;
    a.name = {};
    a.path = {};
    a.seconds = 0.0;
    const auto hexd = [] (double v)
    {
        juce::uint64 bits;
        std::memcpy (&bits, &v, sizeof (bits));
        return juce::String::toHexString ((juce::int64) bits);
    };
    return a.fingerprint() + " LL=" + hexd (m.sumLL) + " RR=" + hexd (m.sumRR) + " LR=" + hexd (m.sumLR);
}

// El pedido de la herramienta.
std::string request (const juce::File& f, std::optional<double> from = {}, std::optional<double> to = {})
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("file", f.getFullPathName());
    if (from) o->setProperty ("from_s", *from);
    if (to)   o->setProperty ("to_s", *to);
    return juce::JSON::toString (juce::var (o), true).toStdString();
}

// La parte de la línea que es MEDICIÓN (de seconds_measured en adelante): sin el archivo y sin el tramo.
std::string measuredPart (const std::string& line)
{
    const auto p = line.find ("\"seconds_measured\"");
    return p == std::string::npos ? std::string ("<sin medición>") : line.substr (p);
}

std::string rawValue (const std::string& line, const std::string& k)
{
    const auto p = line.find ("\"" + k + "\":");
    if (p == std::string::npos) return "<falta>";
    const auto s = p + k.size() + 3;
    return line.substr (s, line.find_first_of (",}", s) - s);
}
}

TEST_CASE ("telescope: RANGE[corte] medir [a,b) de un archivo es medir el archivo cortado ahi", "[telescope][range]")
{
    for (const double sr : { 48000.0, 44100.0 })
    {
        const auto s = pinkSamples (sr, 20.0, 0.4f);
        const double a = 3.217, b = 14.9031;                      // no caen en ningún borde de hop ni de segundo
        const auto sa = telescope::sampleAt (a, sr), sb = telescope::sampleAt (b, sr);
        INFO ("sr " << sr << " · tramo [" << sa << ", " << sb << ")");

        const auto whole = writeSamples ("entero", sr, s, 0, (juce::int64) s.size());
        const auto cut   = writeSamples ("corte", sr, s, sa, sb);

        RangeAnalyzer ra;
        const auto mRange = ra.measure (whole, a, b);
        const auto mCut   = ra.measure (cut);
        REQUIRE (mRange.status == RangeMeasurement::Status::measured);
        REQUIRE (mCut.status == RangeMeasurement::Status::measured);
        CHECK (mRange.range.start == sa);
        CHECK (mRange.range.end == sb);
        CHECK_FALSE (mRange.clipped);
        CHECK (mRange.analysis.secondRows.size() == 11);           // 11.69 s → 11 filas enteras

        // AL BIT: el FileAnalysis entero (bandas, series, filas por segundo, agregados) y las sumas del estéreo.
        CHECK (measurementPrint (mRange) == measurementPrint (mCut));

        // Y la herramienta: la parte medida de la línea, byte por byte.
        telescope::measure::Session session (kEngine);
        const auto lineRange = session.handle (request (whole, a, b));
        const auto lineCut   = session.handle (request (cut));
        CHECK (measuredPart (lineRange) == measuredPart (lineCut));
        CHECK (rawValue (lineRange, "from_sample") == std::to_string (sa));
        CHECK (rawValue (lineRange, "to_sample") == std::to_string (sb));

        // CONTROL: el corte corrido UNA muestra (al principio) ya no es el mismo audio, y la huella lo ve.
        const auto shifted = writeSamples ("corte-1", sr, s, sa + 1, sb + 1);
        const auto mShift = ra.measure (shifted);
        CHECK (measurementPrint (mShift) != measurementPrint (mRange));
        // En la línea redondeada (0.1 dB) una muestra de ruido no mueve nada: por eso la prueba de verdad es la
        // huella de arriba, al bit. El control de la línea es un tramo corrido un segundo.
        const bool lineSees1 = measuredPart (session.handle (request (shifted))) != measuredPart (lineRange);
        CHECK (measuredPart (session.handle (request (whole, a + 1.0, b + 1.0))) != measuredPart (lineRange));

        // Y el final corrido una muestra: la cola que agrega no llega a un hop ni a un cuadro nuevo, así que el
        // motor puede no cambiar; las sumas del estéreo, sí. Se imprime qué vio cada una.
        const auto longer = writeSamples ("corte+1", sr, s, sa, sb + 1);
        const auto mLonger = ra.measure (longer);
        const bool engineSame = [&] { auto x = mLonger.analysis, y = mRange.analysis;
                                      x.name = y.name = {}; x.path = y.path = {}; x.seconds = y.seconds = 0.0;
                                      return x.fingerprint() == y.fingerprint(); }();
        std::printf ("RANGE[corte] sr %.0f · [%lld, %lld) = archivo cortado, al bit · corrido 1 muestra al "
                     "principio: huella distinta, línea JSON %s · 1 muestra más al final: motor %s, sumas %s\n",
                     sr, (long long) sa, (long long) sb, lineSees1 ? "distinta" : "igual (redondeo)",
                     engineSame ? "igual" : "distinto", mLonger.sumLL != mRange.sumLL ? "distintas" : "iguales");
        CHECK (mLonger.sumLL != mRange.sumLL);
        CHECK (measurementPrint (mLonger) != measurementPrint (mRange));

        for (const auto& f : { whole, cut, shifted, longer }) f.deleteFile();
    }
}

TEST_CASE ("telescope: RANGE[reuso] un motor que ya midio otra cosa da los mismos bits que uno nuevo", "[telescope][range]")
{
    // RangeAnalyzer re-prepara el CQT sólo si cambia la SR (sus kernels cuestan ~155 ms). Lo que el CQT mide —la
    // tonalidad de cada fila y del archivo— no sale en el JSON, así que esto se prueba acá, al bit, sobre la
    // huella entera: cada medición de un motor reusado == la de un motor recién creado.
    struct Item { juce::File f; std::optional<double> from, to; };
    const auto s44 = pinkSamples (44100.0, 6.0, 0.4f);
    const auto s48 = pinkSamples (48000.0, 6.0, 0.25f);
    const std::vector<Item> items {
        { writeSamples ("reuso-44", 44100.0, s44, 0, (juce::int64) s44.size()), {}, {} },
        { writeSamples ("reuso-48", 48000.0, s48, 0, (juce::int64) s48.size()), {}, {} },
    };
    const std::vector<Item> plan {
        items[0], items[0], { items[0].f, 1.0, 4.5 }, items[1], items[1], { items[1].f, 0.5, 5.0 }, items[0], items[1] };

    int same = 0;
    RangeAnalyzer reused;
    std::vector<juce::String> prints;
    for (const auto& it : plan)
    {
        RangeAnalyzer fresh;
        const auto a = measurementPrint (reused.measure (it.f, it.from, it.to));
        const auto b = measurementPrint (fresh.measure (it.f, it.from, it.to));
        if (a == b) ++same;
        prints.push_back (a);
    }
    CHECK (same == (int) plan.size());

    // CONTROL: las huellas de audios distintos son distintas (la igualdad de arriba no es trivial).
    CHECK (prints[0] != prints[3]);
    CHECK (prints[0] != prints[2]);
    for (const auto& it : items) it.f.deleteFile();
}

TEST_CASE ("telescope: RANGE[recorte] lo que se pasa del archivo se recorta y se avisa", "[telescope][range]")
{
    constexpr double sr = 48000.0;
    const auto s = pinkSamples (sr, 12.0, 0.4f);
    const auto f = writeSamples ("recorte", sr, s, 0, (juce::int64) s.size());

    RangeAnalyzer ra;
    const auto whole = ra.measure (f);
    const auto over  = ra.measure (f, -2.5, 99.0);
    REQUIRE (over.status == RangeMeasurement::Status::measured);
    CHECK (over.clipped);
    CHECK (over.range.start == 0);
    CHECK (over.range.end == (juce::int64) s.size());
    CHECK (*over.requestedFromS == -2.5);
    CHECK (*over.requestedToS == 99.0);
    CHECK (measurementPrint (over) == measurementPrint (whole));   // recortado = el archivo entero

    telescope::measure::Session session (kEngine);
    const auto line = session.handle (request (f, -2.5, 99.0));
    CHECK (line.find ("\"warnings\":[\"RANGE_CLIPPED\"]") != std::string::npos);
    CHECK (rawValue (line, "requested_from_s") == "-2.50");
    CHECK (rawValue (line, "requested_to_s") == "99.00");
    CHECK (rawValue (line, "from_s") == "0.00");
    CHECK (rawValue (line, "to_s") == "12.00");
    CHECK (rawValue (line, "seconds_measured") == "12.00");

    // Sólo el final pasado, también avisa.
    CHECK (session.handle (request (f, 2.0, 30.0)).find ("RANGE_CLIPPED") != std::string::npos);

    // CONTROL: un tramo adentro del archivo no avisa (el aviso sale del recorte, no de que haya tramo).
    const auto inside = session.handle (request (f, 1.0, 11.5));
    INFO (inside);
    CHECK (inside.find ("RANGE_CLIPPED") == std::string::npos);
    CHECK_FALSE (ra.measure (f, 1.0, 11.5).clipped);
    f.deleteFile();
}

TEST_CASE ("telescope: RANGE[vacio] un tramo vacio es una negativa", "[telescope][range]")
{
    constexpr double sr = 48000.0;
    const auto s = pinkSamples (sr, 3.0, 0.4f);
    const auto f = writeSamples ("vacio", sr, s, 0, (juce::int64) s.size());

    RangeAnalyzer ra;
    CHECK (ra.measure (f, 1.0, 1.0).status == RangeMeasurement::Status::rangeEmpty);   // to = from
    CHECK (ra.measure (f, 2.0, 1.0).status == RangeMeasurement::Status::rangeEmpty);   // al revés
    CHECK (ra.measure (f, 5.0, 9.0).status == RangeMeasurement::Status::rangeEmpty);   // entero después del final
    CHECK (ra.measure (f, -9.0, -5.0).status == RangeMeasurement::Status::rangeEmpty); // entero antes del principio

    telescope::measure::Session session (kEngine);
    for (const auto& r : { request (f, 1.0, 1.0), request (f, 2.0, 1.0), request (f, 5.0, 9.0), request (f, -9.0, -5.0) })
    {
        const auto line = session.handle (r);
        INFO (line);
        CHECK (rawValue (line, "status") == "\"refused\"");
        CHECK (rawValue (line, "refusal") == "\"RANGE_EMPTY\"");
        CHECK (rawValue (line, "seconds_measured") == "null");
        CHECK (rawValue (line, "integrated_lufs") == "null");
    }

    // CONTROL: UNA muestra ya es un tramo — se mide, y dice que no le alcanzó para casi nada.
    const auto one = ra.measure (f, 1.0, 1.0 + 1.0 / sr);
    CHECK (one.status == RangeMeasurement::Status::measured);
    CHECK (one.range.length() == 1);
    const auto line = session.handle (request (f, 1.0, 1.0 + 1.0 / sr));
    INFO (line);
    CHECK (rawValue (line, "status") == "\"measured\"");
    CHECK (rawValue (line, "to_sample") == "48001");
    CHECK (line.find ("TRUE_PEAK_TOO_SHORT") != std::string::npos);
    f.deleteFile();
}

TEST_CASE ("telescope: RANGE[muestras] de segundos a muestras con llround, y lo medido de verdad", "[telescope][range]")
{
    // 32 768 Hz: 1.5 / 32 768 s es una fracción binaria EXACTA, así que segundos × sr da 1.5 justo y se ve para
    // qué lado redondea el medio.
    constexpr double sr = 32768.0;
    const auto s = pinkSamples (sr, 2.0, 0.4f);
    const auto f = writeSamples ("muestras", sr, s, 0, (juce::int64) s.size());

    CHECK (telescope::sampleAt (1.5 / sr, sr) == 2);     // el medio, lejos del cero
    CHECK (telescope::sampleAt (2.5 / sr, sr) == 3);     // no al par (no es redondeo bancario)
    CHECK (telescope::sampleAt (-1.5 / sr, sr) == -2);

    RangeAnalyzer ra;
    const auto m = ra.measure (f, 1.5 / sr, 1.0 + 2.5 / sr);
    REQUIRE (m.status == RangeMeasurement::Status::measured);
    CHECK (m.range.start == 2);
    CHECK (m.range.end == 32768 + 3);
    CHECK (m.secondsMeasured() == (double) (32768 + 1) / sr);

    telescope::measure::Session session (kEngine);
    const auto line = session.handle (request (f, 0.25, 1.75));
    INFO (line);
    CHECK (rawValue (line, "from_sample") == "8192");
    CHECK (rawValue (line, "to_sample") == "57344");
    CHECK (rawValue (line, "seconds_measured") == "1.50");
    CHECK (rawValue (line, "duration_s") == "2.00");

    // CONTROL: un poco menos del medio redondea para abajo (el test de arriba no es un «siempre para arriba»).
    CHECK (telescope::sampleAt (1.49 / sr, sr) == 1);
    CHECK (ra.measure (f, 1.49 / sr, 1.0).range.start == 1);
    f.deleteFile();
}
