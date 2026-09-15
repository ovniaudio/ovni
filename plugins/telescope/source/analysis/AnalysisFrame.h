#pragma once
#include <juce_core/juce_core.h>

// ========================================================================================================
// AnalysisFrame — la foto que el AnalysisThread publica y la lente lee (POD, latest-wins vía TripleBuffer).
//
// CONGELADO: los prompts siguientes AGREGAN campos (espectro, correlación por banda, campo…), NUNCA
// renombran ni reordenan los que ya están. Una lente vieja tiene que seguir leyendo lo que leía.
// ========================================================================================================
namespace telescope
{
// Bit por módulo del motor. El editor fija la máscara según la lente visible (lente a demanda: lo que no
// se ve, no se calcula). Los reservados todavía no tienen módulo — están acá para que el contrato no cambie.
enum ModuleBit : juce::uint32
{
    kLoudness  = 1u << 0,
    kSpectrum  = 1u << 1,   // reservado (prompt 50)
    kStereo    = 1u << 2,   // reservado (prompts 49/51)
    kCqt       = 1u << 3,   // reservado (prompt 52)
    kField     = 1u << 4,   // reservado (prompt 53)
    kReference = 1u << 5,   // reservado (prompt 54)
    // Estéreo POR BANDA (prompt 51). Come de los complejos L y R de cada frame del módulo Spectrum, así
    // que en la práctica viaja siempre junto con kSpectrum: las dos lentes que lo piden (BAND CORRELATION
    // y STEREO SPECTROGRAM) declaran kSpectrum | kStereoBands.
    kStereoBands = 1u << 6
};

// Los módulos que corren SIEMPRE, mire uno la lente que mire (decisión de la auditora, prompt 50).
//
// La lente a demanda existe para no pagar los módulos CAROS que nadie está mirando (kStereo, kSpectrum,
// kCqt, kField, kReference). `kLoudness` no es uno de esos: cuesta dos biquads y un FIR de 48 taps por
// canal, y sus números —integrado, LRA, histograma, contador de clips— ACUMULAN desde el reset. Apagarlo
// mientras el usuario mira el espectro le dejaría al integrado un agujero del tamaño de ese rato, y nada
// en pantalla lo diría. Un medidor con agujeros invisibles es peor que no tener medidor.
inline constexpr juce::uint32 kAlwaysOnModules = kLoudness;

// dB de "silencio digital": el piso que muestran los campos de loudness cuando todavía no hay medición.
inline constexpr float kSilenceDb = -300.0f;

struct AnalysisFrame
{
    double timeSeconds = 0.0;          // audio analizado desde el último reset

    float peakL = 0.0f, peakR = 0.0f;  // pico lineal del último hop (100 ms)
    float rmsL  = 0.0f, rmsR  = 0.0f;  // RMS lineal del último hop

    struct Loudness
    {
        float momentary    = kSilenceDb;   // LUFS, ventana de 400 ms
        float shortTerm    = kSilenceDb;   // LUFS, ventana de 3 s
        float integrated   = kSilenceDb;   // LUFS, doble compuerta (-70 absoluta / -10 relativa)
        float lra          = 0.0f;         // LU (EBU Tech 3342): P95 - P10 de los short-term compuertados
        float truePeakMax  = kSilenceDb;   // dBTP máximo desde el último reset
        float momentaryMax = kSilenceDb;   // LUFS
        float shortTermMax = kSilenceDb;   // LUFS
        bool  integratedValid = false;     // false mientras ningún bloque pase la compuerta absoluta
    } loudness;

    juce::uint32 droppedSamples = 0;   // muestras que el bus tuvo que descartar (UI: "análisis atrasado")
    juce::uint32 enabledModules = 0;   // máscara efectiva con la que se calculó este frame

    // ---- prompt 49 · módulo Stereo de banda ancha (kStereo). Ver analysis/modules/Stereo.h ----
    float corr       = 0.0f;   // ΣLR / √(ΣLL·ΣRR)                         +1 mono · 0 sin correlación · -1 fuera de fase
    float width      = 0.0f;   // √(ΣSS/ΣMM)                               0 mono · 1 independientes · tope 10
    float balanceDb  = 0.0f;   // 10·log10(ΣRR/ΣLL), clampeado a ±60       + = R más fuerte
    float monoLossDb = 0.0f;   // 10·log10(ΣMM) - 10·log10((ΣLL+ΣRR)/2)    0 si L=R · -3.01 indep · piso -60
    float stereoWindowSec = 0.0f;   // la ventana EFECTIVA con la que se calcularon los cuatro de arriba

    // ---- prompt 49 · datos de DYNAMICS (salen del mismo medidor, kLoudness) ----
    float truePeakHop = kSilenceDb;   // dBTP máximo del último hop
    float psr         = 0.0f;         // dB · TP máx de los últimos 3 s - short-term
    float plr         = 0.0f;         // dB · TP máx desde el reset - integrado (AES TD1004)
    bool  psrValid    = false;
    bool  plrValid    = false;
    juce::uint32 clipEvents = 0;      // eventos de clip desde el reset, sobre el umbral vigente
    juce::uint32 histogram[61] = {};  // short-term por bin de 1 LU; el bin i está centrado en (i-60) LUFS

    // ---- prompt 51 · módulo StereoBands (kStereoBands). Ver analysis/modules/StereoBands.h ----
    // Los mismos cuatro números del estéreo de banda ancha, pero POR BANDA de ⅓ de octava (ISO 266, las
    // 30 de SpectrumFrame::kNumThird), calculados sobre los bins de la STFT (Parseval). Una banda sin
    // ningún bin —o sin energía— queda en 0 y no cuenta en `bandsValid`: no es "cero correlación", es
    // que a esa resolución no hay medición.
    float bandCorr      [30] = {};   // ΣLR / √(ΣLL·ΣRR)                       por banda
    float bandWidth     [30] = {};   // √(ΣSS/ΣMM)                             por banda, tope 10
    float bandBalanceDb [30] = {};   // 10·log10(ΣRR/ΣLL), clampeado a ±60     por banda
    float bandMonoLossDb[30] = {};   // 10·log10(ΣMM) − 10·log10((ΣLL+ΣRR)/2)  por banda, piso −60
    float bandsWindowSec = 0.0f;     // la ventana EFECTIVA (los frames que realmente se sumaron)
    int   bandsValid     = 0;        // cuántas de las 30 bandas tienen medición

    // ---- prompt 52 · tonalidad estimada (kCqt). Ver analysis/modules/Cqt.h ----
    // El espectro constant-Q y el cromagrama viajan por su propio TripleBuffer (son 4 KB); acá van sólo
    // los CUATRO números de la tonalidad, que son 16 bytes y los va a leer VERDICT (lente 13).
    //
    // tonic = clase de nota (C=0 … B=11), mode 0 = mayor, 1 = menor. Los DOS en −1 mientras no haya
    // cromagrama que correlacionar — que es también lo que dicen con el módulo apagado, y es lo honesto:
    // "no hay medición" no es "Do mayor con confianza 0".
    int   keyTonic = -1, keyMode = -1;
    float keyConfidence   = 0.0f;    // correlación de Pearson con el perfil ganador (−1 … 1)
    float keyTimeFraction = 0.0f;    // % del tiempo desde el reset en que ESA tonalidad fue la mejor

    // ---- prompt 55 · historia por segundo y continua (kLoudness). Ver analysis/SecondHistory.h ----
    // `secondsAnalysed` es cuántas FILAS de un segundo se cerraron desde el reset — no `timeSeconds`
    // redondeado: el segundo en curso todavía no existe como fila, y VERDICT sólo puede concluir sobre
    // filas cerradas.
    juce::uint32 secondsAnalysed = 0;
    // Media de TODAS las muestras desde el reset, por canal, sobre la señal cruda. |dc| > 0.01 es la
    // regla de continua de VERDICT (ver plugins/telescope/docs/telescope-diccionario.md).
    float dcL = 0.0f, dcR = 0.0f;

    // ========================================================================================================
    // ---- prompt 57c ---- APPEND-ONLY (ver el encabezado: los prompts AGREGAN, nunca reordenan).
    //
    // «Sólo es M–S, también debería poder ser L y R, y que no haya retraso al cargar el medidor»
    // (Joaquín, 12-sep). Las dos mitades salen del MISMO medidor que ya estaba —ver
    // analysis/modules/Loudness.h, donde está el porqué de cada uno— y ninguna toca un número existente:
    //
    //   · TRUE-PEAK POR CANAL. El módulo ya calculaba `tpL` y `tpR` por muestra y se quedaba con el
    //     máximo de los dos; ahora los guarda por separado. `truePeakHop` y `truePeakMax` no cambian.
    //   · MOMENTARY / SHORT-TERM PARCIALES. La misma media, sobre `min (hops, N)` hops: válidos desde el
    //     PRIMER hop en vez de a los 400 ms y a los 3 s, e iguales AL BIT a los oficiales en cuanto la
    //     ventana se llena. El INTEGRADO no tiene parcial: la compuerta es la compuerta.
    // ========================================================================================================
    float truePeakHopL = kSilenceDb, truePeakHopR = kSilenceDb;   // dBTP del último hop, por canal
    float truePeakMaxL = kSilenceDb, truePeakMaxR = kSilenceDb;   // dBTP desde el reset, por canal
    float momentaryPartial = kSilenceDb;   // LUFS sobre la ventana de 400 ms INCOMPLETA
    float shortTermPartial = kSilenceDb;   // LUFS sobre la ventana de 3 s INCOMPLETA
};
}
