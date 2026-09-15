#pragma once
#include <vector>
#include "analysis/FieldFrame.h"
#include "analysis/modules/StereoBands.h"

// ========================================================================================================
// Field — el motor de la lente FIELD (lente 11, spec §5.5, prompt 53): la energía repartida por DIRECCIÓN
// DE PANEO × FRECUENCIA, con decaimiento y estela.
//
// DE DÓNDE COME. De los frames EMITIDOS del módulo StereoBands (su `FieldSink`), que le pasa dos vectores
// por bin ya calculados: el paneo por energía de la ventana (`pan`) y la energía instantánea del frame
// (`energy`). No vuelve a transformar nada ni a recorrer la STFT dos veces.
//
// POR QUÉ LOS EMITIDOS Y NO LOS CALCULADOS. Los dos son deterministas (posiciones fijas del stream), pero
// con orden 12 y 87.5 % de solape se CALCULAN 375 frames por segundo y se EMITEN 60. Acumular una grilla
// de 96×64 seis veces de más por frame sería tirar CPU para dibujar exactamente lo mismo. El precio es
// que la constante de tiempo del decaimiento se cuantiza a la tasa de emisión (~21 ms a 48 kHz con
// orden 12): sobre τ = 0.5 s eso es un 4 % en el peor caso, y `decaySec` publica el τ que se usó.
//
// EL DECAIMIENTO. Por frame, `grid *= exp(−dt/τ)` con dt = 1/emitRate; después cada bin suma su energía.
// Es un promediado exponencial en energía: sin señal nueva, la grilla cae a 1/e en exactamente τ. No es
// decoración — es lo que hace que la lente muestre "qué viene sonando" y no "qué sonó en este frame",
// que a 21 ms sería confeti.
//
// EL REPARTO ENTRE COLUMNAS ES LINEAL Y NADA MÁS. Un bin con paneo p suma su energía a las DOS columnas
// que rodean a p, pesada por la distancia. Ni gaussiana ni kernel ancho: una fuente puntual tiene que
// verse puntual. Ensancharla para que "quede lindo" sería dibujar una anchura que la medición no tiene.
//
// CERO ASIGNACIONES POR FRAME. La grilla y la estela son arrays fijos dentro del FieldFrame; lo único que
// se dimensiona es la tabla bin→fila, y sólo cuando cambia la geometría del análisis.
// ========================================================================================================
namespace telescope
{
class Field : public StereoBands::FieldSink
{
public:
    static constexpr int kDir   = FieldFrame::kDir;
    static constexpr int kRows  = FieldFrame::kRows;
    static constexpr int kTrail = FieldFrame::kTrail;

    // Las tres constantes de tiempo del setting `fieldDecaySec` de la lente.
    static constexpr int   kNumDecayOptions = 3;
    static constexpr float kDecaySecOptions[kNumDecayOptions] = { 0.5f, 1.0f, 2.0f };
    static constexpr int   kDefaultDecayIndex = 1;   // 1 s

    // Por debajo de esto no hay energía que acumular (el mismo piso del estéreo por banda).
    static constexpr double kTinyEnergy = StereoBands::kTinyEnergy;

    // 0 = 0.5 s · 1 = 1 s · 2 = 2 s. Cambiarla NO limpia la grilla: el decaimiento es un promedio
    // exponencial, y cambiar su constante en caliente es exactamente lo que un promedio exponencial
    // sabe hacer. Lo que sí cambia es `decaySec` del frame, que dice con qué τ se está midiendo.
    void setDecayIndex (int i) noexcept;
    int  decayIndex() const noexcept { return decayIdx; }

    void reset();

    //== StereoBands::FieldSink ==
    void stereoBandsFrameEmitted (const StereoBands::FieldFrameInfo&) override;
    void stereoBandsReset() override { reset(); }

    const FieldFrame& frame() const noexcept { return out; }
    juce::uint32      framesEmitted() const noexcept { return emitted; }

    // Lo que el módulo está corriendo AHORA (lo leen los tests y la lente para rotular).
    double emitRate() const noexcept { return lastEmitRate; }
    int    numBins()  const noexcept { return lastBins; }
    // Bins que caen DENTRO de la grilla (los de fuera de 20 Hz–20 kHz se descartan: ver FieldFrame.h).
    int    binsInGrid() const noexcept { return insideBins; }

private:
    void rebuild (const StereoBands::FieldFrameInfo&);
    void updateDecay() noexcept;   // el factor por frame: depende de τ y de la tasa de emisión, nada más
    void pushTrail();      // la grilla del frame ANTERIOR pasa a la estela (nunca la actual)

    FieldFrame   out;
    juce::uint32 emitted = 0;
    int          decayIdx = kDefaultDecayIndex;

    // La tabla bin → fila, precalculada: un std::log10 por bin por frame serían ~1 M logaritmos por
    // segundo con orden 15, y la geometría sólo cambia cuando cambian los settings del espectro.
    std::vector<int> binRow;
    int    lastBins     = 0;
    int    insideBins   = 0;
    double lastBinHz    = 0.0;
    double lastEmitRate = 0.0;
    float  decayPerFrame = 1.0f;
    bool   dirty = true;
};
}
