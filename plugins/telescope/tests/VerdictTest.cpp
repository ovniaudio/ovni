// [telescope][verdict] — EL MOTOR DE REGLAS DE LA LENTE 13 (spec §5.8, D-47).
//
// Lo que este archivo tiene que probar es una sola cosa, dicha de varias maneras: que cada regla se
// dispara con el defecto que dice medir y NO se dispara con material sano. Una regla que no distingue las
// dos cosas no es una regla, es una opinión con un número al lado.
//
//   VERDICT[regla]        la tabla: una entrada por regla, con la señal que la dispara y la sana que no.
//   VERDICT[sano]         ruido rosa sano -> CERO hallazgos (⚠ o ●). Cero falsos positivos.
//   VERDICT[hueco]        el hueco REAL de 400-1000 Hz entre 0:20 y 0:35, por audio de verdad: desde el 57d
//                         sale UN hallazgo (las bandas contiguas se fusionan), con la peor banda, el tramo
//                         entero, t0 = 20 ± 1, t1 = 35 ± 1 y el número en la frase.
//   VERDICT[archivo]      el mismo audio por archivo y en vivo -> el MISMO informe, frase por frase.
//   VERDICT[plantillas]   las SEIS lenguas renderizan todas las frases sin una llave `{...}` sin resolver.
//   VERDICT[fallback]     un idioma al que le falta una clave cae en inglés, nunca en vacío.
//   VERDICT[determinismo] dos evaluaciones de la misma entrada dan el mismo informe, carácter por carácter.
//   VERDICT[referencia]   con referencia cargada la sección 1 compara contra ella y no contra la tendencia.
//   VERDICT[titular]      (57d) la línea de arriba cuenta lo medido: n dentro de rango · m para revisar, el
//                         primero en t0. Sin adjetivos.
//   VERDICT[rango]        (57d) "Dentro de rango": una línea por regla evaluada que NO se disparó, con su
//                         número y su regla — y nunca la de una regla que sí se disparó.
//   VERDICT[tono]         (57d) las seis tablas sin palabras de opinión; en en/es el número va antes del
//                         primer punto y cada hallazgo termina diciendo dónde mirar.
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>
#include "PluginProcessor.h"
#include "TestDefectSignal.h"
#include "TestHelpers.h"
#include <juce_dsp/juce_dsp.h>
#include "data/Rules.h"
#include "TestSignals.h"
#include "TestWav.h"
#include "analysis/FileAnalyzer.h"
#include "analysis/SecondHistory.h"
#include "analysis/modules/Verdict.h"

using telescope::AnalysisFrame;
using telescope::SecondRow;
using telescope::Verdict;
using telescope::VerdictReport;
using telescope::rules::RuleId;
using telescope::rules::Severity;

namespace
{
constexpr int kBands = SecondRow::kNumBands;
constexpr int kRows  = 60;

// ========================================================================================================
// EL MATERIAL SANO, en el dominio en el que vive el motor: filas por segundo.
//
// Es el equivalente exacto del "ruido rosa a −14 LUFS con corr 0.9 y PSR ~10" del spec: todas las bandas
// al mismo nivel (que es lo que da un rosa medido en ⅓ de octava), correlación 0.9, sin desbalance, sin
// clips, sin continua. Si alguna regla se dispara acá, es un falso positivo y el test lo dice.
// ========================================================================================================
SecondRow healthyRow()
{
    SecondRow r;
    for (int b = 0; b < kBands; ++b)
    {
        r.bandsDb[b]        = -25.0f;
        r.bandCorr[b]       = 0.9f;
        r.bandMonoLossDb[b] = -0.22f;   // corr 0.9 con LL = RR  ->  width ≈ 0.23
    }
    r.shortTermMin = -14.5f;
    r.shortTermMax = -14.0f;
    r.momentaryMax = -13.5f;
    r.tpMaxDbtp    = -4.0f;             // PSR = 10 dB, por encima del umbral de 8
    r.clipEvents   = 0;
    r.corr = 0.9f;  r.width = 0.23f;  r.balanceDb = 0.0f;  r.monoLossDb = -0.22f;
    r.dcL = r.dcR = 0.0f;
    r.keyTonic = -1; r.keyMode = -1; r.keyConfidence = 0.0f;
    r.measuredModules = telescope::kSecondRowModules;
    return r;
}

std::vector<SecondRow> healthyRows (int n = kRows)
{
    return std::vector<SecondRow> ((size_t) n, healthyRow());
}

AnalysisFrame healthyAggregates()
{
    AnalysisFrame f;
    f.loudness.integrated      = -14.0f;
    f.loudness.integratedValid = true;
    f.loudness.lra             = 6.0f;
    f.loudness.truePeakMax     = -1.5f;
    f.loudness.shortTermMax    = -13.0f;
    f.loudness.momentaryMax    = -12.5f;
    f.plr = 12.5f;  f.plrValid = true;
    f.corr = 0.9f;  f.width = 0.23f;
    f.keyTonic = -1; f.keyMode = -1;
    f.dcL = f.dcR = 0.0f;
    f.secondsAnalysed = (juce::uint32) kRows;
    return f;
}

Verdict::Inputs inputsFor (const std::vector<SecondRow>& rows, const AnalysisFrame& agg,
                           const char* lang = "en")
{
    Verdict::Inputs in;
    in.aggregates = agg;
    in.rows       = rows.data();
    in.n          = (int) rows.size();
    in.language   = lang;
    in.targetIndex = 0;                 // "Ninguno": la regla de plataforma no aplica salvo que se pida
    in.clipThresholdDbtp = -1.0f;
    return in;
}

bool fired (const VerdictReport& rep, RuleId id)
{
    for (const auto& f : rep.findings)
        if (f.ruleId == id && f.severity != Severity::info) return true;
    return false;
}

const telescope::VerdictFinding* findingFor (const VerdictReport& rep, RuleId id)
{
    for (const auto& f : rep.findings) if (f.ruleId == id) return &f;
    return nullptr;
}

int hallazgos (const VerdictReport& rep)
{
    int n = 0;
    for (const auto& f : rep.findings) if (f.severity != Severity::info) ++n;
    return n;
}

void addDb (std::vector<SecondRow>& rows, int lo, int hi, float db, int from = 0, int to = kRows)
{
    for (int i = from; i < to && i < (int) rows.size(); ++i)
        for (int b = lo; b <= hi && b < kBands; ++b) rows[(size_t) i].bandsDb[b] += db;
}

// ===== 57d =====
// La línea de "Dentro de rango" de una regla, por su id textual (el de la evidencia), o nullptr.
const telescope::VerdictFinding* strengthFor (const VerdictReport& rep, const char* ruleName)
{
    for (const auto& s : rep.strengths)
        if (std::string (telescope::rules::rule (s.ruleId).name) == ruleName) return &s;
    return nullptr;
}

// Una entrada que dispara TODO lo que se pueda disparar a la vez. La comparten VERDICT[plantillas] (ve el
// máximo de frases posible) y VERDICT[tono] (juzga el tono sobre ese mismo máximo).
void applyAllDefects (std::vector<SecondRow>& rows, AnalysisFrame& agg)
{
    for (auto& r : rows)
    {
        r.tpMaxDbtp = -10.0f;
        for (int b = 9; b <= 13; ++b) r.bandsDb[b] += 8.0f;
        for (int b = 19; b <= 23; ++b) r.bandsDb[b] += 8.0f;
        for (int b = 26; b < kBands; ++b) r.bandsDb[b] -= 8.0f;
        for (int b = 0; b <= 6; ++b) r.bandMonoLossDb[b] = -6.0f;
        for (int b = 11; b <= 19; ++b) r.bandMonoLossDb[b] = -0.30f;
        for (int b = 20; b < kBands; ++b) r.bandMonoLossDb[b] = -6.00f;
        r.bandCorr[26] = -0.9f;
        r.balanceDb = 5.0f;
        r.clipEvents = 2;
    }
    for (int i = 40; i < 50; ++i) { rows[(size_t) i].shortTermMax = -20.0f; rows[(size_t) i].shortTermMin = -20.5f; }
    for (int i = 20; i < 35; ++i) rows[(size_t) i].bandsDb[13] -= 12.0f;
    agg.keyTonic = 9; agg.keyMode = 1; agg.keyConfidence = 0.85f; agg.keyTimeFraction = 0.9f;
    agg.dcL = 0.02f; agg.dcR = -0.015f;
    agg.loudness.integrated = -8.0f;
    agg.loudness.truePeakMax = 0.4f;
}

// "El número primero": hay al menos un dígito antes del primer punto de la frase (el punto de un decimal
// cuenta como punto, así que "PSR 6.2 dB" pasa por el 6 y "Crushed: average PSR…" no pasa).
bool digitBeforeFirstPeriod (const std::string& s)
{
    const auto dot = s.find ('.');
    const auto end = dot == std::string::npos ? s.size() : dot;
    const auto digit = s.find_first_of ("0123456789");
    return digit != std::string::npos && digit < end;
}

// Las palabras de una frase, en minúsculas. Los bytes ≥ 0x80 cuentan como letra: una palabra con tilde no
// se parte en dos, y "normal" sigue siendo UNA palabra (la lista prohibida tiene "mal", que no es "normal").
std::vector<std::string> wordsOf (const std::string& text)
{
    std::vector<std::string> out;
    std::string cur;
    for (const unsigned char c : text)
    {
        if (std::isalpha (c) || c >= 0x80) cur += (char) std::tolower (c);
        else if (! cur.empty()) { out.push_back (cur); cur.clear(); }
    }
    if (! cur.empty()) out.push_back (cur);
    return out;
}
}

// ========================================================================================================
TEST_CASE ("telescope: cada regla de VERDICT se dispara con su defecto y no con material sano",
           "[telescope][verdict]")
{
    // El sano, primero: es la referencia contra la que se lee toda la tabla.
    {
        const auto rows = healthyRows();
        const auto rep  = Verdict::evaluate (inputsFor (rows, healthyAggregates()));
        std::printf ("VERDICT[sano] %d hallazgos (criterio 0)  ·  %d lineas informativas  ·  pie: \"%s\"\n",
                     hallazgos (rep), (int) rep.findings.size() - hallazgos (rep), rep.footer.c_str());
        for (const auto& f : rep.findings)
            if (f.severity != Severity::info)
                std::printf ("VERDICT[sano] FALSO POSITIVO: %s\n", f.text.c_str());
        REQUIRE (hallazgos (rep) == 0);
        REQUIRE_FALSE (rep.footer.empty());
        for (const auto& d : rep.devices) REQUIRE (d.verdict == telescope::devices::DeviceVerdict::ok);
    }

    struct Case
    {
        const char* label;
        RuleId      id;
        std::function<void (std::vector<SecondRow>&, AnalysisFrame&, Verdict::Inputs&)> defect;
    };

    const Case cases[] = {
        { "aplastado (PSR 4 dB)", RuleId::crushed,
          [] (auto& rows, auto&, auto&) { for (auto& r : rows) r.tpMaxDbtp = -10.0f; } },

        { "delgado (150-400 Hz, -8 dB)", RuleId::thin,
          [] (auto& rows, auto&, auto&) { addDb (rows, 8, 12, -8.0f); } },

        { "turbio (200-500 Hz, +8 dB)", RuleId::muddy,
          [] (auto& rows, auto&, auto&) { addDb (rows, 9, 13, +8.0f); } },

        { "aspero (2-5 kHz, +8 dB, todo el tiempo)", RuleId::harsh,
          [] (auto& rows, auto&, auto&) { addDb (rows, 19, 23, +8.0f); } },

        { "sin aire (> 10 kHz, -8 dB)", RuleId::noAir,
          [] (auto& rows, auto&, auto&) { addDb (rows, 26, 29, -8.0f); } },

        { "centro vacio (medios 0.26 / agudos 1.73)", RuleId::hollowCentre,
          [] (auto& rows, auto&, auto&)
          {
              for (auto& r : rows)
              {
                  for (int b = 11; b <= 19; ++b) r.bandMonoLossDb[b] = -0.30f;
                  for (int b = 20; b < kBands; ++b) r.bandMonoLossDb[b] = -6.00f;
              }
          } },

        { "tonalidad (La menor, confianza 0.85)", RuleId::key,
          [] (auto&, auto& agg, auto&)
          { agg.keyTonic = 9; agg.keyMode = 1; agg.keyConfidence = 0.85f; agg.keyTimeFraction = 0.9f; } },

        { "celular (99 % bajo 300 Hz, armonicos -32 dB)", RuleId::devPhone,
          [] (auto& rows, auto&, auto&)
          {
              for (auto& r : rows)
                  for (int b = 0; b < kBands; ++b) r.bandsDb[b] = (b <= 10) ? -10.0f : -40.0f;
          } },

        { "auriculares (2-5 kHz +8 dB)", RuleId::devHeadphones,
          [] (auto& rows, auto&, auto&) { addDb (rows, 19, 23, +8.0f); } },

        { "laptop (2-4 kHz -8 dB)", RuleId::devLaptop,
          [] (auto& rows, auto&, auto&) { addDb (rows, 19, 22, -8.0f); } },

        { "auto (< 100 Hz +12 dB)", RuleId::devCar,
          [] (auto& rows, auto&, auto&) { addDb (rows, 0, 6, +12.0f); } },

        { "club (monoLoss -6 dB < 120 Hz)", RuleId::devClub,
          [] (auto& rows, auto&, auto&)
          { for (auto& r : rows) for (int b = 0; b <= 6; ++b) r.bandMonoLossDb[b] = -6.0f; } },

        { "hi-fi (hereda la seccion 1)", RuleId::devHifi,
          [] (auto& rows, auto&, auto&) { addDb (rows, 9, 13, +8.0f); } },

        { "hueco (500 Hz, -12 dB, 15 s)", RuleId::hole,
          [] (auto& rows, auto&, auto&) { addDb (rows, 13, 13, -12.0f, 20, 35); } },

        { "seccion baja (-6 LU, 10 s)", RuleId::quietSection,
          [] (auto& rows, auto&, auto&)
          { for (int i = 40; i < 50; ++i) { rows[(size_t) i].shortTermMax = -20.0f;
                                            rows[(size_t) i].shortTermMin = -20.5f; } } },

        { "seccion alta (+6 LU, 10 s)", RuleId::loudSection,
          [] (auto& rows, auto&, auto&)
          { for (int i = 5; i < 15; ++i) { rows[(size_t) i].shortTermMin = -8.0f;
                                           rows[(size_t) i].shortTermMax = -7.5f; } } },

        { "picos (10 eventos en 5 s seguidos)", RuleId::peaks,
          [] (auto& rows, auto&, auto&)
          { for (int i = 12; i < 17; ++i) rows[(size_t) i].clipEvents = 2; } },

        { "fuera de fase (1 kHz, corr -0.9 el 75 % del tiempo)", RuleId::outOfPhase,
          [] (auto& rows, auto&, auto&)
          { for (int i = 0; i < 45; ++i) rows[(size_t) i].bandCorr[16] = -0.9f; } },

        { "desbalance (+5 dB, 25 s)", RuleId::imbalance,
          [] (auto& rows, auto&, auto&)
          { for (int i = 10; i < 35; ++i) rows[(size_t) i].balanceDb = 5.0f; } },

        { "continua (dc 0.02)", RuleId::dcOffset,
          [] (auto&, auto& agg, auto&) { agg.dcL = 0.02f; agg.dcR = -0.015f; } },

        { "plataforma (Spotify, -8 LUFS)", RuleId::platform,
          [] (auto&, auto& agg, auto& in)
          { agg.loudness.integrated = -8.0f; in.targetIndex = 1; } },
    };

    std::printf ("\nVERDICT[regla]  %-42s  dispara  sana  frase\n", "senal");
    std::printf ("VERDICT[regla]  %s\n", std::string (110, '-').c_str());

    const auto healthy    = healthyRows();
    const auto healthyAgg = healthyAggregates();
    const auto healthyRep = Verdict::evaluate (inputsFor (healthy, healthyAgg));

    for (const auto& c : cases)
    {
        auto rows = healthyRows();
        auto agg  = healthyAggregates();
        auto in   = inputsFor (rows, agg);
        c.defect (rows, agg, in);
        in.rows = rows.data();
        in.n    = (int) rows.size();
        in.aggregates = agg;

        const auto rep = Verdict::evaluate (in);
        const auto* f  = findingFor (rep, c.id);
        // Para las SEIS reglas de dispositivo, "dispara" no es "salió una línea" —siempre sale una, aunque
        // sea el ✓— sino "el veredicto de esa caja no es ✓". Sin esta distinción, la columna diría "si"
        // sobre una frase que dice que está todo bien.
        bool triggers = f != nullptr;
        for (const auto& d : rep.devices)
            if (d.id == c.id) triggers = (d.verdict != telescope::devices::DeviceVerdict::ok);
        // Las informativas (tonalidad, plataforma, dispositivos en ✓) SALEN siempre que haya dato: para
        // ellas "no dispara con material sano" quiere decir "no dice nada" o "dice que está bien".
        const auto* hf = findingFor (healthyRep, c.id);
        const bool  healthyFires = hf != nullptr
                                && (hf->severity != Severity::info
                                    || c.id == RuleId::key || c.id == RuleId::platform);

        std::printf ("VERDICT[regla]  %-42s  %-7s  %-4s  %s\n", c.label,
                     triggers ? "si" : "NO", healthyFires ? "SI" : "no",
                     triggers ? f->text.c_str() : "(no salio)");

        REQUIRE (triggers);
        REQUIRE_FALSE (healthyFires);
        REQUIRE_FALSE (f->text.empty());
        REQUIRE_FALSE (f->evidence.empty());
        // La frase lleva el número que la sostiene: al menos un dígito.
        REQUIRE (f->text.find_first_of ("0123456789") != std::string::npos);
        // Y ninguna plantilla sin resolver.
        REQUIRE (f->text.find ('{') == std::string::npos);
    }
    std::printf ("\n");
}

// ========================================================================================================
TEST_CASE ("telescope: las seis lenguas de VERDICT renderizan sin plantillas sueltas",
           "[telescope][verdict]")
{
    // Una entrada que dispara TODO lo que se pueda disparar a la vez: así el barrido de idiomas ve el
    // máximo de frases posible, no las tres que salen con material sano.
    auto rows = healthyRows();
    auto agg  = healthyAggregates();
    applyAllDefects (rows, agg);
    const auto healthyIn = healthyRows();
    const auto healthyAg = healthyAggregates();

    int totalPhrases = 0;
    for (int l = 0; l < telescope::rules::kNumLanguages; ++l)
    {
        const auto& lang = telescope::rules::kLanguages[l];
        auto in = inputsFor (rows, agg, lang.code);
        in.targetIndex = 1;   // Spotify
        const auto rep = Verdict::evaluate (in);

        int braces = 0;
        for (const auto& f : rep.findings)
        {
            ++totalPhrases;
            if (f.text.find ('{') != std::string::npos)
            {
                ++braces;
                std::printf ("VERDICT[plantillas] %s: SIN RESOLVER -> %s\n", lang.code, f.text.c_str());
            }
            REQUIRE_FALSE (f.text.empty());
        }
        if (rep.footer.find ('{')      != std::string::npos) ++braces;
        if (rep.summary.text.find ('{') != std::string::npos) ++braces;

        std::printf ("VERDICT[plantillas] %s (%-10s) %2d frases  ·  %d sin resolver  ·  resumen: %s\n",
                     lang.code, lang.name, (int) rep.findings.size(), braces, rep.summary.text.c_str());
        REQUIRE (braces == 0);
        REQUIRE_FALSE (rep.footer.empty());
        REQUIRE (rep.findings.size() >= 10);

        // 57d — lo nuevo también se renderiza entero en las seis: el titular (con defectos y sano) y cada
        // línea de "Dentro de rango" (con material sano, que es cuando salen todas).
        auto healthyLang = inputsFor (healthyIn, healthyAg, lang.code);
        const auto healthyRep = Verdict::evaluate (healthyLang);
        int newBraces = 0;
        if (rep.summary.headline.find ('{') != std::string::npos)        ++newBraces;
        if (healthyRep.summary.headline.find ('{') != std::string::npos) ++newBraces;
        for (const auto& s : healthyRep.strengths)
            if (s.text.find ('{') != std::string::npos)
            {
                ++newBraces;
                std::printf ("VERDICT[plantillas] %s: SIN RESOLVER -> %s\n", lang.code, s.text.c_str());
            }
        totalPhrases += (int) healthyRep.strengths.size() + 2;
        std::printf ("VERDICT[plantillas] %s  titular \"%s\"  ·  %d lineas dentro de rango  ·  %d sin resolver\n",
                     lang.code, rep.summary.headline.c_str(), (int) healthyRep.strengths.size(), newBraces);
        REQUIRE (newBraces == 0);
        REQUIRE_FALSE (rep.summary.headline.empty());
        REQUIRE_FALSE (healthyRep.summary.headline.empty());
        REQUIRE (healthyRep.strengths.size() >= 8);
    }
    std::printf ("VERDICT[plantillas] %d frases renderizadas en %d lenguas\n",
                 totalPhrases, telescope::rules::kNumLanguages);

    // 57d — NINGUNA clave de VERDICT sale por fallback. Las nuevas (titular, "Dentro de rango", `hole.one`,
    // las `.ok`) tienen que estar en las seis tablas, no sólo en inglés: el fallback existe para que un
    // idioma incompleto no quede mudo, no para publicar una sección entera en inglés dentro del castellano.
    {
        const auto& en = telescope::rules::kLanguages[0];
        int missingKeys = 0;
        for (int l = 1; l < telescope::rules::kNumLanguages; ++l)
        {
            const auto& other = telescope::rules::kLanguages[l];
            for (int i = 0; i < en.count; ++i)
            {
                bool found = false;
                for (int k = 0; k < other.count && ! found; ++k)
                    found = std::string (other.table[k].key) == en.table[i].key;
                if (! found)
                {
                    ++missingKeys;
                    std::printf ("VERDICT[plantillas] %s no tiene la clave `%s`\n", other.code, en.table[i].key);
                }
            }
        }
        std::printf ("VERDICT[plantillas] %d claves x %d lenguas  ·  %d sin traducir\n", en.count,
                     telescope::rules::kNumLanguages, missingKeys);
        REQUIRE (missingKeys == 0);
    }

    // ---- fallback: una clave que sólo existe en inglés sale en inglés en las otras cinco ----
    for (int l = 0; l < telescope::rules::kNumLanguages; ++l)
    {
        const auto& lang = telescope::rules::kLanguages[l];
        // `platform.only` está en las seis; se usa una clave INVENTADA para probar el camino de fallback
        // sin tener que mutilar la tabla.
        const auto missing = Verdict::translate ("clave.que.no.existe", lang.code);
        REQUIRE (missing == "clave.que.no.existe");   // ni vacío ni una llave: la clave misma, visible
    }
    // Y el fallback de verdad: `pt` no tiene la clave `hi-fi.ok` si se la busca con otro nombre; se
    // comprueba con una clave que SÍ existe sólo en inglés por construcción de la tabla.
    std::printf ("VERDICT[fallback] idioma inexistente -> %s  ·  clave inexistente -> visible, no vacia\n",
                 Verdict::translate ("footer", "xx").c_str());
    REQUIRE (Verdict::translate ("footer", "xx") == Verdict::translate ("footer", "en"));
    REQUIRE (Verdict::translate ("footer", nullptr) == Verdict::translate ("footer", "en"));

    // ---- determinismo ----
    auto in1 = inputsFor (rows, agg, "es");
    in1.targetIndex = 1;
    const auto a = Verdict::evaluate (in1);
    const auto b = Verdict::evaluate (in1);
    REQUIRE (a.findings.size() == b.findings.size());
    for (size_t i = 0; i < a.findings.size(); ++i)
    {
        REQUIRE (a.findings[i].text     == b.findings[i].text);
        REQUIRE (a.findings[i].evidence == b.findings[i].evidence);
        REQUIRE (a.findings[i].t0       == b.findings[i].t0);
        REQUIRE (a.findings[i].band     == b.findings[i].band);
    }
    std::printf ("VERDICT[determinismo] dos evaluaciones: %d hallazgos identicos, caracter por caracter\n",
                 (int) a.findings.size());

    // ---- sin NaN, en ningún número publicado ----
    int checked = 0;
    for (const auto& f : a.findings)
        for (const float v : f.values) { REQUIRE_FALSE (std::isnan (v)); ++checked; }
    for (const auto& d : a.devices) { REQUIRE_FALSE (std::isnan (d.value)); REQUIRE_FALSE (std::isnan (d.value2)); checked += 2; }
    for (const auto& p : a.platforms) { REQUIRE_FALSE (std::isnan (p.deltaDb)); ++checked; }
    REQUIRE_FALSE (std::isnan (a.summary.integrated));
    std::printf ("VERDICT[nan] %d numeros publicados, ninguno NaN\n", checked);
}

// ========================================================================================================
// EL CAMINO COMPLETO, con audio de verdad: la señal con el hueco de 0:20 a 0:35 y el tramo bajo de 0:40 a
// 0:50 (TestDefectSignal.h), analizada POR ARCHIVO y EN VIVO. VERDICT tiene que encontrar los dos
// defectos donde están, y los dos informes tienen que ser el mismo informe.
// ========================================================================================================
TEST_CASE ("telescope: VERDICT encuentra el hueco donde esta, y el archivo dice lo mismo que el vivo",
           "[telescope][verdict]")
{
    using telescope::test::kDefectHoleFrom;
    using telescope::test::kDefectHoleTo;
    using telescope::test::kDefectQuietFrom;
    using telescope::test::kDefectQuietTo;
    constexpr double kSr = telescope::test::kDefectSr;

    const auto signal = telescope::test::makeDefectSignal();
    const auto wav = telescope::test::writeWav ("verdict_60s.wav", kSr, 2,
                                                (juce::int64) signal.getNumSamples(),
                                                [&] (juce::int64 i)
                                                {
                                                    return std::pair<float, float> {
                                                        signal.getSample (0, (int) i),
                                                        signal.getSample (1, (int) i) };
                                                });
    double srRead = 0.0;
    const auto audio = telescope::test::readWavStereo (wav, srRead);
    REQUIRE (srRead == kSr);

    // ---------- el archivo ----------
    telescope::FileAnalyzer fa;
    fa.start (wav);
    REQUIRE (telescope::test::waitUntil ([&] { return ! fa.busy(); }, 180000));
    const auto off = fa.result();
    REQUIRE (off.valid);
    REQUIRE ((int) off.secondRows.size() == 60);

    Verdict::Inputs fileIn;
    fileIn.aggregates = Verdict::aggregatesFrom (off);
    fileIn.rows       = off.secondRows.data();
    fileIn.n          = (int) off.secondRows.size();
    fileIn.language   = "es";
    const auto fileRep = Verdict::evaluate (fileIn);

    // ---------- el vivo ----------
    std::vector<SecondRow> liveRows;
    AnalysisFrame liveAgg;
    {
        telescope::TelescopeProcessor proc;
        proc.prepareToPlay (kSr, 512);
        proc.setEnabledModules (telescope::kLoudness | telescope::kReference | telescope::kCqt);

        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        const int n = audio.getNumSamples();
        for (int done = 0; done < n; done += 512)
        {
            const int k = juce::jmin (512, n - done);
            buf.clear();
            for (int c = 0; c < 2; ++c) buf.copyFrom (c, 0, audio, c, done, k);
            buf.setSize (2, k, true, false, true);
            proc.processBlock (buf, midi);
            buf.setSize (2, 512, false, false, true);
            const double pushed = (double) (done + k) / kSr;
            if (pushed - proc.analysis().read().timeSeconds > 2.0)
                REQUIRE (telescope::test::waitUntil (
                    [&] { return pushed - proc.analysis().read().timeSeconds <= 1.0; }, 60000));
        }
        REQUIRE (telescope::test::waitStable (
            [&] { return (double) proc.analysis().read().secondsAnalysed; }, 300, 60000));

        proc.secondHistory().copyLatest (liveRows, telescope::SecondHistory::kCapacity);
        liveAgg = proc.analysis().read();
        proc.releaseResources();
    }
    REQUIRE ((int) liveRows.size() == 60);

    Verdict::Inputs liveIn;
    liveIn.aggregates = liveAgg;
    liveIn.rows       = liveRows.data();
    liveIn.n          = (int) liveRows.size();
    liveIn.language   = "es";
    const auto liveRep = Verdict::evaluate (liveIn);

    // ---------- VERDICT[hueco]: UN hallazgo, donde está el defecto ----------
    // EL HUECO QUE SE LE SACÓ A LA SEÑAL VA DE 400 A 1 000 Hz (ver TestDefectSignal.h), así que las bandas
    // de ⅓ de octava que pueden caer ≥ 6 dB son las de 400 a 1 000 inclusive — HISTORY[hueco] las midió una
    // por una: 400 Hz -11.5 dB, 500 -20.2, 630 -28.5, 800 -20.2, 1 000 -11.5, y las de al lado por debajo
    // del umbral. Hasta b777c08 VERDICT las decía UNA POR BANDA: cinco frases para un solo evento. Medido en
    // b777c08, en este mismo test: 400 Hz 9.2 dB · 500 17.7 · 630 26.0 · 800 17.7 · 1 000 9.1, las cinco
    // entre 0:20 y 0:35.
    //
    // ===== 57d · SE FUSIONAN, y ésta es la derivación del hallazgo que queda =====
    // Bandas contiguas (índices consecutivos de ⅓ de octava) cuyos tramos [t0, t1) se solapan son UN hueco.
    // Las cinco de arriba son contiguas (12..16) y tienen el mismo tramo, así que:
    //   · band      = la PEOR del tramo                        → 630 Hz
    //   · values[0] = su caída media en la racha              → 25.96 dB (el número de la línea de 630)
    //   · values[1] = la banda más baja del tramo             → 400 Hz
    //   · values[2] = la más alta                             → 1 000 Hz
    //   · values[3] = los segundos de la racha de la peor     → 15
    //   · t0 / t1   = la UNIÓN de las ventanas del tramo      → 0:20 / 0:35 (acá las cinco son iguales; ver
    //                 el test del tramo corrido, abajo, para cuando no lo son)
    //   · severidad ⚠ (warn): un pozo medido es algo a revisar; ● queda para lo objetivamente roto.
    // El prompt 57d esperaba 400/800 porque en la captura verdict_M de b777c08 se ven CUATRO: la quinta
    // (1 000 Hz) y el tramo bajo quedaban debajo del pliegue de la lista. El informe tenía cinco.
    int holes = 0;
    const telescope::VerdictFinding* hole = nullptr;
    for (const auto& f : liveRep.findings)
    {
        if (f.ruleId != RuleId::hole) continue;
        ++holes;
        hole = &f;
        std::printf ("VERDICT[hueco] %6.0f Hz (tramo %.0f-%.0f Hz)  ·  %d s -> %d s (esperado %d -> %d)  ·  "
                     "%.2f dB / %.0f s  ·  %s  ·  [%s]\n",
                     telescope::kThirdOctaveHz[f.band], f.values[1], f.values[2], f.t0, f.t1,
                     kDefectHoleFrom, kDefectHoleTo, f.values[0], f.values[3], f.text.c_str(), f.evidence.c_str());
    }
    std::printf ("VERDICT[hueco] %d hallazgo(s) de hueco (hasta b777c08: 5, uno por banda)\n", holes);
    REQUIRE (holes == 1);
    REQUIRE (hole != nullptr);
    REQUIRE (std::abs (telescope::kThirdOctaveHz[hole->band] - 630.0) < 1.0);
    REQUIRE (std::abs (hole->t0 - kDefectHoleFrom) <= 1);
    REQUIRE (std::abs (hole->t1 - kDefectHoleTo) <= 1);
    REQUIRE (hole->text.find ("0:20") != std::string::npos);
    REQUIRE (hole->text.find ("0:35") != std::string::npos);
    REQUIRE (std::abs (hole->values[0] - 25.96f) < 0.5f);
    REQUIRE (std::abs (hole->values[1] - 400.0f) < 1.0f);
    REQUIRE (std::abs (hole->values[2] - 1000.0f) < 1.0f);
    REQUIRE (hole->values[3] == 15.0f);
    REQUIRE (hole->severity == Severity::warn);
    // La evidencia dice también la regla: los dos números de `Rule` (6 dB durante 10 s).
    REQUIRE (hole->evidence.find ("rule 6 dB / 10 s") != std::string::npos);

    // ---------- y el tramo bajo también ----------
    const auto* quiet = findingFor (liveRep, RuleId::quietSection);
    REQUIRE (quiet != nullptr);
    std::printf ("VERDICT[tramo] %d s -> %d s (esperado %d -> %d)  ·  %s\n",
                 quiet->t0, quiet->t1, kDefectQuietFrom, kDefectQuietTo, quiet->text.c_str());
    REQUIRE (std::abs (quiet->t0 - kDefectQuietFrom) <= 2);
    REQUIRE (std::abs (quiet->t1 - kDefectQuietTo)   <= 2);

    // ---------- VERDICT[archivo]: el mismo informe, frase por frase ----------
    std::printf ("VERDICT[archivo] archivo %d hallazgos  ·  vivo %d hallazgos\n",
                 (int) fileRep.findings.size(), (int) liveRep.findings.size());
    REQUIRE (fileRep.findings.size() == liveRep.findings.size());
    for (size_t i = 0; i < fileRep.findings.size(); ++i)
    {
        const auto& a = fileRep.findings[i];
        const auto& b = liveRep.findings[i];
        if (a.text != b.text)
            std::printf ("VERDICT[archivo] DISTINTAS:\n  archivo: %s\n  vivo   : %s\n",
                         a.text.c_str(), b.text.c_str());
        REQUIRE (a.ruleId   == b.ruleId);
        REQUIRE (a.t0       == b.t0);
        REQUIRE (a.t1       == b.t1);
        REQUIRE (a.band     == b.band);
        REQUIRE (a.text     == b.text);
        REQUIRE (a.evidence == b.evidence);
    }
    for (int d = 0; d < telescope::devices::kNumDevices; ++d)
    {
        REQUIRE (fileRep.devices[d].verdict == liveRep.devices[d].verdict);
        REQUIRE (fileRep.devices[d].value   == liveRep.devices[d].value);
    }
    REQUIRE (fileRep.summary.text == liveRep.summary.text);
    REQUIRE (fileRep.footer       == liveRep.footer);
    // 57d — el titular y "Dentro de rango" también, frase por frase: salen de las mismas filas.
    REQUIRE (fileRep.summary.headline         == liveRep.summary.headline);
    REQUIRE (fileRep.summary.headlineEvidence == liveRep.summary.headlineEvidence);
    REQUIRE (fileRep.strengths.size() == liveRep.strengths.size());
    for (size_t i = 0; i < fileRep.strengths.size(); ++i)
    {
        REQUIRE (fileRep.strengths[i].text     == liveRep.strengths[i].text);
        REQUIRE (fileRep.strengths[i].evidence == liveRep.strengths[i].evidence);
    }
    std::printf ("VERDICT[archivo] titular \"%s\"  ·  %d lineas dentro de rango\n",
                 liveRep.summary.headline.c_str(), (int) liveRep.strengths.size());
    std::printf ("VERDICT[archivo] los dos informes son IDENTICOS, frase por frase\n");

    wav.deleteFile();
}

// ========================================================================================================
// CON REFERENCIA CARGADA, la sección 1 compara contra ELLA y no contra la tendencia del propio material.
// Es la diferencia entre "esta región no se parece al resto de tu mezcla" y "esta región no se parece a
// la mezcla que elegiste como objetivo" — y la frase dice cuál de las dos cosas está diciendo.
// ========================================================================================================
TEST_CASE ("telescope: con referencia cargada VERDICT compara contra la referencia", "[telescope][verdict]")
{
    const auto rows = healthyRows();
    const auto agg  = healthyAggregates();

    telescope::ReferenceFrame ref;
    ref.refValid = true;
    for (int b = 0; b < kBands; ++b) { ref.refBands[b] = -25.0f; ref.bandValid[b] = true; }

    // ---- 1 · la referencia es el MISMO rosa: no hay nada que reportar ----
    {
        auto in = inputsFor (rows, agg);
        in.ref = &ref;
        const auto rep = Verdict::evaluate (in);
        std::printf ("VERDICT[referencia] ref = el mismo rosa  ·  usa referencia: %s  ·  delgado: %s  ·  "
                     "%d hallazgos\n", rep.summary.usedReference ? "si" : "NO",
                     fired (rep, RuleId::thin) ? "SI" : "no", hallazgos (rep));
        REQUIRE (rep.summary.usedReference);
        REQUIRE_FALSE (fired (rep, RuleId::thin));
        REQUIRE (hallazgos (rep) == 0);
    }

    // ---- 2 · la referencia tiene 6 dB MÁS de cuerpo: el programa esta delgado CONTRA ELLA ----
    {
        auto withBody = ref;
        for (int b = 8; b <= 12; ++b) withBody.refBands[b] = -19.0f;   // 160-400 Hz, +6 dB

        auto in = inputsFor (rows, agg);
        in.ref = &withBody;
        const auto rep = Verdict::evaluate (in);
        const auto* f = findingFor (rep, RuleId::thin);
        std::printf ("VERDICT[referencia] ref con +6 dB de cuerpo  ·  delgado: %s  ·  %s\n",
                     f != nullptr ? "SI" : "no", f != nullptr ? f->text.c_str() : "(no salio)");
        REQUIRE (f != nullptr);
        REQUIRE (f->values[0] >= 3.0f);
        // Y la frase DICE contra qué comparó: es la mitad del valor del hallazgo.
        REQUIRE (f->text.find (Verdict::translate ("baseline.reference", "en")) != std::string::npos);
    }

    // ---- 3 · sin referencia, la misma entrada compara contra la tendencia y lo dice ----
    {
        const auto rep = Verdict::evaluate (inputsFor (rows, agg));
        REQUIRE_FALSE (rep.summary.usedReference);
        auto muddy = healthyRows();
        for (auto& r : muddy) for (int b = 9; b <= 13; ++b) r.bandsDb[b] += 8.0f;
        const auto rep2 = Verdict::evaluate (inputsFor (muddy, agg));
        const auto* f = findingFor (rep2, RuleId::muddy);
        REQUIRE (f != nullptr);
        REQUIRE (f->text.find (Verdict::translate ("baseline.trend", "en")) != std::string::npos);
        std::printf ("VERDICT[referencia] sin referencia  ·  %s\n", f->text.c_str());
    }
}

// ========================================================================================================
// 56b · CADA REGLA DE FORMA, CON AUDIO DE VERDAD (M1 del revisor del 55)
//
// El caso "cada regla de VERDICT se dispara…" inyecta campos en SecondRow/AnalysisFrame: prueba la TABLA
// DE DECISIÓN, que es lo que tiene que probar, pero no prueba que una mezcla con ese defecto produzca esos
// campos. Sólo `hole` y `quiet-section` iban de punta a punta. Acá van las nueve que faltaban: se sintetiza
// una señal POR REGLA, se la empuja por FileAnalyzer —el mismo motor, offline, mucho más rápido que
// processBlock— y se le pide el informe.
//
// ================== POR QUÉ EL NÚMERO QUE SALE NO ES EL QUE SE INYECTA ==================
//
// Las reglas de forma no miran el nivel de una banda: miran el RESIDUO contra la tendencia del propio
// material (`shapeOf` menos `trendOf`, Verdict.cpp). Y la tendencia es una recta de mínimos cuadrados
// sobre log2(f) ajustada entre 50 Hz y 16 kHz — las 26 bandas de kFitLo. O sea que subir R bandas Δ dB
// TAMBIÉN sube la recta, y el residuo que queda es menor que Δ.
//
// Cuánto menos, exacto. Para un ajuste por mínimos cuadrados, sumarle Δ a las R bandas de una región mueve
// el valor ajustado en el punto x según
//
//     Δ_recta(x) = Δ · [ R/N  +  (x − x̄) · Σ_{i∈R}(x_i − x̄) / Σ_i (x_i − x̄)² ]
//
// con N = 26 bandas ajustadas y x = log2(f). El primer término es el corrimiento del promedio (la recta
// sube parejo) y el segundo el de la pendiente (la recta se inclina hacia la región). Los dos son
// POSITIVOS dentro de la región —la recta persigue al bulto—, así que el residuo siempre es una FRACCIÓN
// de Δ. Para las cinco bandas de kBody (160–400 Hz, log2 medio ≈ 7.97 contra x̄ ≈ 9.8 del ajuste):
//
//     R/N = 5/26 = 0.19    ·    (x − x̄)·Σ(x_i − x̄)/Σ(x_i − x̄)² ≈ (−1.83)(−9.15)/162.5 = 0.10
//     Δ_recta ≈ 0.29·Δ   →   residuo ≈ 0.71·Δ
//
// Por eso las señales llevan 10–12 dB para reglas cuyo umbral es 3: con 4 dB nominales el residuo quedaría
// en 2.8 y la regla NO se dispararía, y el test estaría midiendo el filtro y no la regla. Lo que se exige
// es justamente eso: que el número reportado sea una fracción del nominal (la tendencia se comió el
// resto) y que aun así pase el umbral.
//
// Los filtros tienen falda, así que la fracción real es algo menor que la teórica: se pide un rango ancho
// (0.25–0.95 del nominal) porque lo que se está probando es la REGLA, no el diseño del filtro.
// ========================================================================================================
namespace
{
constexpr double kSr = telescope::test::kDefectSr;

// UN ECUALIZADOR GRÁFICO DE ⅓ DE OCTAVA. Una campana por banda de la región, en el centro ISO 266 de esa
// banda y con el Q de ⅓ de octava (4.32): es la forma de mover exactamente las bandas que la regla mira.
//
// Se probó primero con un pasabanda en cascada (4 biquads por lado, como DefectCascade) y NO servía: sobre
// 160–400 Hz —1.3 octavas— la cascada llega a −6 dB en el centro de su propia banda, así que un "−12 dB"
// nominal daba −3 dB reales y ninguna regla se disparaba. Queda anotado porque el error es fácil de
// repetir: cuanto más angosta la banda, menos parecido tiene una cascada Butterworth a un escalón.
void shapeBands (juce::AudioBuffer<float>& buf, double sr, int bandLo, int bandHi, float dB)
{
    constexpr float kThirdOctaveQ = 4.32f;   // Q de una banda de ⅓ de octava
    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
    {
        auto* p = buf.getWritePointer (ch);
        for (int b = bandLo; b <= bandHi; ++b)
        {
            juce::dsp::IIR::Filter<float> peak;
            peak.coefficients = juce::dsp::IIR::Coefficients<float>::makePeakFilter (
                sr, (float) telescope::kThirdOctaveHz[b], kThirdOctaveQ, std::pow (10.0f, dB / 20.0f));
            peak.reset();
            for (int i = 0; i < buf.getNumSamples(); ++i) p[i] = peak.processSample (p[i]);
        }
    }
}

// Cuánto se movió DE VERDAD la región, medido contra el sano. Es el "nominal" honesto: no lo que se le
// pidió al filtro, sino lo que el mismo analizador midió que cambió.
double appliedChange (const telescope::FileAnalysis& shaped, const telescope::FileAnalysis& healthy,
                      int bandLo, int bandHi)
{
    double sum = 0.0; int n = 0;
    for (int b = bandLo; b <= bandHi; ++b) { sum += shaped.bandsDb[b] - healthy.bandsDb[b]; ++n; }
    return n > 0 ? sum / (double) n : 0.0;
}

// Rosa estéreo sano: L = R + un poco de ruido independiente (corr ≈ 0.9), a ≈ −14 LUFS. Es la base de
// TODAS las señales de acá: cada defecto es esta señal más una sola cosa.
juce::AudioBuffer<float> pinkBase (double seconds)
{
    const auto total = (int) std::llround (seconds * kSr);
    juce::AudioBuffer<float> buf (2, total);
    const float peak = std::pow (10.0f, -2.4f / 20.0f);
    telescope::test::Pink mid { telescope::test::kPinkSeedA },
                          sL  { telescope::test::kPinkSeedB },
                          sR  { telescope::test::kPinkSeedB ^ 0x5bd1e995u };
    for (int i = 0; i < total; ++i)
    {
        const float m = mid.next();
        buf.setSample (0, i, peak * (m + 0.33f * sL.next()) * 0.95f);
        buf.setSample (1, i, peak * (m + 0.33f * sR.next()) * 0.95f);
    }
    return buf;
}

// El informe de un buffer, por el camino de ARCHIVO (el mismo motor, offline).
VerdictReport reportOf (const juce::AudioBuffer<float>& b, const juce::String& name,
                        telescope::FileAnalysis& outAnalysis)
{
    const auto wav = telescope::test::writeWav (name, kSr, 2, (juce::int64) b.getNumSamples(),
                                                [&] (juce::int64 i)
                                                {
                                                    return std::pair<float, float> { b.getSample (0, (int) i),
                                                                                     b.getSample (1, (int) i) };
                                                });
    telescope::FileAnalyzer fa;
    fa.start (wav);
    REQUIRE (telescope::test::waitUntil ([&] { return ! fa.busy(); }, 180000));
    outAnalysis = fa.result();
    REQUIRE (outAnalysis.ok);
    REQUIRE (outAnalysis.secondRows.size() > 10);

    Verdict::Inputs in;
    in.aggregates = Verdict::aggregatesFrom (outAnalysis);
    in.rows       = outAnalysis.secondRows.data();
    in.n          = (int) outAnalysis.secondRows.size();
    in.language   = "en";
    in.targetIndex = 0;
    in.clipThresholdDbtp = -1.0f;
    return Verdict::evaluate (in);
}
}

TEST_CASE ("telescope: las reglas de forma de VERDICT se disparan con AUDIO, no con campos inyectados",
           "[telescope][verdict]")
{
    constexpr double kSecs = 20.0;

    // ---- 0 · EL SANO DE VERDAD: rosa a -14 LUFS, L = R + ruido chico, sin huecos ----
    telescope::FileAnalysis healthy;
    {
        telescope::FileAnalysis& a = healthy;
        const auto rep = reportOf (pinkBase (kSecs), "verdict_real_healthy.wav", a);
        std::printf ("VERDICT[audio] sano: I %.2f LUFS  ·  %d filas  ·  %d hallazgos (criterio 0)\n",
                     a.integratedLufs, (int) a.secondRows.size(), hallazgos (rep));
        for (const auto& f : rep.findings)
            if (f.severity != Severity::info)
                std::printf ("VERDICT[audio] sano FALSO POSITIVO: %s  [%s]\n", f.text.c_str(), f.evidence.c_str());
        REQUIRE (a.integratedLufs > -18.0f);
        REQUIRE (a.integratedLufs < -10.0f);
        REQUIRE (hallazgos (rep) == 0);
    }

    // ---- 1 · las cinco reglas de FORMA y las tres de DISPOSITIVO que salen del residuo ----
    // Las regiones son LAS DE Verdict.cpp (kBody, kLowMids, kPresence, kAir, kLaptop, kSubCar), por índice
    // de banda de ⅓ de octava: se mueve exactamente lo que la regla mira, ni una banda más.
    struct Shaped { const char* label; RuleId id; int lo, hi; float dB; };
    const Shaped kShaped[] = {
        //  regla                 bandas de la región (Verdict.cpp)      dB pedidos
        { "thin",     RuleId::thin,       8, 12, -10.0f },   // kBody      160-400 Hz
        { "muddy",    RuleId::muddy,      9, 13, +10.0f },   // kLowMids   200-500 Hz
        { "harsh",    RuleId::harsh,     19, 23, +10.0f },   // kPresence  2-5 kHz
        { "no-air",   RuleId::noAir,     26, 29, -12.0f },   // kAir       10-20 kHz
        { "laptop",   RuleId::devLaptop, 19, 22, -10.0f },   // kLaptop    2-4 kHz
        { "car",      RuleId::devCar,     0,  6, +10.0f },   // kSubCar    25-100 Hz
    };

    for (const auto& c : kShaped)
    {
        auto buf = pinkBase (kSecs);
        shapeBands (buf, kSr, c.lo, c.hi, c.dB);

        telescope::FileAnalysis a;
        const auto rep = reportOf (buf, juce::String ("verdict_real_") + c.label + ".wav", a);

        // El número que la regla reportó (values[0] es la magnitud medida en todas ellas).
        const auto* f = findingFor (rep, c.id);
        if (f == nullptr)
        {
            std::printf ("VERDICT[audio] %s: la regla NO aparece. Informe completo:\n", c.label);
            for (const auto& x : rep.findings)
                std::printf ("    [%d] %s  |  %s\n", (int) x.severity, x.text.c_str(), x.evidence.c_str());
            for (const auto& d : rep.devices)
                std::printf ("    dev %s = %d (%.2f)\n", d.key, (int) d.verdict, d.value);
        }
        REQUIRE (f != nullptr);
        const float  measured = f->values[0];
        const double applied  = appliedChange (a, healthy, c.lo, c.hi);
        const double fraction = std::abs ((double) measured) / std::abs (applied);

        std::printf ("VERDICT[audio] %-8s bandas %d-%d: se pidieron %+.0f dB, el analizador midio %+.2f  ->  "
                     "residuo contra la tendencia %+.2f dB (%.0f %%; la recta se comio el resto)  ·  %s\n",
                     c.label, c.lo, c.hi, c.dB, applied, measured, 100.0 * fraction, f->evidence.c_str());

        INFO (c.label);
        REQUIRE (std::abs (applied) > 4.0);   // el filtro movió la región de verdad…
        REQUIRE (fraction > 0.30);            // …el residuo existe…
        REQUIRE (fraction < 1.00);            // …y es MENOR que lo aplicado: la recta persiguió al bulto
        if (c.id == RuleId::devLaptop || c.id == RuleId::devCar)
        {
            // Las de dispositivo no son "hallazgos": son pronósticos con veredicto.
            bool failed = false;
            for (const auto& d : rep.devices) if (d.id == c.id) failed = d.verdict == telescope::devices::DeviceVerdict::fail;
            REQUIRE (failed);
        }
        else
        {
            REQUIRE (fired (rep, c.id));
        }
    }

    // ---- 2 · APLASTADO: el PSR medio por debajo de 8 dB. No es espectral: es de dinámica ----
    {
        auto buf = pinkBase (kSecs);
        // Recorte duro a un tercio del pico: baja el pico real sin bajar el short-term, que es exactamente
        // lo que mide el PSR (pico real - short-term). El rosa sin recortar tiene PSR ~12 dB.
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* p = buf.getWritePointer (ch);
            for (int i = 0; i < buf.getNumSamples(); ++i)
                p[i] = juce::jlimit (-0.16f, 0.16f, p[i]) * 4.0f;
        }
        telescope::FileAnalysis a;
        const auto rep = reportOf (buf, "verdict_real_crushed.wav", a);
        const auto* f = findingFor (rep, RuleId::crushed);
        REQUIRE (f != nullptr);
        std::printf ("VERDICT[audio] crushed   recorte duro  ->  PSR medio %.2f dB (umbral %.1f)  ·  %s\n",
                     f->values[0], f->values[1], f->evidence.c_str());
        REQUIRE (fired (rep, RuleId::crushed));
        REQUIRE (f->values[0] < 8.0f);
    }

    // ---- 3 · CLUB: el sub se cancela al monoficar (monoLoss <= -3 dB por debajo de 120 Hz) ----
    {
        auto buf = pinkBase (kSecs);
        // Se le resta a R el doble de su propio grave: por debajo de 120 Hz R queda invertido respecto de
        // L, así que la suma mono se cancela ahí y sólo ahí.
        //
        // El corte va en 400 Hz y no en 120: la cascada de cuatro secciones ya está 6 dB abajo bastante
        // antes de su nominal, así que con 110 Hz la inversión sólo alcanzaba a la mitad de la amplitud en
        // 100 Hz y la pérdida medida daba -1.5 dB en vez de cancelar. La regla mira 25-100 Hz (kSubClub):
        // lo que hace falta es que TODA esa región quede adentro del pasabajos.
        telescope::test::DefectCascade lp;
        lp.prepare (juce::dsp::IIR::Coefficients<float>::makeLowPass (kSr, 400.0f));
        auto* r = buf.getWritePointer (1);
        for (int i = 0; i < buf.getNumSamples(); ++i) r[i] -= 2.0f * lp.process (r[i]);

        telescope::FileAnalysis a;
        const auto rep = reportOf (buf, "verdict_real_club.wav", a);
        float monoLoss = 0.0f;
        bool  failed = false;
        for (const auto& d : rep.devices)
            if (d.id == RuleId::devClub) { monoLoss = d.value; failed = d.verdict == telescope::devices::DeviceVerdict::fail; }
        std::printf ("VERDICT[audio] club      sub invertido en R  ->  monoLoss medio %.2f dB (umbral -3.0)\n",
                     monoLoss);
        REQUIRE (monoLoss <= -3.0f);
        REQUIRE (failed);
    }

    // ---- 4 · CENTRO VACIO: medios angostos y agudos MUY anchos ----
    //
    // La regla pide las dos cosas a la vez (Verdict.cpp): width medio (315 Hz-2 kHz) <= 0.4 Y width agudo
    // (> 2.5 kHz) >= 1.2. La base ya cumple lo primero —L = R + un poco de ruido chico da width ~0.23 en
    // todas las bandas— así que el defecto es SÓLO lo segundo: hay que ensanchar los agudos.
    //
    // Cómo, con la cuenta: width = sqrt(SS/MM). Si por encima de 2.5 kHz se pone R = -0.8·L + 0.3·w (con
    // w un rosa independiente), entonces S = (L-R)/2 = (1.8·L - 0.3·w)/2 y M = (L+R)/2 = (0.2·L + 0.3·w)/2,
    // así que SS/MM = (1.8² + 0.3²)/(0.2² + 0.3²) = 3.33/0.13 = 25.6 y width = 5.06.
    //
    // POR QUÉ TAN ANCHO, y no el 2.07 que salía de -0.5·L + 0.6·w: la región que la regla promedia es
    // kHighWidth = 2.5-20 kHz (diez bandas) y el pasaaltos en cascada recién pasa de verdad desde ~5 kHz,
    // así que la mitad de abajo de la región queda como la base (width 0.23). Con 2.07 el promedio daba
    // 1.15 y el umbral es 1.2: se quedaba a cinco centésimas, midiendo la falda del filtro y no la regla.
    // Con 5.06 el promedio queda en ~2.6 y el criterio deja de depender del filtro. El precio es que los
    // agudos quedan casi en contrafase, así que `out-of-phase` también se dispara: es cierto y se imprime.
    {
        // UN SOLO BIQUAD y no la cascada de cuatro: con la cascada a 2500 Hz la banda de 2.5 kHz recibía
        // apenas 0.68 dB de pérdida mono y el promedio de la región 2.5-20 kHz se quedaba en 0.99 contra el
        // umbral de 1.2 — o sea que el test habría estado midiendo la falda del filtro. Un biquad de
        // segundo orden a 3 kHz deja pasar el 57 % en 2.5 kHz, el 87 % en 4 kHz y el 99 % de 8 kHz para
        // arriba: cubre la región de la regla sin llegar a los 2 kHz, que son de la región de MEDIOS y
        // tienen que quedarse angostos.
        auto buf = pinkBase (kSecs);
        telescope::test::Pink w { telescope::test::kPinkSeedA ^ 0x9e3779b9u };
        juce::dsp::IIR::Filter<float> hpL, hpR, hpW;
        for (auto* c : { &hpL, &hpR, &hpW })
        {
            c->coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass (kSr, 3000.0f);
            c->reset();
        }

        auto* L = buf.getWritePointer (0);
        auto* R = buf.getWritePointer (1);
        for (int i = 0; i < buf.getNumSamples(); ++i)
        {
            const float hiL = hpL.processSample (L[i]);
            const float hiR = hpR.processSample (R[i]);
            const float hiW = hpW.processSample (0.6f * w.next());
            R[i] += (-0.8f * hiL + 0.3f * hiW) - hiR;      // los agudos de R se reemplazan
        }

        telescope::FileAnalysis a;
        const auto rep = reportOf (buf, "verdict_real_hollow.wav", a);
        const auto* f = findingFor (rep, RuleId::hollowCentre);
        std::printf ("VERDICT[audio] hollow   agudos de R = -0.8 L + 0.3 indep  ->  %s\n",
                     f != nullptr ? f->evidence.c_str() : "(no se disparo)");
        for (const auto& x : rep.findings)
            if (x.severity != Severity::info && x.ruleId != RuleId::hollowCentre)
                std::printf ("VERDICT[audio] hollow   ademas (y es cierto): %s\n", x.evidence.c_str());
        REQUIRE (f != nullptr);
        REQUIRE (fired (rep, RuleId::hollowCentre));
    }
}

// ========================================================================================================
// 56b · EL DICCIONARIO DICE LO QUE CORRE (M2 del revisor del 55)
//
// `docs/telescope-diccionario.md` es el gemelo legible de rules::kRules[]: es lo que Joaquín lee para
// saber qué mide el plugin. Se había quedado atrás en dos reglas —`fuera de fase` y `auriculares`, a las
// que les faltaba la condición de la media ≤ −0.2 que SÍ corre en Verdict.cpp— y nada lo iba a notar
// hasta que alguien comparara los dos archivos a mano.
//
// Esto lo ata: recorre la tabla y exige que el `name` de cada regla y CADA UNO de sus umbrales numéricos
// aparezcan en el documento. No comprueba la prosa (eso no se puede automatizar); comprueba que ningún
// número de la tabla viva sin su renglón. Si mañana alguien cambia un umbral en Rules.h y no toca el
// diccionario, este test se pone rojo y dice cuál.
// ========================================================================================================
TEST_CASE ("telescope: el diccionario tiene el id y los umbrales de cada regla", "[telescope][verdict]")
{
    const auto doc = juce::File (juce::String (__FILE__)).getParentDirectory()
                         .getParentDirectory().getChildFile ("docs")
                         .getChildFile ("telescope-diccionario.md");
    INFO ("diccionario: " << doc.getFullPathName());
    REQUIRE (doc.existsAsFile());
    const auto text = doc.loadFileAsString();
    REQUIRE (text.length() > 4000);

    // Un umbral aparece "escrito" si está su valor con la cantidad de decimales que tenga sentido. 8.0 se
    // escribe "8" en prosa y -0.2 se escribe "-0.2" o "−0.2" (el guion largo tipográfico): las dos formas
    // valen, porque el diccionario es un texto para leer, no un volcado.
    const auto mentions = [&text] (float v)
    {
        if (std::abs (v) < 1.0e-6f) return true;                     // 0 = "la regla no tiene ese umbral"
        juce::StringArray forms;
        const double a = std::abs ((double) v);
        forms.add (juce::String (a, 0));                             // 8
        forms.add (juce::String (a, 1));                             // 8.0
        forms.add (juce::String (a, 2));                             // 0.20
        if (a < 1.0) forms.add (juce::String (a, 1).substring (1));   // .2
        if (a <= 1.0 + 1.0e-9) forms.add (juce::String (a * 100.0, 0));  // una fracción, escrita en %
        for (const auto& f : forms)
            if (text.contains (f)) return true;
        return false;
    };

    juce::StringArray missing;
    for (const auto& r : telescope::rules::kRules)
    {
        if (! text.contains (juce::String ("`") + r.name + "`"))
            missing.add (juce::String ("id `") + r.name + "` no esta en el diccionario");
        if (! mentions (r.threshold))
            missing.add (juce::String (r.name) + ": umbral " + juce::String (r.threshold, 2) + " no esta escrito");
        if (! mentions (r.threshold2))
            missing.add (juce::String (r.name) + ": umbral2 " + juce::String (r.threshold2, 2) + " no esta escrito");
    }

    std::printf ("VERDICT[diccionario] %d reglas verificadas contra %s  ·  %d faltantes\n",
                 (int) (sizeof (telescope::rules::kRules) / sizeof (telescope::rules::kRules[0])),
                 doc.getFileName().toRawUTF8(), missing.size());
    for (const auto& m : missing) std::printf ("VERDICT[diccionario]   FALTA: %s\n", m.toRawUTF8());

    INFO ("faltantes:\n" << missing.joinIntoString ("\n"));
    REQUIRE (missing.isEmpty());

    // Y las DOS correcciones concretas del M2, nombradas: la condición de la media que faltaba en las dos
    // reglas de fase. Si alguien la saca del documento, esto lo dice sin ambigüedad.
    CHECK (text.contains (juce::String::fromUTF8 ("media \xe2\x89\xa4 \xe2\x88\x92" "0.2")));   // "media <= -0.2"
    CHECK (text.contains ("out-of-phase"));
    CHECK (text.contains ("auriculares"));
}

// ========================================================================================================
// ===== 57d · LA MISMA MEDICIÓN, CONTADA CON PROPORCIÓN =====
//
// «Que el veredicto sea perfecto, que no tire tan para abajo al proyecto, o que sea más técnico» (Joaquín,
// 14-sep, con el 57c en su DAW). Lo que VERDICT mide no se toca: los umbrales son los mismos y cada frase
// sigue con su número y su regla. Lo que cambia es cómo lo cuenta, y estos tres tests atan esas tres cosas:
//
//   VERDICT[titular]  la primera línea es la CUENTA de lo medido, no un adjetivo;
//   VERDICT[rango]    lo que se evaluó y quedó dentro de rango también se dice, con su número;
//   VERDICT[tono]     ninguna tabla opina, y cada hallazgo dice primero el número y al final dónde mirar.
// ========================================================================================================
TEST_CASE ("telescope: VERDICT abre con un titular que cuenta lo medido", "[telescope][verdict]")
{
    const auto boxesOk = [] (const VerdictReport& rep)
    {
        int n = 0;
        for (const auto& d : rep.devices) if (d.verdict == telescope::devices::DeviceVerdict::ok) ++n;
        return n;
    };

    // ---- sano: todo lo evaluado quedó dentro de rango y no hay nada para revisar ----
    {
        const auto rows = healthyRows();
        const auto rep  = Verdict::evaluate (inputsFor (rows, healthyAggregates()));
        const auto& s   = rep.summary;
        std::printf ("VERDICT[titular] sano       n=%d (%d dentro de rango + %d cajas en ok)  m=%d  primero=%d  ·  "
                     "\"%s\"  ·  [%s]\n", s.checksWithin, (int) rep.strengths.size(), boxesOk (rep), s.toCheck,
                     s.firstAt, s.headline.c_str(), s.headlineEvidence.c_str());
        REQUIRE (s.toCheck == 0);
        REQUIRE (s.checksWithin >= 14);
        // La cuenta es la de la definición: cada línea de "Dentro de rango" más cada caja en ✓ con dato.
        REQUIRE (s.checksWithin == (int) rep.strengths.size() + boxesOk (rep));
        REQUIRE (s.firstAt == -1);
        REQUIRE_FALSE (s.headline.empty());
        REQUIRE (s.headline.find (std::to_string (s.checksWithin)) != std::string::npos);
        REQUIRE (s.headline.find ('{') == std::string::npos);
        REQUIRE (s.headlineEvidence == "headline \xc2\xb7 " + std::to_string (s.checksWithin) + "/0");
    }

    // ---- con defectos: m son los HALLAZGOS y el primero es el más temprano ----
    {
        auto rows = healthyRows();
        addDb (rows, 13, 13, -12.0f, 20, 35);                              // hueco en 500 Hz, 0:20-0:35
        for (int i = 40; i < 50; ++i) { rows[(size_t) i].shortTermMax = -20.0f;   // tramo bajo, 0:40-0:50
                                        rows[(size_t) i].shortTermMin = -20.5f; }
        for (int i = 12; i < 17; ++i) rows[(size_t) i].clipEvents = 2;      // ráfaga de picos, 0:12-0:17
        const auto rep = Verdict::evaluate (inputsFor (rows, healthyAggregates()));
        const auto& s  = rep.summary;

        int earliest = -1;
        for (const auto& f : rep.findings)
            if (f.severity != Severity::info && f.t0 >= 0 && (earliest < 0 || f.t0 < earliest)) earliest = f.t0;

        std::printf ("VERDICT[titular] defectos   n=%d  m=%d (hallazgos %d, lineas en findings %d)  primero=%d "
                     "(el mas temprano %d)  ·  \"%s\"  ·  [%s]\n", s.checksWithin, s.toCheck, hallazgos (rep),
                     (int) rep.findings.size(), s.firstAt, earliest, s.headline.c_str(),
                     s.headlineEvidence.c_str());
        // m = los HALLAZGOS: ⚠ y ● (diccionario §3). `findings.size()` NO sirve de cuenta: ese vector también
        // lleva las seis cajas de "Dónde traduce" y las líneas ○ informativas, que no son "para revisar".
        REQUIRE (s.toCheck == hallazgos (rep));
        REQUIRE (s.toCheck == 3);                              // el hueco, el tramo bajo y la ráfaga
        REQUIRE (s.firstAt == earliest);
        REQUIRE (s.firstAt == 12);                             // la ráfaga (0:12) va antes que el hueco (0:20)
        REQUIRE (s.headline.find (Verdict::timeLabel (12)) != std::string::npos);
        REQUIRE (s.headline.find (std::to_string (s.toCheck)) != std::string::npos);
        REQUIRE (s.checksWithin == (int) rep.strengths.size() + boxesOk (rep));
        REQUIRE (s.headlineEvidence == "headline \xc2\xb7 " + std::to_string (s.checksWithin) + "/3");
    }

    // ---- un solo hallazgo: la frase del singular ----
    {
        auto rows = healthyRows();
        addDb (rows, 13, 13, -12.0f, 20, 35);
        const auto rep = Verdict::evaluate (inputsFor (rows, healthyAggregates()));
        std::printf ("VERDICT[titular] uno        \"%s\"\n", rep.summary.headline.c_str());
        REQUIRE (rep.summary.toCheck == 1);
        REQUIRE (rep.summary.firstAt == 20);
        REQUIRE (rep.summary.headline.find ("1 to look at") != std::string::npos);
        REQUIRE (rep.summary.headline.find ("0:20") != std::string::npos);
    }

    // ---- sin un solo segundo analizado: no se cuenta lo que no se midió ----
    {
        const std::vector<SecondRow> none;
        auto agg = healthyAggregates();
        agg.secondsAnalysed = 0;
        const auto rep = Verdict::evaluate (inputsFor (none, agg));
        std::printf ("VERDICT[titular] sin filas  n=%d  m=%d  ·  titular \"%s\" (vacio)  ·  %d lineas dentro de rango\n",
                     rep.summary.checksWithin, rep.summary.toCheck, rep.summary.headline.c_str(),
                     (int) rep.strengths.size());
        // Revisor del 57d (HIGH): sin filas, la caja del celular sale ⚠ sin dato —su armónico relativo vale −60
        // por defecto, comportamiento anterior al 57d— y la primera versión del titular la contaba como "para
        // revisar". Sin un segundo analizado no se cuenta NADA: ni n, ni m, ni "el primero".
        REQUIRE (rep.summary.checksWithin == 0);
        REQUIRE (rep.summary.toCheck == 0);
        REQUIRE (rep.summary.firstAt == -1);
        REQUIRE (rep.summary.headline.empty());
        REQUIRE (rep.strengths.empty());
    }
}

// ========================================================================================================
TEST_CASE ("telescope: VERDICT dice que esta dentro de rango, con su numero, y nunca lo que se disparo",
           "[telescope][verdict]")
{
    // Las doce reglas de forma y de tiempo que tienen línea de "Dentro de rango". Las que NO la tienen, y por
    // qué: `key`, `loud-section` y `platform` son ○ informativas (no hay rango del que estar adentro), y las
    // seis cajas de dispositivo ya dicen su ✓ en "Dónde traduce".
    const char* const kOk[] = { "crushed", "thin", "muddy", "harsh", "no-air", "hollow-centre",
                                "hole", "quiet-section", "peaks", "out-of-phase", "imbalance", "dc" };

    const auto rows = healthyRows();
    const auto rep  = Verdict::evaluate (inputsFor (rows, healthyAggregates()));
    std::printf ("VERDICT[rango] sano: %d lineas dentro de rango  ·  %d hallazgos (criterio 0)\n",
                 (int) rep.strengths.size(), hallazgos (rep));
    for (const auto& s : rep.strengths)
        std::printf ("VERDICT[rango]   %s  [%s]\n", s.text.c_str(), s.evidence.c_str());

    REQUIRE (rep.strengths.size() >= 8);
    REQUIRE (hallazgos (rep) == 0);                                                  // VERDICT[sano], intacto
    REQUIRE (rep.countInSection (telescope::rules::Section::withinRange) == 0);      // no son hallazgos
    for (const auto& s : rep.strengths)
    {
        INFO (s.text);
        REQUIRE (s.section  == telescope::rules::Section::withinRange);
        REQUIRE (s.severity == Severity::info);
        REQUIRE (s.text.find_first_of ("0123456789") != std::string::npos);          // con su número
        REQUIRE (s.text.find ('{') == std::string::npos);
        const std::string key = std::string (telescope::rules::rule (s.ruleId).name) + ".ok";
        REQUIRE (s.evidence.rfind (key, 0) == 0);                                    // `<regla>.ok · …`
        REQUIRE (s.evidence.find_first_of ("0123456789") != std::string::npos);
    }
    for (const auto* name : kOk)
    {
        INFO (name);
        REQUIRE (strengthFor (rep, name) != nullptr);
    }

    // ---- cada defecto: la regla dispara y su línea de "Dentro de rango" DESAPARECE ----
    struct Def
    {
        const char* name;
        RuleId      id;
        std::function<void (std::vector<SecondRow>&, AnalysisFrame&)> apply;
    };
    const Def defs[] = {
        { "crushed",       RuleId::crushed,      [] (auto& r, auto&) { for (auto& x : r) x.tpMaxDbtp = -10.0f; } },
        { "thin",          RuleId::thin,         [] (auto& r, auto&) { addDb (r, 8, 12, -8.0f); } },
        { "muddy",         RuleId::muddy,        [] (auto& r, auto&) { addDb (r, 9, 13, +8.0f); } },
        { "harsh",         RuleId::harsh,        [] (auto& r, auto&) { addDb (r, 19, 23, +8.0f); } },
        { "no-air",        RuleId::noAir,        [] (auto& r, auto&) { addDb (r, 26, 29, -8.0f); } },
        { "hollow-centre", RuleId::hollowCentre, [] (auto& r, auto&)
          {
              for (auto& x : r)
              {
                  for (int b = 11; b <= 19; ++b) x.bandMonoLossDb[b] = -0.30f;
                  for (int b = 20; b < kBands; ++b) x.bandMonoLossDb[b] = -6.00f;
              }
          } },
        { "hole",          RuleId::hole,         [] (auto& r, auto&) { addDb (r, 13, 13, -12.0f, 20, 35); } },
        { "quiet-section", RuleId::quietSection, [] (auto& r, auto&)
          { for (int i = 40; i < 50; ++i) { r[(size_t) i].shortTermMax = -20.0f; r[(size_t) i].shortTermMin = -20.5f; } } },
        { "peaks",         RuleId::peaks,        [] (auto& r, auto&) { for (int i = 12; i < 17; ++i) r[(size_t) i].clipEvents = 2; } },
        { "out-of-phase",  RuleId::outOfPhase,   [] (auto& r, auto&) { for (int i = 0; i < 45; ++i) r[(size_t) i].bandCorr[16] = -0.9f; } },
        { "imbalance",     RuleId::imbalance,    [] (auto& r, auto&) { for (int i = 10; i < 35; ++i) r[(size_t) i].balanceDb = 5.0f; } },
        { "dc",            RuleId::dcOffset,     [] (auto&, auto& a) { a.dcL = 0.02f; a.dcR = -0.015f; } },
    };

    for (const auto& d : defs)
    {
        auto r = healthyRows();
        auto a = healthyAggregates();
        d.apply (r, a);
        const auto x = Verdict::evaluate (inputsFor (r, a));
        const bool firedNow = fired (x, d.id);
        const bool stillOk  = strengthFor (x, d.name) != nullptr;
        std::printf ("VERDICT[rango] defecto %-14s  dispara: %-2s  ·  %s.ok: %s  ·  %d lineas dentro de rango\n",
                     d.name, firedNow ? "si" : "NO", d.name, stillOk ? "SIGUE AHI" : "no esta",
                     (int) x.strengths.size());
        INFO (d.name);
        REQUIRE (firedNow);
        REQUIRE_FALSE (stillOk);
    }

    // ---- `harsh` en su forma BREVE (revisor del 57d, LOW: esa rama no tenía test) ----
    // Sobra en 2-5 kHz pero NO sostenido: +12 dB en 12 de las 60 filas, el 20 % del tiempo contra el 30 % de la
    // regla. La media del programa pasa el umbral de 3 dB y la regla igual no se dispara, así que la línea de
    // "Dentro de rango" tiene que ser la breve y decir los dos porcentajes.
    {
        auto r = healthyRows();
        addDb (r, 19, 23, +12.0f, 0, 12);
        const auto x = Verdict::evaluate (inputsFor (r, healthyAggregates()));
        const auto* s = strengthFor (x, "harsh");
        std::printf ("VERDICT[rango] harsh breve: dispara %s  ·  %s  [%s]\n", fired (x, RuleId::harsh) ? "SI" : "no",
                     s != nullptr ? s->text.c_str() : "(sin linea)", s != nullptr ? s->evidence.c_str() : "");
        REQUIRE_FALSE (fired (x, RuleId::harsh));
        REQUIRE (s != nullptr);
        REQUIRE (s->values[0] >= 3.0f);                          // la media sí pasa el umbral…
        REQUIRE (s->values[1] < 30.0f);                          // …pero no durante el tiempo que pide la regla
        REQUIRE (s->text.find ("20 %") != std::string::npos);
        REQUIRE (s->text.find ("30 %") != std::string::npos);
    }
}

// ========================================================================================================
TEST_CASE ("telescope: VERDICT mide sin opinar - el numero primero y donde mirar al final",
           "[telescope][verdict]")
{
    // POR IDIOMA: el verbo de "dónde mirar", las palabras de opinión de ESA lengua (además de las del prompt,
    // que se barren en las seis) y los adjetivos con los que arrancaban las frases hasta b777c08.
    //
    // Revisor del 57d (MEDIUM): la primera versión barría la lista en inglés/castellano sobre las seis tablas
    // y miraba el número y el "dónde mirar" sólo en en/es — o sea que una opinión en alemán, o una frase en
    // italiano que volviera a arrancar con el adjetivo, no la veía nadie. Ahora las seis lenguas pasan por las
    // cuatro comprobaciones.
    struct Lang
    {
        const char* code;
        const char* look;
        std::vector<std::string> banned;       // además de kBannedAll
        std::vector<std::string> adjectives;   // primera palabra prohibida en una frase de hallazgo
    };
    // Las palabras cortas se comparan como PALABRA ("mal" no es "normal"); las de dos palabras, como frase.
    const std::vector<std::string> kBannedAll     = { "profesional", "professional", "listo", "malo", "mal",
                                                      "arruina", "ruins", "amateur" };
    const std::vector<std::string> kBannedPhrases = { "bien hecho", "bad mix" };
    const Lang kLangs[] = {
        { "en", "Check ",    {},
          { "crushed", "thin", "muddy", "harsh", "no", "hollow", "hole", "quiet", "loud", "peak" } },
        { "es", "Revisa",    {},
          { "aplastado", "delgado", "turbio", "aspero", "sin", "centro", "hueco", "seccion", "rafaga" } },
        { "pt", "Verifique", { "profissional", "pronto", "ruim", "amador", "estraga" },
          { "achatado", "fino", "embolado", "aspero", "sem", "centro", "buraco", "trecho", "rajada" } },
        { "fr", "Verifiez",  { "professionnel", "pret", "mauvais", "gache", "rate" },
          { "ecrase", "maigre", "boueux", "agressif", "sans", "centre", "trou", "passage", "rafale" } },
        { "de", "Pruefe",    { "professionell", "fertig", "schlecht", "amateurhaft", "ruiniert" },
          { "plattgedrueckt", "duenn", "matschig", "hart", "keine", "hohle", "loch", "leise", "laute", "peak" } },
        { "it", "Controlla", { "professionale", "pronto", "cattivo", "brutto", "amatoriale", "rovina" },
          { "schiacciato", "sottile", "impastato", "aspro", "senza", "centro", "buco", "sezione", "raffica" } },
    };
    REQUIRE ((int) (sizeof (kLangs) / sizeof (kLangs[0])) == telescope::rules::kNumLanguages);
    const auto langFor = [&] (const char* code) -> const Lang*
    {
        for (const auto& l : kLangs) if (std::string (l.code) == code) return &l;
        return nullptr;
    };

    // ---- 1 · LA LISTA PROHIBIDA, en las SEIS tablas enteras (no sólo en las frases de hallazgo) ----
    int scanned = 0;
    std::vector<std::string> offences;
    for (int l = 0; l < telescope::rules::kNumLanguages; ++l)
    {
        const auto& table = telescope::rules::kLanguages[l];
        const auto* lang  = langFor (table.code);
        REQUIRE (lang != nullptr);
        std::vector<std::string> banned = kBannedAll;
        banned.insert (banned.end(), lang->banned.begin(), lang->banned.end());

        for (int i = 0; i < table.count; ++i)
        {
            ++scanned;
            const std::string text (table.table[i].text);
            for (const auto& w : wordsOf (text))
                for (const auto& b : banned)
                    if (w == b) offences.push_back (std::string (table.code) + " `" + table.table[i].key + "`: " + b);
            std::string lower;
            for (const unsigned char c : text) lower += (char) std::tolower (c);
            for (const auto& p : kBannedPhrases)
                if (lower.find (p) != std::string::npos)
                    offences.push_back (std::string (table.code) + " `" + table.table[i].key + "`: " + p);
        }
    }
    std::printf ("VERDICT[tono] %d frases de 6 tablas barridas contra la lista prohibida (la del prompt en las seis "
                 "+ la de cada lengua)  ·  %d infracciones\n", scanned, (int) offences.size());
    for (const auto& o : offences) std::printf ("VERDICT[tono]   %s\n", o.c_str());
    REQUIRE (offences.empty());

    // ---- 2 · EL NÚMERO ANTES DEL PRIMER PUNTO, y 3 · CADA HALLAZGO DICE DÓNDE MIRAR — en las seis ----
    // Sobre el informe con todos los defectos a la vez. "Dónde mirar", nunca "qué hacer".
    auto rows = healthyRows();
    auto agg  = healthyAggregates();
    applyAllDefects (rows, agg);

    for (const auto& lk : kLangs)
    {
        auto in = inputsFor (rows, agg, lk.code);
        in.targetIndex = 1;   // Spotify: que salgan también las dos frases de plataforma
        const auto rep = Verdict::evaluate (in);

        int noDigit = 0, noLook = 0, checked = 0;
        for (const auto& f : rep.findings)
        {
            ++checked;
            if (! digitBeforeFirstPeriod (f.text))
            {
                ++noDigit;
                std::printf ("VERDICT[tono] %s SIN NUMERO ANTES DEL PUNTO: %s\n", lk.code, f.text.c_str());
            }
            if (f.severity != Severity::info && f.text.find (lk.look) == std::string::npos)
            {
                ++noLook;
                std::printf ("VERDICT[tono] %s SIN DONDE MIRAR: %s\n", lk.code, f.text.c_str());
            }
        }
        std::printf ("VERDICT[tono] %s  %d frases del set de defectos  ·  %d sin numero antes del primer punto  ·  "
                     "%d hallazgos sin \"%s\"\n", lk.code, checked, noDigit, noLook, lk.look);
        REQUIRE (checked >= 10);
        REQUIRE (noDigit == 0);
        REQUIRE (noLook == 0);
    }

    // ---- 4 · NINGUNA frase de hallazgo arranca con el adjetivo (lo que había hasta b777c08) — en las seis ----
    const char* const kFindingKeys[] = { "crushed", "thin", "muddy", "harsh", "harsh.loud", "no-air",
                                         "hollow-centre", "hole", "hole.one", "quiet-section", "loud-section",
                                         "peaks" };
    int adjectiveFirst = 0, keysChecked = 0;
    for (const auto& lk : kLangs)
        for (const auto* key : kFindingKeys)
        {
            ++keysChecked;
            const auto phrase = Verdict::translate (key, lk.code);
            const auto words  = wordsOf (phrase);
            const bool bad = ! words.empty()
                          && std::find (lk.adjectives.begin(), lk.adjectives.end(), words.front()) != lk.adjectives.end();
            if (bad)
            {
                ++adjectiveFirst;
                std::printf ("VERDICT[tono] %s `%s` ARRANCA CON EL ADJETIVO: %s\n", lk.code, key, phrase.c_str());
            }
        }
    std::printf ("VERDICT[tono] 6 lenguas x %d frases de hallazgo = %d  ·  %d arrancan con el adjetivo\n",
                 (int) (sizeof (kFindingKeys) / sizeof (kFindingKeys[0])), keysChecked, adjectiveFirst);
    REQUIRE (adjectiveFirst == 0);
}

// ========================================================================================================
// 57d · revisor (MEDIUM): UN HUECO FUSIONADO DICE LA VENTANA DE TODO EL TRAMO
//
// La primera versión tomaba t0/t1 de la banda más honda. Con las bandas del tramo corridas en el tiempo —se
// solapan, pero no empiezan juntas— eso achicaba la ventana: 400 Hz abajo desde 0:10 y 500 Hz, la más honda,
// desde 0:18, salía "entre 0:18 y 0:32", y el titular decía "el primero en 0:18" con un pozo que ya estaba
// desde 0:10. La ventana del hallazgo es la UNIÓN de las ventanas del tramo; `values[3]` sigue siendo la
// racha de la banda más honda, la del número de la evidencia.
// ========================================================================================================
TEST_CASE ("telescope: un hueco fusionado dice la ventana de todo el tramo", "[telescope][verdict]")
{
    auto rows = healthyRows();
    addDb (rows, 12, 12,  -8.0f, 10, 22);    // 400 Hz abajo de 0:10 a 0:22
    addDb (rows, 13, 13, -12.0f, 18, 32);    // 500 Hz, la más honda, de 0:18 a 0:32
    const auto rep = Verdict::evaluate (inputsFor (rows, healthyAggregates()));

    int holes = 0;
    const telescope::VerdictFinding* hole = nullptr;
    for (const auto& f : rep.findings)
    {
        if (f.ruleId != RuleId::hole) continue;
        ++holes;
        hole = &f;
        std::printf ("VERDICT[hueco] corrido: %.0f Hz (tramo %.0f-%.0f Hz)  ·  %d s -> %d s (esperado 10 -> 32)  ·  "
                     "racha de la mas honda %.0f s  ·  %s\n", (double) telescope::kThirdOctaveHz[f.band],
                     f.values[1], f.values[2], f.t0, f.t1, f.values[3], f.text.c_str());
    }
    REQUIRE (holes == 1);
    REQUIRE (std::abs ((double) telescope::kThirdOctaveHz[hole->band] - 500.0) < 1.0);
    REQUIRE (hole->t0 == 10);
    REQUIRE (hole->t1 == 32);
    REQUIRE (std::abs (hole->values[1] - 400.0f) < 1.0f);
    REQUIRE (std::abs (hole->values[2] - 500.0f) < 1.0f);
    REQUIRE (hole->values[3] == 14.0f);
    REQUIRE (hole->text.find ("0:10") != std::string::npos);
    REQUIRE (hole->text.find ("0:32") != std::string::npos);
    REQUIRE (rep.summary.firstAt == 10);
}
