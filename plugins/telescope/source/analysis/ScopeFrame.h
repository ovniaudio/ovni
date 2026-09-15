#pragma once

// ========================================================================================================
// ScopeFrame — los BUFFERS de onda que la lente SCOPE dibuja (goniómetro + osciloscopio).
//
// Viaja por su PROPIO TripleBuffer, aparte del AnalysisFrame. Motivo: son ~40 KB de muestras por frame y
// el AnalysisFrame lo leen las 13 lentes 30 veces por segundo — meterle 40 KB adentro haría que WATERFALL
// copie el osciloscopio que no va a dibujar nunca. Un POD de arrays fijos: cero allocations en el worker.
//
// Lo llena el módulo Stereo al cerrar cada hop (100 ms) y lo publica el AnalysisThread.
// ========================================================================================================
namespace telescope
{
struct ScopeFrame
{
    // El goniómetro dibuja a lo sumo unos miles de puntos: más no se ven y cuestan. 2 048 es el tope.
    static constexpr int kMaxXy  = 2048;
    // El osciloscopio muestra 40 ms. A 48 k son 1 920 muestras; el tope de 2 048 alcanza hasta ~51.2 kHz.
    // Por encima de eso la ventana visible se acorta (se guarda lo que entra) — está dicho en el README.
    static constexpr int kMaxOsc = 2048;

    // --- goniómetro: pares (L,R) del hop, decimados uniformemente si el hop tiene más de kMaxXy ---
    float xyL[kMaxXy] {};
    float xyR[kMaxXy] {};
    int   xyCount = 0;

    // --- osciloscopio: los últimos 40 ms del hop ---
    float oscM[kMaxOsc] {};
    float oscL[kMaxOsc] {};
    float oscR[kMaxOsc] {};
    int   oscCount = 0;

    // Índice del PRIMER cruce por cero ASCENDENTE de M (oscM[t-1] < 0 <= oscM[t]) dentro de los primeros
    // 20 ms. -1 si no hay ninguno: ahí el osciloscopio dibuja desde 0 y lo dice.
    int trigger = -1;

    double timeSeconds = 0.0;   // el mismo reloj del AnalysisFrame (audio analizado desde el reset)

    // ========================================================================================================
    // ===== 56: HEMISFERIO · la envolvente por dirección =====
    //
    // La vista que pidió Joaquín para MEZCLA ESTÉREO: un semicírculo con mono arriba, L y R en la base, y
    // la energía dibujada como una envolvente RELLENA por dirección. Es lo que un Insight muestra al lado
    // del correlímetro, y es lo que hace que una mezcla se lea de un vistazo.
    //
    // CONVENCIÓN DE ÁNGULOS (la misma que documenta el README y verifica [hemis]):
    //
    //     θ = 90° + 2 · atan2 (R − L, R + L)        ≡  2 · atan2 (R, L)   (mod 360°)
    //
    //     sólo L  →   0°        L = R (mono)  →  90°        sólo R  → 180°
    //     L = −R  → 270°  (hemisferio INFERIOR: lo que está fuera de fase cae abajo de la base)
    //
    // El factor 2 es lo que hace que el semicírculo de arriba cubra TODO el estéreo en fase: el cuadrante
    // real de un vector (L, R) con las dos componentes positivas mide 90°, y acá se abre a 180°. Sin él,
    // media pantalla no se usaría nunca.
    //
    // NO ES LOCALIZACIÓN. Esto es dirección de PANEO por energía instantánea, no de dónde viene el sonido
    // en una sala: no hay HRTF, ni ITD, ni nada por el estilo. Está dicho en la lente y en el README (la
    // misma honestidad que el rótulo de FIELD).
    static constexpr int   kHemiBins    = 360;      // un grado por bin
    static constexpr float kHemiFloorDb = -60.0f;   // el piso: por debajo no hay dirección que mostrar

    // Por GRADO, el nivel máximo —20·log10 √(L²+R²)— de las muestras del hop que apuntan a esa dirección.
    // Es del HOP, sin decaimiento: la memoria (el peak-hold que se desvanece) la pone la lente, que es
    // donde vive el setting de vista, igual que las líneas y la inclinación de WATERFALL. Así el motor
    // publica un hecho y la vista decide cuánto lo recuerda.
    float envelope[kHemiBins] {};
    float envelopePeakDb = kHemiFloorDb;   // el máximo de todo el hop (rotula la escala del dibujo)
};
}
