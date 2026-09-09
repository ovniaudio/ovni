#pragma once

// ExportWarmup — el clip no arranca de cero.
//
// El export crea un renderer FRESCO: partículas en su hogar, velocidades en cero, `time` en cero. El cuadro 0
// del MP4 era la imagen QUIETA y recién ahí empezaba el viaje — la ventana, en cambio, lleva minutos en
// régimen. Antes del cuadro 0 corremos un tramo de calentamiento DETERMINISTA con el análisis que precede al
// cuadro 0 EN EL LOOP (los últimos cuadros de la ventana): así el estado con el que arranca el clip es el
// mismo con el que lo cerraría, y el punto de loop deja de tener un salto.
//
// Puro: la regla se prueba sola (tests [exportwarmup]).
namespace supernova
{
// 2 s a 60 fps. Alcanza de sobra para el régimen (el re-armado más lento del catálogo vive en <1 s) y es
// +6,7 % de cuadros en un clip de 30 s.
inline constexpr int kExportWarmupFrames = 120;

// El cuadro de VIDEO que le toca al paso `k` del calentamiento: los `warmup` cuadros que preceden al cuadro 0
// dentro del loop de `loopV` cuadros. Con loopV corto se dan varias vueltas, que es lo correcto.
inline int exportWarmupVideoFrame (int k, int warmup, int loopV) noexcept
{
    if (loopV <= 0) return 0;
    const long long raw = (long long) loopV - (long long) warmup + (long long) k;
    long long m = raw % (long long) loopV;
    if (m < 0) m += (long long) loopV;
    return (int) m;
}
}
