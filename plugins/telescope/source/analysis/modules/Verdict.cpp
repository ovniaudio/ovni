#include "analysis/modules/Verdict.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace telescope
{
namespace
{
using rules::RuleId;
using rules::Section;
using rules::Severity;

constexpr int kNumBands = SecondRow::kNumBands;

// ---- las regiones, por índice de ⅓ de octava (ISO 266; ver SpectrumFrame::kThirdOctaveHz) ----
//   0:25  1:31.5  2:40  3:50  4:63  5:80  6:100  7:125  8:160  9:200
//  10:250 11:315 12:400 13:500 14:630 15:800 16:1k 17:1.25k 18:1.6k 19:2k
//  20:2.5k 21:3.15k 22:4k 23:5k 24:6.3k 25:8k 26:10k 27:12.5k 28:16k 29:20k
struct Region { int lo, hi; };                    // inclusive
constexpr Region kBody      { 8, 12 };            // 160-400 Hz   ("delgado")
constexpr Region kLowMids   { 9, 13 };            // 200-500 Hz   ("turbio")
constexpr Region kPresence  { 19, 23 };           // 2-5 kHz      ("áspero", auriculares)
constexpr Region kLaptop    { 19, 22 };           // 2-4 kHz
constexpr Region kAir       { 26, 29 };           // 10-20 kHz    ("sin aire")
constexpr Region kSubCar    { 0, 6 };             // 25-100 Hz    (auto)
constexpr Region kSubClub   { 0, 6 };             // 25-100 Hz    (club: el sub va mono por debajo de 120)
constexpr Region kMidWidth  { 11, 19 };           // 315 Hz-2 kHz (centro vacío)
constexpr Region kHighWidth { 20, 29 };           // 2.5-20 kHz
constexpr Region kPhoneLow  { 0, 10 };            // hasta 250 Hz (lo que un teléfono no da)
constexpr Region kPhoneHarm { 11, 17 };           // 315 Hz-1.25 kHz (los armónicos del bajo)
constexpr Region kFitLo     { 3, 28 };            // 50 Hz-16 kHz: donde se ajusta la tendencia
constexpr int    kPhaseFrom = 25;                 // 8 kHz: de acá para arriba mira la regla de auriculares

// Una banda sólo cuenta para las reglas de fase cuando TIENE señal: 40 dB por debajo de la banda más
// fuerte del programa ya es piso de ruido, y la correlación del piso de ruido es un número al azar.
constexpr float kPhaseFloorDb = -40.0f;
constexpr int   kMinRowsForShape = 3;

double dbToPow (double db) { return std::pow (10.0, db / 10.0); }

// 57d — lo que redondea a cero se escribe como cero: "-0.0 dB on average" es ruido de coma flotante con
// cara de dato, y en una línea de "Dentro de rango" se lee como si hubiera algo.
double roundsToZero (double v, int decimals) noexcept
{
    return std::abs (v) < 0.5 * std::pow (10.0, -decimals) ? 0.0 : v;
}

std::string fmt (double v, int decimals)
{
    char buf[64];
    std::snprintf (buf, sizeof (buf), "%+.*f", decimals, roundsToZero (v, decimals));
    // El signo sólo cuando aporta: "+3.2 dB de exceso" se lee bien, "+15 %" no.
    return std::string (buf);
}

std::string fmtNoSign (double v, int decimals)
{
    char buf[64];
    std::snprintf (buf, sizeof (buf), "%.*f", decimals, roundsToZero (v, decimals));
    return std::string (buf);
}

struct Subst
{
    std::string valor, valor2, valor3, banda, t0, t1;
};

void replaceAll (std::string& s, const std::string& from, const std::string& to)
{
    if (from.empty()) return;
    for (size_t p = s.find (from); p != std::string::npos; p = s.find (from, p + to.size()))
        s.replace (p, from.size(), to);
}

// Renderiza una clave: se reemplazan SIEMPRE las seis plantillas, aunque el valor venga vacío. Así nunca
// queda una llave suelta en pantalla — lo verifica VERDICT[plantillas] sobre las seis lenguas.
std::string render (const char* key, const char* lang, const Subst& s)
{
    std::string out = rules::phrase (key, lang);
    replaceAll (out, "{valor3}", s.valor3);
    replaceAll (out, "{valor2}", s.valor2);
    replaceAll (out, "{valor}",  s.valor);
    replaceAll (out, "{banda}",  s.banda);
    replaceAll (out, "{t0}",     s.t0);
    replaceAll (out, "{t1}",     s.t1);
    return out;
}

// LA EVIDENCIA ES NEUTRA DE IDIOMA: el id de la regla (que es el mismo en las seis lenguas y el que
// figura en plugins/telescope/docs/telescope-diccionario.md) más los NÚMEROS con sus unidades. La prosa de `Rule::metric`
// es documentación y se queda en Rules.h: ponerla en pantalla la habría mostrado en castellano al lado de
// una frase en inglés, que es exactamente el problema que D-50 vino a arreglar.
// ========================================================================================================
// 57d · LA EVIDENCIA TAMBIÉN DICE LA REGLA. Al lado del número medido van los dos umbrales de `Rule` con sus
// unidades — `hole · -25.96 dB / 15 s · rule 6 dB / 10 s` —, para el que quiere lo técnico sin abrir el
// diccionario. Las unidades se escriben acá y no se leen de `Rule::units`: esa columna es prosa en castellano
// ("eventos") y la evidencia es neutra de idioma (D-50). Las ○ informativas sin umbral no llevan sufijo.
// ========================================================================================================
std::string trimNum (double v)
{
    char buf[32];
    std::snprintf (buf, sizeof (buf), "%.3f", v);
    std::string s (buf);
    while (! s.empty() && s.back() == '0') s.pop_back();
    if (! s.empty() && s.back() == '.') s.pop_back();
    if (s == "-0") s = "0";
    return s;
}

std::string ruleSuffix (RuleId id)
{
    const auto& r = rules::rule (id);
    const auto t1   = trimNum (r.threshold);
    const auto t2   = trimNum (r.threshold2);
    const auto pct2 = trimNum ((double) r.threshold2 * 100.0);   // las fracciones de tiempo, en %

    switch (id)
    {
        case RuleId::crushed: case RuleId::thin: case RuleId::muddy: case RuleId::noAir:
        case RuleId::devLaptop: case RuleId::devCar: case RuleId::devClub:
            return "rule " + t1 + " dB";
        case RuleId::harsh: case RuleId::devHeadphones:
            return "rule " + t1 + " dB / " + pct2 + " %";
        case RuleId::hollowCentre:  return "rule " + t1 + " / " + t2;
        case RuleId::devPhone:      return "rule " + t1 + " % / " + t2 + " dB";
        case RuleId::hole:
        case RuleId::imbalance:     return "rule " + t1 + " dB / " + t2 + " s";
        case RuleId::quietSection:
        case RuleId::loudSection:   return "rule " + t1 + " LU / " + t2 + " s";
        case RuleId::peaks:         return "rule " + t1 + " / " + t2 + " s";
        case RuleId::outOfPhase:    return "rule " + t1 + " / " + pct2 + " %";
        case RuleId::dcOffset:      return "rule " + t1;
        case RuleId::key: case RuleId::devHifi: case RuleId::platform: case RuleId::kNumRules:
            break;
    }
    return {};
}

std::string withRule (std::string e, RuleId id, const std::string& detail)
{
    if (! detail.empty()) { e += " · "; e += detail; }
    const auto rule = ruleSuffix (id);
    if (! rule.empty()) { e += " · "; e += rule; }
    return e;
}

std::string evidenceFor (RuleId id, const std::string& detail)
{
    return withRule (rules::rule (id).name, id, detail);
}

// La de "Dentro de rango" arranca con `<regla>.ok`: es la misma regla, del otro lado del umbral.
std::string strengthEvidenceFor (RuleId id, const std::string& detail)
{
    return withRule (std::string (rules::rule (id).name) + ".ok", id, detail);
}

const char* noteName (int pc)
{
    static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return (pc >= 0 && pc < 12) ? names[pc] : "?";
}

// ========================================================================================================
// LA FORMA DEL PROGRAMA. Cada curva menos su propio nivel de banda ancha, así la comparación es de TILT y
// no de volumen (la misma idea que TONAL BALANCE, ver ReferenceFrame.h).
// ========================================================================================================
struct Shape
{
    float value[kNumBands] {};
    bool  has[kNumBands] {};
    int   measured = 0;
};

Shape shapeOf (const float* bandsDb, const bool* has)
{
    Shape s;
    double total = 0.0;
    for (int b = 0; b < kNumBands; ++b)
    {
        s.has[b] = has[b];
        if (! has[b]) continue;
        total += dbToPow (bandsDb[b]);
        ++s.measured;
    }
    if (s.measured == 0 || total <= 0.0) return s;

    const double levelDb = 10.0 * std::log10 (total);
    for (int b = 0; b < kNumBands; ++b)
        if (s.has[b]) s.value[b] = (float) ((double) bandsDb[b] - levelDb);
    return s;
}

// La recta de mínimos cuadrados sobre log2(f), ajustada entre 50 Hz y 16 kHz (fuera de ahí las bandas
// tienen uno o dos bins y arrastran el ajuste sin aportar información).
Shape trendOf (const Shape& src)
{
    double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
    int n = 0;
    for (int b = kFitLo.lo; b <= kFitLo.hi; ++b)
    {
        if (! src.has[b]) continue;
        const double x = std::log2 (kThirdOctaveHz[b]);
        const double y = src.value[b];
        sx += x; sy += y; sxx += x * x; sxy += x * y;
        ++n;
    }

    Shape t;
    for (int b = 0; b < kNumBands; ++b) t.has[b] = src.has[b];
    t.measured = src.measured;
    if (n < 4) return t;

    const double den = (double) n * sxx - sx * sx;
    if (std::abs (den) < 1.0e-12) return t;
    const double slope = ((double) n * sxy - sx * sy) / den;
    const double inter = (sy - slope * sx) / (double) n;

    for (int b = 0; b < kNumBands; ++b)
        if (t.has[b]) t.value[b] = (float) (slope * std::log2 (kThirdOctaveHz[b]) + inter);
    return t;
}

double regionMean (const float* v, const bool* has, Region r)
{
    double sum = 0.0; int n = 0;
    for (int b = r.lo; b <= r.hi && b < kNumBands; ++b)
        if (has[b]) { sum += v[b]; ++n; }
    return n > 0 ? sum / (double) n : 0.0;
}

int regionCount (const bool* has, Region r)
{
    int n = 0;
    for (int b = r.lo; b <= r.hi && b < kNumBands; ++b) if (has[b]) ++n;
    return n;
}
}

//======================================================================================== helpers públicos
std::string Verdict::translate (const char* key, const char* language)
{
    return std::string (rules::phrase (key, language));
}

std::string Verdict::timeLabel (int seconds)
{
    if (seconds < 0) seconds = 0;
    char buf[32];
    std::snprintf (buf, sizeof (buf), "%d:%02d", seconds / 60, seconds % 60);
    return std::string (buf);
}

std::string Verdict::number (double v, int decimals) { return fmtNoSign (v, decimals); }

AnalysisFrame Verdict::aggregatesFrom (const FileAnalysis& a)
{
    AnalysisFrame f;
    f.timeSeconds              = a.seconds;
    f.loudness.integrated      = a.integratedLufs;
    f.loudness.integratedValid = a.integratedLufs > kSilenceDb;
    f.loudness.lra             = a.lra;
    f.loudness.truePeakMax     = a.truePeakDbtp;
    f.loudness.momentaryMax    = a.momentaryMax;
    f.loudness.shortTermMax    = a.shortTermMax;
    f.clipEvents               = a.clipEvents;
    f.dcL                      = a.dcL;
    f.dcR                      = a.dcR;
    f.keyTonic                 = a.keyTonic;
    f.keyMode                  = a.keyMode;
    f.keyConfidence            = a.keyConfidence;
    f.keyTimeFraction          = a.keyTimeFraction;
    f.secondsAnalysed          = (juce::uint32) a.secondRows.size();
    // El PLR es TP máximo − integrado (AES TD1004): la misma cuenta que hace el medidor en vivo.
    f.plrValid = f.loudness.integratedValid && a.truePeakDbtp > kSilenceDb;
    f.plr      = f.plrValid ? a.truePeakDbtp - a.integratedLufs : 0.0f;
    return f;
}

// ========================================================================================================
// EL MOTOR
// ========================================================================================================
VerdictReport Verdict::evaluate (const Inputs& in)
{
    VerdictReport rep;
    const char* lang = (in.language != nullptr && rules::hasLanguage (in.language)) ? in.language
                                                                                   : rules::kDefaultLanguage;
    rep.footer = rules::phrase ("footer", lang);

    const int n = std::max (0, in.n);
    const auto* rows = in.rows;

    // ---- las filas útiles ----
    std::vector<int> spectral, loud;
    spectral.reserve ((size_t) n);
    loud.reserve ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        if (rows[i].hasSpectrum()) spectral.push_back (i);
        if (rows[i].hasLoudness()) loud.push_back (i);
    }

    // ---- resumen ----
    auto& sum = rep.summary;
    sum.seconds         = (int) spectral.size();
    sum.secondsTotal    = n;
    sum.integrated      = in.aggregates.loudness.integrated;
    sum.lra             = in.aggregates.loudness.lra;
    sum.plr             = in.aggregates.plr;
    sum.truePeakMax     = in.aggregates.loudness.truePeakMax;
    sum.keyTonic        = in.aggregates.keyTonic;
    sum.keyMode         = in.aggregates.keyMode;
    sum.keyConfidence   = in.aggregates.keyConfidence;
    sum.keyTimeFraction = in.aggregates.keyTimeFraction;

    // ---- la curva media del programa (promedio de POTENCIA sobre las filas con espectro) ----
    float avgBands[kNumBands] {};
    bool  hasBand[kNumBands] {};
    {
        double acc[kNumBands] {};
        int    cnt[kNumBands] {};
        for (const int i : spectral)
            for (int b = 0; b < kNumBands; ++b)
                if (rows[i].bandMeasured (b)) { acc[b] += dbToPow (rows[i].bandsDb[b]); ++cnt[b]; }
        for (int b = 0; b < kNumBands; ++b)
            if (cnt[b] > 0) { avgBands[b] = (float) (10.0 * std::log10 (acc[b] / (double) cnt[b])); hasBand[b] = true; }
    }

    // La correlación de banda ancha del programa: media sobre las filas medidas (con el módulo apagado no
    // hay filas con espectro, y entonces vale la del último frame, que es lo único que hay).
    if (! spectral.empty())
    {
        double c = 0.0;
        for (const int i : spectral) c += rows[i].corr;
        sum.corr = (float) (c / (double) spectral.size());
    }
    else
    {
        sum.corr = in.aggregates.corr;
    }

    const Shape program = shapeOf (avgBands, hasBand);

    // ---- la línea de base: la referencia si la hay, la tendencia del propio material si no ----
    Shape baseline;
    bool  usedRef = false;
    if (in.ref != nullptr && in.ref->refValid)
    {
        bool refHas[kNumBands] {};
        for (int b = 0; b < kNumBands; ++b)
            refHas[b] = program.has[b] && in.ref->refBands[b] > SpectrumFrame::kFloorDb;
        const Shape refShape = shapeOf (in.ref->refBands, refHas);
        if (refShape.measured >= 10) { baseline = refShape; usedRef = true; }
    }
    if (! usedRef) baseline = trendOf (program);
    sum.usedReference = usedRef;

    const std::string baseKey = usedRef ? "baseline.reference" : "baseline.trend";
    const std::string baseTxt = rules::phrase (baseKey.c_str(), lang);

    float residual[kNumBands] {};
    bool  hasRes[kNumBands] {};
    for (int b = 0; b < kNumBands; ++b)
    {
        hasRes[b] = program.has[b] && baseline.has[b];
        if (hasRes[b]) residual[b] = program.value[b] - baseline.value[b];
    }

    const bool haveShape = (int) spectral.size() >= kMinRowsForShape && program.measured >= 10;

    // la banda más fuerte, para el piso de las reglas de fase
    float loudestBand = SpectrumFrame::kFloorDb;
    for (int b = 0; b < kNumBands; ++b) if (hasBand[b]) loudestBand = std::max (loudestBand, avgBands[b]);

    const auto push = [&] (RuleId id, Severity sev, const Subst& s, const char* key,
                           int t0, int t1, int band, std::initializer_list<float> vals,
                           const std::string& detail)
    {
        VerdictFinding f;
        f.ruleId   = id;
        f.section  = rules::rule (id).section;
        f.severity = sev;
        f.t0 = t0; f.t1 = t1; f.band = band;
        int k = 0;
        for (const float v : vals) { if (k < 4) f.values[k++] = v; }
        f.text     = render (key, lang, s);
        f.evidence = evidenceFor (id, detail);
        rep.findings.push_back (f);
    };

    // ===== 57d · DENTRO DE RANGO =====
    // Una línea por regla que se EVALUÓ y NO se disparó, con su número y su límite. No son hallazgos: no
    // entran a `findings`, así que nada de lo que cuenta hallazgos se entera (VERDICT[sano] sigue en cero).
    // Una regla que no tuvo con qué evaluarse no dice nada: "dentro de rango" es una medición, no un default.
    const auto strength = [&] (RuleId id, const char* key, const Subst& s,
                               std::initializer_list<float> vals, const std::string& detail)
    {
        VerdictFinding f;
        f.ruleId   = id;
        f.section  = Section::withinRange;
        f.severity = Severity::info;
        int k = 0;
        for (const float v : vals) { if (k < 4) f.values[k++] = v; }
        f.text     = render (key, lang, s);
        f.evidence = strengthEvidenceFor (id, detail);
        rep.strengths.push_back (f);
    };
    // La comparación dicha corta, para las líneas de una sola fila ("vs the trend" / "vs the reference").
    const std::string baseShort = rules::phrase (usedRef ? "baseline.reference.short" : "baseline.trend.short", lang);
    int  devicesWithin = 0;       // cajas en ✓ que tuvieron DATO para decidir (ver el titular, al final)
    bool feelEvaluable = false;   // ¿alguna regla de la sección 1 pudo evaluarse? (la caja hi-fi hereda de ahí)

    // ====================================================================== 1 · cómo se va a sentir
    // --- aplastado: PSR medio ---
    if (! loud.empty())
    {
        double psrSum = 0.0; int psrN = 0;
        for (const int i : loud)
        {
            if (rows[i].shortTermMax <= -60.0f || rows[i].tpMaxDbtp <= -60.0f) continue;
            psrSum += (double) rows[i].tpMaxDbtp - (double) rows[i].shortTermMax;
            ++psrN;
        }
        if (psrN >= kMinRowsForShape)
        {
            feelEvaluable = true;
            const double psr = psrSum / (double) psrN;
            const auto& r = rules::rule (RuleId::crushed);
            if (psr < r.threshold)
            {
                Subst s;
                s.valor  = fmtNoSign (psr, 1);
                s.valor2 = fmtNoSign (r.threshold, 1);
                push (RuleId::crushed, psr < r.threshold - 2.0f ? Severity::bad : Severity::warn, s,
                      "crushed", -1, -1, -1, { (float) psr, r.threshold },
                      "PSR " + fmtNoSign (psr, 2) + " dB / " + std::to_string (psrN) + " s");
            }
            else
            {
                Subst s;
                s.valor  = fmtNoSign (psr, 1);
                s.valor2 = fmtNoSign (r.threshold, 1);
                strength (RuleId::crushed, "crushed.ok", s, { (float) psr, r.threshold },
                          "PSR " + fmtNoSign (psr, 2) + " dB / " + std::to_string (psrN) + " s");
            }
        }
    }

    if (haveShape)
    {
        // --- delgado / turbio / sin aire ---
        struct SpectralRule { RuleId id; const char* key; const char* okKey; Region region; bool deficit; };
        const SpectralRule kSpectral[] = {
            { RuleId::thin,  "thin",   "thin.ok",   kBody,    true  },
            { RuleId::muddy, "muddy",  "muddy.ok",  kLowMids, false },
            { RuleId::noAir, "no-air", "no-air.ok", kAir,     true  },
        };

        for (const auto& sr : kSpectral)
        {
            if (regionCount (hasRes, sr.region) < 2) continue;
            const double mean = regionMean (residual, hasRes, sr.region);
            const double amount = sr.deficit ? -mean : mean;
            const auto& r = rules::rule (sr.id);
            if (amount < r.threshold)
            {
                // 57d — dentro de rango, con el residuo CON SU SIGNO: en `thin`, +0.4 dB quiere decir "no falta
                // nada" y −2.1 "falta, pero menos que el umbral". Los dos son ciertos y dicen cosas distintas.
                Subst s;
                s.valor  = fmt (mean, 1);
                s.valor2 = fmtNoSign (r.threshold, 1);
                s.valor3 = baseShort;
                strength (sr.id, sr.okKey, s, { (float) mean, r.threshold }, fmt (mean, 2) + " dB");
                continue;
            }

            Subst s;
            s.valor  = fmtNoSign (amount, 1);
            s.valor3 = baseTxt;
            push (sr.id, amount >= r.threshold * 2.0f ? Severity::bad : Severity::warn, s, sr.key,
                  -1, -1, -1, { (float) amount, r.threshold },
                  std::string (sr.deficit ? "-" : "+") + fmtNoSign (amount, 2) + " dB");
        }

        // --- áspero: exceso en 2-5 kHz SOSTENIDO, y agravado si además está fuerte ---
        if (regionCount (hasRes, kPresence) >= 2)
        {
            const auto& r = rules::rule (RuleId::harsh);
            const double mean = regionMean (residual, hasRes, kPresence);
            if (mean >= r.threshold)
            {
                int over = 0;
                for (const int i : spectral)
                {
                    bool rowHas[kNumBands] {};
                    for (int b = 0; b < kNumBands; ++b) rowHas[b] = rows[i].bandMeasured (b);
                    const Shape rs = shapeOf (rows[i].bandsDb, rowHas);
                    if (rs.measured < 10) continue;
                    float rowRes[kNumBands] {};
                    bool  rowResHas[kNumBands] {};
                    for (int b = 0; b < kNumBands; ++b)
                    {
                        rowResHas[b] = rs.has[b] && baseline.has[b];
                        if (rowResHas[b]) rowRes[b] = rs.value[b] - baseline.value[b];
                    }
                    if (regionMean (rowRes, rowResHas, kPresence) >= r.threshold) ++over;
                }
                const double frac = spectral.empty() ? 0.0 : (double) over / (double) spectral.size();
                if (frac >= r.threshold2)
                {
                    const bool loudToo = in.aggregates.loudness.integratedValid
                                      && in.aggregates.loudness.integrated > -10.0f;
                    Subst s;
                    s.valor  = fmtNoSign (mean, 1);
                    s.valor2 = fmtNoSign (frac * 100.0, 0);
                    s.valor3 = baseTxt;
                    s.t0     = fmtNoSign (in.aggregates.loudness.integrated, 1);
                    push (RuleId::harsh, loudToo ? Severity::bad : Severity::warn, s,
                          loudToo ? "harsh.loud" : "harsh", -1, -1, -1,
                          { (float) mean, (float) (frac * 100.0) },
                          "+" + fmtNoSign (mean, 2) + " dB / " + fmtNoSign (frac * 100.0, 0) + " %");
                }
                else
                {
                    // 57d — sobra, pero NO sostenido: la regla no se dispara, y la línea dice los dos números
                    // (cuánto tiempo pasó el umbral y a partir de cuánto tiempo la regla lo llama áspero).
                    Subst s;
                    s.valor  = fmtNoSign (frac * 100.0, 0);
                    s.valor2 = fmtNoSign (r.threshold2 * 100.0, 0);
                    s.valor3 = fmtNoSign (r.threshold, 1);
                    strength (RuleId::harsh, "harsh.ok.brief", s, { (float) mean, (float) (frac * 100.0) },
                              "+" + fmtNoSign (mean, 2) + " dB / " + fmtNoSign (frac * 100.0, 0) + " %");
                }
            }
            else
            {
                Subst s;
                s.valor  = fmt (mean, 1);
                s.valor2 = fmtNoSign (r.threshold, 1);
                s.valor3 = baseShort;
                strength (RuleId::harsh, "harsh.ok", s, { (float) mean, r.threshold }, fmt (mean, 2) + " dB");
            }
        }

        // --- centro vacío: los medios angostos con los agudos abiertos ---
        {
            float widthByBand[kNumBands] {};
            bool  widthHas[kNumBands] {};
            for (int b = 0; b < kNumBands; ++b)
            {
                double sumDb = 0.0; int cnt = 0;
                for (const int i : spectral)
                    if (rows[i].bandMeasured (b)) { sumDb += rows[i].bandMonoLossDb[b]; ++cnt; }
                if (cnt == 0) continue;
                widthHas[b]   = true;
                widthByBand[b] = SecondRow::widthFromMonoLossDb ((float) (sumDb / (double) cnt));
            }

            if (regionCount (widthHas, kMidWidth) >= 4 && regionCount (widthHas, kHighWidth) >= 4)
            {
                const auto& r = rules::rule (RuleId::hollowCentre);
                const double mid  = regionMean (widthByBand, widthHas, kMidWidth);
                const double high = regionMean (widthByBand, widthHas, kHighWidth);
                if (mid < r.threshold && high > r.threshold2)
                {
                    Subst s;
                    s.valor  = fmtNoSign (mid, 2);
                    s.valor2 = fmtNoSign (high, 2);
                    push (RuleId::hollowCentre, Severity::warn, s, "hollow-centre", -1, -1, -1,
                          { (float) mid, (float) high },
                          "width " + fmtNoSign (mid, 2) + " / " + fmtNoSign (high, 2));
                }
                else
                {
                    Subst s;
                    s.valor  = fmtNoSign (mid, 2);
                    s.valor2 = fmtNoSign (high, 2);
                    s.t0     = fmtNoSign (r.threshold, 1);
                    s.t1     = fmtNoSign (r.threshold2, 1);
                    strength (RuleId::hollowCentre, "hollow-centre.ok", s, { (float) mid, (float) high },
                              "width " + fmtNoSign (mid, 2) + " / " + fmtNoSign (high, 2));
                }
            }
        }
    }

    // --- tonalidad (informativa) ---
    if (in.aggregates.keyTonic >= 0 && in.aggregates.keyMode >= 0)
    {
        Subst s;
        s.banda  = std::string (noteName (in.aggregates.keyTonic)) + " "
                 + rules::phrase (in.aggregates.keyMode == 0 ? "key.major" : "key.minor", lang);
        s.valor  = fmtNoSign (in.aggregates.keyConfidence, 2);
        s.valor2 = fmtNoSign (in.aggregates.keyTimeFraction * 100.0f, 0);
        push (RuleId::key, Severity::info, s, "key", -1, -1, -1,
              { (float) in.aggregates.keyTonic, (float) in.aggregates.keyMode,
                in.aggregates.keyConfidence, in.aggregates.keyTimeFraction },
              fmtNoSign (in.aggregates.keyConfidence, 2) + " / "
                + fmtNoSign (in.aggregates.keyTimeFraction * 100.0f, 0) + " %");
    }

    const int feelFindings = rep.countInSection (Section::feel)
                           - (in.aggregates.keyTonic >= 0 ? 1 : 0);   // la tonalidad no es un defecto

    // ====================================================================== 2 · dónde traduce
    {
        feelEvaluable = feelEvaluable || haveShape;

        // 57d: `evaluable` = la caja tuvo DATO para decidir. Sin espectro, laptop/auto/auriculares salen ✓
        // porque su exceso vale 0 por defecto; eso es una caja sin medición, no un chequeo dentro de rango, y
        // no entra a la cuenta del titular (la línea de la caja se sigue dibujando como siempre).
        auto setDevice = [&] (int slot, RuleId id, const char* key, devices::DeviceVerdict v,
                              float a, float b, const char* phraseKey, const Subst& s,
                              const std::string& detail, bool evaluable)
        {
            rep.devices[slot] = { id, key, v, a, b };
            push (id, v == devices::DeviceVerdict::fail ? Severity::bad
                    : (v == devices::DeviceVerdict::warn ? Severity::warn : Severity::info),
                  s, phraseKey, -1, -1, -1, { a, b }, detail);
            if (evaluable && v == devices::DeviceVerdict::ok) ++devicesWithin;
        };

        // --- celular ---
        {
            double low = 0.0, harm = 0.0, all = 0.0;
            for (int b = 0; b < kNumBands; ++b)
            {
                if (! hasBand[b]) continue;
                const double p = dbToPow (avgBands[b]);
                all += p;
                if (b >= kPhoneLow.lo  && b <= kPhoneLow.hi)  low  += p;
                if (b >= kPhoneHarm.lo && b <= kPhoneHarm.hi) harm += p;
            }
            const auto& r = rules::rule (RuleId::devPhone);
            const double lowPct  = all  > 0.0 ? 100.0 * low / all : 0.0;
            const double harmRel = (low > 0.0 && harm > 0.0) ? 10.0 * std::log10 (harm / low) : -60.0;
            const bool   tooLow  = lowPct  > r.threshold;
            const bool   noHarm  = harmRel < r.threshold2;
            const auto   v = (tooLow && noHarm) ? devices::DeviceVerdict::fail
                           : ((tooLow || noHarm) ? devices::DeviceVerdict::warn : devices::DeviceVerdict::ok);
            Subst s;
            s.valor  = fmtNoSign (lowPct, 0);
            s.valor2 = fmtNoSign (harmRel, 1);
            setDevice (0, RuleId::devPhone, "phone", v, (float) lowPct, (float) harmRel,
                       v == devices::DeviceVerdict::ok ? "phone.ok" : "phone.bad", s,
                       fmtNoSign (lowPct, 1) + " % < 300 Hz · " + fmt (harmRel, 1) + " dB", all > 0.0);
        }

        // --- auriculares ---
        {
            const auto& r = rules::rule (RuleId::devHeadphones);
            const double excess = haveShape ? regionMean (residual, hasRes, kPresence) : 0.0;
            // La misma disciplina que `out-of-phase`: sólo cuentan las bandas cuya correlación MEDIA es
            // claramente negativa. Un auricular separa el estéreo, no inventa una contrafase que no está.
            bool bandIsOut[kNumBands] {};
            for (int b = kPhaseFrom; b < kNumBands; ++b)
            {
                if (! hasBand[b] || avgBands[b] <= loudestBand + kPhaseFloorDb) continue;
                double sumc = 0.0; int cnt = 0;
                for (const int i : spectral)
                    if (rows[i].bandMeasured (b)) { sumc += rows[i].bandCorr[b]; ++cnt; }
                bandIsOut[b] = cnt > 0 && (sumc / (double) cnt) <= rules::rule (RuleId::outOfPhase).threshold;
            }

            int phaseRows = 0;
            for (const int i : spectral)
            {
                bool any = false;
                for (int b = kPhaseFrom; b < kNumBands && ! any; ++b)
                    if (bandIsOut[b] && rows[i].bandMeasured (b) && rows[i].bandCorr[b] < 0.0f) any = true;
                if (any) ++phaseRows;
            }
            const double frac = spectral.empty() ? 0.0 : (double) phaseRows / (double) spectral.size();
            const bool bad = excess >= r.threshold || frac > r.threshold2;
            Subst s;
            s.valor  = fmtNoSign (excess, 1);
            s.valor2 = fmtNoSign (frac * 100.0, 0);
            setDevice (1, RuleId::devHeadphones, "headphones",
                       bad ? devices::DeviceVerdict::fail : devices::DeviceVerdict::ok,
                       (float) excess, (float) (frac * 100.0),
                       bad ? "headphones.bad" : "headphones.ok", s,
                       fmt (excess, 2) + " dB 2-5 kHz · " + fmtNoSign (frac * 100.0, 0) + " %", haveShape);
        }

        // --- laptop ---
        {
            const auto& r = rules::rule (RuleId::devLaptop);
            const double pres = haveShape ? regionMean (residual, hasRes, kLaptop) : 0.0;
            const bool bad = pres <= -r.threshold;
            Subst s;
            s.valor = bad ? fmtNoSign (-pres, 1) : fmt (pres, 1);
            setDevice (2, RuleId::devLaptop, "laptop", bad ? devices::DeviceVerdict::fail : devices::DeviceVerdict::ok,
                       (float) pres, 0.0f, bad ? "laptop.bad" : "laptop.ok", s,
                       fmt (pres, 2) + " dB 2-4 kHz", haveShape);
        }

        // --- auto ---
        {
            const auto& r = rules::rule (RuleId::devCar);
            const double lowExcess = haveShape ? regionMean (residual, hasRes, kSubCar) : 0.0;
            const bool bad = lowExcess >= r.threshold;
            Subst s;
            s.valor = bad ? fmtNoSign (lowExcess, 1) : fmt (lowExcess, 1);
            setDevice (3, RuleId::devCar, "car", bad ? devices::DeviceVerdict::fail : devices::DeviceVerdict::ok,
                       (float) lowExcess, 0.0f, bad ? "car.bad" : "car.ok", s,
                       fmt (lowExcess, 2) + " dB < 100 Hz", haveShape);
        }

        // --- club (sub mono) ---
        {
            const auto& r = rules::rule (RuleId::devClub);
            double sumDb = 0.0; int cnt = 0;
            for (int b = kSubClub.lo; b <= kSubClub.hi; ++b)
            {
                double rowSum = 0.0; int rowN = 0;
                for (const int i : spectral)
                    if (rows[i].bandMeasured (b)) { rowSum += rows[i].bandMonoLossDb[b]; ++rowN; }
                if (rowN > 0) { sumDb += rowSum / (double) rowN; ++cnt; }
            }
            const double monoLoss = cnt > 0 ? sumDb / (double) cnt : 0.0;
            const bool bad = monoLoss <= r.threshold;
            Subst s;
            s.valor = fmtNoSign (-monoLoss, 1);
            setDevice (4, RuleId::devClub, "club", bad ? devices::DeviceVerdict::fail : devices::DeviceVerdict::ok,
                       (float) monoLoss, 0.0f, bad ? "club.bad" : "club.ok", s,
                       "monoLoss " + fmt (monoLoss, 2) + " dB < 120 Hz", cnt > 0);
        }

        // --- hi-fi: no agrega chequeos, hereda la sección 1 ---
        {
            Subst s;
            s.valor = std::to_string (feelFindings);
            const bool bad = feelFindings > 0;
            // 56b: singular y plural son DOS frases, no una con "1 findings". La forma en que un idioma
            // marca el plural no es un sufijo que se pueda pegar afuera (en francés el verbo también
            // cambia), así que se elige la frase entera.
            const char* hifiKey = ! bad ? "hi-fi.ok"
                                        : (feelFindings == 1 ? "hi-fi.bad.one" : "hi-fi.bad.many");
            setDevice (5, RuleId::devHifi, "hi-fi", bad ? devices::DeviceVerdict::warn : devices::DeviceVerdict::ok,
                       (float) feelFindings, 0.0f, hifiKey, s,
                       std::to_string (feelFindings) + " (1)", feelEvaluable);
        }
    }

    // ====================================================================== 3 · qué falta y dónde
    const auto rowSecond = [&] (int i) { return in.firstSecond + i; };

    // --- huecos: una banda por debajo de SU PROPIA media del tema, sostenido ---
    if (haveShape)
    {
        const auto& r = rules::rule (RuleId::hole);
        const int minRun = (int) r.threshold2;

        // La racha de cada banda que llega al umbral. `longest` es la racha más larga de CUALQUIER banda,
        // llegue o no a los 10 s: es el número de la línea de "Dentro de rango" cuando no hay huecos.
        struct HoleRun { int band, t0, t1, run; double drop; };
        std::vector<HoleRun> holes;
        int longest = 0;

        // la forma media, para comparar cada fila contra el resto del tema y no contra el nivel
        for (int b = 0; b < kNumBands; ++b)
        {
            if (! program.has[b]) continue;
            int run = 0, bestRun = 0, bestEnd = -1;
            double bestDrop = 0.0, runDrop = 0.0;

            for (const int i : spectral)
            {
                bool rowHas[kNumBands] {};
                for (int k = 0; k < kNumBands; ++k) rowHas[k] = rows[i].bandMeasured (k);
                const Shape rs = shapeOf (rows[i].bandsDb, rowHas);
                const double drop = (rs.measured >= 10 && rs.has[b])
                                      ? (double) program.value[b] - (double) rs.value[b] : 0.0;
                if (drop >= r.threshold)
                {
                    ++run;
                    runDrop += drop;
                    if (run > bestRun) { bestRun = run; bestEnd = i; bestDrop = runDrop / (double) run; }
                }
                else { run = 0; runDrop = 0.0; }
            }

            longest = std::max (longest, bestRun);
            if (bestRun >= minRun && bestEnd >= 0)
                holes.push_back ({ b, rowSecond (bestEnd - bestRun + 1), rowSecond (bestEnd) + 1, bestRun, bestDrop });
        }

        // ===== 57d · UN EVENTO, UNA FRASE =====
        //
        // Hasta el 57c un pozo que atravesaba varias bandas salía una vez POR BANDA: la señal de prueba (400 a
        // 1 000 Hz sacados entre 0:20 y 0:35) daba cinco frases rojas para un solo evento, y eso es buena parte
        // de lo que se leía como "tira para abajo". Lo que se mide no cambia —cada banda pasa por la misma
        // condición de siempre—; cambia cómo se cuenta. Bandas CONTIGUAS (índices consecutivos de ⅓ de
        // octava) cuyos tramos [t0, t1) se SOLAPAN son un solo hueco, y la frase sale con la banda más honda,
        // su caída y su tramo, más el rango de bandas que abarca (`values` = caída, banda baja, banda alta,
        // segundos). La cadena se arma de a pares, cada banda contra la anterior: dos pozos en bandas vecinas
        // pero en momentos que no se tocan siguen siendo dos frases.
        for (size_t i = 0; i < holes.size(); )
        {
            size_t j = i + 1;
            while (j < holes.size() && holes[j].band == holes[j - 1].band + 1
                   && holes[j].t0 < holes[j - 1].t1 && holes[j - 1].t0 < holes[j].t1)
                ++j;

            size_t deepest = i;
            int spanT0 = holes[i].t0, spanT1 = holes[i].t1;
            for (size_t k = i + 1; k < j; ++k)
            {
                if (holes[k].drop > holes[deepest].drop) deepest = k;
                spanT0 = std::min (spanT0, holes[k].t0);
                spanT1 = std::max (spanT1, holes[k].t1);
            }
            const auto& w = holes[deepest];
            const double loHz = kThirdOctaveHz[holes[i].band];
            const double hiHz = kThirdOctaveHz[holes[j - 1].band];

            // LA VENTANA ES LA DEL TRAMO ENTERO —la unión de las ventanas de sus bandas—, no la de la banda más
            // honda. Con las bandas corridas en el tiempo (se solapan pero no empiezan juntas) la de la más
            // honda achicaba el pozo y atrasaba "el primero" del titular (revisor del 57d). La racha de
            // `values[3]` y la evidencia siguen siendo las de la banda más honda: es de la que habla el número.
            Subst s;
            s.valor  = fmtNoSign (w.drop, 1);
            s.banda  = fmtNoSign (kThirdOctaveHz[w.band], 0);
            s.valor2 = fmtNoSign (loHz, 0);
            s.valor3 = fmtNoSign (hiHz, 0);
            s.t0     = timeLabel (spanT0);
            s.t1     = timeLabel (spanT1);
            // La severidad sale de la TABLA (⚠ desde el 57d): un pozo medido es algo para revisar; ● queda para
            // lo objetivamente roto (clips, continua, contrafase).
            push (RuleId::hole, r.severity, s, j - i > 1 ? "hole" : "hole.one", spanT0, spanT1, w.band,
                  { (float) w.drop, (float) loHz, (float) hiHz, (float) w.run },
                  "-" + fmtNoSign (w.drop, 2) + " dB / " + std::to_string (w.run) + " s");
            i = j;
        }

        if (holes.empty())
        {
            Subst s;
            s.valor  = fmtNoSign (r.threshold, 0);
            s.valor2 = fmtNoSign (r.threshold2, 0);
            s.t0     = std::to_string (longest);
            strength (RuleId::hole, "hole.ok", s, { (float) longest, r.threshold, r.threshold2 },
                      std::to_string (longest) + " s");
        }
    }

    // --- secciones baja y alta ---
    if (in.aggregates.loudness.integratedValid && ! loud.empty())
    {
        struct SectionRule { RuleId id; const char* key; bool below; };
        const SectionRule kSections[] = { { RuleId::quietSection, "quiet-section", true },
                                          { RuleId::loudSection,  "loud-section",  false } };
        const float I = in.aggregates.loudness.integrated;

        for (const auto& sr : kSections)
        {
            const auto& r = rules::rule (sr.id);
            const int minRun = (int) r.threshold2;
            int run = 0;
            double acc = 0.0;
            const auto before = rep.findings.size();   // 57d: ¿esta regla dijo algo?

            const auto close = [&] (int endIdx)
            {
                if (run < minRun) { run = 0; acc = 0.0; return; }
                const int t0 = rowSecond (endIdx - run + 1);
                const int t1 = rowSecond (endIdx) + 1;
                const double mean = acc / (double) run;
                Subst s;
                s.valor = fmtNoSign (mean, 1);
                s.t0 = timeLabel (t0);
                s.t1 = timeLabel (t1);
                push (sr.id, r.severity, s, sr.key, t0, t1, -1, { (float) mean, (float) run },
                      fmtNoSign (mean, 2) + " LU / " + std::to_string (run) + " s");
                run = 0; acc = 0.0;
            };

            for (size_t k = 0; k < loud.size(); ++k)
            {
                const int i = loud[k];
                const float st = sr.below ? rows[i].shortTermMax : rows[i].shortTermMin;
                const double d = sr.below ? (double) I - (double) st : (double) st - (double) I;
                if (st > -60.0f && d >= r.threshold) { ++run; acc += d; }
                else if (run > 0) close (i - 1);
            }
            if (run > 0) close (loud.back());

            // 57d — el tramo bajo que NO aparece también se dice. El alto no: es ○ informativo, no hay un
            // rango del que estar adentro.
            if (sr.below && rep.findings.size() == before)
            {
                const float lra = in.aggregates.loudness.lra;
                Subst s;
                s.valor  = fmtNoSign (lra, 1);
                s.valor2 = fmtNoSign (r.threshold, 0);
                s.t0     = fmtNoSign (r.threshold2, 0);
                strength (RuleId::quietSection, "quiet-section.ok", s, { lra, r.threshold, r.threshold2 },
                          "LRA " + fmtNoSign (lra, 1) + " LU");
            }
        }
    }

    // --- ráfagas de picos ---
    //
    // La condición es "≥ N eventos en una ventana de W segundos", pero lo que se REPORTA es la ráfaga
    // ENTERA que contiene esa ventana, con su total y su tramo. Reportar la primera ventana que cruza el
    // umbral diría "6 eventos entre 0:10 y 0:15" sobre una ráfaga de 10 que va de 0:12 a 0:17: el número
    // sería cierto y el tramo estaría corrido, que es la peor combinación posible.
    if (! loud.empty())
    {
        const auto& r = rules::rule (RuleId::peaks);
        const int window = (int) r.threshold2;
        const int need   = (int) r.threshold;
        bool anyBurst = false;   // 57d

        for (size_t k = 0; k < loud.size(); )
        {
            if (rows[loud[k]].clipEvents == 0) { ++k; continue; }

            // la ráfaga: filas consecutivas con al menos un evento
            size_t end = k;
            juce::uint32 total = 0;
            while (end < loud.size() && rows[loud[end]].clipEvents > 0)
            {
                total += rows[loud[end]].clipEvents;
                ++end;
            }

            // ¿hay alguna ventana de W segundos con N o más? (una ráfaga más corta que la ventana cuenta
            // igual: cinco clips en dos segundos son cinco clips en cinco segundos)
            bool dense = false;
            for (size_t a = k; a < end && ! dense; ++a)
            {
                juce::uint32 w = 0;
                for (size_t b = a; b < end && b < a + (size_t) window; ++b) w += rows[loud[b]].clipEvents;
                dense = (int) w >= need;
            }

            if (dense)
            {
                anyBurst = true;
                const int t0 = rowSecond (loud[k]);
                const int t1 = rowSecond (loud[end - 1]) + 1;
                Subst s;
                s.valor  = std::to_string ((int) total);
                s.valor2 = fmt (in.clipThresholdDbtp, 1);
                s.t0 = timeLabel (t0);
                s.t1 = timeLabel (t1);
                push (RuleId::peaks, Severity::bad, s, "peaks", t0, t1, -1,
                      { (float) total, in.clipThresholdDbtp },
                      std::to_string ((int) total) + " / " + std::to_string ((int) (end - k)) + " s");
            }
            k = end;
        }

        // 57d — sin ráfagas. La línea dice también cuántos eventos SUELTOS hubo: "no hay ráfagas" con tres
        // clips aislados es cierto, y esconder esos tres sería decir menos de lo que se midió.
        if (! anyBurst)
        {
            juce::uint32 events = 0;
            for (const int i : loud) events += rows[i].clipEvents;
            Subst s;
            s.valor  = fmt (in.clipThresholdDbtp, 1);
            s.valor2 = std::to_string ((int) events);
            strength (RuleId::peaks, "peaks.ok", s, { (float) events, in.clipThresholdDbtp },
                      std::to_string ((int) events) + " / " + std::to_string ((int) loud.size()) + " s");
        }
    }

    // --- bandas fuera de fase ---
    if (haveShape)
    {
        const auto& r = rules::rule (RuleId::outOfPhase);
        const auto before = rep.findings.size();   // 57d
        double lowestMean = 1.0;                   // la correlación media más baja de las bandas con señal
        int    eligible   = 0;
        for (int b = 0; b < kNumBands; ++b)
        {
            if (! hasBand[b] || avgBands[b] <= loudestBand + kPhaseFloorDb) continue;
            int neg = 0, tot = 0;
            double corrSum = 0.0;
            for (const int i : spectral)
            {
                if (! rows[i].bandMeasured (b)) continue;
                ++tot;
                corrSum += rows[i].bandCorr[b];
                // El signo, fila por fila: el 0 es literal (una banda "en negativo" es corr < 0) y no un
                // umbral de la tabla — el umbral de la tabla es el de la MEDIA, acá abajo.
                if (rows[i].bandCorr[b] < 0.0f) ++neg;
            }
            if (tot < kMinRowsForShape) continue;
            const double frac = (double) neg / (double) tot;
            const double mean = corrSum / (double) tot;
            ++eligible;
            lowestMean = std::min (lowestMean, mean);
            // LAS DOS CONDICIONES, y la segunda es la que hace honesta a la regla: material simplemente
            // DECORRELACIONADO (una reverb ancha, dobles a los costados) oscila alrededor de cero y pasa
            // la mitad del tiempo en negativo sin que haya nada roto. Lo que se cancela al monoficar es lo
            // que está CLARAMENTE en contrafase, y eso se ve en la media, no en el signo instantáneo.
            if (frac <= r.threshold2 || mean > r.threshold) continue;

            Subst s;
            s.valor = fmtNoSign (frac * 100.0, 0);
            s.banda = fmtNoSign (kThirdOctaveHz[b], 0);
            push (RuleId::outOfPhase, Severity::bad, s, "out-of-phase", -1, -1, b,
                  { (float) (frac * 100.0), (float) tot },
                  fmtNoSign (frac * 100.0, 0) + " % / " + std::to_string (tot) + " s");
        }

        if (eligible > 0 && rep.findings.size() == before)
        {
            Subst s;
            s.valor = fmt (lowestMean, 2);
            strength (RuleId::outOfPhase, "out-of-phase.ok", s, { (float) lowestMean, (float) eligible },
                      "corr " + fmt (lowestMean, 2) + " / " + std::to_string (eligible));
        }
    }

    // --- desbalance L/R sostenido ---
    if (! spectral.empty())
    {
        const auto& r = rules::rule (RuleId::imbalance);
        const int minRun = (int) r.threshold2;
        int run = 0;
        double acc = 0.0;

        const auto close = [&] (int endIdx)
        {
            if (run < minRun) { run = 0; acc = 0.0; return; }
            const int t0 = rowSecond (endIdx - run + 1);
            const int t1 = rowSecond (endIdx) + 1;
            const double mean = acc / (double) run;
            Subst s;
            s.valor  = fmt (mean, 1);
            s.valor2 = std::to_string (run);
            s.t0 = timeLabel (t0);
            s.t1 = timeLabel (t1);
            push (RuleId::imbalance, Severity::warn, s, "imbalance", t0, t1, -1,
                  { (float) mean, (float) run },
                  fmt (mean, 2) + " dB / " + std::to_string (run) + " s");
            run = 0; acc = 0.0;
        };

        const auto before = rep.findings.size();   // 57d
        double balSum = 0.0;
        for (size_t k = 0; k < spectral.size(); ++k)
        {
            const int i = spectral[k];
            balSum += rows[i].balanceDb;
            if (std::abs (rows[i].balanceDb) >= r.threshold) { ++run; acc += rows[i].balanceDb; }
            else if (run > 0) close (spectral[k - 1]);
        }
        if (run > 0) close (spectral.back());

        if (rep.findings.size() == before)
        {
            const double avg = balSum / (double) spectral.size();
            Subst s;
            s.valor  = fmt (avg, 1);
            s.valor2 = fmtNoSign (r.threshold, 0);
            s.t0     = fmtNoSign (r.threshold2, 0);
            strength (RuleId::imbalance, "imbalance.ok", s, { (float) avg, r.threshold, r.threshold2 },
                      fmt (avg, 2) + " dB");
        }
    }

    // --- continua ---
    {
        const auto& r = rules::rule (RuleId::dcOffset);
        const float dcL = in.aggregates.dcL, dcR = in.aggregates.dcR;
        // 57d: la evidencia dice el MEDIDO (el peor de los dos canales); el umbral va en el sufijo de regla.
        const std::string worstDc = fmtNoSign (std::max (std::abs (dcL), std::abs (dcR)), 4);
        if (std::abs (dcL) > r.threshold || std::abs (dcR) > r.threshold)
        {
            Subst s;
            s.valor  = fmt (dcL, 4);
            s.valor2 = fmt (dcR, 4);
            push (RuleId::dcOffset, Severity::bad, s, "dc", -1, -1, -1, { dcL, dcR }, worstDc);
        }
        else if (n > 0)   // sin un segundo analizado no hay de qué estar dentro de rango
        {
            Subst s;
            s.valor  = fmt (dcL, 4);
            s.valor2 = fmt (dcR, 4);
            s.t0     = fmtNoSign (r.threshold, 2);
            strength (RuleId::dcOffset, "dc.ok", s, { dcL, dcR }, worstDc);
        }
    }

    // --- plataformas ---
    {
        const float I  = in.aggregates.loudness.integrated;
        const float tp = in.aggregates.loudness.truePeakMax;
        const bool  ok = in.aggregates.loudness.integratedValid;

        for (int t = 0; t < kNumTargets; ++t)
        {
            const auto& target = kStreamingTargets[t];
            if (! target.hasTarget) continue;

            PlatformDelta d;
            d.name           = target.name;
            d.targetLufs     = target.targetLufs;
            d.deltaDb        = ok ? I - target.targetLufs : 0.0f;
            d.ceilingDbtp    = (ok && I > target.targetLufs) ? target.ceilingDbtpLoud : target.ceilingDbtp;
            d.truePeakDbtp   = tp;
            d.attenuatesOnly = target.attenuatesOnly;
            d.overCeiling    = ok && tp > d.ceilingDbtp;
            rep.platforms.push_back (d);
        }

        // El hallazgo de la sección 3 sale SÓLO para el objetivo elegido en la lente LOUDNESS: el resto
        // vive en la tabla `platforms`, que la lente dibuja entera. Un informe con seis frases de
        // plataforma sería seis veces el mismo dato.
        const auto& sel = streamingTarget (in.targetIndex);
        if (ok && sel.hasTarget)
        {
            const float delta = I - sel.targetLufs;
            const float ceil  = I > sel.targetLufs ? sel.ceilingDbtpLoud : sel.ceilingDbtp;
            if (std::abs (delta) >= 0.5f)
            {
                const char* verbKey = delta > 0.0f ? "platform.down"
                                                   : (sel.attenuatesOnly ? "platform.only" : "platform.up");
                Subst s;
                s.valor  = fmtNoSign (std::abs (delta), 1);
                s.valor2 = fmt (I, 1);
                s.valor3 = rules::phrase (verbKey, lang);
                s.banda  = sel.name;
                push (RuleId::platform, Severity::info, s, "platform", -1, -1, -1,
                      { delta, I, sel.targetLufs },
                      std::string (sel.name) + " " + fmt (sel.targetLufs, 1) + " LUFS");
            }
            if (tp > ceil)
            {
                Subst s;
                s.valor  = fmt (tp, 2);
                s.valor2 = fmt (ceil, 1);
                s.banda  = sel.name;
                push (RuleId::platform, Severity::warn, s, "platform.tp", -1, -1, -1, { tp, ceil },
                      std::string (sel.name) + " <= " + fmt (ceil, 1) + " dBTP");
            }
        }
    }

    // ---- el resumen, ya renderizado ----
    {
        Subst s;
        s.valor  = std::to_string (sum.seconds);
        s.valor2 = fmt (sum.integrated, 1);
        s.valor3 = fmtNoSign (sum.lra, 1);
        sum.text = render ("summary", lang, s);
    }

    // ---- 57d · "Dentro de rango" en el orden de la tabla, no en el orden en que se evaluó ----
    std::stable_sort (rep.strengths.begin(), rep.strengths.end(),
                      [] (const VerdictFinding& a, const VerdictFinding& b) { return (int) a.ruleId < (int) b.ruleId; });

    // ===== 57d · EL TITULAR =====
    // La cuenta de lo medido, sin adjetivos: n chequeos dentro de rango · m para revisar, el primero en t0.
    // `m` son los HALLAZGOS (⚠ y ●); las ○ no son "para revisar".
    //
    // SIN UN SOLO SEGUNDO ANALIZADO NO SE CUENTA NADA — ni n, ni m, ni "el primero", y el titular queda vacío.
    // Contar "0 chequeos dentro de rango · nada fuera de estas reglas" sería decir que se midió. Y hay un caso
    // concreto (revisor del 57d, HIGH): sin filas la caja del celular sale ⚠ —su armónico relativo vale −60 por
    // defecto, desde antes del 57d— y la primera versión la contaba como "1 para revisar" sin haber medido nada.
    if (n > 0)
    {
        int m = 0, first = -1;
        for (const auto& f : rep.findings)
        {
            if (f.severity == Severity::info) continue;
            ++m;
            if (f.t0 >= 0 && (first < 0 || f.t0 < first)) first = f.t0;
        }
        sum.checksWithin = (int) rep.strengths.size() + devicesWithin;
        sum.toCheck      = m;
        sum.firstAt      = first;

        Subst s;
        s.valor  = std::to_string (sum.checksWithin);
        s.valor2 = std::to_string (m);
        s.t0     = first >= 0 ? timeLabel (first) : std::string();
        const char* key = m == 0 ? "headline.none"
                        : m == 1 ? (first >= 0 ? "headline.one" : "headline.one.untimed")
                                 : (first >= 0 ? "headline"     : "headline.untimed");
        sum.headline         = render (key, lang, s);
        sum.headlineEvidence = "headline · " + std::to_string (sum.checksWithin) + "/" + std::to_string (m);
    }

    return rep;
}
}
