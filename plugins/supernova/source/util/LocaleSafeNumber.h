#pragma once
// Números ↔ texto INDEPENDIENTES del locale, para las persistencias planas (LfoBank, MidiCcMap, SceneBank).
//
// El defecto (informe 24 · M5): std::to_string / std::stof / std::stoi usan el locale C (LC_NUMERIC). Si el
// host —o un plugin vecino Qt/GTK— llama setlocale, el separador decimal pasa a ser COMA, y la coma es el
// separador de CAMPOS de esos tres formatos: el registro se destruye al escribirlo y el VJ pierde su sesión.
//
// La cura son streams con locale CLASSIC explícito. Cubre las DOS puertas: setlocale() del lado C y
// std::locale::global() del lado C++ (los ostringstream sin imbue seguían el global, no el de setlocale).
// No se usa std::to_chars/from_chars: en libc++ la versión de punto flotante pide macOS 13.3 / 26.0 y el
// producto despliega desde macOS 11.
#include <cstring>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <string>

namespace supernova::num
{
namespace detail
{
// "¿es EXACTAMENTE el mismo float?" comparado por bits — que es la pregunta real del round-trip, y además
// no dispara -Wfloat-equal (comparar magnitudes con == sí es sospechoso; comparar la representación no).
inline bool sameBits (float a, float b) noexcept { return std::memcmp (&a, &b, sizeof (float)) == 0; }
}

inline float toFloat (const std::string& s, float fallback = 0.0f)
{
    std::istringstream is (s);
    is.imbue (std::locale::classic());
    float v = 0.0f;
    return (is >> v) ? v : fallback;
}

inline int toInt (const std::string& s, int fallback = 0)
{
    std::istringstream is (s);
    is.imbue (std::locale::classic());
    int v = 0;
    return (is >> v) ? v : fallback;
}

// La representación MÁS CORTA que vuelve a leerse como el mismo float ("2" en vez de "2.000000", "0.25" en
// vez de "0.250000") → el estado guardado además encoge. max_digits10 = 9 garantiza el round-trip exacto.
inline std::string toString (float v)
{
    std::ostringstream os;
    os.imbue (std::locale::classic());
    std::string out;
    for (int prec = 6; prec <= std::numeric_limits<float>::max_digits10; ++prec)
    {
        os.str (std::string());
        os.clear();
        os << std::setprecision (prec) << v;
        out = os.str();
        if (detail::sameBits (toFloat (out, v + 1.0f), v)) break;
    }
    return out;
}

inline std::string toString (int v)
{
    std::ostringstream os;
    os.imbue (std::locale::classic());
    os << v;
    return os.str();
}
}
