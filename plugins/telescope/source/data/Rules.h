#pragma once
#include <cstddef>
#include <cstring>

// ========================================================================================================
// Rules.h — LA TABLA DE VERDICT (spec §5.8, D-47). Métrica → umbral → frase.
//
// Acá no hay lógica: hay DATOS. Cada regla dice qué mide, con qué umbral, con qué severidad y con qué
// frase — y de dónde sale el umbral. El motor (`analysis/modules/Verdict.cpp`) implementa las condiciones
// leyendo los umbrales de acá; el gemelo legible para Joaquín está en `plugins/telescope/docs/telescope-diccionario.md`.
//
// LA REGLA DE HIERRO (D-47): cada frase lleva el NÚMERO que la sostiene y el ID de la regla que la
// produjo. Prohibido opinar: la lista de palabras que ninguna de las seis tablas puede usar vive en el test
// VERDICT[tono], que las barre enteras. Vocabulario de mezcla ↔ número.
//
// 57d · CÓMO SE ESCRIBE UNA FRASE DE HALLAZGO: el número antes del primer punto, el término de mezcla entre
// paréntesis, y al final DÓNDE MIRAR ("Check …" / "Revisa …") — nunca QUÉ HACER ("cortá 2 dB" es gusto).
// Lo que se evaluó y quedó dentro de rango también se dice (`*.ok`), con su número y su límite.
//
// ========================================================================================================
// IDIOMAS (D-50, resuelta por Joaquín el 8-sep: "inglés por defecto y posibilidad en todos los idiomas")
//
// El setting `language` es un CÓDIGO ISO 639-1 (`en`, `es`, `pt`, `fr`, `de`, `it`, …), no un enum de dos
// valores. Default `en`. Una tabla por idioma, buscada POR CLAVE con fallback a `en`: si a un idioma le
// falta una frase, sale en inglés — nunca vacía y nunca con la plantilla sin resolver.
//
// AGREGAR UN IDIOMA ES AGREGAR UNA TABLA. Cero código: se escribe el array de `Phrase`, se lo suma a
// `kLanguages` y nada más. Por eso las claves son strings y no un enum: un idioma incompleto tiene que
// compilar igual.
//
// Las 6 tablas de acá son `en` y `es` (revisadas) más `pt`, `fr`, `de` e `it` (traducidas con los términos
// de la industria — Integrated, Short-term, True Peak, Correlation, Width, Mono compatibility, Tonal
// balance, Key — y PENDIENTES de revisión de hablante nativo; está dicho en el reporte y en el README).
//
// PLANTILLAS. Las frases llevan `{valor}`, `{valor2}`, `{valor3}`, `{banda}`, `{t0}` y `{t1}`. El motor
// las reemplaza SIEMPRE (VERDICT[plantillas] escanea las seis lenguas y falla si queda una llave suelta).
// ========================================================================================================
namespace telescope::rules
{
// 57d · `withinRange` va AL FINAL (append-only): es la sección de las reglas que se evaluaron y NO se
// dispararon. Sus líneas no son hallazgos y viven en `VerdictReport::strengths`, no en `findings`.
enum class Section { feel = 0, translate, missing, withinRange, kNumSections };
enum class Severity { info = 0, warn, bad };   // ○  ⚠  ●

enum class RuleId
{
    // --- 1 · cómo se va a sentir ---
    crushed = 0, thin, muddy, harsh, noAir, hollowCentre, key,
    // --- 2 · dónde traduce (una por dispositivo) ---
    devPhone, devHeadphones, devLaptop, devCar, devClub, devHifi,
    // --- 3 · qué falta y dónde ---
    hole, quietSection, loudSection, peaks, outOfPhase, imbalance, dcOffset, platform,
    kNumRules
};

// ========================================================================================================
// LA TABLA. `threshold` es el umbral principal en las unidades de `units`; `threshold2` el secundario
// (fracción de tiempo, segundos sostenidos, o el agravante), o 0 si la regla no tiene.
// ========================================================================================================
struct Rule
{
    RuleId      id;
    const char* name;         // el id textual que sale en la evidencia, al lado del número
    Section     section;
    Severity    severity;     // la severidad BASE (una regla puede agravarse: ver `worse`)
    const char* metric;       // qué se mide, en una línea
    float       threshold;
    float       threshold2;
    const char* units;
    const char* source;       // de dónde sale el umbral
};

inline constexpr Rule kRules[] = {
//   id                        name             sección              sev                métrica                                                     umbral  umbral2  unidades  fuente
    { RuleId::crushed,       "crushed",       Section::feel,      Severity::warn, "PSR medio (pico real - short-term) sobre las filas con medida",   8.0f,   0.0f,  "dB",   "AES TD1004 (PSR); el 8 es convencion de la casa" },
    { RuleId::thin,          "thin",          Section::feel,      Severity::warn, "150-400 Hz contra la tendencia del propio material (o la ref)",   3.0f,   0.0f,  "dB",   "convencion de la casa: 3 dB es el escalon audible en una region ancha" },
    { RuleId::muddy,         "muddy",         Section::feel,      Severity::warn, "200-500 Hz contra la tendencia del propio material (o la ref)",   3.0f,   0.0f,  "dB",   "convencion de la casa (misma escala que `thin`)" },
    { RuleId::harsh,         "harsh",         Section::feel,      Severity::warn, "2-5 kHz contra la tendencia, sostenido en el tiempo",             3.0f,   0.30f, "dB",   "convencion de la casa; se agrava con I > -10 LUFS" },
    { RuleId::noAir,         "no-air",        Section::feel,      Severity::warn, "por encima de 10 kHz contra la tendencia del propio material",    3.0f,   0.0f,  "dB",   "convencion de la casa (misma escala que `thin`)" },
    { RuleId::hollowCentre,  "hollow-centre", Section::feel,      Severity::warn, "width medio 300 Hz-2 kHz vs width agudo (> 2 kHz)",               0.4f,   1.2f,  "",     "width = sqrt(SS/MM), spec 5.3; los dos topes son convencion de la casa" },
    { RuleId::key,           "key",           Section::feel,      Severity::info, "tonalidad estimada por correlacion con perfiles Krumhansl",       0.0f,   0.0f,  "",     "Krumhansl-Schmuckler; se muestra con su confianza y su % de tiempo" },

    { RuleId::devPhone,      "phone",         Section::translate, Severity::warn, "fraccion de energia < 300 Hz y armonicos del bajo en 300-1200",  60.0f, -20.0f,  "%",    "convencion de la casa; un altavoz de telefono no tiene respuesta util por debajo de 300 Hz" },
    { RuleId::devHeadphones, "headphones",    Section::translate, Severity::warn, "exceso 2-5 kHz y correlacion por banda > 8 kHz (media <= -0.2)",  3.0f,   0.20f, "dB",   "convencion de la casa; en auricular el estereo se escucha separado. El 20 % del tiempo y el -0.2 de la media, como `out-of-phase`" },
    { RuleId::devLaptop,     "laptop",        Section::translate, Severity::warn, "presencia 2-4 kHz contra la tendencia del propio material",       3.0f,   0.0f,  "dB",   "convencion de la casa (misma escala que `thin`); un altavoz de laptop pone todo el peso en la presencia" },
    { RuleId::devCar,        "car",           Section::translate, Severity::warn, "exceso por debajo de 100 Hz contra la tendencia",                 4.0f,   0.0f,  "dB",   "convencion de la casa; el habitaculo de un auto realza los graves por los modos del recinto" },
    { RuleId::devClub,       "club",          Section::translate, Severity::warn, "perdida al monoficar por debajo de 120 Hz",                      -3.0f,   0.0f,  "dB",   "el sub de un club es MONO; monoLoss segun spec 5.3" },
    { RuleId::devHifi,       "hi-fi",         Section::translate, Severity::info, "no agrega chequeos propios: hereda los hallazgos de la seccion 1", 0.0f,  0.0f,  "",     "un equipo full-range no esconde nada; lo que falla ahi ya lo dijo la seccion 1" },

    { RuleId::hole,          "hole",          Section::missing,   Severity::warn, "banda por debajo de su propia media del tema, sostenido",         6.0f,  10.0f,  "dB",   "convencion de la casa: 6 dB durante 10 s es un hueco, no una variacion. 57d: warn y no bad (un pozo medido es algo a revisar; bad queda para lo roto: peaks, dc, out-of-phase)" },
    { RuleId::quietSection,  "quiet-section", Section::missing,   Severity::warn, "short-term por debajo del integrado, sostenido",                  3.0f,   8.0f,  "LU",   "3 LU es el escalon de la escala EBU; 8 s es convencion de la casa" },
    { RuleId::loudSection,   "loud-section",  Section::missing,   Severity::info, "short-term por encima del integrado, sostenido",                  3.0f,   8.0f,  "LU",   "idem `quiet-section`, del otro lado" },
    { RuleId::peaks,         "peaks",         Section::missing,   Severity::bad,  "rafaga de eventos de clip sobre el umbral vigente",               5.0f,   5.0f,  "eventos", "convencion de la casa: 5 eventos en 5 s es una rafaga, no un pico" },
    { RuleId::outOfPhase,    "out-of-phase",  Section::missing,   Severity::bad,  "correlacion MEDIA de la banda, y fraccion del tiempo en negativo", -0.2f,  0.20f, "",     "corr < 0 = la banda se cancela al monoficar (spec 5.3). El -0.2 de la MEDIA es convencion de la casa: material simplemente decorrelacionado (reverb ancha, dobles) oscila alrededor de 0 y pasa la mitad del tiempo en negativo sin que haya nada roto; -0.2 equivale a perder ~1 dB MAS que la decorrelacion pura al monoficar" },
    { RuleId::imbalance,     "imbalance",     Section::missing,   Severity::warn, "|balance L/R| sostenido",                                         3.0f,  20.0f,  "dB",   "convencion de la casa: 3 dB durante 20 s no es un pasaje, es la mezcla" },
    { RuleId::dcOffset,      "dc",            Section::missing,   Severity::bad,  "media de las muestras (continua), por canal",                     0.01f,  0.0f,  "",     "convencion de la casa: 0.01 = -40 dBFS de continua, ya se come headroom" },
    { RuleId::platform,      "platform",      Section::missing,   Severity::info, "integrado y pico real contra el objetivo de cada plataforma",     0.0f,   0.0f,  "dB",   "source/data/StreamingTargets.h (verificado 2026-09-07)" },
};

static_assert (sizeof (kRules) / sizeof (kRules[0]) == (size_t) RuleId::kNumRules,
               "la tabla tiene que tener una entrada por regla");

inline constexpr const Rule& rule (RuleId id) noexcept { return kRules[(int) id]; }

// ========================================================================================================
// LAS FRASES, por idioma. Ver el encabezado: una tabla por código, búsqueda por clave, fallback a `en`.
// ========================================================================================================
struct Phrase { const char* key; const char* text; };

inline constexpr Phrase kPhrasesEn[] = {
    { "ui.reset",            "RESET" },
    { "ui.reset.value",      "from 0" },
    { "ui.mode",             "MODE" },
    { "ui.file",             "FILE" },
    { "ui.file.value",       "LOAD" },
    { "ui.language",         "LANGUAGE" },
    { "ui.drop",               "drag a file here, or press LOAD" },
    { "ui.nosecs",             "no complete seconds yet: let the track play from the start, or use FILE" },
    { "ui.choose",             "Choose the track VERDICT has to analyse" },
    { "ui.analysing",          "analysing " },
    { "ui.unreadable",         "could not read: " },
    { "ui.notafile",           "that is not a file" },
    { "ui.notaudio",           "\" is not audio (" },
    { "section.within",     "Within range" },
    { "section.feel",       "How it will feel" },
    { "section.translate",  "Where it translates" },
    { "section.missing",    "What to check, and where" },
    { "footer",             "Measurement, not taste. Device checks are generic. Re-run the analysis after every change." },
    { "none",               "Nothing outside the ranges of these rules. They measure; what they can't hear is yours." },
    { "mode.live",          "LIVE (since RESET)" },
    { "mode.file",          "FILE" },
    { "summary",            "{valor} s analysed" },
    { "baseline.trend",     "vs the material's own spectral trend" },
    { "baseline.reference", "vs the loaded reference" },
    { "baseline.trend.short",     "vs the trend" },
    { "baseline.reference.short", "vs the reference" },
    // 57d · EL TITULAR: la cuenta de lo medido, sin adjetivos.
    { "headline",             "{valor} checks within range \xc2\xb7 {valor2} to look at, the first at {t0}" },
    { "headline.untimed",     "{valor} checks within range \xc2\xb7 {valor2} to look at" },
    { "headline.one",         "{valor} checks within range \xc2\xb7 1 to look at, at {t0}" },
    { "headline.one.untimed", "{valor} checks within range \xc2\xb7 1 to look at" },
    { "headline.none",        "{valor} checks within range \xc2\xb7 nothing outside these rules" },

    // 57d · el número primero, el término de mezcla entre paréntesis, y al final dónde mirar (ver arriba).
    { "crushed",       "PSR {valor} dB, under the {valor2} dB floor: transients are flattened (crushed). Check the limiter's input." },
    { "thin",          "150-400 Hz sits {valor} dB low {valor3} (what mixers call thin). Check what gives that range its body." },
    { "muddy",         "200-500 Hz sits {valor} dB high {valor3} (what mixers call muddy). Check what builds up in that range." },
    { "harsh",         "2-5 kHz sits {valor} dB high {valor3} for {valor2} % of the time (what mixers call harsh). Check the presence range." },
    { "harsh.loud",    "2-5 kHz sits {valor} dB high {valor3} for {valor2} % of the time, at {t0} LUFS integrated (harsh and loud). Check the presence range and the limiter's input." },
    { "no-air",        "Above 10 kHz the level sits {valor} dB low {valor3} (what mixers call no air). Check the top end." },
    { "hollow-centre", "Width {valor} in the mids (300 Hz-2 kHz) against {valor2} in the highs (what mixers call a hollow centre). Check how the highs are widened against the mids." },
    { "key",           "Estimated key: {banda}, confidence {valor}, best for {valor2} % of the time." },
    { "key.major",         "major" },
    { "key.minor",         "minor" },

    { "phone.ok",         "Phone: {valor} % of the energy sits below 300 Hz, which a phone speaker does not reproduce, but the bass harmonics in 300-1200 Hz are only {valor2} dB down: the bass line survives." },
    { "phone.bad",        "Phone: {valor} % of the energy is below 300 Hz and the bass harmonics in 300-1200 Hz are {valor2} dB down (the bass disappears). Check the bass harmonics above 300 Hz." },
    { "headphones.ok",    "Headphones: {valor} dB in 2-5 kHz and no band above 8 kHz out of phase." },
    { "headphones.bad",   "Headphones: {valor} dB high in 2-5 kHz, and a band above 8 kHz is out of phase {valor2} % of the time (tiring). Check the presence range and the width above 8 kHz." },
    { "laptop.ok",        "Laptop: presence at 2-4 kHz is {valor} dB, so the voice holds up on a small speaker." },
    { "laptop.bad",       "Laptop: presence at 2-4 kHz is {valor} dB under the material's own trend (the voice sinks). Check the voice's presence." },
    { "car.ok",           "Car: {valor} dB below 100 Hz, which a cabin will not turn into boom." },
    { "car.bad",          "Car: {valor} dB high below 100 Hz, and a cabin adds its own (boomy). Check the sub and the kick below 100 Hz." },
    { "club.ok",          "Club: {valor} dB of low end goes when the sub sums to mono." },
    { "club.bad",         "Club: {valor} dB of low end goes when the sub sums to mono. Check the bass below 120 Hz in mono." },
    { "hi-fi.ok",         "Hi-fi: full range hides nothing, and \"How it will feel\" has {valor} findings." },
    { "hi-fi.bad.one",    "Hi-fi: full range hides nothing, and \"How it will feel\" has 1 finding. Check it above." },
    { "hi-fi.bad.many",   "Hi-fi: full range hides nothing, and \"How it will feel\" has {valor} findings. Check them above." },

    { "hole",          "{banda} Hz dips {valor} dB below its own average (deepest band of a {valor2}-{valor3} Hz stretch that dips between {t0} and {t1}). Check what plays in that range there." },
    { "hole.one",      "{banda} Hz dips {valor} dB below its own average between {t0} and {t1}. Check what plays in that band there." },
    { "quiet-section", "{valor} LU under the integrated level between {t0} and {t1} (a quiet section). Check the arrangement and the gain there." },
    { "loud-section",  "{valor} LU over the integrated level between {t0} and {t1} (a loud section)." },
    { "peaks",         "{valor} clip events over {valor2} dBTP between {t0} and {t1} (a peak burst). Check the limiter's ceiling there." },
    { "out-of-phase",  "{banda} Hz out of phase {valor} % of the time: that band cancels when summed to mono. Check that band in mono." },
    { "imbalance",     "{valor} dB of L/R imbalance held for {valor2} s, from {t0} to {t1}. Check the panning there." },
    { "dc",            "DC offset {valor} on L and {valor2} on R: it eats headroom and does not sound. Check the source files for an offset." },
    { "platform",      "{banda}: {valor} dB {valor3} at {valor2} LUFS integrated." },
    { "platform.down", "turns you down" },
    { "platform.up",   "would turn you up" },
    { "platform.only", "away from the target (it only turns things down)" },
    { "platform.tp",   "{banda}: true peak {valor} dBTP is over its {valor2} dBTP ceiling. Check the limiter's ceiling." },

    // 57d · DENTRO DE RANGO. `within.*` es el nombre corto de cada regla (la línea colapsada, cuando el alto no
    // alcanza); `*.ok` es la línea entera, con el número medido y el límite de la regla.
    { "within.crushed",       "transients" },
    { "within.thin",          "body" },
    { "within.muddy",         "low mids" },
    { "within.harsh",         "presence" },
    { "within.no-air",        "top end" },
    { "within.hollow-centre", "centre" },
    { "within.hole",          "bands" },
    { "within.quiet-section", "loudness" },
    { "within.peaks",         "peaks" },
    { "within.out-of-phase",  "mono" },
    { "within.imbalance",     "balance" },
    { "within.dc",            "DC" },
    { "crushed.ok",        "Transients have room: PSR {valor} dB (floor {valor2} dB)" },
    { "thin.ok",           "Body 150-400 Hz: {valor} dB {valor3} (thin from -{valor2} dB)" },
    { "muddy.ok",          "Low mids 200-500 Hz: {valor} dB {valor3} (muddy from +{valor2} dB)" },
    { "harsh.ok",          "Presence 2-5 kHz: {valor} dB {valor3} (harsh from +{valor2} dB)" },
    { "harsh.ok.brief",    "Presence 2-5 kHz: over +{valor3} dB only {valor} % of the time (harsh from {valor2} %)" },
    { "no-air.ok",         "Top end open: {valor} dB above 10 kHz {valor3} (no air from -{valor2} dB)" },
    { "hollow-centre.ok",  "Width {valor} in the mids, {valor2} in the highs (hollow centre below {t0} with over {t1})" },
    { "hole.ok",           "No band dips {valor} dB below its own average for {valor2} s (longest run {t0} s)" },
    { "quiet-section.ok",  "Loudness holds: LRA {valor} LU, no section {valor2} LU under the integrated for {t0} s" },
    { "peaks.ok",          "No clip bursts over {valor} dBTP ({valor2} clip events in total)" },
    { "out-of-phase.ok",   "No band cancels in mono (lowest mean correlation {valor})" },
    { "imbalance.ok",      "L/R balanced: {valor} dB on average (imbalance from {valor2} dB held {t0} s)" },
    { "dc.ok",             "No DC offset: {valor} on L, {valor2} on R (limit {t0})" },
};

inline constexpr Phrase kPhrasesEs[] = {
    { "ui.reset",            "RESET" },
    { "ui.reset.value",      "desde 0" },
    { "ui.mode",             "MODO" },
    { "ui.file",             "ARCHIVO" },
    { "ui.file.value",       "CARGAR" },
    { "ui.language",         "IDIOMA" },
    { "ui.drop",               "arrastra un archivo aca, o toca CARGAR" },
    { "ui.nosecs",             "todavia no hay segundos completos: deja sonar el tema desde el principio, o usa ARCHIVO" },
    { "ui.choose",             "Elegi el tema que VERDICT tiene que analizar" },
    { "ui.analysing",          "analizando " },
    { "ui.unreadable",         "no se pudo leer: " },
    { "ui.notafile",           "eso no es un archivo" },
    { "ui.notaudio",           "\" no es audio (" },
    { "section.within",     "Dentro de rango" },
    { "section.feel",       "Como se va a sentir" },
    { "section.translate",  "Donde traduce" },
    { "section.missing",    "Que revisar y donde" },
    { "footer",             "Medicion, no gusto. Chequeos por dispositivo genericos. Rehace el analisis tras cada cambio." },
    { "none",               "Nada fuera de rango en estas reglas. Miden; lo que no escuchan es tuyo." },
    { "mode.live",          "EN VIVO (desde RESET)" },
    { "mode.file",          "ARCHIVO" },
    { "summary",            "{valor} s analizados" },
    { "baseline.trend",     "contra la tendencia espectral del propio material" },
    { "baseline.reference", "contra la referencia cargada" },
    { "baseline.trend.short",     "contra la tendencia" },
    { "baseline.reference.short", "contra la referencia" },
    { "headline",             "{valor} chequeos dentro de rango \xc2\xb7 {valor2} para revisar, el primero en {t0}" },
    { "headline.untimed",     "{valor} chequeos dentro de rango \xc2\xb7 {valor2} para revisar" },
    { "headline.one",         "{valor} chequeos dentro de rango \xc2\xb7 1 para revisar, en {t0}" },
    { "headline.one.untimed", "{valor} chequeos dentro de rango \xc2\xb7 1 para revisar" },
    { "headline.none",        "{valor} chequeos dentro de rango \xc2\xb7 nada fuera de estas reglas" },

    { "crushed",       "PSR {valor} dB, por debajo del piso de {valor2} dB: los transitorios quedan aplastados. Revisa la entrada del limitador." },
    { "thin",          "150-400 Hz esta {valor} dB abajo {valor3} (lo que en mezcla se llama delgado). Revisa que le da cuerpo a ese rango." },
    { "muddy",         "200-500 Hz esta {valor} dB arriba {valor3} (lo que en mezcla se llama turbio). Revisa que se acumula en ese rango." },
    { "harsh",         "2-5 kHz esta {valor} dB arriba {valor3} el {valor2} % del tiempo (lo que en mezcla se llama aspero). Revisa el rango de presencia." },
    { "harsh.loud",    "2-5 kHz esta {valor} dB arriba {valor3} el {valor2} % del tiempo, a {t0} LUFS integrados (aspero y fuerte). Revisa el rango de presencia y la entrada del limitador." },
    { "no-air",        "Por encima de 10 kHz el nivel esta {valor} dB abajo {valor3} (lo que en mezcla se llama sin aire). Revisa los agudos." },
    { "hollow-centre", "Width {valor} en los medios (300 Hz-2 kHz) contra {valor2} en los agudos (lo que en mezcla se llama centro vacio). Revisa como se abren los agudos contra los medios." },
    { "key",           "Tonalidad estimada: {banda}, confianza {valor}, la mejor el {valor2} % del tiempo." },
    { "key.major",         "mayor" },
    { "key.minor",         "menor" },

    { "phone.ok",         "Celular: el {valor} % de la energia esta por debajo de 300 Hz, que un parlante de telefono no reproduce, pero los armonicos del bajo en 300-1200 Hz estan solo {valor2} dB abajo: la linea de bajo se escucha igual." },
    { "phone.bad",        "Celular: el {valor} % de la energia esta por debajo de 300 Hz y los armonicos del bajo en 300-1200 Hz estan {valor2} dB abajo (el bajo desaparece). Revisa los armonicos del bajo por encima de 300 Hz." },
    { "headphones.ok",    "Auriculares: {valor} dB en 2-5 kHz y ninguna banda por encima de 8 kHz fuera de fase." },
    { "headphones.bad",   "Auriculares: {valor} dB arriba en 2-5 kHz, y una banda por encima de 8 kHz esta fuera de fase el {valor2} % del tiempo (cansador). Revisa el rango de presencia y el ancho por encima de 8 kHz." },
    { "laptop.ok",        "Laptop: la presencia en 2-4 kHz esta {valor} dB, asi que la voz aguanta en un parlante chico." },
    { "laptop.bad",       "Laptop: la presencia en 2-4 kHz esta {valor} dB por debajo de la tendencia del propio material (la voz se hunde). Revisa la presencia de la voz." },
    { "car.ok",           "Auto: {valor} dB por debajo de 100 Hz, que un habitaculo no va a convertir en retumbe." },
    { "car.bad",          "Auto: {valor} dB arriba por debajo de 100 Hz, y el habitaculo agrega los suyos (retumba). Revisa el sub y el bombo por debajo de 100 Hz." },
    { "club.ok",          "Club: se pierden {valor} dB de graves cuando el sub suma en mono." },
    { "club.bad",         "Club: se pierden {valor} dB de graves cuando el sub suma en mono. Revisa el bajo por debajo de 120 Hz en mono." },
    { "hi-fi.ok",         "Hi-fi: un equipo full-range no esconde nada, y \"Como se va a sentir\" tiene {valor} hallazgos." },
    { "hi-fi.bad.one",    "Hi-fi: un equipo full-range no esconde nada, y \"Como se va a sentir\" tiene 1 hallazgo. Revisalo arriba." },
    { "hi-fi.bad.many",   "Hi-fi: un equipo full-range no esconde nada, y \"Como se va a sentir\" tiene {valor} hallazgos. Revisalos arriba." },

    { "hole",          "{banda} Hz cae {valor} dB bajo su propia media (la banda mas honda de un tramo de {valor2}-{valor3} Hz que cae entre {t0} y {t1}). Revisa que suena en ese rango ahi." },
    { "hole.one",      "{banda} Hz cae {valor} dB bajo su propia media entre {t0} y {t1}. Revisa que suena en esa banda ahi." },
    { "quiet-section", "{valor} LU por debajo del integrado entre {t0} y {t1} (una seccion baja). Revisa el arreglo y la ganancia en ese tramo." },
    { "loud-section",  "{valor} LU por encima del integrado entre {t0} y {t1} (una seccion alta)." },
    { "peaks",         "{valor} eventos de clip sobre {valor2} dBTP entre {t0} y {t1} (una rafaga de picos). Revisa el techo del limitador en ese tramo." },
    { "out-of-phase",  "{banda} Hz fuera de fase el {valor} % del tiempo: esa banda se cancela al monoficar. Revisa esa banda en mono." },
    { "imbalance",     "{valor} dB de desbalance L/R sostenido {valor2} s, de {t0} a {t1}. Revisa el paneo en ese tramo." },
    { "dc",            "Continua de {valor} en L y {valor2} en R: se come headroom y no suena. Revisa si los archivos de origen tienen continua." },
    { "platform",      "{banda}: {valor} dB {valor3} a {valor2} LUFS integrados." },
    { "platform.down", "te baja" },
    { "platform.up",   "te subiria" },
    { "platform.only", "del objetivo (solo atenua)" },
    { "platform.tp",   "{banda}: el pico real de {valor} dBTP se pasa de su techo de {valor2} dBTP. Revisa el techo del limitador." },

    { "within.crushed",       "transitorios" },
    { "within.thin",          "cuerpo" },
    { "within.muddy",         "medios graves" },
    { "within.harsh",         "presencia" },
    { "within.no-air",        "agudos" },
    { "within.hollow-centre", "centro" },
    { "within.hole",          "bandas" },
    { "within.quiet-section", "loudness" },
    { "within.peaks",         "picos" },
    { "within.out-of-phase",  "mono" },
    { "within.imbalance",     "balance" },
    { "within.dc",            "continua" },
    { "crushed.ok",        "Los transitorios tienen aire: PSR {valor} dB (piso {valor2} dB)" },
    { "thin.ok",           "Cuerpo 150-400 Hz: {valor} dB {valor3} (delgado desde -{valor2} dB)" },
    { "muddy.ok",          "Medios graves 200-500 Hz: {valor} dB {valor3} (turbio desde +{valor2} dB)" },
    { "harsh.ok",          "Presencia 2-5 kHz: {valor} dB {valor3} (aspero desde +{valor2} dB)" },
    { "harsh.ok.brief",    "Presencia 2-5 kHz: pasa +{valor3} dB solo el {valor} % del tiempo (aspero desde el {valor2} %)" },
    { "no-air.ok",         "Agudos abiertos: {valor} dB por encima de 10 kHz {valor3} (sin aire desde -{valor2} dB)" },
    { "hollow-centre.ok",  "Width {valor} en los medios, {valor2} en los agudos (centro vacio con menos de {t0} y mas de {t1})" },
    { "hole.ok",           "Ninguna banda cae {valor} dB bajo su propia media durante {valor2} s (racha mas larga {t0} s)" },
    { "quiet-section.ok",  "El loudness se sostiene: LRA {valor} LU, ninguna seccion {valor2} LU bajo el integrado durante {t0} s" },
    { "peaks.ok",          "Sin rafagas de clip sobre {valor} dBTP ({valor2} eventos de clip en total)" },
    { "out-of-phase.ok",   "Ninguna banda se cancela en mono (correlacion media mas baja {valor})" },
    { "imbalance.ok",      "L/R balanceado: {valor} dB de media (desbalance desde {valor2} dB durante {t0} s)" },
    { "dc.ok",             "Sin continua: {valor} en L, {valor2} en R (limite {t0})" },
};

inline constexpr Phrase kPhrasesPt[] = {
    { "ui.reset",            "RESET" },
    { "ui.reset.value",      "do 0" },
    { "ui.mode",             "MODO" },
    { "ui.file",             "ARQUIVO" },
    { "ui.file.value",       "CARREGAR" },
    { "ui.language",         "IDIOMA" },
    { "ui.drop",               "arraste um arquivo aqui, ou toque CARREGAR" },
    { "ui.nosecs",             "ainda nao ha segundos completos: deixe a faixa tocar desde o inicio, ou use ARQUIVO" },
    { "ui.choose",             "Escolha a faixa que o VERDICT tem que analisar" },
    { "ui.analysing",          "analisando " },
    { "ui.unreadable",         "nao foi possivel ler: " },
    { "ui.notafile",           "isso nao e um arquivo" },
    { "ui.notaudio",           "\" nao e audio (" },
    { "section.within",     "Dentro da faixa" },
    { "section.feel",       "Como vai soar" },
    { "section.translate",  "Onde traduz" },
    { "section.missing",    "O que verificar, e onde" },
    { "footer",             "Medicao, nao gosto. Verificacoes por dispositivo genericas. Refaca a analise apos cada mudanca." },
    { "none",               "Nada fora da faixa nestas regras. Elas medem; o que nao escutam e seu." },
    { "mode.live",          "AO VIVO (desde o RESET)" },
    { "mode.file",          "ARQUIVO" },
    { "summary",            "{valor} s analisados" },
    { "baseline.trend",     "em relacao a tendencia espectral do proprio material" },
    { "baseline.reference", "em relacao a referencia carregada" },
    { "baseline.trend.short",     "em relacao a tendencia" },
    { "baseline.reference.short", "em relacao a referencia" },
    { "headline",             "{valor} verificacoes dentro da faixa \xc2\xb7 {valor2} para verificar, a primeira em {t0}" },
    { "headline.untimed",     "{valor} verificacoes dentro da faixa \xc2\xb7 {valor2} para verificar" },
    { "headline.one",         "{valor} verificacoes dentro da faixa \xc2\xb7 1 para verificar, em {t0}" },
    { "headline.one.untimed", "{valor} verificacoes dentro da faixa \xc2\xb7 1 para verificar" },
    { "headline.none",        "{valor} verificacoes dentro da faixa \xc2\xb7 nada fora destas regras" },

    { "crushed",       "PSR {valor} dB, abaixo do piso de {valor2} dB: os transientes ficam achatados. Verifique a entrada do limitador." },
    { "thin",          "150-400 Hz esta {valor} dB abaixo {valor3} (o que na mixagem se chama fino). Verifique o que da corpo a essa faixa." },
    { "muddy",         "200-500 Hz esta {valor} dB acima {valor3} (o que na mixagem se chama embolado). Verifique o que se acumula nessa faixa." },
    { "harsh",         "2-5 kHz esta {valor} dB acima {valor3} durante {valor2} % do tempo (o que na mixagem se chama aspero). Verifique a faixa de presenca." },
    { "harsh.loud",    "2-5 kHz esta {valor} dB acima {valor3} durante {valor2} % do tempo, a {t0} LUFS integrados (aspero e alto). Verifique a faixa de presenca e a entrada do limitador." },
    { "no-air",        "Acima de 10 kHz o nivel esta {valor} dB abaixo {valor3} (o que na mixagem se chama sem ar). Verifique os agudos." },
    { "hollow-centre", "Width {valor} nos medios (300 Hz-2 kHz) contra {valor2} nos agudos (o que na mixagem se chama centro vazio). Verifique como os agudos se abrem em relacao aos medios." },
    { "key",           "Tonalidade estimada: {banda}, confianca {valor}, a melhor em {valor2} % do tempo." },
    { "key.major",         "maior" },
    { "key.minor",         "menor" },

    { "phone.ok",         "Celular: {valor} % da energia esta abaixo de 300 Hz, que um alto-falante de telefone nao reproduz, mas os harmonicos do baixo em 300-1200 Hz estao apenas {valor2} dB abaixo: a linha de baixo se mantem." },
    { "phone.bad",        "Celular: {valor} % da energia esta abaixo de 300 Hz e os harmonicos do baixo em 300-1200 Hz estao {valor2} dB abaixo (o baixo desaparece). Verifique os harmonicos do baixo acima de 300 Hz." },
    { "headphones.ok",    "Fones: {valor} dB em 2-5 kHz e nenhuma banda acima de 8 kHz fora de fase." },
    { "headphones.bad",   "Fones: {valor} dB acima em 2-5 kHz, e uma banda acima de 8 kHz fica fora de fase {valor2} % do tempo (cansativo). Verifique a faixa de presenca e a largura acima de 8 kHz." },
    { "laptop.ok",        "Laptop: a presenca em 2-4 kHz esta {valor} dB, entao a voz aguenta num alto-falante pequeno." },
    { "laptop.bad",       "Laptop: a presenca em 2-4 kHz esta {valor} dB abaixo da tendencia do proprio material (a voz afunda). Verifique a presenca da voz." },
    { "car.ok",           "Carro: {valor} dB abaixo de 100 Hz, que uma cabine nao vai transformar em ronco." },
    { "car.bad",          "Carro: sobram {valor} dB abaixo de 100 Hz, e a cabine acrescenta os dela (ronca). Verifique o sub e o bumbo abaixo de 100 Hz." },
    { "club.ok",          "Club: {valor} dB de graves se perdem quando o sub soma em mono." },
    { "club.bad",         "Club: {valor} dB de graves se perdem quando o sub soma em mono. Verifique o baixo abaixo de 120 Hz em mono." },
    { "hi-fi.ok",         "Hi-fi: um sistema full-range nao esconde nada, e \"Como vai soar\" tem {valor} achados." },
    { "hi-fi.bad.one",    "Hi-fi: um sistema full-range nao esconde nada, e \"Como vai soar\" tem 1 achado. Verifique-o acima." },
    { "hi-fi.bad.many",   "Hi-fi: um sistema full-range nao esconde nada, e \"Como vai soar\" tem {valor} achados. Verifique-os acima." },

    { "hole",          "{banda} Hz cai {valor} dB abaixo da propria media (a banda mais funda de um trecho de {valor2}-{valor3} Hz que cai entre {t0} e {t1}). Verifique o que toca nessa faixa ali." },
    { "hole.one",      "{banda} Hz cai {valor} dB abaixo da propria media entre {t0} e {t1}. Verifique o que toca nessa banda ali." },
    { "quiet-section", "{valor} LU abaixo do integrado entre {t0} e {t1} (um trecho baixo). Verifique o arranjo e o ganho nesse trecho." },
    { "loud-section",  "{valor} LU acima do integrado entre {t0} e {t1} (um trecho alto)." },
    { "peaks",         "{valor} eventos de clip acima de {valor2} dBTP entre {t0} e {t1} (uma rajada de picos). Verifique o teto do limitador nesse trecho." },
    { "out-of-phase",  "{banda} Hz fora de fase em {valor} % do tempo: essa banda se cancela ao somar em mono. Verifique essa banda em mono." },
    { "imbalance",     "{valor} dB de desequilibrio L/R sustentado por {valor2} s, de {t0} a {t1}. Verifique o panorama nesse trecho." },
    { "dc",            "Componente continua de {valor} em L e {valor2} em R: come headroom e nao soa. Verifique se os arquivos de origem tem componente continua." },
    { "platform",      "{banda}: {valor} dB {valor3} a {valor2} LUFS integrados." },
    { "platform.down", "te abaixa" },
    { "platform.up",   "te aumentaria" },
    { "platform.only", "do alvo (so atenua)" },
    { "platform.tp",   "{banda}: o pico real de {valor} dBTP passa do seu teto de {valor2} dBTP. Verifique o teto do limitador." },

    { "within.crushed",       "transientes" },
    { "within.thin",          "corpo" },
    { "within.muddy",         "medios graves" },
    { "within.harsh",         "presenca" },
    { "within.no-air",        "agudos" },
    { "within.hollow-centre", "centro" },
    { "within.hole",          "bandas" },
    { "within.quiet-section", "loudness" },
    { "within.peaks",         "picos" },
    { "within.out-of-phase",  "mono" },
    { "within.imbalance",     "equilibrio" },
    { "within.dc",            "continua" },
    { "crushed.ok",        "Os transientes tem espaco: PSR {valor} dB (piso {valor2} dB)" },
    { "thin.ok",           "Corpo 150-400 Hz: {valor} dB {valor3} (fino a partir de -{valor2} dB)" },
    { "muddy.ok",          "Medios graves 200-500 Hz: {valor} dB {valor3} (embolado a partir de +{valor2} dB)" },
    { "harsh.ok",          "Presenca 2-5 kHz: {valor} dB {valor3} (aspero a partir de +{valor2} dB)" },
    { "harsh.ok.brief",    "Presenca 2-5 kHz: passa de +{valor3} dB so {valor} % do tempo (aspero a partir de {valor2} %)" },
    { "no-air.ok",         "Agudos abertos: {valor} dB acima de 10 kHz {valor3} (sem ar a partir de -{valor2} dB)" },
    { "hollow-centre.ok",  "Width {valor} nos medios, {valor2} nos agudos (centro vazio abaixo de {t0} com mais de {t1})" },
    { "hole.ok",           "Nenhuma banda cai {valor} dB abaixo da propria media por {valor2} s (sequencia mais longa {t0} s)" },
    { "quiet-section.ok",  "O loudness se mantem: LRA {valor} LU, nenhum trecho {valor2} LU abaixo do integrado por {t0} s" },
    { "peaks.ok",          "Sem rajadas de clip acima de {valor} dBTP ({valor2} eventos de clip no total)" },
    { "out-of-phase.ok",   "Nenhuma banda se cancela em mono (correlacao media mais baixa {valor})" },
    { "imbalance.ok",      "L/R equilibrado: {valor} dB em media (desequilibrio a partir de {valor2} dB por {t0} s)" },
    { "dc.ok",             "Sem componente continua: {valor} em L, {valor2} em R (limite {t0})" },
};

inline constexpr Phrase kPhrasesFr[] = {
    { "ui.reset",            "RESET" },
    { "ui.reset.value",      "depuis 0" },
    { "ui.mode",             "MODE" },
    { "ui.file",             "FICHIER" },
    { "ui.file.value",       "CHARGER" },
    { "ui.language",         "LANGUE" },
    { "ui.drop",               "glissez un fichier ici, ou appuyez sur CHARGER" },
    { "ui.nosecs",             "pas encore de secondes completes : laissez le morceau jouer depuis le debut, ou utilisez FICHIER" },
    { "ui.choose",             "Choisissez le morceau que VERDICT doit analyser" },
    { "ui.analysing",          "analyse en cours " },
    { "ui.unreadable",         "lecture impossible : " },
    { "ui.notafile",           "ce n'est pas un fichier" },
    { "ui.notaudio",           "\" n'est pas de l'audio (" },
    { "section.within",     "Dans la plage" },
    { "section.feel",       "Comment ca va sonner" },
    { "section.translate",  "Ou ca se traduit" },
    { "section.missing",    "Quoi verifier, et ou" },
    { "footer",             "Mesure, pas gout. Verifications par appareil generiques. Relancez l'analyse apres chaque modification." },
    { "none",               "Rien hors des plages de ces regles. Elles mesurent ; ce qu'elles n'entendent pas vous appartient." },
    { "mode.live",          "EN DIRECT (depuis le RESET)" },
    { "mode.file",          "FICHIER" },
    { "summary",            "{valor} s analysees" },
    { "baseline.trend",     "par rapport a la tendance spectrale du materiau lui-meme" },
    { "baseline.reference", "par rapport a la reference chargee" },
    { "baseline.trend.short",     "par rapport a la tendance" },
    { "baseline.reference.short", "par rapport a la reference" },
    { "headline",             "{valor} controles dans la plage \xc2\xb7 {valor2} a verifier, le premier a {t0}" },
    { "headline.untimed",     "{valor} controles dans la plage \xc2\xb7 {valor2} a verifier" },
    { "headline.one",         "{valor} controles dans la plage \xc2\xb7 1 a verifier, a {t0}" },
    { "headline.one.untimed", "{valor} controles dans la plage \xc2\xb7 1 a verifier" },
    { "headline.none",        "{valor} controles dans la plage \xc2\xb7 rien hors de ces regles" },

    { "crushed",       "PSR {valor} dB, sous le plancher de {valor2} dB : les transitoires sont ecrases. Verifiez l'entree du limiteur." },
    { "thin",          "150-400 Hz est {valor} dB en dessous {valor3} (ce qu'on appelle maigre au mixage). Verifiez ce qui donne du corps a cette zone." },
    { "muddy",         "200-500 Hz est {valor} dB au-dessus {valor3} (ce qu'on appelle boueux au mixage). Verifiez ce qui s'accumule dans cette zone." },
    { "harsh",         "2-5 kHz est {valor} dB au-dessus {valor3} pendant {valor2} % du temps (ce qu'on appelle agressif au mixage). Verifiez la zone de presence." },
    { "harsh.loud",    "2-5 kHz est {valor} dB au-dessus {valor3} pendant {valor2} % du temps, a {t0} LUFS integres (agressif et fort). Verifiez la zone de presence et l'entree du limiteur." },
    { "no-air",        "Au-dessus de 10 kHz le niveau est {valor} dB en dessous {valor3} (ce qu'on appelle sans air au mixage). Verifiez le haut du spectre." },
    { "hollow-centre", "Width {valor} dans les mediums (300 Hz-2 kHz) contre {valor2} dans les aigus (ce qu'on appelle un centre vide au mixage). Verifiez comment les aigus s'ouvrent par rapport aux mediums." },
    { "key",           "Tonalite estimee : {banda}, confiance {valor}, la meilleure {valor2} % du temps." },
    { "key.major",         "majeur" },
    { "key.minor",         "mineur" },

    { "phone.ok",         "Telephone : {valor} % de l'energie est sous 300 Hz, ce qu'un haut-parleur de telephone ne reproduit pas, mais les harmoniques de la basse entre 300 et 1200 Hz ne sont qu'a {valor2} dB : la ligne de basse tient." },
    { "phone.bad",        "Telephone : {valor} % de l'energie est sous 300 Hz et les harmoniques de la basse entre 300 et 1200 Hz sont a {valor2} dB (la basse disparait). Verifiez les harmoniques de la basse au-dessus de 300 Hz." },
    { "headphones.ok",    "Casque : {valor} dB entre 2 et 5 kHz et aucune bande au-dessus de 8 kHz hors phase." },
    { "headphones.bad",   "Casque : {valor} dB de trop entre 2 et 5 kHz, et une bande au-dessus de 8 kHz est hors phase {valor2} % du temps (fatigant). Verifiez la zone de presence et la largeur au-dessus de 8 kHz." },
    { "laptop.ok",        "Portable : la presence entre 2 et 4 kHz est a {valor} dB, la voix tient sur un petit haut-parleur." },
    { "laptop.bad",       "Portable : la presence entre 2 et 4 kHz est {valor} dB sous la tendance du materiau (la voix s'enfonce). Verifiez la presence de la voix." },
    { "car.ok",           "Voiture : {valor} dB sous 100 Hz, qu'un habitacle ne transformera pas en boum." },
    { "car.bad",          "Voiture : {valor} dB de trop sous 100 Hz, et l'habitacle ajoute les siens (ca resonne). Verifiez le sub et la grosse caisse sous 100 Hz." },
    { "club.ok",          "Club : {valor} dB de grave se perdent quand le sub passe en mono." },
    { "club.bad",         "Club : {valor} dB de grave se perdent quand le sub passe en mono. Verifiez la basse sous 120 Hz en mono." },
    { "hi-fi.ok",         "Hi-fi : un systeme full-range ne cache rien, et \"Comment ca va sonner\" compte {valor} constats." },
    { "hi-fi.bad.one",    "Hi-fi : un systeme full-range ne cache rien, et \"Comment ca va sonner\" compte 1 constat. Verifiez-le plus haut." },
    { "hi-fi.bad.many",   "Hi-fi : un systeme full-range ne cache rien, et \"Comment ca va sonner\" compte {valor} constats. Verifiez-les plus haut." },

    { "hole",          "{banda} Hz descend de {valor} dB sous sa propre moyenne (la bande la plus creuse d'une zone de {valor2}-{valor3} Hz qui descend entre {t0} et {t1}). Verifiez ce qui joue dans cette zone a ce moment." },
    { "hole.one",      "{banda} Hz descend de {valor} dB sous sa propre moyenne entre {t0} et {t1}. Verifiez ce qui joue dans cette bande a ce moment." },
    { "quiet-section", "{valor} LU sous le niveau integre entre {t0} et {t1} (un passage bas). Verifiez l'arrangement et le gain dans ce passage." },
    { "loud-section",  "{valor} LU au-dessus du niveau integre entre {t0} et {t1} (un passage fort)." },
    { "peaks",         "{valor} evenements de clip au-dessus de {valor2} dBTP entre {t0} et {t1} (une rafale de cretes). Verifiez le plafond du limiteur dans ce passage." },
    { "out-of-phase",  "{banda} Hz hors phase {valor} % du temps : cette bande s'annule en mono. Verifiez cette bande en mono." },
    { "imbalance",     "{valor} dB de desequilibre L/R tenu {valor2} s, de {t0} a {t1}. Verifiez le panoramique dans ce passage." },
    { "dc",            "Composante continue de {valor} sur L et {valor2} sur R : elle mange de la marge et ne s'entend pas. Verifiez si les fichiers source ont une composante continue." },
    { "platform",      "{banda} : {valor} dB {valor3} a {valor2} LUFS integres." },
    { "platform.down", "vous baisse de" },
    { "platform.up",   "vous monterait de" },
    { "platform.only", "de la cible (il ne fait qu'attenuer)" },
    { "platform.tp",   "{banda} : le vrai pic de {valor} dBTP depasse son plafond de {valor2} dBTP. Verifiez le plafond du limiteur." },

    { "within.crushed",       "transitoires" },
    { "within.thin",          "corps" },
    { "within.muddy",         "bas-medium" },
    { "within.harsh",         "presence" },
    { "within.no-air",        "aigus" },
    { "within.hollow-centre", "centre" },
    { "within.hole",          "bandes" },
    { "within.quiet-section", "loudness" },
    { "within.peaks",         "cretes" },
    { "within.out-of-phase",  "mono" },
    { "within.imbalance",     "equilibre" },
    { "within.dc",            "continu" },
    { "crushed.ok",        "Les transitoires ont de la place : PSR {valor} dB (plancher {valor2} dB)" },
    { "thin.ok",           "Corps 150-400 Hz : {valor} dB {valor3} (maigre a partir de -{valor2} dB)" },
    { "muddy.ok",          "Bas-medium 200-500 Hz : {valor} dB {valor3} (boueux a partir de +{valor2} dB)" },
    { "harsh.ok",          "Presence 2-5 kHz : {valor} dB {valor3} (agressif a partir de +{valor2} dB)" },
    { "harsh.ok.brief",    "Presence 2-5 kHz : au-dessus de +{valor3} dB seulement {valor} % du temps (agressif a partir de {valor2} %)" },
    { "no-air.ok",         "Aigus ouverts : {valor} dB au-dessus de 10 kHz {valor3} (sans air a partir de -{valor2} dB)" },
    { "hollow-centre.ok",  "Width {valor} dans les mediums, {valor2} dans les aigus (centre vide sous {t0} avec plus de {t1})" },
    { "hole.ok",           "Aucune bande ne descend de {valor} dB sous sa moyenne pendant {valor2} s (plus longue serie {t0} s)" },
    { "quiet-section.ok",  "Le loudness tient : LRA {valor} LU, aucun passage {valor2} LU sous l'integre pendant {t0} s" },
    { "peaks.ok",          "Aucune rafale de clip au-dessus de {valor} dBTP ({valor2} evenements de clip au total)" },
    { "out-of-phase.ok",   "Aucune bande ne s'annule en mono (correlation moyenne la plus basse {valor})" },
    { "imbalance.ok",      "L/R equilibre : {valor} dB en moyenne (desequilibre a partir de {valor2} dB tenu {t0} s)" },
    { "dc.ok",             "Pas de composante continue : {valor} sur L, {valor2} sur R (limite {t0})" },
};

inline constexpr Phrase kPhrasesDe[] = {
    { "ui.reset",            "RESET" },
    { "ui.reset.value",      "ab 0" },
    { "ui.mode",             "MODUS" },
    { "ui.file",             "DATEI" },
    { "ui.file.value",       "LADEN" },
    { "ui.language",         "SPRACHE" },
    { "ui.drop",               "Datei hierher ziehen, oder LADEN druecken" },
    { "ui.nosecs",             "noch keine vollstaendigen Sekunden: den Track von Anfang an abspielen, oder DATEI benutzen" },
    { "ui.choose",             "Waehle den Track, den VERDICT analysieren soll" },
    { "ui.analysing",          "analysiere " },
    { "ui.unreadable",         "konnte nicht gelesen werden: " },
    { "ui.notafile",           "das ist keine Datei" },
    { "ui.notaudio",           "\" ist kein Audio (" },
    { "section.within",     "Im Bereich" },
    { "section.feel",       "Wie es sich anfuehlen wird" },
    { "section.translate",  "Wo es uebersetzt" },
    { "section.missing",    "Was pruefen, und wo" },
    { "footer",             "Messung, kein Geschmack. Geraetepruefungen sind generisch. Analyse nach jeder Aenderung neu laufen lassen." },
    { "none",               "Nichts ausserhalb der Bereiche dieser Regeln. Sie messen; was sie nicht hoeren, gehoert dir." },
    { "mode.live",          "LIVE (seit RESET)" },
    { "mode.file",          "DATEI" },
    { "summary",            "{valor} s analysiert" },
    { "baseline.trend",     "gegen den eigenen spektralen Trend des Materials" },
    { "baseline.reference", "gegen die geladene Referenz" },
    { "baseline.trend.short",     "gegen den Trend" },
    { "baseline.reference.short", "gegen die Referenz" },
    { "headline",             "{valor} Pruefungen im Bereich \xc2\xb7 {valor2} zum Pruefen, die erste bei {t0}" },
    { "headline.untimed",     "{valor} Pruefungen im Bereich \xc2\xb7 {valor2} zum Pruefen" },
    { "headline.one",         "{valor} Pruefungen im Bereich \xc2\xb7 1 zum Pruefen, bei {t0}" },
    { "headline.one.untimed", "{valor} Pruefungen im Bereich \xc2\xb7 1 zum Pruefen" },
    { "headline.none",        "{valor} Pruefungen im Bereich \xc2\xb7 nichts ausserhalb dieser Regeln" },

    { "crushed",       "PSR {valor} dB, unter der Grenze von {valor2} dB: die Transienten sind plattgedrueckt. Pruefe den Eingang des Limiters." },
    { "thin",          "150-400 Hz liegt {valor} dB tiefer {valor3} (was man beim Mischen duenn nennt). Pruefe, was diesem Bereich Koerper gibt." },
    { "muddy",         "200-500 Hz liegt {valor} dB hoeher {valor3} (was man beim Mischen matschig nennt). Pruefe, was sich in diesem Bereich staut." },
    { "harsh",         "2-5 kHz liegt {valor} dB hoeher {valor3} waehrend {valor2} % der Zeit (was man beim Mischen hart nennt). Pruefe den Praesenzbereich." },
    { "harsh.loud",    "2-5 kHz liegt {valor} dB hoeher {valor3} waehrend {valor2} % der Zeit, bei {t0} LUFS integriert (hart und laut). Pruefe den Praesenzbereich und den Eingang des Limiters." },
    { "no-air",        "Oberhalb 10 kHz liegt der Pegel {valor} dB tiefer {valor3} (was man beim Mischen keine Luft nennt). Pruefe die Hoehen." },
    { "hollow-centre", "Width {valor} in den Mitten (300 Hz-2 kHz) gegen {valor2} in den Hoehen (was man beim Mischen eine hohle Mitte nennt). Pruefe, wie die Hoehen gegen die Mitten verbreitert sind." },
    { "key",           "Geschaetzte Tonart: {banda}, Konfidenz {valor}, in {valor2} % der Zeit die beste." },
    { "key.major",         "Dur" },
    { "key.minor",         "Moll" },

    { "phone.ok",         "Handy: {valor} % der Energie liegt unter 300 Hz, was ein Handylautsprecher nicht wiedergibt, aber die Bass-Obertoene in 300-1200 Hz liegen nur {valor2} dB darunter: die Basslinie bleibt hoerbar." },
    { "phone.bad",        "Handy: {valor} % der Energie liegt unter 300 Hz und die Bass-Obertoene in 300-1200 Hz liegen {valor2} dB darunter (der Bass verschwindet). Pruefe die Bass-Obertoene ueber 300 Hz." },
    { "headphones.ok",    "Kopfhoerer: {valor} dB in 2-5 kHz und kein Band ueber 8 kHz ausser Phase." },
    { "headphones.bad",   "Kopfhoerer: {valor} dB zu viel in 2-5 kHz, und ein Band ueber 8 kHz ist {valor2} % der Zeit ausser Phase (ermuedend). Pruefe den Praesenzbereich und die Breite ueber 8 kHz." },
    { "laptop.ok",        "Laptop: die Praesenz bei 2-4 kHz liegt bei {valor} dB, die Stimme haelt sich auf kleinen Lautsprechern." },
    { "laptop.bad",       "Laptop: die Praesenz bei 2-4 kHz liegt {valor} dB unter dem eigenen Trend des Materials (die Stimme versinkt). Pruefe die Praesenz der Stimme." },
    { "car.ok",           "Auto: {valor} dB unter 100 Hz, daraus macht ein Innenraum kein Droehnen." },
    { "car.bad",          "Auto: {valor} dB zu viel unter 100 Hz, und der Innenraum legt noch drauf (es droehnt). Pruefe Sub und Kick unter 100 Hz." },
    { "club.ok",          "Club: {valor} dB Bass gehen verloren, wenn der Sub mono summiert." },
    { "club.bad",         "Club: {valor} dB Bass gehen verloren, wenn der Sub mono summiert. Pruefe den Bass unter 120 Hz in Mono." },
    { "hi-fi.ok",         "Hi-Fi: ein Full-Range-System versteckt nichts, und \"Wie es sich anfuehlen wird\" hat {valor} Befunde." },
    { "hi-fi.bad.one",    "Hi-Fi: ein Full-Range-System versteckt nichts, und \"Wie es sich anfuehlen wird\" hat 1 Befund. Pruefe ihn weiter oben." },
    { "hi-fi.bad.many",   "Hi-Fi: ein Full-Range-System versteckt nichts, und \"Wie es sich anfuehlen wird\" hat {valor} Befunde. Pruefe sie weiter oben." },

    { "hole",          "{banda} Hz faellt {valor} dB unter das eigene Mittel (das tiefste Band eines Bereichs von {valor2}-{valor3} Hz, der zwischen {t0} und {t1} faellt). Pruefe, was dort in diesem Bereich spielt." },
    { "hole.one",      "{banda} Hz faellt {valor} dB unter das eigene Mittel zwischen {t0} und {t1}. Pruefe, was dort in diesem Band spielt." },
    { "quiet-section", "{valor} LU unter dem integrierten Pegel zwischen {t0} und {t1} (eine leise Passage). Pruefe Arrangement und Gain in dieser Passage." },
    { "loud-section",  "{valor} LU ueber dem integrierten Pegel zwischen {t0} und {t1} (eine laute Passage)." },
    { "peaks",         "{valor} Clip-Ereignisse ueber {valor2} dBTP zwischen {t0} und {t1} (eine Peak-Salve). Pruefe die Obergrenze des Limiters in dieser Passage." },
    { "out-of-phase",  "{banda} Hz in {valor} % der Zeit ausser Phase: dieses Band loescht sich in Mono aus. Pruefe dieses Band in Mono." },
    { "imbalance",     "{valor} dB L/R-Ungleichgewicht ueber {valor2} s, von {t0} bis {t1}. Pruefe das Panning in dieser Passage." },
    { "dc",            "Gleichanteil von {valor} auf L und {valor2} auf R: frisst Headroom und klingt nicht. Pruefe, ob die Quelldateien einen Gleichanteil haben." },
    { "platform",      "{banda}: {valor} dB {valor3} bei {valor2} LUFS integriert." },
    { "platform.down", "dreht dich runter um" },
    { "platform.up",   "wuerde dich hochdrehen um" },
    { "platform.only", "vom Ziel entfernt (es senkt nur ab)" },
    { "platform.tp",   "{banda}: der True Peak von {valor} dBTP ueberschreitet seine Grenze von {valor2} dBTP. Pruefe die Obergrenze des Limiters." },

    { "within.crushed",       "Transienten" },
    { "within.thin",          "Koerper" },
    { "within.muddy",         "tiefe Mitten" },
    { "within.harsh",         "Praesenz" },
    { "within.no-air",        "Hoehen" },
    { "within.hollow-centre", "Mitte" },
    { "within.hole",          "Baender" },
    { "within.quiet-section", "Lautheit" },
    { "within.peaks",         "Peaks" },
    { "within.out-of-phase",  "Mono" },
    { "within.imbalance",     "Balance" },
    { "within.dc",            "Gleichanteil" },
    { "crushed.ok",        "Die Transienten haben Raum: PSR {valor} dB (Grenze {valor2} dB)" },
    { "thin.ok",           "Koerper 150-400 Hz: {valor} dB {valor3} (duenn ab -{valor2} dB)" },
    { "muddy.ok",          "Tiefe Mitten 200-500 Hz: {valor} dB {valor3} (matschig ab +{valor2} dB)" },
    { "harsh.ok",          "Praesenz 2-5 kHz: {valor} dB {valor3} (hart ab +{valor2} dB)" },
    { "harsh.ok.brief",    "Praesenz 2-5 kHz: ueber +{valor3} dB nur {valor} % der Zeit (hart ab {valor2} %)" },
    { "no-air.ok",         "Offene Hoehen: {valor} dB oberhalb 10 kHz {valor3} (keine Luft ab -{valor2} dB)" },
    { "hollow-centre.ok",  "Width {valor} in den Mitten, {valor2} in den Hoehen (hohle Mitte unter {t0} mit ueber {t1})" },
    { "hole.ok",           "Kein Band faellt {valor} dB unter sein Mittel fuer {valor2} s (laengste Strecke {t0} s)" },
    { "quiet-section.ok",  "Die Lautheit haelt: LRA {valor} LU, keine Passage {valor2} LU unter dem Integrierten fuer {t0} s" },
    { "peaks.ok",          "Keine Clip-Salven ueber {valor} dBTP ({valor2} Clip-Ereignisse insgesamt)" },
    { "out-of-phase.ok",   "Kein Band loescht sich in Mono aus (niedrigste mittlere Korrelation {valor})" },
    { "imbalance.ok",      "L/R ausgeglichen: {valor} dB im Mittel (Ungleichgewicht ab {valor2} dB ueber {t0} s)" },
    { "dc.ok",             "Kein Gleichanteil: {valor} auf L, {valor2} auf R (Grenze {t0})" },
};

inline constexpr Phrase kPhrasesIt[] = {
    { "ui.reset",            "RESET" },
    { "ui.reset.value",      "da 0" },
    { "ui.mode",             "MODO" },
    { "ui.file",             "FILE" },
    { "ui.file.value",       "CARICA" },
    { "ui.language",         "LINGUA" },
    { "ui.drop",               "trascina un file qui, o premi CARICA" },
    { "ui.nosecs",             "non ci sono ancora secondi completi: fai suonare il brano dall'inizio, o usa FILE" },
    { "ui.choose",             "Scegli il brano che VERDICT deve analizzare" },
    { "ui.analysing",          "analisi " },
    { "ui.unreadable",         "impossibile leggere: " },
    { "ui.notafile",           "quello non e un file" },
    { "ui.notaudio",           "\" non e audio (" },
    { "section.within",     "Nel range" },
    { "section.feel",       "Come suonera" },
    { "section.translate",  "Dove traduce" },
    { "section.missing",    "Cosa controllare, e dove" },
    { "footer",             "Misura, non gusto. Controlli per dispositivo generici. Rifai l'analisi dopo ogni modifica." },
    { "none",               "Niente fuori dai range di queste regole. Misurano; quello che non sentono e tuo." },
    { "mode.live",          "DAL VIVO (dal RESET)" },
    { "mode.file",          "FILE" },
    { "summary",            "{valor} s analizzati" },
    { "baseline.trend",     "rispetto alla tendenza spettrale del materiale stesso" },
    { "baseline.reference", "rispetto al riferimento caricato" },
    { "baseline.trend.short",     "rispetto alla tendenza" },
    { "baseline.reference.short", "rispetto al riferimento" },
    { "headline",             "{valor} controlli nel range \xc2\xb7 {valor2} da controllare, il primo a {t0}" },
    { "headline.untimed",     "{valor} controlli nel range \xc2\xb7 {valor2} da controllare" },
    { "headline.one",         "{valor} controlli nel range \xc2\xb7 1 da controllare, a {t0}" },
    { "headline.one.untimed", "{valor} controlli nel range \xc2\xb7 1 da controllare" },
    { "headline.none",        "{valor} controlli nel range \xc2\xb7 niente fuori da queste regole" },

    { "crushed",       "PSR {valor} dB, sotto il minimo di {valor2} dB: i transienti sono schiacciati. Controlla l'ingresso del limiter." },
    { "thin",          "150-400 Hz sta {valor} dB sotto {valor3} (quello che nel mix si chiama sottile). Controlla cosa da corpo a quella zona." },
    { "muddy",         "200-500 Hz sta {valor} dB sopra {valor3} (quello che nel mix si chiama impastato). Controlla cosa si accumula in quella zona." },
    { "harsh",         "2-5 kHz sta {valor} dB sopra {valor3} per il {valor2} % del tempo (quello che nel mix si chiama aspro). Controlla la zona di presenza." },
    { "harsh.loud",    "2-5 kHz sta {valor} dB sopra {valor3} per il {valor2} % del tempo, a {t0} LUFS integrati (aspro e forte). Controlla la zona di presenza e l'ingresso del limiter." },
    { "no-air",        "Sopra i 10 kHz il livello sta {valor} dB sotto {valor3} (quello che nel mix si chiama senza aria). Controlla gli acuti." },
    { "hollow-centre", "Width {valor} nei medi (300 Hz-2 kHz) contro {valor2} negli acuti (quello che nel mix si chiama centro vuoto). Controlla come si aprono gli acuti rispetto ai medi." },
    { "key",           "Tonalita stimata: {banda}, confidenza {valor}, la migliore per il {valor2} % del tempo." },
    { "key.major",         "maggiore" },
    { "key.minor",         "minore" },

    { "phone.ok",         "Telefono: il {valor} % dell'energia sta sotto i 300 Hz, che un altoparlante di telefono non riproduce, ma le armoniche del basso in 300-1200 Hz sono solo {valor2} dB sotto: la linea di basso regge." },
    { "phone.bad",        "Telefono: il {valor} % dell'energia sta sotto i 300 Hz e le armoniche del basso in 300-1200 Hz sono {valor2} dB sotto (il basso sparisce). Controlla le armoniche del basso sopra i 300 Hz." },
    { "headphones.ok",    "Cuffie: {valor} dB in 2-5 kHz e nessuna banda sopra gli 8 kHz fuori fase." },
    { "headphones.bad",   "Cuffie: {valor} dB di troppo in 2-5 kHz, e una banda sopra gli 8 kHz e fuori fase per il {valor2} % del tempo (affaticante). Controlla la zona di presenza e l'ampiezza sopra gli 8 kHz." },
    { "laptop.ok",        "Laptop: la presenza in 2-4 kHz e a {valor} dB, la voce regge su un altoparlante piccolo." },
    { "laptop.bad",       "Laptop: la presenza in 2-4 kHz e {valor} dB sotto la tendenza del materiale (la voce affonda). Controlla la presenza della voce." },
    { "car.ok",           "Auto: {valor} dB sotto i 100 Hz, che un abitacolo non trasformera in rimbombo." },
    { "car.bad",          "Auto: {valor} dB di troppo sotto i 100 Hz, e l'abitacolo ci mette i suoi (rimbomba). Controlla sub e cassa sotto i 100 Hz." },
    { "club.ok",          "Club: si perdono {valor} dB di bassi quando il sub somma in mono." },
    { "club.bad",         "Club: si perdono {valor} dB di bassi quando il sub somma in mono. Controlla il basso sotto i 120 Hz in mono." },
    { "hi-fi.ok",         "Hi-fi: un sistema full-range non nasconde niente, e \"Come suonera\" ha {valor} rilievi." },
    { "hi-fi.bad.one",    "Hi-fi: un sistema full-range non nasconde niente, e \"Come suonera\" ha 1 rilievo. Controllalo piu in alto." },
    { "hi-fi.bad.many",   "Hi-fi: un sistema full-range non nasconde niente, e \"Come suonera\" ha {valor} rilievi. Controllali piu in alto." },

    { "hole",          "{banda} Hz scende di {valor} dB sotto la sua media (la banda piu profonda di una zona di {valor2}-{valor3} Hz che scende tra {t0} e {t1}). Controlla cosa suona in quella zona in quel punto." },
    { "hole.one",      "{banda} Hz scende di {valor} dB sotto la sua media tra {t0} e {t1}. Controlla cosa suona in quella banda in quel punto." },
    { "quiet-section", "{valor} LU sotto il livello integrato tra {t0} e {t1} (una sezione bassa). Controlla arrangiamento e guadagno in quel tratto." },
    { "loud-section",  "{valor} LU sopra il livello integrato tra {t0} e {t1} (una sezione alta)." },
    { "peaks",         "{valor} eventi di clip sopra {valor2} dBTP tra {t0} e {t1} (una raffica di picchi). Controlla il tetto del limiter in quel tratto." },
    { "out-of-phase",  "{banda} Hz fuori fase per il {valor} % del tempo: quella banda si cancella in mono. Controlla quella banda in mono." },
    { "imbalance",     "{valor} dB di sbilanciamento L/R per {valor2} s, da {t0} a {t1}. Controlla il panning in quel tratto." },
    { "dc",            "Componente continua di {valor} su L e {valor2} su R: mangia headroom e non suona. Controlla se i file di origine hanno componente continua." },
    { "platform",      "{banda}: {valor} dB {valor3} a {valor2} LUFS integrati." },
    { "platform.down", "ti abbassa di" },
    { "platform.up",   "ti alzerebbe di" },
    { "platform.only", "dall'obiettivo (attenua soltanto)" },
    { "platform.tp",   "{banda}: il true peak di {valor} dBTP supera il suo tetto di {valor2} dBTP. Controlla il tetto del limiter." },

    { "within.crushed",       "transienti" },
    { "within.thin",          "corpo" },
    { "within.muddy",         "medio-bassi" },
    { "within.harsh",         "presenza" },
    { "within.no-air",        "acuti" },
    { "within.hollow-centre", "centro" },
    { "within.hole",          "bande" },
    { "within.quiet-section", "loudness" },
    { "within.peaks",         "picchi" },
    { "within.out-of-phase",  "mono" },
    { "within.imbalance",     "bilanciamento" },
    { "within.dc",            "continua" },
    { "crushed.ok",        "I transienti hanno spazio: PSR {valor} dB (minimo {valor2} dB)" },
    { "thin.ok",           "Corpo 150-400 Hz: {valor} dB {valor3} (sottile da -{valor2} dB)" },
    { "muddy.ok",          "Medio-bassi 200-500 Hz: {valor} dB {valor3} (impastato da +{valor2} dB)" },
    { "harsh.ok",          "Presenza 2-5 kHz: {valor} dB {valor3} (aspro da +{valor2} dB)" },
    { "harsh.ok.brief",    "Presenza 2-5 kHz: sopra +{valor3} dB solo il {valor} % del tempo (aspro dal {valor2} %)" },
    { "no-air.ok",         "Acuti aperti: {valor} dB sopra i 10 kHz {valor3} (senza aria da -{valor2} dB)" },
    { "hollow-centre.ok",  "Width {valor} nei medi, {valor2} negli acuti (centro vuoto sotto {t0} con oltre {t1})" },
    { "hole.ok",           "Nessuna banda scende di {valor} dB sotto la sua media per {valor2} s (serie piu lunga {t0} s)" },
    { "quiet-section.ok",  "Il loudness tiene: LRA {valor} LU, nessuna sezione {valor2} LU sotto l'integrato per {t0} s" },
    { "peaks.ok",          "Nessuna raffica di clip sopra {valor} dBTP ({valor2} eventi di clip in totale)" },
    { "out-of-phase.ok",   "Nessuna banda si cancella in mono (correlazione media piu bassa {valor})" },
    { "imbalance.ok",      "L/R bilanciato: {valor} dB in media (sbilanciamento da {valor2} dB per {t0} s)" },
    { "dc.ok",             "Nessuna componente continua: {valor} su L, {valor2} su R (limite {t0})" },
};

struct Language
{
    const char*   code;    // ISO 639-1
    const char*   name;    // como se muestra en el selector
    const Phrase* table;
    int           count;
};

inline constexpr Language kLanguages[] = {
    { "en", "English",    kPhrasesEn, (int) (sizeof (kPhrasesEn) / sizeof (Phrase)) },
    { "es", "Espanol",    kPhrasesEs, (int) (sizeof (kPhrasesEs) / sizeof (Phrase)) },
    { "pt", "Portugues",  kPhrasesPt, (int) (sizeof (kPhrasesPt) / sizeof (Phrase)) },
    { "fr", "Francais",   kPhrasesFr, (int) (sizeof (kPhrasesFr) / sizeof (Phrase)) },
    { "de", "Deutsch",    kPhrasesDe, (int) (sizeof (kPhrasesDe) / sizeof (Phrase)) },
    { "it", "Italiano",   kPhrasesIt, (int) (sizeof (kPhrasesIt) / sizeof (Phrase)) },
};

inline constexpr int kNumLanguages = (int) (sizeof (kLanguages) / sizeof (Language));
inline constexpr const char* kDefaultLanguage = "en";   // D-50

// El idioma por código, o el default si no está.
inline const Language& language (const char* code) noexcept
{
    if (code != nullptr)
        for (const auto& l : kLanguages)
            if (std::strcmp (l.code, code) == 0) return l;
    return kLanguages[0];
}

inline bool hasLanguage (const char* code) noexcept
{
    if (code == nullptr) return false;
    for (const auto& l : kLanguages)
        if (std::strcmp (l.code, code) == 0) return true;
    return false;
}

// LA BÚSQUEDA CON FALLBACK. Si el idioma no tiene la clave, sale en inglés; si el inglés tampoco la
// tiene, sale la clave misma (que es un bug visible, no una frase vacía que nadie nota).
inline const char* phrase (const char* key, const char* code) noexcept
{
    const auto find = [key] (const Language& l) -> const char*
    {
        for (int i = 0; i < l.count; ++i)
            if (std::strcmp (l.table[i].key, key) == 0) return l.table[i].text;
        return nullptr;
    };

    if (const auto* t = find (language (code))) return t;
    if (const auto* t = find (kLanguages[0]))   return t;
    return key;
}
}
