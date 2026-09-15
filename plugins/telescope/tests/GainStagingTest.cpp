// [gain][telescope] — la puerta ANTI-CLIP del sello, sobre TELESCOPE.
//
// QUÉ ES ESTO Y POR QUÉ HACE FALTA. `tools/validate.sh` —la puerta que corre el CI por cada plugin del
// catálogo— tiene una puerta 6 de gain-staging: corre `tools/gain-staging-check.sh`, que busca en el exe
// de tests un caso etiquetado `[gain]` que imprima una línea `PEAK=<pico lineal>` y compara ese número
// contra el techo del plugin. Sin ese caso, el script reporta `method="none"`, peak = −1, y validate.sh
// se pone ROJO con "no se pudo medir el peak" — que es lo correcto: una puerta que no puede medir no es
// una puerta que pasa.
//
// EL TECHO DE TELESCOPE ES 1.0, NO 0.85 (plugins/telescope/validate.env). El 0.85 (−1.4 dBFS) del catálogo
// es el margen de un plugin que PROCESA: suma reflexiones, resuena, satura, y ahí un techo por debajo de
// la unidad es lo que evita que el usuario descubra el clip en el máster. TELESCOPE no agrega ni una
// muestra: su techo honesto es 0 dBFS, o sea la unidad, exactamente como SUPERNOVA (que también es
// pass-through). Bajarle el techo a 0.85 haría fallar la puerta con una señal a escala completa que sale
// idéntica a como entró — se estaría midiendo bien y castigando lo correcto.
//
// Y NO SE SALTEA LA MEDICIÓN. El caso pasa una señal full-scale de verdad por el processor ensamblado y
// además verifica la identidad muestra a muestra: si algún día TELESCOPE empezara a tocar el buffer, el
// número subiría (o el conteo de diferencias dejaría de ser cero) y esta puerta lo diría. Es el mismo
// contrato de `[telescope][null]`, medido desde el otro lado: aquél compara contra ruido, éste contra el
// peor caso de amplitud.
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdio>
#include "PluginProcessor.h"

TEST_CASE ("telescope: full-scale entra y sale igual — PEAK para el gate anti-clip", "[gain][telescope]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (48000.0, 512);

    const int n = 512;
    juce::AudioBuffer<float> in (2, n), work (2, n);

    // Full-scale de verdad: un seno de 997 Hz a amplitud 1.0 (la frecuencia de la casa para medidas de
    // loudness — BS.1770 la usa por no ser submúltiplo de ningún sample rate) más las muestras ±1.0
    // exactas en los bordes del bloque, que es donde un de-zipper mal puesto se delataría.
    for (int i = 0; i < n; ++i)
    {
        const float v = std::sin (juce::MathConstants<float>::twoPi * 997.0f * (float) i / 48000.0f);
        in.setSample (0, i, v);
        in.setSample (1, i, -v);          // L y R opuestos: también cubre el peor caso de fase
    }
    in.setSample (0, 0,     1.0f);  in.setSample (1, 0,    -1.0f);
    in.setSample (0, n - 1, -1.0f); in.setSample (1, n - 1, 1.0f);
    work.makeCopyOf (in);

    juce::MidiBuffer midi;
    for (int b = 0; b < 8; ++b)           // varios bloques: el motor se llena y sigue sin tocar el audio
        proc.processBlock (work, midi);

    int diffs = 0;
    float peak = 0.0f;
    for (int ch = 0; ch < work.getNumChannels(); ++ch)
        for (int i = 0; i < n; ++i)
        {
            peak = juce::jmax (peak, std::abs (work.getSample (ch, i)));
            if (work.getSample (ch, i) != in.getSample (ch, i))   // exacto a propósito: es pass-through
                ++diffs;
        }

    // La línea que grepea tools/gain-staging-check.sh. No cambiar el formato.
    std::printf ("PEAK=%.6f\n", peak);
    std::printf ("GAIN_IDENTITY_DIFFS=%d\n", diffs);

    REQUIRE (diffs == 0);                 // salió lo mismo que entró
    REQUIRE (peak <= 1.0f);               // …y por lo tanto no puede pasarse de la unidad
    REQUIRE (peak > 0.99f);               // la señal ERA full-scale: un 0 acá sería "no se midió nada"
}
