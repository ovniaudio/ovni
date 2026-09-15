#pragma once
#include <vector>
#include "analysis/History.h"
#include "lenses/Lens.h"
#include "lenses/Look.h"

namespace telescope
{
class TelescopeProcessor;

// ========================================================================================================
// LOUDNESS — la lente 1. Muestra el integrado como número héroe (es el que decide si el tema pasa o no
// por la puerta de una plataforma), M y S como secundarios con sus barras, LRA / TP máx / máximos como
// lectura fina, la historia de short-term de los últimos 3 minutos, y el delta contra el objetivo.
//
// RESET y PAUSE son botones de la lente, no parámetros: no tiene sentido automatizar "poné el medidor en
// cero" ni guardarlo en un preset.
// ========================================================================================================
class LoudnessLens : public Lens
{
public:
    // 56: los dos umbrales de las zonas de color del medidor, en LUFS. -9 es donde una mezcla de streaming
    // ya está apretada y -6 donde prácticamente no queda aire: no son números redondos por gusto, son
    // dónde un productor empieza a mirar el limitador.
    static constexpr float kCautionLufs = -9.0f;
    static constexpr float kAlertLufs   = -6.0f;

    explicit LoudnessLens (TelescopeProcessor& p);

    juce::String name() const override            { return kLensNames[(int) LensId::loudness]; }
    LensId       id() const override              { return LensId::loudness; }
    juce::uint32 requiredModules() const override { return kLoudness; }
    // El idioma sale del estado del procesador (ver Lens::tr()).
    const juce::ValueTree& stateTree() const override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

protected:
    void renderStatic (juce::Graphics&, int width, int height) override;
    void paintLive (juce::Graphics&) override;
    bool advanceFrame() override;

private:
    // Escala de los medidores de barra: -60 … 0 LUFS, marcas cada 6.
    static constexpr float kScaleTop = 0.0f, kScaleBottom = -60.0f, kScaleStep = 6.0f;
    // 57c — las dos barras de dBTP comparten la escala de las LUFS (0 … −60): es lo que hace que las
    // cuatro se lean de un vistazo como una sola familia. Sus umbrales de color, en cambio, son los del
    // dominio del pico: ámbar en el umbral de clip de DYNAMICS y alerta en 0 dBTP.
    static constexpr float kTpAlertDbtp = 0.0f;
    static constexpr int   kHistorySeconds = 180;                       // 3 minutos
    static constexpr int   kHistoryPoints  = kHistorySeconds * LoudnessHistory::kHz;

    struct Zones
    {
        juce::Rectangle<int> header, meters, history, footer, resetBtn, pauseBtn;
    };
    Zones zonesFor (int w, int h) const;

    // ========================================================================================================
    // ===== 57c · LA COLUMNA DE MEDIDORES, EN UN SOLO LUGAR =====
    //
    // Eran dos barras y ahora son CUATRO (MOMENTARY · SHORT-TERM en LUFS, L · R en dBTP), así que dónde cae
    // cada una la calcula esta función y la usan las DOS capas. Antes cada una repetía la cuenta y por eso
    // el marco de la estática y el relleno de la viva podían quedar desalineados por un píxel.
    //
    // Los rótulos van ADENTRO del área: dos filas, la del nombre y la de la unidad (una sola "LUFS" bajo
    // las dos primeras y una sola "dBTP" bajo las dos últimas — repetirla cuatro veces es ruido).
    static constexpr int kNumBars = 4;
    struct MeterCols
    {
        juce::Rectangle<int> scale, bar[kNumBars], names, units;
        float labelFontSize = 10.0f;
    };
    static MeterCols meterColsFor (juce::Rectangle<int> meters) noexcept;

    // Convierte LUFS a 0..1 dentro de la escala del medidor (0 = fondo, 1 = tope).
    static float toScale01 (float lufs) noexcept;

    void paintReadout (juce::Graphics&, juce::Rectangle<int> area, const juce::String& label,
                       float value, const juce::String& unit, float heroSize, bool valid) const;
    // `partial` = la ventana todavía se está llenando: la barra se dibuja al 55 % con contorno fino (ver
    // el bloque de paintBar). `clipDbtp` > kNoReading dibuja además la línea del umbral de clip.
    void paintBar (juce::Graphics&, juce::Rectangle<int> area, float value, float peakHold,
                   bool partial, float cautionDb, float alertDb, float clipDbtp) const;
    void paintButton (juce::Graphics&, juce::Rectangle<int> area, const juce::String& text,
                      bool active, bool hovered) const;
    juce::String targetLine() const;

    TelescopeProcessor& processor;

    // Valores MOSTRADOS (suavizados hacia el frame real; con reduced-motion saltan de una).
    float dispM = -300.0f, dispS = -300.0f, dispI = -300.0f;
    // 57c — los dos true-peak por canal, en dBTP, con la misma balística que los de arriba.
    float dispTpL = -300.0f, dispTpR = -300.0f;
    AnalysisFrame::Loudness latest {};
    // 57c — lo que viaja FUERA del bloque `loudness` del frame (campos append-only): el true-peak por
    // canal y las ventanas parciales. Ver analysis/AnalysisFrame.h.
    float tpMaxL = -300.0f, tpMaxR = -300.0f;
    bool  momentaryIsPartial = false, shortTermIsPartial = false;
    juce::uint32 dropped = 0;

    std::vector<LoudnessHistory::Point> historyBuf;
    Zones zones {};
    int   hovered = -1;   // 0 = RESET, 1 = PAUSE

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoudnessLens)
};
}
