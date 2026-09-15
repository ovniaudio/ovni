#pragma once
#include <cstddef>

// ========================================================================================================
// Objetivos de loudness por plataforma de streaming.
//
// HONESTIDAD (regla del sello): la columna `official` dice si la PLATAFORMA publica el número o si es
// medición de terceros / consenso de la industria a partir de cómo se comporta su normalización. Sólo
// Spotify y Apple publican algo verificable; el resto NO tiene una página oficial con un LUFS objetivo.
// La UI marca los no-oficiales; el README lo dice. Verificado por la obrera del prompt 48 el 2026-09-07.
//
// Fuentes (consultadas 2026-09-07):
//   Spotify — support.spotify.com/artists/article/loudness-normalization : "-14 dB LUFS integrado",
//             "mantenlo por debajo de -1 dB TP máx"; si el máster es MÁS fuerte que -14 LUFS,
//             "mantené el pico real por debajo de -2 dB" (el transcode a formatos con pérdida sube picos).
//   Apple   — Apple Digital Masters / Apple Video and Audio Asset Guide: techo -1 dBTP (requisito
//             publicado). Sound Check mide LUFS integrado y sólo BAJA lo que supera ~-16 LUFS
//             (no sube lo que está por debajo) — el -16 es comportamiento medido, no una página oficial.
//   YouTube / Amazon Music / Tidal / Deezer — NO publican un objetivo de LUFS en su documentación:
//             los valores son los medidos por la industria sobre su normalización real. Amazon es el
//             más estricto en pico (-2 dBTP) porque re-comprime. Se muestran etiquetados como estimados.
// ========================================================================================================
namespace telescope
{
struct StreamingTarget
{
    const char* name;
    float targetLufs;        // LUFS integrado al que la plataforma normaliza
    float ceilingDbtp;       // techo de true-peak recomendado
    float ceilingDbtpLoud;   // techo si el máster es MÁS fuerte que targetLufs (Spotify); si no aplica, == ceilingDbtp
    bool  official;          // ¿la plataforma publica el número, o es medición/consenso?
    bool  hasTarget;         // false sólo para "Ninguno"

    // ¿Qué hace la plataforma con un máster MÁS BAJO que su objetivo? Bajar lo fuerte lo hacen todas
    // (es la definición de normalizar a un objetivo); SUBIR lo bajo no. Apple (Sound Check) y Amazon
    // están verificados: sólo atenúan, un máster por debajo del objetivo se queda donde está. Del resto
    // no se encontró fuente verificable, así que la UI no afirma nada y sólo dice la distancia medida.
    bool  attenuatesOnly;
};

inline constexpr int kNumTargets = 7;

inline constexpr StreamingTarget kStreamingTargets[kNumTargets] = {
    //  nombre           LUFS     dBTP   dBTP(fuerte) oficial  objetivo  sólo atenúa
    { "Ninguno",          0.0f,    0.0f,    0.0f,     true,    false,    false },
    { "Spotify",        -14.0f,   -1.0f,   -2.0f,     true,    true,     false },
    { "Apple Music",    -16.0f,   -1.0f,   -1.0f,     false,   true,     true  },   // techo oficial; LUFS medido
    { "YouTube",        -14.0f,   -1.0f,   -1.0f,     false,   true,     false },
    { "Amazon Music",   -14.0f,   -2.0f,   -2.0f,     false,   true,     true  },
    { "Tidal",          -14.0f,   -1.0f,   -1.0f,     false,   true,     false },
    { "Deezer",         -15.0f,   -1.0f,   -1.0f,     false,   true,     false },
};

inline constexpr const StreamingTarget& streamingTarget (int index) noexcept
{
    return kStreamingTargets[(index >= 0 && index < kNumTargets) ? index : 0];
}
}
