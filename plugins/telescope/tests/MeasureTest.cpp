// [telescope][measure] — telescope-measure y el JSON v1 (prompt 101, F3 de TELESCOPE 0.2; contrato D-100).
//
// Una prueba por regla del contrato, CADA UNA CON SU CONTROL: el caso que tendría que ponerla en rojo, corrido
// en el mismo test, para que un verde no pueda ser un «el test no mira eso». Lo que sólo se ve desde afuera del
// proceso (el exit del binario, que no escribe en disco, dos corridas con los mismos bytes) lo prueba
// measure/check-measure-cli.sh contra el binario de verdad (ctest: telescope-measure-cli).
//
//   MEASURE[schema]      "schema":1 primero, y `engine` sale del motor que midió (control: otro motor, otra firma)
//   MEASURE[unidades]    las claves, en su orden fijo; toda clave numérica lleva su unidad en el nombre
//   MEASURE[null]        lo que no se mide va en null con su código: el integrado de un silencio, el LRA de muy
//                        poco audio, la correlación de un mono (control: con audio de sobra, los tres salen)
//   MEASURE[codigos]     los códigos del código y los de SCHEMA.md son los mismos, en las dos direcciones
//   MEASURE[ruta]        el archivo va por nombre y sha256, nunca con la ruta (con un nombre de usuario adentro)
//   MEASURE[redondeo]    dB a 0.1, correlación a 0.01, segundos a 0.01, sin "-0.0"
//   MEASURE[locale]      con un locale de coma decimal activo, los mismos bytes
//   MEASURE[negativas]   un pedido mal escrito es una negativa con su código, y run() sale 0 (control: 3 si no
//                        puede escribir)
//   MEASURE[determinismo] el mismo pedido da los mismos bytes: dos veces, en otra sesión y después de otro
//   MEASURE[orden]       con frecuencias de muestreo alternadas, el motor reusado da lo mismo que uno nuevo
//   MEASURE[sha]         el sha256 propio contra los vectores de FIPS 180-4
//   MEASURE[bandas]      el número de la herramienta == el que dibuja TONAL BALANCE, para el mismo audio
#include <catch2/catch_test_macros.hpp>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <locale>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include "MeasureCore.h"
#include "PluginProcessor.h"
#include "Sha256.h"
#include "TestHelpers.h"
#include "TestSignals.h"
#include "TestWav.h"
#include "lenses/TonalBalanceLens.h"

using telescope::measure::EngineInfo;
using telescope::measure::Session;
using telescope::test::Pink;

namespace
{
const EngineInfo kEngine { "0.1.0", "0123456789ab" };

using Gen = std::function<std::pair<float, float> (juce::int64)>;

juce::File wav (const juce::String& name, double sr, int channels, double seconds, const Gen& gen)
{
    return telescope::test::writeWav ("f3measure-" + name + ".wav", sr, channels,
                                      (juce::int64) std::llround (seconds * sr), gen);
}

Gen pinkStereo (float peak)
{
    auto a = std::make_shared<Pink> (telescope::test::kPinkSeedA);
    auto b = std::make_shared<Pink> (telescope::test::kPinkSeedB);
    return [=] (juce::int64) { return std::pair<float, float> { peak * a->next(), peak * b->next() }; };
}

Gen silence()
{
    return [] (juce::int64) { return std::pair<float, float> { 0.0f, 0.0f }; };
}

// El pedido, armado con el JSON de JUCE (escapa la ruta como corresponde).
std::string request (const juce::File& f, std::optional<double> from = {}, std::optional<double> to = {},
                     const juce::String& id = {})
{
    auto* o = new juce::DynamicObject();
    if (id.isNotEmpty()) o->setProperty ("id", id);
    o->setProperty ("file", f.getFullPathName());
    if (from) o->setProperty ("from_s", *from);
    if (to)   o->setProperty ("to_s", *to);
    return juce::JSON::toString (juce::var (o), true).toStdString();
}

juce::var parse (const std::string& line)
{
    juce::var v;
    INFO (line);
    REQUIRE (juce::JSON::parse (juce::String::fromUTF8 (line.data(), (int) line.size()), v).wasOk());
    REQUIRE (v.isObject());
    return v;
}

// Las claves en el orden en que aparecen en el texto (todas: las de adentro de engine, file y range también).
std::vector<std::string> keysInOrder (const std::string& line)
{
    std::vector<std::string> keys;
    static const std::regex key ("\"([a-z0-9_]+)\":");
    for (auto it = std::sregex_iterator (line.begin(), line.end(), key); it != std::sregex_iterator(); ++it)
        keys.push_back ((*it)[1].str());
    return keys;
}

// El texto crudo del valor de una clave de primer nivel (o de engine/file/range): hasta la coma o la llave.
std::string rawValue (const std::string& line, const std::string& k)
{
    const auto p = line.find ("\"" + k + "\":");
    if (p == std::string::npos) return "<falta>";
    auto s = p + k.size() + 3;
    auto e = line.find_first_of (",}", s);
    return line.substr (s, e - s);
}

// Las 30 bandas tal como las escribe la herramienta (texto crudo: el var de JUCE reformatea los números).
std::vector<std::string> rawBands (const std::string& line)
{
    std::vector<std::string> out;
    const std::string head = "\"bands_db_rel_integrated\":[";
    auto p = line.find (head);
    if (p == std::string::npos) return out;
    p += head.size();
    const auto e = line.find (']', p);
    std::stringstream ss (line.substr (p, e - p));
    for (std::string x; std::getline (ss, x, ',');) out.push_back (x);
    return out;
}

std::vector<std::string> warnings (const juce::var& v)
{
    std::vector<std::string> w;
    if (auto* arr = v["warnings"].getArray())
        for (const auto& x : *arr) w.push_back (x.toString().toStdString());
    return w;
}

bool has (const std::vector<std::string>& v, const std::string& s)
{
    return std::find (v.begin(), v.end(), s) != v.end();
}

// Las unidades que acepta el contrato en un nombre, y las magnitudes sin unidad declaradas. La unidad va al final
// (`integrated_lufs`, `from_s`), salvo en `seconds_measured`, que es el nombre que fija el propio contrato (D-100):
// ahí va adelante, y el verificador lo acepta por prefijo en vez de renombrar un campo del contrato.
bool keyCarriesUnit (const std::string& k)
{
    static const std::vector<std::string> suffixes { "_lufs", "_lu", "_dbtp", "_s", "_hz", "_sample", "_db_rel_integrated" };
    static const std::set<std::string> dimensionless { "schema", "channels", "correlation" };
    if (dimensionless.count (k) > 0) return true;
    if (k.rfind ("seconds_", 0) == 0) return true;
    for (const auto& s : suffixes)
        if (k.size() > s.size() && k.compare (k.size() - s.size(), s.size(), s) == 0) return true;
    return false;
}

std::string sha256Hex (const std::string& s)
{
    telescope::measure::Sha256 h;
    h.update (s.data(), s.size());
    return h.hex();
}
}

TEST_CASE ("telescope: MEASURE[schema] la version del esquema y el motor van en cada linea", "[telescope][measure]")
{
    const auto f = wav ("schema", 48000.0, 2, 3.0, pinkStereo (0.3f));

    Session s (kEngine);
    const auto line = s.handle (request (f));
    const auto v = parse (line);

    CHECK (line.rfind ("{\"schema\":1,", 0) == 0);   // la primera clave, y vale 1
    CHECK ((int) v["schema"] == 1);
    CHECK (v["engine"]["name"].toString() == "telescope-measure");
    CHECK (v["engine"]["version"].toString() == "0.1.0");
    CHECK (v["engine"]["sha"].toString() == "0123456789ab");

    // CONTROL: otro motor firma otra cosa. Si `engine` estuviera escrito a mano en la herramienta, esto no cambia.
    Session other ({ "9.9.9", "fedcba987654+dirty" });
    const auto v2 = parse (other.handle (request (f)));
    CHECK (v2["engine"]["version"].toString() == "9.9.9");
    CHECK (v2["engine"]["sha"].toString() == "fedcba987654+dirty");

    // Y la del binario: la versión es la de plugins/telescope/VERSION y el sha tiene la forma prometida.
    const auto built = telescope::measure::builtEngine();
    const auto versionFile = juce::File (TELESCOPE_DOCS_DIR).getSiblingFile ("VERSION").loadFileAsString().trim();
    CHECK (built.version == versionFile.toStdString());
    CHECK (std::regex_match (built.sha, std::regex ("([0-9a-f]{12}(\\+dirty)?|unknown)")));
    f.deleteFile();
}

TEST_CASE ("telescope: MEASURE[unidades] las claves en su orden y con la unidad en el nombre", "[telescope][measure]")
{
    const auto f = wav ("unidades", 48000.0, 2, 3.0, pinkStereo (0.3f));
    Session s (kEngine);
    const std::vector<std::string> expected {
        "schema", "engine", "name", "version", "sha", "id", "status", "refusal",
        "file", "name", "sha256", "sample_rate_hz", "channels", "duration_s",
        "range", "requested_from_s", "requested_to_s", "from_s", "to_s", "from_sample", "to_sample",
        "seconds_measured", "integrated_lufs", "lra_lu", "true_peak_dbtp", "correlation",
        "bands_hz", "bands_db_rel_integrated", "warnings" };

    // Medido, negado por el archivo y negado por el pedido: el mismo orden en los tres.
    for (const auto& line : { s.handle (request (f, 0.5, 2.0)), s.handle (request (f.getSiblingFile ("no-esta.wav"))),
                              s.handle ("{\"file\":3}") })
    {
        INFO (line);
        CHECK (keysInOrder (line) == expected);
    }

    // Toda clave que lleva un número mide algo, y su unidad está en el nombre.
    const auto v = parse (s.handle (request (f, 0.5, 2.0)));
    int numeric = 0;
    for (const auto& k : keysInOrder (s.handle (request (f, 0.5, 2.0))))
    {
        const juce::Identifier id { juce::String (k) };
        juce::var x = v[id];
        if (x.isVoid())
            for (const char* parent : { "engine", "file", "range" })
                if (v[parent].hasProperty (id)) x = v[parent][id];
        const bool isNum = x.isDouble() || x.isInt() || x.isInt64()
                        || (x.isArray() && x.size() > 0 && (x[0].isDouble() || x[0].isInt()));
        if (! isNum) continue;
        ++numeric;
        INFO ("clave numérica sin unidad: " << k);
        CHECK (keyCarriesUnit (k));
    }
    CHECK (numeric >= 15);   // si el filtro de "numérica" no encuentra nada, el CHECK de arriba no probó nada

    // CONTROL: el verificador de unidades rechaza los nombres que el contrato prohíbe.
    CHECK_FALSE (keyCarriesUnit ("integrated"));
    CHECK_FALSE (keyCarriesUnit ("lra"));
    CHECK_FALSE (keyCarriesUnit ("true_peak"));
    CHECK_FALSE (keyCarriesUnit ("from"));
    CHECK_FALSE (keyCarriesUnit ("measured"));
    f.deleteFile();
}

TEST_CASE ("telescope: MEASURE[null] lo que no se puede medir va en null con su codigo", "[telescope][measure]")
{
    Session s (kEngine);

    // El integrado de un silencio.
    const auto sil = wav ("null-silencio", 48000.0, 2, 12.0, silence());
    const auto vs = parse (s.handle (request (sil)));
    CHECK (vs["status"].toString() == "measured");
    CHECK (vs["integrated_lufs"].isVoid());
    CHECK (has (warnings (vs), "INTEGRATED_BELOW_GATE"));
    CHECK (vs["true_peak_dbtp"].isVoid());
    CHECK (has (warnings (vs), "TRUE_PEAK_NO_SIGNAL"));
    CHECK (vs["correlation"].isVoid());
    CHECK (has (warnings (vs), "CORRELATION_NO_SIGNAL"));
    for (int b = 0; b < 30; ++b) CHECK (vs["bands_db_rel_integrated"][b].isVoid());

    // El LRA de muy poco audio (4 s de rosa: integrado sí, LRA no).
    const auto shortF = wav ("null-corto", 48000.0, 2, 4.0, pinkStereo (0.3f));
    const auto vc = parse (s.handle (request (shortF)));
    CHECK_FALSE (vc["integrated_lufs"].isVoid());
    CHECK (vc["lra_lu"].isVoid());
    CHECK (has (warnings (vc), "LRA_TOO_SHORT"));

    // La correlación de un mono.
    const auto mono = wav ("null-mono", 48000.0, 1, 12.0, pinkStereo (0.3f));
    const auto vm = parse (s.handle (request (mono)));
    CHECK (vm["correlation"].isVoid());
    CHECK (has (warnings (vm), "MONO_SOURCE"));
    CHECK_FALSE (vm["integrated_lufs"].isVoid());

    // Menos de 100 ms: ni el true peak.
    const auto tiny = wav ("null-50ms", 48000.0, 2, 0.05, pinkStereo (0.3f));
    const auto vt = parse (s.handle (request (tiny)));
    CHECK (vt["true_peak_dbtp"].isVoid());
    CHECK (has (warnings (vt), "TRUE_PEAK_TOO_SHORT"));
    CHECK (has (warnings (vt), "INTEGRATED_TOO_SHORT"));

    // Nunca 0 disfrazado, nunca −inf, nunca el −300 del motor: en ninguna de las cuatro líneas.
    for (const auto& f : { sil, shortF, mono, tiny })
    {
        const auto line = s.handle (request (f));
        INFO (line);
        CHECK (line.find ("inf") == std::string::npos);
        CHECK (line.find ("nan") == std::string::npos);
        CHECK (line.find ("-300") == std::string::npos);
        CHECK (line.find ("-200") == std::string::npos);
    }

    // CONTROL: con 12 s de rosa estéreo, los tres campos SÍ salen. El null viene de la condición, no de la clave.
    const auto full = wav ("null-control", 48000.0, 2, 12.0, pinkStereo (0.3f));
    const auto vf = parse (s.handle (request (full)));
    CHECK_FALSE (vf["integrated_lufs"].isVoid());
    CHECK_FALSE (vf["lra_lu"].isVoid());
    CHECK_FALSE (vf["correlation"].isVoid());
    CHECK_FALSE (vf["true_peak_dbtp"].isVoid());
    CHECK (warnings (vf).empty());

    for (const auto& f : { sil, shortF, mono, tiny, full }) f.deleteFile();
}

TEST_CASE ("telescope: MEASURE[codigos] los codigos del codigo y los de SCHEMA.md son los mismos", "[telescope][measure]")
{
    const auto schema = juce::File (TELESCOPE_MEASURE_SCHEMA).loadFileAsString().toStdString();
    REQUIRE (schema.size() > 1000);

    std::set<std::string> inCode;
    for (const auto& c : telescope::measure::refusalCodes()) inCode.insert (c);
    for (const auto& c : telescope::measure::warningCodes()) inCode.insert (c);
    CHECK (inCode.size() == telescope::measure::refusalCodes().size() + telescope::measure::warningCodes().size());

    // Del código a SCHEMA.md: cada código tiene su fila (`CÓDIGO` al principio de una fila de tabla).
    for (const auto& c : inCode)
    {
        INFO ("código sin fila en SCHEMA.md: " << c);
        CHECK (schema.find ("| `" + c + "` |") != std::string::npos);
    }

    // De SCHEMA.md al código: toda fila de tabla que empieza con un código en mayúsculas existe en el código.
    std::set<std::string> inSchema;
    static const std::regex row ("\\| `([A-Z][A-Z_]+[A-Z])` \\|");
    for (auto it = std::sregex_iterator (schema.begin(), schema.end(), row); it != std::sregex_iterator(); ++it)
        inSchema.insert ((*it)[1].str());
    CHECK (inSchema == inCode);

    // CONTROL: la búsqueda puede fallar (un código inventado no está), y el lector de filas encuentra algo.
    CHECK (schema.find ("| `NOT_A_REAL_CODE` |") == std::string::npos);
    CHECK (inSchema.size() >= 19);
}

TEST_CASE ("telescope: MEASURE[ruta] el archivo va por nombre y sha256, nunca con la ruta", "[telescope][measure]")
{
    // Una ruta como la de un usuario de verdad: con su nombre adentro.
    const auto dir = telescope::test::tempDir().getChildFile ("f3measure-ruta/Users/joaquincerrano/Music/Mi Tema");
    REQUIRE (dir.createDirectory());
    const auto f = telescope::test::writeWav ("f3measure-ruta/Users/joaquincerrano/Music/Mi Tema/bajo final.wav",
                                              48000.0, 2, 48000 * 3, pinkStereo (0.3f));
    REQUIRE (f.existsAsFile());

    Session s (kEngine);
    const auto line = s.handle (request (f));
    const auto v = parse (line);
    INFO (line);

    const auto leaks = [] (const std::string& l, const juce::File& file)
    {
        return l.find ("joaquincerrano") != std::string::npos
            || l.find (file.getParentDirectory().getFullPathName().toStdString()) != std::string::npos
            || l.find ("/Users/") != std::string::npos;
    };

    CHECK_FALSE (leaks (line, f));
    CHECK (v["file"]["name"].toString() == "bajo final.wav");

    // El sha256 es el de los bytes del archivo (lo recalcula el test por su lado).
    juce::MemoryBlock mb;
    REQUIRE (f.loadFileAsData (mb));
    CHECK (v["file"]["sha256"].toString().toStdString()
           == sha256Hex (std::string ((const char*) mb.getData(), mb.getSize())));

    // Las negativas tampoco la filtran.
    const auto refused = s.handle (request (f.getSiblingFile ("no existe.wav")));
    CHECK_FALSE (leaks (refused, f));
    CHECK (parse (refused)["file"]["name"].toString() == "no existe.wav");

    // CONTROL: el detector ve una ruta cuando la hay (una línea armada a mano con la ruta adentro).
    CHECK (leaks ("{\"file\":\"" + f.getFullPathName().toStdString() + "\"}", f));

    f.deleteFile();
    telescope::test::tempDir().getChildFile ("f3measure-ruta").deleteRecursively();
}

TEST_CASE ("telescope: MEASURE[redondeo] los decimales fijos y nunca -0.0", "[telescope][measure]")
{
    using telescope::measure::fixed;
    CHECK (fixed (-14.04, 1) == "-14.0");
    CHECK (fixed (-14.06, 1) == "-14.1");
    CHECK (fixed (-0.04, 1) == "0.0");      // no "-0.0"
    CHECK (fixed (-0.004, 2) == "0.00");    // no "-0.00"
    CHECK (fixed (0.25, 1) == "0.3");       // medios lejos del cero
    CHECK (fixed (-0.25, 1) == "-0.3");
    CHECK (fixed (0.999, 2) == "1.00");
    CHECK (fixed (-1.0, 2) == "-1.00");
    CHECK (fixed (48000.0, 0) == "48000");
    CHECK (fixed (1234567.891, 2) == "1234567.89");

    // En la línea: dB a 0.1, correlación a 0.01, segundos a 0.01.
    const auto f = wav ("redondeo", 44100.0, 2, 12.0, pinkStereo (0.3f));
    Session s (kEngine);
    const auto line = s.handle (request (f, 1.0 / 3.0, 11.123456));
    INFO (line);
    const std::regex db ("-?[0-9]+\\.[0-9]"), two ("-?[0-9]+\\.[0-9]{2}");
    for (const char* k : { "integrated_lufs", "lra_lu", "true_peak_dbtp" })
        CHECK (std::regex_match (rawValue (line, k), db));
    for (const char* k : { "correlation", "requested_from_s", "requested_to_s", "from_s", "to_s", "seconds_measured", "duration_s" })
        CHECK (std::regex_match (rawValue (line, k), two));
    CHECK (rawValue (line, "requested_from_s") == "0.33");
    CHECK (rawValue (line, "from_sample") == "14700");   // llround (44100 / 3)

    // Las bandas: cada una con un decimal (o null).
    const auto bandsAt = line.find ("\"bands_db_rel_integrated\":[");
    REQUIRE (bandsAt != std::string::npos);
    const auto list = line.substr (bandsAt + 27, line.find (']', bandsAt) - bandsAt - 27);
    const std::regex band ("(-?[0-9]+\\.[0-9]|null)(,(-?[0-9]+\\.[0-9]|null)){29}");
    CHECK (std::regex_match (list, band));

    // Ningún "-0.0" en la línea (ni -0.00).
    CHECK (line.find ("-0.0,") == std::string::npos);
    CHECK (line.find ("-0.00,") == std::string::npos);
    CHECK (line.find ("-0.0]") == std::string::npos);

    // CONTROL: el patrón de un decimal no acepta dos, y el de "-0.0" encuentra uno si lo hay.
    CHECK_FALSE (std::regex_match (std::string ("-14.05"), db));
    CHECK (std::string ("{\"x\":-0.0,").find ("-0.0,") != std::string::npos);
    f.deleteFile();
}

TEST_CASE ("telescope: MEASURE[locale] el formato de los numeros no depende del locale", "[telescope][measure]")
{
    const auto f = wav ("locale", 48000.0, 2, 11.0, pinkStereo (0.3f));
    const auto req = request (f, 2.5, 10.25);

    Session s (kEngine);
    const auto inC = s.handle (req);

    // Un locale de coma decimal. El primero que exista en la máquina ("de-DE" es el nombre de Windows).
    const char* found = nullptr;
    for (const char* name : { "de_DE.UTF-8", "es_AR.UTF-8", "es_ES.UTF-8", "fr_FR.UTF-8", "de_DE", "fr_FR", "de-DE" })
        if (std::setlocale (LC_ALL, name) != nullptr) { found = name; break; }

    // CONTROL: el locale está activo de verdad — printf escribe la coma. Si no hay ninguno, el test no puede
    // probar nada y lo dice en rojo (un test que no corre no es un verde).
    char buf[16];
    std::snprintf (buf, sizeof (buf), "%.1f", 1.5);
    INFO ("locale: " << (found != nullptr ? found : "ninguno") << " · printf da " << buf);
    REQUIRE (found != nullptr);
    CHECK (std::string (buf) == "1,5");
    std::printf ("MEASURE[locale] con %s activo printf escribe %s\n", found, buf);

    std::string inComma;
    try
    {
        std::locale::global (std::locale (found));
        inComma = s.handle (req);
    }
    catch (const std::exception& e)
    {
        FAIL_CHECK ("std::locale (" << found << ") no se pudo armar: " << e.what());
    }
    std::locale::global (std::locale::classic());
    std::setlocale (LC_ALL, "C");

    CHECK (inComma == inC);
    CHECK (rawValue (inComma, "requested_from_s") == "2.50");
    f.deleteFile();
}

TEST_CASE ("telescope: MEASURE[negativas] un pedido mal escrito es una negativa y el exit es 0", "[telescope][measure]")
{
    const auto f = wav ("negativas", 48000.0, 2, 2.0, pinkStereo (0.3f));
    // La ruta YA ESCAPADA para JSON, con comillas. En Windows lleva barras invertidas (D:\a\ovni\build\…) y, pegada
    // cruda, sus \a, \b y \t se leen como escapes de JSON: lo cazó el paso informativo de telescope-measure.yml.
    const auto quote = [] (const juce::File& x) { return juce::JSON::toString (juce::var (x.getFullPathName())).toStdString(); };
    const auto path = quote (f);
    const auto notAudio = telescope::test::tempDir().getChildFile ("f3measure-no-es-audio.wav");
    REQUIRE (notAudio.replaceWithText ("esto no es un wav"));

    struct Case { std::string line; const char* code; };
    const std::vector<Case> cases {
        { "hola", "REQUEST_NOT_JSON" },
        { "", "REQUEST_NOT_JSON" },
        { "[1,2]", "REQUEST_NOT_JSON" },
        { "{\"file\":" + path + ",\"to\":3}", "REQUEST_UNKNOWN_FIELD" },
        { "{\"file\":" + path + ",\"from_s\":\"1\"}", "REQUEST_BAD_FIELD" },
        { "{\"file\":" + path + ",\"from_s\":1e999}", "REQUEST_BAD_FIELD" },
        { "{\"file\":7}", "REQUEST_BAD_FIELD" },
        { "{\"from_s\":1}", "REQUEST_NO_FILE" },
        { "{\"file\":\"\"}", "REQUEST_NO_FILE" },
        { "{\"file\":" + quote (f.getSiblingFile (f.getFileName() + "x")) + "}", "FILE_NOT_FOUND" },
        { "{\"file\":" + quote (notAudio) + "}", "FILE_NOT_AUDIO" },
        { "{\"file\":" + path + ",\"from_s\":1.5,\"to_s\":1.5}", "RANGE_EMPTY" },
        { "{\"file\":" + path + ",\"from_s\":5,\"to_s\":9}", "RANGE_EMPTY" },   // entero fuera del archivo
    };

    std::string input;
    for (const auto& c : cases) input += c.line + "\n";
    input += "{\"id\":\"ok\",\"file\":" + path + "}\r\n";   // uno bueno al final, y con CRLF

    std::istringstream in (input);
    std::ostringstream out;
    CHECK (telescope::measure::run (in, out, kEngine) == 0);

    std::vector<std::string> lines;
    std::istringstream o (out.str());
    for (std::string l; std::getline (o, l);) lines.push_back (l);
    REQUIRE (lines.size() == cases.size() + 1);   // una línea por pedido, también por la vacía

    for (size_t i = 0; i < cases.size(); ++i)
    {
        INFO ("pedido: " << cases[i].line << "\nrespuesta: " << lines[i]);
        const auto v = parse (lines[i]);
        CHECK (v["status"].toString() == "refused");
        CHECK (v["refusal"].toString().toStdString() == cases[i].code);
    }
    const auto last = parse (lines.back());
    CHECK (last["status"].toString() == "measured");
    CHECK (last["id"].toString() == "ok");
    CHECK (last["refusal"].isVoid());

    // CONTROL: el exit no es una constante — si la salida no se puede escribir, run() sale 3.
    std::istringstream in2 ("{\"file\":\"x\"}\n");
    std::ostringstream broken;
    broken.setstate (std::ios::badbit);
    CHECK (telescope::measure::run (in2, broken, kEngine) == 3);

    f.deleteFile();
    notAudio.deleteFile();
}

TEST_CASE ("telescope: MEASURE[determinismo] el mismo pedido da los mismos bytes", "[telescope][measure]")
{
    const auto a = wav ("det-a", 48000.0, 2, 11.0, pinkStereo (0.3f));
    auto g = pinkStereo (0.3f);
    const auto b = wav ("det-b", 48000.0, 2, 11.0, [&] (juce::int64 i)
                        {
                            auto v = g (i);
                            if (i == 100000) v.first += 0.25f;   // UNA muestra distinta
                            return v;
                        });

    Session s1 (kEngine);
    const auto first  = s1.handle (request (a, 1.0, 9.0));
    const auto second = s1.handle (request (a, 1.0, 9.0));
    s1.handle (request (b));                              // otro pedido en el medio…
    const auto third  = s1.handle (request (a, 1.0, 9.0));   // …no deja nada en el motor
    Session s2 (kEngine);
    const auto fresh  = s2.handle (request (a, 1.0, 9.0));

    CHECK (sha256Hex (first) == sha256Hex (second));
    CHECK (first == third);
    CHECK (first == fresh);

    // CONTROL: el mismo pedido sobre el archivo con una muestra distinta da otros bytes (al menos el sha256).
    const auto other = s1.handle (request (b, 1.0, 9.0));
    CHECK (other != first);
    a.deleteFile();
    b.deleteFile();
}

TEST_CASE ("telescope: MEASURE[orden] un motor que ya midio otra cosa da lo mismo que uno nuevo", "[telescope][measure]")
{
    // Tres frecuencias alternadas: el motor se re-prepara cuando cambia la SR y sólo se resetea cuando no
    // (RangeAnalyzer::measure). Cada línea tiene que ser idéntica a la de una sesión recién creada.
    const auto a44 = wav ("orden-44", 44100.0, 2, 3.0, pinkStereo (0.3f));
    const auto b48 = wav ("orden-48", 48000.0, 2, 3.0, pinkStereo (0.2f));
    const auto c96 = wav ("orden-96", 96000.0, 2, 2.0, pinkStereo (0.4f));

    std::vector<std::string> fresh;
    for (const auto& f : { a44, b48, c96 })
    {
        Session s (kEngine);
        fresh.push_back (s.handle (request (f)));
    }

    Session reused (kEngine);
    const std::vector<int> order { 0, 0, 1, 1, 0, 2, 2, 1, 0 };
    const juce::File files[] = { a44, b48, c96 };
    int same = 0;
    for (const int i : order)
        if (reused.handle (request (files[i])) == fresh[(size_t) i]) ++same;
    CHECK (same == (int) order.size());

    // CONTROL: las tres líneas nuevas no son la misma (si lo fueran, la comparación no discriminaría nada).
    CHECK (fresh[0] != fresh[1]);
    CHECK (fresh[1] != fresh[2]);
    for (const auto& f : files) f.deleteFile();
}

TEST_CASE ("telescope: MEASURE[sha] el sha256 propio contra los vectores de FIPS 180-4", "[telescope][measure]")
{
    CHECK (sha256Hex ("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK (sha256Hex ("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK (sha256Hex ("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")
           == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    // Un millón de 'a' (cruza muchos bloques y el relleno cae en un bloque aparte).
    CHECK (sha256Hex (std::string (1000000, 'a')) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    // CONTROL: un carácter cambia todo.
    CHECK (sha256Hex ("abd") != sha256Hex ("abc"));
}

TEST_CASE ("telescope: MEASURE[bandas] el numero de la herramienta es el que dibuja TONAL BALANCE", "[telescope][measure]")
{
    constexpr double sr = 48000.0;
    // Rosa con un pozo en los agudos (un pasa-bajos de un polo): que las bandas no sean todas iguales.
    auto a = std::make_shared<Pink> (telescope::test::kPinkSeedA);
    auto b = std::make_shared<Pink> (telescope::test::kPinkSeedB);
    auto zl = std::make_shared<float> (0.0f), zr = std::make_shared<float> (0.0f);
    const auto f = wav ("bandas", sr, 2, 10.0, [=] (juce::int64)
                        {
                            *zl += 0.2f * (a->next() - *zl);
                            *zr += 0.2f * (b->next() - *zr);
                            return std::pair<float, float> { 0.5f * *zl, 0.5f * *zr };
                        });

    // La herramienta.
    Session s (kEngine);
    const auto toolLine = s.handle (request (f));
    REQUIRE (parse (toolLine)["status"].toString() == "measured");
    const auto tool = rawBands (toolLine);
    REQUIRE (tool.size() == 30);

    // La lente: el mismo archivo cargado como referencia de TONAL BALANCE, en un processor de verdad.
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (sr, 512);
    proc.setEnabledModules (telescope::kSpectrum | telescope::kLoudness | telescope::kReference);
    proc.loadReference (f);
    REQUIRE (telescope::test::waitUntil ([&] { return ! proc.referenceBusy(); }, 60000));
    REQUIRE (proc.referenceError().isEmpty());

    // El frame de la referencia lo publica el worker: hace falta que corra audio.
    {
        Pink p { telescope::test::kPinkSeedA };
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        for (int blk = 0; blk < 200; ++blk)
        {
            for (int i = 0; i < 512; ++i) { const float v = 0.2f * p.next(); buf.setSample (0, i, v); buf.setSample (1, i, v); }
            proc.processBlock (buf, midi);
        }
    }
    REQUIRE (telescope::test::waitUntil ([&] { return proc.reference().read().refValid; }, 30000));

    telescope::TonalBalanceLens lens (proc);
    lens.setSize (1025, 702);
    lens.pumpFrames (4);

    int compared = 0, nulls = 0;
    for (int band = 0; band < 30; ++band)
    {
        const auto r = lens.readoutForBand (band);
        REQUIRE (r.valid);
        const auto& t = tool[(size_t) band];
        INFO ("banda " << band << " (" << telescope::kThirdOctaveHz[band] << " Hz) · lente " << r.refNorm
                       << " · herramienta " << t);
        if (r.refNorm <= telescope::SpectrumFrame::kFloorDb)
        {
            CHECK (t == "null");   // la lente no la tiene: la herramienta tampoco
            ++nulls;
            continue;
        }
        CHECK (telescope::measure::fixed (r.refNorm, 1) == t);
        ++compared;
    }
    std::printf ("MEASURE[bandas] %d bandas iguales a la lente (y %d sin medición en las dos)\n", compared, nulls);
    CHECK (compared >= 25);

    // CONTROL: contra OTRO audio (rosa sin filtrar), la comparación falla en al menos una banda. Si no fallara, la
    // igualdad de arriba no diría nada sobre ESTE archivo.
    const auto flat = wav ("bandas-control", sr, 2, 10.0, pinkStereo (0.5f));
    const auto other = rawBands (s.handle (request (flat)));
    REQUIRE (other.size() == 30);
    int differ = 0;
    for (int band = 0; band < 30; ++band)
    {
        const auto r = lens.readoutForBand (band);
        if (r.refNorm > telescope::SpectrumFrame::kFloorDb && other[(size_t) band] != "null"
            && telescope::measure::fixed (r.refNorm, 1) != other[(size_t) band])
            ++differ;
    }
    CHECK (differ > 0);

    proc.releaseResources();
    f.deleteFile();
    flat.deleteFile();
}
