#pragma once
#include "data/Rules.h"

// ========================================================================================================
// DeviceProfiles.h — las seis "cajas" del pronóstico de VERDICT (spec §5.8, sección 2).
//
// ============================ LO QUE ESTO ES, Y LO QUE NO ============================
//
// NO son mediciones de ningún parlante real, ni de ningún fabricante, ni una simulación, y no hay
// ninguna respuesta en frecuencia guardada acá adentro. Cada caja es una DEFINICIÓN en una línea de qué
// clase de sistema es —dónde empieza a responder, dónde se cae, qué le pasa al estéreo— y nada más:
// tres campos de texto, sin un solo número de audio.
//
// Los números viven todos en Rules.h, y son UMBRALES sobre el material: qué fracción de la energía del
// programa cae por debajo de 300 Hz, cuánto se pierde al monoficar por debajo de 120 Hz, cuánto exceso
// hay en 2-5 kHz contra la tendencia del propio material. El chequeo mira la MEZCLA contra un umbral; no
// pesa la mezcla contra ninguna respuesta de parlante, porque no hay ninguna que pesar.
//
// Sirven para una sola cosa: decir "esta mezcla apoya casi todo donde un teléfono no llega". Eso es un
// pronóstico, y la lente lo rotula como pronóstico, siempre, en las seis lenguas. Cualquier frase del
// tipo "así suena en un iPhone" sería mentira y está prohibida (D-47). El pie fijo del informe dice
// exactamente lo que hay, en los seis idiomas: *"Chequeos por dispositivo genéricos."*
//
// 56c — hasta acá esta cabecera describía las cajas como si tuvieran una respuesta en frecuencia escrita
// a mano, y citaba un pie del informe que ya no se usa; tres líneas más abajo el bloque del 56b explica
// que no hay ninguna. El array se sacó en el 56b y la cabecera se quedó como estaba. Un archivo que se
// contradice a sí mismo es peor que uno que miente: el lector se queda con la mitad que leyó primero. La
// frase vieja, tal cual fue, está en el CHANGELOG, que es donde va el registro histórico. Lo cubre el
// test de honestidad de VisualTest.cpp.
// ========================================================================================================
namespace telescope::devices
{
inline constexpr int kNumDevices = 6;

// 56b — ACA NO HAY CURVAS, Y ESO ES LO HONESTO (M4 del revisor del 55, D-47).
//
// Hasta el 56 cada caja traía un `responseDb[30]` escrito a mano y NINGÚN chequeo lo leía: los seis
// pronósticos salen del residuo espectral y de la pérdida mono, no de una curva. O sea que el array era
// código muerto que además contaba una historia falsa — un lector del repo veía treinta números por
// dispositivo y concluía que el plugin simula parlantes. Eso es exactamente lo que D-47 prohíbe.
//
// Lo que queda es lo que de verdad define a cada caja para este plugin: su id, su clave y la frase que
// dice qué clase de sistema es. Los chequeos son GENÉRICOS POR DISPOSITIVO y así lo dice el pie del
// informe en los seis idiomas, el README y el diccionario. Si algún día hay curvas medidas, entran acá
// con su procedencia.
struct DeviceProfile
{
    rules::RuleId id;
    const char*   key;          // clave base de las frases: `<key>.ok` / `<key>.bad`
    const char*   what;         // qué caja es, en una línea (va en la evidencia)
};

inline constexpr DeviceProfile kDevices[kNumDevices] = {
    // ---- CELULAR: no hay nada útil por debajo de ~300 Hz (el parlante no mueve aire ahí) y se cae
    //      arriba de 10 kHz. Es la caja que más castiga apoyarse en el grave.
    { rules::RuleId::devPhone, "phone", "altavoz de telefono, sin respuesta util por debajo de 300 Hz" },

    // ---- AURICULARES: full range de verdad. Lo que cambia no es la curva sino que el estéreo se
    //      escucha SEPARADO: una banda fuera de fase que en un parlante se disimula, acá molesta.
    { rules::RuleId::devHeadphones, "headphones", "auricular full-range: el estereo se escucha separado" },

    // ---- LAPTOP: todo el peso en la presencia. Sin graves y con los agudos cortados.
    { rules::RuleId::devLaptop, "laptop", "altavoz de laptop: todo el peso en la presencia (2-4 kHz)" },

    // ---- AUTO: el habitáculo REALZA los graves por sus modos propios, y el ruido de rodadura tapa
    //      justo esa región. Una mezcla ya pesada abajo se vuelve retumbe.
    { rules::RuleId::devCar, "car", "habitaculo: realza los graves por modos del recinto" },

    // ---- CLUB CON SUB MONO: el sub va en mono por diseño. Lo que esté fuera de fase abajo, no suena.
    { rules::RuleId::devClub, "club", "PA grande con el sub en MONO por debajo de ~120 Hz" },

    // ---- HI-FI: plano. No esconde nada — y por eso no tiene chequeo propio: hereda la sección 1.
    { rules::RuleId::devHifi, "hi-fi", "equipo full-range plano: no esconde nada" },
};

// El veredicto de UNA CAJA. Se llama `DeviceVerdict` y no `Verdict` a secas para no chocar con la clase
// `telescope::Verdict`, que es el motor entero: dos cosas con el mismo nombre en el mismo árbol se leen mal.
enum class DeviceVerdict { ok = 0, warn, fail };

struct DeviceForecast
{
    rules::RuleId id      = rules::RuleId::devHifi;
    const char*   key     = "hi-fi";
    DeviceVerdict verdict = DeviceVerdict::ok;
    float         value   = 0.0f;
    float         value2  = 0.0f;
};

inline constexpr const DeviceProfile& device (int i) noexcept
{
    return kDevices[(i >= 0 && i < kNumDevices) ? i : 0];
}

// Las fronteras de banda que usan los chequeos, por índice de ⅓ de octava (ISO 266, ver kThirdOctaveHz):
//   0: 25 · 6: 100 · 9: 200 · 11: 315 · 12: 400 · 17: 1250 · 19: 2000 · 22: 4000 · 23: 5000 · 25: 8000
inline constexpr int kBand100Hz  = 6;
inline constexpr int kBand120Hz  = 7;    // la banda de 125 Hz: el corte del sub en un club
inline constexpr int kBand300Hz  = 11;   // 315 Hz: el primer punto donde un telefono empieza a existir
inline constexpr int kBand1200Hz = 17;   // 1250 Hz: el techo de los armonicos utiles del bajo
inline constexpr int kBand2kHz   = 19;
inline constexpr int kBand4kHz   = 22;
inline constexpr int kBand5kHz   = 23;
inline constexpr int kBand8kHz   = 25;
inline constexpr int kBand10kHz  = 26;
}
