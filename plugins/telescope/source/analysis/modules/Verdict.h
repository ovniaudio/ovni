#pragma once
#include <string>
#include <vector>
#include "analysis/AnalysisFrame.h"
#include "analysis/FileAnalysis.h"
#include "analysis/ReferenceFrame.h"
#include "analysis/SecondHistory.h"
#include "data/DeviceProfiles.h"
#include "data/Rules.h"
#include "data/StreamingTargets.h"

// ========================================================================================================
// Verdict — el motor de reglas de la lente 13 (spec §5.8, D-47).
//
// C++ PURO: `std::string` y `std::vector`, cero JUCE de interfaz. Lo único que entra de JUCE son los POD
// del motor (`AnalysisFrame`, `SecondRow`, `ReferenceFrame`), que traen `juce::uint32` y poco más. Se
// puede correr entero sin editor, sin ventana y sin audio — y eso es exactamente lo que hace su test.
//
// DETERMINISTA, SIN IA Y SIN RED (D-47). La misma entrada da la misma salida, frase por frase. No hay
// modelo, no hay pesos, no hay llamada a ningún lado: hay una tabla de umbrales (`data/Rules.h`) y las
// condiciones que los leen. Cada frase sale con el NÚMERO que la sostiene y con el ID de la regla que la
// produjo, y las dos cosas se muestran juntas. Prohibido "suena profesional", "emociona", "está listo".
//
// ========================================================================================================
// CONTRA QUÉ SE COMPARA — la decisión que hace honesto todo lo demás
//
// Las reglas espectrales de la sección 1 ("delgado", "turbio", "áspero", "sin aire") preguntan si una
// REGIÓN sobra o falta. Sobra ¿respecto de qué? Hay dos respuestas y el motor usa la que haya:
//
//   · CON REFERENCIA CARGADA — respecto de la referencia, comparadas por su FORMA (cada curva menos su
//     propio nivel de banda ancha). Es lo que dibuja TONAL BALANCE, y es la mejor información posible:
//     alguien eligió ese archivo como el objetivo.
//
//   · SIN REFERENCIA — respecto de la TENDENCIA DEL PROPIO MATERIAL: una recta de mínimos cuadrados
//     ajustada a la forma del programa en log de frecuencia. O sea: se mide una región contra el RESTO DE
//     LA MISMA MEZCLA, no contra ninguna curva "correcta" escondida en el código.
//
// La segunda opción es deliberada. Inventar una "curva neutra de una buena mezcla" y no decirlo sería
// justo lo que D-47 prohíbe: opinar disfrazado de medición. Contra la propia tendencia no hay nada que
// opinar — un pozo de 6 dB en los medios graves es un pozo respecto del resto de ESE material, y eso es
// un hecho. A cambio, es un instrumento grueso: la frase dice siempre cuál de las dos comparaciones usó,
// y cargar una referencia es estrictamente mejor. Está dicho en el diccionario y en el README.
// ========================================================================================================
namespace telescope
{
struct VerdictFinding
{
    rules::RuleId   ruleId   = rules::RuleId::key;
    rules::Section  section  = rules::Section::feel;
    rules::Severity severity = rules::Severity::info;

    int   t0   = -1;   // segundos desde el RESET (o desde el principio del archivo); −1 = todo
    int   t1   = -1;
    int   band = -1;   // índice de ⅓ de octava; −1 = todas
    float values[4] {};

    std::string text;       // la frase, ya renderizada y con el número adentro
    std::string evidence;   // el id de la regla y la métrica: lo que va en gris al lado
};

// Lo que una plataforma le hace a este máster. Se calcula para las SEIS de StreamingTargets.h, aunque la
// lente LOUDNESS tenga elegida una sola.
struct PlatformDelta
{
    const char* name          = "";
    float targetLufs          = 0.0f;
    float deltaDb             = 0.0f;   // integrado − objetivo (positivo = más fuerte que el objetivo)
    float ceilingDbtp         = 0.0f;
    float truePeakDbtp        = kSilenceDb;
    bool  attenuatesOnly      = false;
    bool  overCeiling         = false;
};

struct VerdictSummary
{
    int   seconds        = 0;      // filas con medición completa
    int   secondsTotal   = 0;      // filas analizadas (con o sin parte espectral)
    float integrated     = kSilenceDb;
    float lra            = 0.0f;
    float plr            = 0.0f;
    float truePeakMax    = kSilenceDb;
    float corr           = 0.0f;
    int   keyTonic       = -1, keyMode = -1;
    float keyConfidence  = 0.0f;
    float keyTimeFraction = 0.0f;
    bool  usedReference  = false;   // ¿la sección 1 comparó contra una referencia?
    std::string text;               // la línea de resumen, ya renderizada

    // ---- prompt 57d · EL TITULAR (append-only) ----
    // La cuenta de lo medido, sin adjetivos. `checksWithin` = las reglas que se evaluaron y quedaron dentro
    // de rango (cada línea de "Dentro de rango" más cada caja en ✓ que tuvo dato para decidir); `toCheck` =
    // los hallazgos, o sea ⚠ y ● (las líneas ○ son informativas, diccionario §3), ya fusionados.
    int         checksWithin = 0;
    int         toCheck      = 0;
    int         firstAt      = -1;  // el t0 del primer hallazgo que tiene tiempo; −1 = ninguno lo tiene
    std::string headline;           // la línea ya renderizada; vacía si no se analizó ni un segundo
    std::string headlineEvidence;   // "headline · n/m"
};

struct VerdictReport
{
    std::vector<VerdictFinding> findings;
    devices::DeviceForecast     devices[devices::kNumDevices] {};
    std::vector<PlatformDelta>  platforms;
    VerdictSummary              summary;
    std::string                 footer;

    // ---- prompt 57d · DENTRO DE RANGO (append-only) ----
    // Una línea por regla de forma o de tiempo que se EVALUÓ y NO se disparó, con su número y su regla. No
    // son hallazgos: viven fuera de `findings`, así que `countInSection` y VERDICT[sano] no se enteran.
    std::vector<VerdictFinding> strengths;

    int countInSection (rules::Section s) const
    {
        int n = 0;
        for (const auto& f : findings) if (f.section == s) ++n;
        return n;
    }
};

class Verdict
{
public:
    struct Inputs
    {
        AnalysisFrame         aggregates;              // I, LRA, TP, PLR, tonalidad, continua, clips
        const SecondRow*      rows = nullptr;          // la historia por segundo
        int                   n = 0;
        const ReferenceFrame* ref = nullptr;           // opcional: si está y es válida, manda ella
        const char*           language = rules::kDefaultLanguage;
        int                   targetIndex = 0;         // el `target` de la lente LOUDNESS
        float                 clipThresholdDbtp = -1.0f;
        int                   firstSecond = 0;         // el segundo del tema en que arranca rows[0]
    };

    static VerdictReport evaluate (const Inputs& in);

    // Expuestas para el test y para la lente (que muestra el pie y los rótulos con el mismo idioma).
    static std::string translate (const char* key, const char* language);
    static std::string timeLabel (int seconds);        // mm:ss
    static std::string number (double v, int decimals);

    // ========================================================================================================
    // EL PUENTE ARCHIVO -> VIVO. VERDICT come un `AnalysisFrame` (los agregados) más las filas por segundo.
    // En vivo el frame lo publica el motor; sobre un archivo hay que armarlo, y tiene que quedar IDÉNTICO
    // campo por campo — si no, el mismo audio daría dos informes distintos según por dónde entró, que es
    // exactamente lo que este módulo promete que no pasa (VERDICT[archivo] lo verifica frase por frase).
    // ========================================================================================================
    static AnalysisFrame aggregatesFrom (const FileAnalysis& a);
};
}
