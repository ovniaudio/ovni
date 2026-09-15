#pragma once
#include <cmath>
#include "analysis/ScopeFrame.h"
#include "lenses/Lens.h"
#include "lenses/Look.h"
#include "lenses/Raster.h"
#include "lenses/Strings.h"

namespace telescope
{
class TelescopeProcessor;

// ========================================================================================================
// SCOPE — la lente 8. Las tres formas clásicas de MIRAR el estéreo, en una pantalla:
//
//   izquierda  GONIÓMETRO — Lissajous rotado 45°: mono vertical, L y R en las diagonales, S horizontal.
//              Es el dibujo que dice de un vistazo si la mezcla es mono, ancha o está fuera de fase.
//              Modo POLAR (setting scopePolar): mismo ángulo, pero el radio es el nivel en dB desde -60
//              hasta el borde en vez de la amplitud lineal — así el material bajo no se colapsa al centro.
//   centro     CORRELÍMETRO -1…+1 con la mitad negativa en el color de alerta, más WIDTH / BALANCE /
//              MONO LOSS como lectura fina, y el selector de ventana 100 / 300 / 1000 ms.
//   derecha    OSCILOSCOPIO — 40 ms: M sólido, L y R finos. Trigger (setting scopeTrigger) en el primer
//              cruce por cero ascendente de M; sin trigger la onda se dibuja desde el arranque del hop y
//              se ve nadar, que es la verdad de lo que llega.
//
// ESTELA del goniómetro: una capa propia que se ATENÚA cada frame y encima recibe los puntos nuevos
// (fósforo). Nunca acumula: el decaimiento es exponencial, así que un punto viejo desaparece. Con
// reduced-motion no hay estela — se dibuja sólo el hop actual, un cuadro estático coherente.
//
// ========================================================================================================
// ===== 56: EL HEMISFERIO · el tercer modo, y la vista de MEZCLA ESTÉREO =====
//
// Lo que pidió Joaquín: "esa vista que sería en mezcla estéreo tenemos que ponerla también, además de la
// que es 360". Es el semicírculo con mono arriba, la envolvente RELLENA por dirección y el correlímetro
// al lado — la lectura que un productor hace de un vistazo antes de tocar nada.
//
// POR QUÉ UN MODO Y NO OTRA LENTE. Mira exactamente el mismo dato que el Lissajous (el mismo ScopeFrame,
// la misma ventana): lo que cambia es la PROYECCIÓN. Partirlo en dos lentes obligaría a elegir cuál de
// las dos mirar cuando las dos dicen lo mismo de distinta manera.
//
// QUÉ AGREGA SOBRE EL GONIÓMETRO. La nube del Lissajous dice DÓNDE hay muestras; la envolvente dice
// CUÁNTO hay en cada dirección, que es la pregunta de una mezcla ("¿el bajo está centrado?", "¿cuánto
// material tengo fuera de fase?"). Y como el ángulo es lineal en el paneo (θ = 2α), las distancias en la
// pantalla se leen como distancias de paneo.
//
// LA MEMORIA VIVE ACÁ, NO EN EL MOTOR. El módulo publica la envolvente del HOP (ver ScopeFrame.h) y la
// vista le pone el peak-hold que se desvanece, con su propio setting de decaimiento. Es el mismo reparto
// que ya usaban las líneas y la inclinación de WATERFALL: lo que no cambia lo que se MIDE es setting de
// vista. Además hace el decaimiento medible sin reloj de pared — decae por FRAME, así que un test que
// bombea n frames sabe exactamente cuántos dB tienen que haber bajado.
// ========================================================================================================
class ScopeLens : public Lens
{
public:
    // ===== 57c · EL RECTÁNGULO DE LA CACHÉ, para VISUAL[hd] =====
    //
    // La mutación de la auditora sobre el 57b encontró que el test medía el gradiente del PANEL ENTERO, y
    // ahí la rejilla, los textos y los trazos vectoriales —que se dibujan a escala física siempre— tapan
    // lo que hace la caché: con la caché forzada de vuelta a 1×, SPECTRUM seguía dando 3.17. Midiendo
    // sólo adentro de este rectángulo, la razón habla de la caché y de nada más.
    juce::Rectangle<int> cacheAreaForTest() const noexcept { return trailArea(); }
    // 57c — la escala física con la que se horneó la caché. VISUAL[hd] lo verifica además de medir la
    // nitidez: es la comprobación ESTRUCTURAL de la regla de lenses/Raster.h, y la que hace imposible
    // que una mutación de esa clase pase inadvertida.
    float cacheScaleForTest() const noexcept { return trail.scale(); }

    explicit ScopeLens (TelescopeProcessor& p);

    juce::String name() const override            { return kLensNames[(int) LensId::scope]; }
    LensId       id() const override              { return LensId::scope; }
    juce::uint32 requiredModules() const override { return kStereo; }

    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    // El canal del correlímetro vertical del modo hemisferio, para que el test mire DONDE se dibuja y
    // no donde cree que se dibuja.
    juce::Rectangle<int> correlationArea() const noexcept { return zones.centre; }

    // ===== 56 =====
    // Los TRES modos del panel izquierdo. El índice persiste en el ValueTree como `scopeMode`; si esa
    // propiedad no está (un preset guardado ANTES de este modo) se deriva del viejo `scopePolar`, así un
    // preset de ayer sigue abriendo en la vista en la que se guardó.
    enum class Mode { lissajous = 0, polar, hemisphere };
    static constexpr const char* kScopeModeProperty = "scopeMode";
    static constexpr const char* kHemiDecayProperty = "hemisphereDecayDbPerSec";
    static constexpr int   kNumHemiDecayOptions = 3;
    static constexpr float kHemiDecayOptions[kNumHemiDecayOptions] = { 12.0f, 24.0f, 48.0f };
    static constexpr int   kDefaultHemiDecayIndex = 1;   // 24 dB/s
    // Los fps a los que corre el decaimiento del hemisferio. Estaba escrito tres veces —dos en
    // ScopeLens.cpp y una en el test— así que "24 dB/s" podía querer decir dos cosas distintas.
    static constexpr int   kHemiDecayFps = 30;

    // Puras y sin estado: públicas para que [hemis] las mida solas, sin fabricar una ventana.
    // ===== 57b: LA ESCALA RADIAL DEL HEMISFERIO =====
    //
    // `relative` (default) es la de Insight: el radio es AMPLITUD relativa a la dirección más fuerte —
    // 0 dB en el borde, −6 dB a medio radio, −12 a un cuarto. Es la que hace que la dirección dominante
    // forme un LÓBULO en vez de un abanico que llena el semicírculo.
    //
    // Por qué la anterior no servía, y es una cuenta: mapeaba −60…0 dB linealmente sobre el radio, así
    // que una dirección a −12 dB quedaba a 0.8 del radio. Con cualquier música real TODAS las direcciones
    // caen dentro de los 20 dB de arriba, o sea entre 0.67 y 1.0 del radio: la envolvente se pega al arco
    // exterior y la lente dice "todo ancho" siempre. Eso es lo que Joaquín vio contra Insight.
    //
    // `decibel` conserva una escala en dB para quien la quiera leer así, pero con piso −24 y no −60: 24 dB
    // es el rango donde vive la diferencia entre direcciones de una mezcla.
    enum class HemiScale { relative = 0, decibel };
    static constexpr const char* kHemiScaleProperty = "scopeHemiScale";
    HemiScale hemiScale() const;
    void      setHemiScale (HemiScale s);
    static constexpr float kHemiRelFloorDb = -24.0f;   // piso de la opción en dB

    // El área del semicírculo, para que [hemis] mida DONDE se dibuja y no donde cree que se dibuja.
    juce::Rectangle<int> hemiPlotAreaForTest() const noexcept { return hemiPlotArea (zones); }

    // La energía FUERA DE FASE del hop, en porcentaje: Σ(l²+r²) de los pares con l·r < 0 sobre el total.
    // Pública para que [hemis] la mida sin fabricar una ventana.
    float outOfPhasePercent() const noexcept;

    struct HemiGeometry { float cx = 0.0f, baseY = 0.0f, rad = 0.0f; };
    static HemiGeometry hemiGeometry (juce::Rectangle<float> plot) noexcept;
    // dB → fracción del radio con la escala vigente. Compartida por la retícula y el dibujo (57b).
    static float hemiRadiusFrac (float db, float peakDb, HemiScale sc) noexcept;

    // ===== 57c · LOS 181 RAYOS =====
    //
    // El perfil que se DIBUJA: un valor por grado de 0° a 180°, ya plegado (θ' = 360° − θ manda lo que
    // está fuera de fase al lugar que le toca por paneo). `folded` = true devuelve el lóbulo de fuera de
    // fase; false, el que está en fase. Pública y estática para que [hemis] mida los rayos que se ven,
    // sin fabricar una ventana.
    static constexpr int kHemiRays = 181;
    static void foldProfile (const float* src360, float* dst181, bool folded) noexcept
    {
        for (int d = 0; d < kHemiRays; ++d)
            dst181[d] = src360[(size_t) (folded ? (360 - d) % 360 : d)];
    }

    Mode  scopeMode() const;
    void  setScopeMode (Mode m);

    int   hemiDecayIndex() const;
    void  setHemiDecayIndex (int i);
    float hemiDecayDbPerSec() const { return kHemiDecayOptions[hemiDecayIndex()]; }

    // ========================================================================================================
    // La ENVOLVENTE que se dibuja: el peak-hold por dirección, con decaimiento POR FRAME.
    //
    // Es pública y sin dependencias del editor a propósito: así [hemis] mide el decaimiento sobre esta
    // estructura sola, sin fabricar una ventana ni esperar un reloj. `decayDbPerFrame` se deriva de los
    // fps de la lente, que es la única forma de que "24 dB por segundo" quiera decir lo mismo en el test
    // que en la pantalla.
    struct HemisphereView
    {
        float env[ScopeFrame::kHemiBins] {};      // PICO retenido (el contorno)
        float avg[ScopeFrame::kHemiBins] {};      // PROMEDIO en el tiempo (el relleno) · 57c
        float peakDb = ScopeFrame::kHemiFloorDb;
        bool  primed = false;

        // ===== 57c · LA CONSTANTE DE TIEMPO DEL PROMEDIO =====
        //
        // 0.3 s, en ENERGÍA y por bin: es el tiempo de integración de un medidor de programa, corto para
        // seguir una mezcla y largo para que el relleno deje de latir hop a hop. El coeficiente sale de
        // los fps de la lente igual que el decaimiento del pico, que es la única forma de que "0.3 s"
        // quiera decir lo mismo en el test que en la pantalla:
        //     α = 1 − e^(−1/(fps·τ))  →  0.1051 a 30 fps
        static constexpr float kAvgTauSec = 0.3f;

        void reset() noexcept
        {
            for (auto& v : env) v = ScopeFrame::kHemiFloorDb;
            for (auto& v : avg) v = ScopeFrame::kHemiFloorDb;
            peakDb = ScopeFrame::kHemiFloorDb;
            primed = false;
        }

        // `fresh` = llegó un hop nuevo. Con `hold` en false (reduced-motion) NO hay memoria: ni el pico
        // retiene ni el promedio promedia — las dos capas son exactamente el hop, un cuadro quieto y
        // coherente.
        void update (const ScopeFrame& f, float decayDbPerSec, int fps, bool hold) noexcept
        {
            if (! primed) { reset(); primed = true; }

            const float step  = (hold && fps > 0) ? decayDbPerSec / (float) fps : 0.0f;
            const float alpha = (hold && fps > 0)
                                  ? 1.0f - std::exp (-1.0f / ((float) fps * kAvgTauSec))
                                  : 1.0f;
            peakDb = ScopeFrame::kHemiFloorDb;

            for (int i = 0; i < ScopeFrame::kHemiBins; ++i)
            {
                const float decayed = hold ? juce::jmax (ScopeFrame::kHemiFloorDb, env[i] - step)
                                           : ScopeFrame::kHemiFloorDb;
                env[i] = juce::jmax (decayed, f.envelope[i]);
                peakDb = juce::jmax (peakDb, env[i]);

                // EL PROMEDIO VA EN ENERGÍA, no en dB. Promediar decibeles es promediar logaritmos: un
                // hop en silencio arrastraría la media hacia el piso mucho más de lo que corresponde,
                // y dos hops de +0 y −20 dB darían −10 en vez de los −2.6 reales.
                const auto toE = [] (float db)
                {
                    return db > ScopeFrame::kHemiFloorDb ? std::pow (10.0f, db * 0.1f) : 0.0f;
                };
                const float e = toE (avg[i]) + (toE (f.envelope[i]) - toE (avg[i])) * alpha;
                avg[i] = e > 0.0f ? juce::jmax (ScopeFrame::kHemiFloorDb, 10.0f * std::log10 (e))
                                  : ScopeFrame::kHemiFloorDb;
            }
        }
    };

    // ===== LOS RAYOS QUE SE DIBUJARON, para [hemis] =====
    //
    // No la envolvente: los 181 valores con los que `paintHemisphere` armó el camino en el ÚLTIMO
    // pintado. La diferencia importa — el defecto que el 57c cerró era un filtro metido entre la
    // envolvente y el dibujo, así que un test que mirara la envolvente lo habría dado por bueno.
    // `peakLayer` elige el contorno (la retención) o el relleno (el promedio).
    const float* drawnRaysForTest (bool peakLayer) const noexcept
    {
        return peakLayer ? drawnPeak : drawnAvg;
    }

    // La envolvente dibujada, para [hemis]: mide los rayos que se ven, no los que cree que se ven.
    const HemisphereView& hemiViewForTest() const noexcept { return hemi; }

protected:
    void renderStatic (juce::Graphics&, int width, int height) override;
    void paintLive (juce::Graphics&) override;
    bool advanceFrame() override;

private:
    static constexpr float kTrailDecay  = 0.72f;   // por frame: a los ~8 frames (0.27 s) un punto se apagó
    static constexpr float kOscFloorDb  = -60.0f;  // piso del modo polar
    // Tope de amplificación de la auto-escala del Lissajous: por debajo de -40 dBFS ya no se agranda más.
    // Estirar la nube de una señal que no está sería exactamente la clase de mentira que este plugin no hace.
    static constexpr float kGonioMinPeak = 0.01f;  // -40 dBFS
    static constexpr int   kWindowOptions = 3;     // 100 / 300 / 1000 ms
    // ===== 56 ===== el arco exterior del hemisferio vale 0 dBFS y el centro el piso. Por encima de 0 hay
    // un MARGEN corto (una mono a escala completa mide +3.01 dB, ver ScopeFrame.h): el dato se dibuja ahí
    // en el color de caución en vez de recortarse contra el arco, que sería esconder justo el exceso.
    static constexpr float kHemiTopDb    = 0.0f;
    static constexpr float kHemiOverDb   = 6.0f;   // techo del margen
    static constexpr float kHemiOverFrac = 0.09f;  // cuánto radio ocupa ese margen


    struct Zones
    {
        juce::Rectangle<int> gonio, centre, osc, footer, readouts;
        juce::Rectangle<int> polarBtn, triggerBtn, decayBtn, scaleBtn;
        juce::Rectangle<int> windowBtn[kWindowOptions];
    };
    Zones zonesFor (int w, int h) const;
    // El área de dibujo del semicírculo: el panel menos la franja de números de arriba (57b).
    static juce::Rectangle<int> hemiPlotArea (const Zones& z) noexcept;

    void paintGonio (juce::Graphics&);
    // ===== 56 =====
    void paintHemisphere (juce::Graphics&);
    // 57b — ANCHO / BALANCE / PÉRDIDA MONO en fila, para el modo hemisferio (ver el .cpp).
    void paintStereoNumbers (juce::Graphics&, juce::Rectangle<int> area) const;
    void paintCorrelationVertical (juce::Graphics&, juce::Rectangle<int>) const;
    const juce::ValueTree& stateTree() const override;
    void paintCorrelation (juce::Graphics&, juce::Rectangle<int>) const;
    void paintOsc (juce::Graphics&, juce::Rectangle<int>) const;
    void paintReadout (juce::Graphics&, juce::Rectangle<int>, const juce::String& label,
                       const juce::String& value, juce::Colour tint) const;
    void paintButton (juce::Graphics&, juce::Rectangle<int>, const juce::String&, bool active, bool hovered) const;

    void decayTrail();                                    // atenúa la capa de fósforo un frame
    void plotPoints (juce::Image&, juce::Rectangle<int> area, float alpha) const;
    // El área LÓGICA que cubre la estela: es el rectángulo de la caché (ver cacheAreaForTest()).
    juce::Rectangle<int> trailArea() const noexcept { return zones.gonio.reduced (10); }

    TelescopeProcessor& processor;

    ScopeFrame   scope {};        // último ScopeFrame leído (copia: la lente lo dibuja a su ritmo)
    float        corr = 0.0f, width = 0.0f, balanceDb = 0.0f, monoLossDb = 0.0f, windowSec = 0.0f;
    float        dispCorr = 0.0f;   // el correlímetro se suaviza; los demás números son lectura directa
    // Auto-escala del Lissajous: el anillo exterior = el PICO del hop (suavizado, ataque rápido / caída
    // lenta como un medidor de picos). Sin esto una mezcla a -20 dBFS pico se dibuja como un punto en el
    // centro y el goniómetro deja de servir para lo que sirve: leer la FORMA del estéreo, no el nivel.
    // El pico se ROTULA, así la escala nunca queda escondida.
    float        gonioPeak = kGonioMinPeak;
    double       lastFrameTime = -1.0;

    // ===== 57c · LA CACHÉ DE PÍXELES, EN raster::Cache =====
    //
    // Era un `juce::Image` más un `float rasterScale` y la lógica de "¿cambió el tamaño o la escala?"
    // copiada a mano, en cinco lentes. Eso es lo que encontró la mutación de la auditora: poniendo la
    // caché de `raster::Cache` de vuelta a 1× —el defecto que el 57b vino a cerrar— `VISUAL[hd]` seguía
    // VERDE, porque de las seis lentes que mide, cinco no pasaban por esa clase. Ahora las seis sí: una
    // mutación de lenses/Raster.h las tumba a todas.
    raster::Cache trail;          // capa de fósforo del goniómetro, EN PÍXELES DE DISPOSITIVO: el punto
                                  // de 1 px es la unidad de esta lente y estirarlo ×2 lo convertía en un
                                  // cuadradito de 4 (ver lenses/Raster.h).
    Zones        zones {};
    int          hovered = -1;    // 0 = modo, 1 = trigger, 2..4 = ventana, 5 = decaimiento del hemisferio
    HemisphereView hemi {};       // 56
    // 57c — los 181 rayos del último pintado (ver drawnRaysForTest). `mutable` porque paintHemisphere
    // los llena y el resto de la lente no los mira.
    mutable float drawnPeak[kHemiRays] {}, drawnAvg[kHemiRays] {};
    Mode         lastMode = Mode::lissajous;   // 56: cambiar de modo rehornea la capa estática

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScopeLens)
};
}
