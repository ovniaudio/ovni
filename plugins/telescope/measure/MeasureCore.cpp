#include "MeasureCore.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <algorithm>
#include <cmath>
#include <istream>
#include <optional>
#include <ostream>
#include "Sha256.h"
#include "analysis/RangeAnalysis.h"
#include "analysis/SpectrumFrame.h"

namespace telescope::measure
{
namespace
{
// ---- los códigos (SCHEMA.md los explica; el orden de warningCodes() es el orden en que salen) ----
namespace refusal
{
constexpr const char* notJson       = "REQUEST_NOT_JSON";
constexpr const char* unknownField  = "REQUEST_UNKNOWN_FIELD";
constexpr const char* badField      = "REQUEST_BAD_FIELD";
constexpr const char* noFile        = "REQUEST_NO_FILE";
constexpr const char* fileNotFound  = "FILE_NOT_FOUND";
constexpr const char* fileNotAudio  = "FILE_NOT_AUDIO";
constexpr const char* fileNoAudio   = "FILE_NO_AUDIO";
constexpr const char* fileReadError = "FILE_READ_ERROR";
constexpr const char* rangeEmpty    = "RANGE_EMPTY";
}

namespace warn
{
constexpr const char* rangeClipped       = "RANGE_CLIPPED";
constexpr const char* channelsFirstTwo   = "CHANNELS_FIRST_TWO";
constexpr const char* integratedTooShort = "INTEGRATED_TOO_SHORT";
constexpr const char* integratedBelowGate = "INTEGRATED_BELOW_GATE";
constexpr const char* lraTooShort        = "LRA_TOO_SHORT";
constexpr const char* truePeakTooShort   = "TRUE_PEAK_TOO_SHORT";
constexpr const char* truePeakNoSignal   = "TRUE_PEAK_NO_SIGNAL";
constexpr const char* monoSource         = "MONO_SOURCE";
constexpr const char* correlationNoSignal = "CORRELATION_NO_SIGNAL";
constexpr const char* bandBelowFloor     = "BAND_BELOW_FLOOR";
}

// ---- los umbrales, en hops de 100 ms del medidor (los mismos que usa el motor) ----
constexpr juce::int64 kHopsIntegrated = 4;     // el integrado necesita un bloque momentáneo de 400 ms
constexpr juce::int64 kHopsLra        = 100;   // el LRA, 10 s (ver SCHEMA.md: por qué 10)
constexpr juce::int64 kHopsTruePeak   = 1;     // el true peak se mide por hop: sin un hop entero no hay
constexpr float       kMeasurableDb   = -90.0f;   // el piso de lo medible de TONAL BALANCE (Reference.cpp)
constexpr double      kTinyEnergy     = 1.0e-20;  // el de Stereo.h: por debajo no hay señal que medir
constexpr size_t      kMaxIdChars     = 256;
constexpr double      kMaxAbsSeconds  = 1.0e9;    // un borde más allá de ~31 años no es un tramo: es un error

// ---- JSON de salida, a mano: el orden de las claves y el formato de cada número son parte del contrato ----
std::string quoted (const std::string& s)
{
    static const char* hexd = "0123456789abcdef";
    std::string o = "\"";
    for (const unsigned char c : s)
    {
        if (c == '"')       o += "\\\"";
        else if (c == '\\') o += "\\\\";
        else if (c < 0x20 || c == 0x7f)
        {
            o += "\\u00";
            o += hexd[c >> 4];
            o += hexd[c & 0xf];
        }
        else o += (char) c;
    }
    return o + "\"";
}

std::string orNull (const std::optional<std::string>& v) { return v ? *v : "null"; }

std::string integer (juce::int64 v) { return std::to_string ((long long) v); }

// La frecuencia de muestreo: entera si lo es (lo normal), con dos decimales si no.
std::string rateHz (double sr)
{
    return sr == std::floor (sr) && sr < 1.0e9 ? integer ((juce::int64) sr) : fixed (sr, 2);
}

struct Line
{
    std::optional<std::string> id;
    bool measured = false;
    std::optional<std::string> refusalCode;

    std::optional<std::string> name, sha256, sampleRate, channels, duration;
    std::optional<std::string> reqFrom, reqTo, from, to, fromSample, toSample;
    std::optional<std::string> seconds, integrated, lra, truePeak, correlation;
    std::vector<std::optional<std::string>> bands = std::vector<std::optional<std::string>> (SpectrumFrame::kNumThird);
    std::vector<std::string> warnings;

    std::string render (const EngineInfo& engine) const
    {
        std::string s;
        s.reserve (1200);
        s += "{\"schema\":" + std::to_string (kSchema);
        s += ",\"engine\":{\"name\":\"telescope-measure\",\"version\":" + quoted (engine.version)
           + ",\"sha\":" + quoted (engine.sha) + "}";
        s += ",\"id\":" + (id ? quoted (*id) : std::string ("null"));
        s += ",\"status\":" + quoted (measured ? "measured" : "refused");
        s += ",\"refusal\":" + (refusalCode ? quoted (*refusalCode) : std::string ("null"));
        s += ",\"file\":{\"name\":" + (name ? quoted (*name) : std::string ("null"))
           + ",\"sha256\":" + (sha256 ? quoted (*sha256) : std::string ("null"))
           + ",\"sample_rate_hz\":" + orNull (sampleRate)
           + ",\"channels\":" + orNull (channels)
           + ",\"duration_s\":" + orNull (duration) + "}";
        s += ",\"range\":{\"requested_from_s\":" + orNull (reqFrom)
           + ",\"requested_to_s\":" + orNull (reqTo)
           + ",\"from_s\":" + orNull (from)
           + ",\"to_s\":" + orNull (to)
           + ",\"from_sample\":" + orNull (fromSample)
           + ",\"to_sample\":" + orNull (toSample) + "}";
        s += ",\"seconds_measured\":" + orNull (seconds);
        s += ",\"integrated_lufs\":" + orNull (integrated);
        s += ",\"lra_lu\":" + orNull (lra);
        s += ",\"true_peak_dbtp\":" + orNull (truePeak);
        s += ",\"correlation\":" + orNull (correlation);
        s += ",\"bands_hz\":[";
        for (int b = 0; b < SpectrumFrame::kNumThird; ++b)
            s += (b > 0 ? "," : "") + fixedHz (kThirdOctaveHz[b]);
        s += "],\"bands_db_rel_integrated\":[";
        for (size_t b = 0; b < bands.size(); ++b)
            s += (b > 0 ? "," : "") + orNull (bands[b]);
        s += "],\"warnings\":[";
        for (size_t w = 0; w < warnings.size(); ++w)
            s += (w > 0 ? "," : "") + quoted (warnings[w]);
        s += "]}";
        return s;
    }

    // Los centros ISO: 31.5 con su decimal, el resto enteros.
    static std::string fixedHz (double hz)
    {
        return hz == std::floor (hz) ? integer ((juce::int64) hz) : fixed (hz, 1);
    }

    void warnOnce (const char* code)
    {
        if (std::find (warnings.begin(), warnings.end(), code) == warnings.end()) warnings.push_back (code);
    }

    // Los avisos salen en el orden de warningCodes(), no en el que se agregaron: el mismo pedido da los
    // mismos bytes aunque cambie el orden de las cuentas de arriba.
    void sortWarnings()
    {
        std::vector<std::string> sorted;
        for (const auto& c : warningCodes())
            if (std::find (warnings.begin(), warnings.end(), c) != warnings.end()) sorted.push_back (c);
        warnings = sorted;
    }
};

// El sha256 del archivo tal como está en el disco. `nullopt` si no se pudo leer entero.
std::optional<std::string> sha256Of (const juce::File& f)
{
    juce::FileInputStream in (f);
    if (! in.openedOk()) return std::nullopt;
    Sha256 h;
    juce::HeapBlock<char> buf (1 << 20);
    for (;;)
    {
        const int n = in.read (buf.getData(), 1 << 20);
        if (n < 0) return std::nullopt;
        if (n == 0) break;
        h.update (buf.getData(), (size_t) n);
    }
    if (! in.isExhausted()) return std::nullopt;
    return h.hex();
}

bool isNumber (const juce::var& v) { return v.isDouble() || v.isInt() || v.isInt64(); }
}

// ========================================================================================================
const std::vector<std::string>& refusalCodes()
{
    static const std::vector<std::string> c { refusal::notJson, refusal::unknownField, refusal::badField,
                                              refusal::noFile, refusal::fileNotFound, refusal::fileNotAudio,
                                              refusal::fileNoAudio, refusal::fileReadError, refusal::rangeEmpty };
    return c;
}

const std::vector<std::string>& warningCodes()
{
    static const std::vector<std::string> c { warn::rangeClipped, warn::channelsFirstTwo, warn::integratedTooShort,
                                              warn::integratedBelowGate, warn::lraTooShort, warn::truePeakTooShort,
                                              warn::truePeakNoSignal, warn::monoSource, warn::correlationNoSignal,
                                              warn::bandBelowFloor };
    return c;
}

std::string fixed (double value, int decimals)
{
    long long scale = 1;
    for (int i = 0; i < decimals; ++i) scale *= 10;
    const long long n = std::llround (value * (double) scale);
    const unsigned long long u = n < 0 ? (unsigned long long) (-(n + 1)) + 1u : (unsigned long long) n;
    std::string frac = std::to_string (u % (unsigned long long) scale);
    while ((int) frac.size() < decimals) frac.insert (frac.begin(), '0');
    // n == 0 no lleva signo: nunca "-0.0".
    return (n < 0 ? "-" : "") + std::to_string (u / (unsigned long long) scale) + (decimals > 0 ? "." + frac : "");
}

Session::Session (EngineInfo e) : engine (std::move (e)), analyzer (std::make_unique<RangeAnalyzer>()) {}
Session::~Session() = default;

// ========================================================================================================
// UN PEDIDO. Se lee entero antes de tocar el disco: un pedido mal escrito es una NEGATIVA con su código, nunca
// un exit distinto de 0 y nunca una medición de otra cosa (un "to" mal escrito no mide el archivo entero).
// ========================================================================================================
std::string Session::handle (const std::string& requestLine)
{
    Line out;

    juce::var req;
    const auto text = juce::String::fromUTF8 (requestLine.data(), (int) requestLine.size());
    // Ojo: en JUCE un arreglo también es `isObject()`, y su getDynamicObject() es nulo. Lo que cuenta como pedido
    // es un objeto de verdad (MEASURE[negativas]: "[1,2]" tiraba la herramienta con un SIGSEGV).
    const auto* obj = text.trim().isNotEmpty() && juce::JSON::parse (text, req).wasOk() && ! req.isArray()
                        ? req.getDynamicObject() : nullptr;
    if (obj == nullptr)
    {
        out.refusalCode = refusal::notJson;
        return out.render (engine);
    }

    // El id primero: si viene bien, vuelve en toda respuesta (también en las negativas) para que quien pide
    // pueda cruzarla con su pedido.
    std::optional<const char*> fail;
    if (obj->hasProperty ("id"))
    {
        const auto& id = obj->getProperty ("id");
        if (id.isString() && (size_t) id.toString().toStdString().size() <= kMaxIdChars)
            out.id = id.toString().toStdString();
        else if (! id.isVoid())
            fail = refusal::badField;
    }

    for (const auto& p : obj->getProperties())
    {
        const auto k = p.name.toString();
        if (k != "file" && k != "from_s" && k != "to_s" && k != "id")
        {
            if (! fail) fail = refusal::unknownField;
        }
    }

    juce::String path;
    if (! fail)
    {
        const auto& f = obj->getProperty ("file");
        if (f.isVoid() || (f.isString() && f.toString().isEmpty())) fail = refusal::noFile;
        else if (! f.isString())                                   fail = refusal::badField;
        else                                                       path = f.toString();
    }

    std::optional<double> from, to;
    for (const char* key : { "from_s", "to_s" })
    {
        if (fail) break;
        const auto& v = obj->getProperty (key);
        if (v.isVoid()) continue;   // ausente (o null) = sin ese borde
        if (! isNumber (v) || ! std::isfinite ((double) v) || std::abs ((double) v) > kMaxAbsSeconds)
        {
            fail = refusal::badField;
            break;
        }
        (juce::String (key) == "from_s" ? from : to) = (double) v;
    }

    if (from) out.reqFrom = fixed (*from, 2);
    if (to)   out.reqTo   = fixed (*to, 2);

    if (fail)
    {
        if (path.isNotEmpty()) out.name = juce::File::getCurrentWorkingDirectory().getChildFile (path).getFileName().toStdString();
        out.refusalCode = *fail;
        return out.render (engine);
    }

    // Una ruta relativa se resuelve contra el directorio de trabajo de la herramienta.
    const auto file = juce::File::getCurrentWorkingDirectory().getChildFile (path);
    out.name = file.getFileName().toStdString();

    if (file.existsAsFile())
    {
        out.sha256 = sha256Of (file);
        if (! out.sha256)
        {
            out.refusalCode = refusal::fileReadError;
            return out.render (engine);
        }
    }

    const auto m = analyzer->measure (file, from, to);
    const auto& a = m.analysis;

    using S = RangeMeasurement::Status;
    if (m.status == S::fileNotFound) { out.refusalCode = refusal::fileNotFound;  return out.render (engine); }
    if (m.status == S::notAudio)     { out.refusalCode = refusal::fileNotAudio;  return out.render (engine); }
    if (m.status == S::noAudio)      { out.refusalCode = refusal::fileNoAudio;   return out.render (engine); }

    // Desde acá el archivo se abrió: lo que se sabe de él va en la línea, se mida o no.
    out.sampleRate = rateHz (a.sr);
    out.channels   = integer (a.channels);
    out.duration   = fixed (a.seconds, 2);

    if (m.status == S::readError || m.status == S::cancelled)
    {
        out.refusalCode = refusal::fileReadError;
        return out.render (engine);
    }
    if (m.status == S::rangeEmpty)
    {
        out.refusalCode = refusal::rangeEmpty;
        return out.render (engine);
    }

    // ---- MEDIDO ----
    out.measured   = true;
    out.from       = fixed ((double) m.range.start / a.sr, 2);
    out.to         = fixed ((double) m.range.end / a.sr, 2);
    out.fromSample = integer (m.range.start);
    out.toSample   = integer (m.range.end);
    out.seconds    = fixed (m.secondsMeasured(), 2);

    if (m.clipped)         out.warnOnce (warn::rangeClipped);
    if (a.channels > 2)    out.warnOnce (warn::channelsFirstTwo);

    // Cuántos hops de 100 ms entraron enteros al medidor: el motor descarta el último si queda por la mitad.
    const juce::int64 hop  = juce::jmax ((juce::int64) 1, (juce::int64) std::llround (a.sr / 10.0));
    const juce::int64 hops = m.range.length() / hop;

    // Integrado: el mismo criterio de "válido" que la referencia de TONAL BALANCE (Reference.cpp).
    const bool integratedValid = a.integratedLufs > kSilenceDb;
    if (integratedValid) out.integrated = fixed (a.integratedLufs, 1);
    else out.warnOnce (hops < kHopsIntegrated ? warn::integratedTooShort : warn::integratedBelowGate);

    // LRA: necesita el integrado y 10 s de audio.
    if (hops < kHopsLra) out.warnOnce (warn::lraTooShort);
    if (integratedValid && hops >= kHopsLra) out.lra = fixed (a.lra, 1);

    // True peak: sin un hop entero no se midió; con hops y en −300, no hubo una sola muestra distinta de cero.
    if (a.truePeakDbtp > kSilenceDb) out.truePeak = fixed (a.truePeakDbtp, 1);
    else out.warnOnce (hops < kHopsTruePeak ? warn::truePeakTooShort : warn::truePeakNoSignal);

    // Correlación de banda ancha sobre el tramo: ΣLR / √(ΣLL·ΣRR), la definición del módulo Stereo.
    if (a.channels == 1) out.warnOnce (warn::monoSource);
    else if (m.sumLL <= kTinyEnergy || m.sumRR <= kTinyEnergy) out.warnOnce (warn::correlationNoSignal);
    else out.correlation = fixed (juce::jlimit (-1.0, 1.0, m.sumLR / std::sqrt (m.sumLL * m.sumRR)), 2);

    // Las 30 bandas: EL NÚMERO QUE DIBUJA TONAL BALANCE. Es la cuenta de Reference::fill, en float y en el
    // mismo orden: banda cruda (dBFS, referencia de SpectrumFrame) menos el integrado del mismo audio.
    if (integratedValid)
        for (int b = 0; b < SpectrumFrame::kNumThird; ++b)
        {
            if (a.bandsDb[b] >= kMeasurableDb) out.bands[(size_t) b] = fixed (a.bandsDb[b] - a.integratedLufs, 1);
            else out.warnOnce (warn::bandBelowFloor);
        }

    out.sortWarnings();
    return out.render (engine);
}

int run (std::istream& in, std::ostream& os, const EngineInfo& engine)
{
    Session session (engine);
    std::string line;
    while (std::getline (in, line))
    {
        if (! line.empty() && line.back() == '\r') line.pop_back();   // un pedido escrito con CRLF
        os << session.handle (line) << '\n';
        os.flush();
        if (! os) return 3;   // no se pudo escribir la salida: eso sí es una falla
    }
    return 0;
}
}
