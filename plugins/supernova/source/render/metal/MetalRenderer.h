#pragma once
#include "render/IRenderer.h"
#include "render/ViewPhases.h"
#include <memory>

// MetalRenderer — backend Metal (macOS). Fachada C++ pura; los tipos Obj-C viven detrás de un pimpl en el .mm
// (así ningún header C++ arrastra Cocoa/Metal). GPU-opcional: si MTLCreateSystemDefaultDevice() da nil,
// isAvailable()==false y todo lo demás es no-op (CI sin GPU / spec §8).
namespace supernova
{
class MetalRenderer : public IRenderer
{
public:
    MetalRenderer();
    ~MetalRenderer() override;

    bool isAvailable() const override;
    void prepare (int gridW, int gridH) override;
    void uploadImage (const SourceImage& img) override;
    void resize (int pxW, int pxH, float contentsScale) override;
    void render (const AnalysisFrame&, const ParticleParams&, double dtSeconds) override;
    bool renderOffscreen (const AnalysisFrame&, const ParticleParams&, int pxW, int pxH, uint8_t* outRgba) override;

    // Handle del MTLDevice (como void*) para que el NSView cree su CAMetalLayer con el mismo device.
    void* deviceHandle() const;
    // Conecta la CAMetalLayer del PREVIEW del editor (= target[0], primary). void* = CAMetalLayer*.
    void  setLayer (void* caMetalLayer);
    // Present targets adicionales (fullscreen a 2º monitor): N pantallas, UNA sola simulación por tick.
    void  addPresentTarget    (void* caMetalLayer, bool primary);   // primary = el que publica Syphon
    void  removePresentTarget (void* caMetalLayer);

    // Densidad adaptativa (RNF2): partículas efectivamente dibujadas este frame / total. render() las ajusta
    // según el frame-time; renderOffscreen() siempre usa el total (determinismo para QA).
    unsigned activeParticles() const;
    unsigned totalParticles() const;

    // Servidor Syphon (RF7): publica la textura final a OBS/Resolume/VDMX. On/off perezoso (msg thread).
    void setSyphonEnabled (bool on);

    // CLEAR (estado cero): teletransporta las partículas a su hogar, mata velocidades y resetea el estado
    // acumulado del mundo (giro/deriva/órbita/pulsos) → el lienzo aparece en el PRIMER frame, sin viaje.
    void snapToHome();

    // FIT / FILL (MEDIA SESSION PRO): cómo entra la imagen en cada target. 0 = FIT/contain (letterbox, el de
    // siempre) · 1 = FILL/cover (la imagen LLENA el target, recortada). Sólo cambia el cálculo CPU de fitX/fitY
    // por target; con imagen y target del mismo aspecto (los goldens) ambos dan 1,1 → byte-exacto.
    void setFitMode (int mode);
    int  fitMode() const;

    // FUNDIDO entre fotos (siempre, sin ajuste): cuánto dura la disolución de la foto que está hacia la que
    // entra. 0 = CORTE — el comportamiento histórico, y el DEFAULT: el renderer nace en identidad byte-exacta
    // y son el editor y el export los que le pasan la duración real (mismo patrón que ParticleParams). Con
    // s > 0 y una imagen ya en pantalla, uploadImage no toca posiciones ni velocidades: deja la foto nueva en
    // un segundo juego de buffers y las mezcla por atributo. NO está en IRenderer.h (congelado en M0).
    void   setDissolveSeconds (double s);
    double dissolveSeconds() const;

    // VIDEO (spec §C): camino RÁPIDO — reescribe SOLO el buffer de colores (muestreo por celda + sRGB→lineal)
    // sin re-analizar geometría. La primera imagen del video se sube con uploadImage (fija flow/máscara);
    // los frames siguientes solo recolorean el lattice → la pantalla LED reproduce el video.
    void updateColors (const uint8_t* rgba, int w, int h);
    bool isSyphonActive() const;

    // FASES ACUMULADAS del mundo (ROTATE / ORBIT / HUE CYC). El export las LEE del renderer vivo en el
    // message thread y se las pone al suyo: sin esto el MP4 salía frontal y con el tono base mientras la
    // ventana estaba inclinada por ORBIT. Tres floats por valor — nada compartido entre hilos.
    ViewPhases viewPhases() const;
    void       setViewPhases (const ViewPhases& p);

    // INVARIANCIA AL TAMAÑO en el camino OFFSCREEN. En pantalla el glifo escala con min(w,h)/1024 para que la
    // figura cubra la MISMA fracción del cuadro en cualquier vista; el offscreen quedaba clavado en 1,0, así
    // que el mismo clip salía ~2,7× más oscuro en 4K que en 1080p. Default `false` = camino legacy byte-exacto
    // (goldens, --render-frames); el EXPORT la enciende. NO está en IRenderer.h (congelado en M0).
    void setOffscreenSizeInvariance (bool on);
    bool offscreenSizeInvariance() const;

    // PASO DE TIEMPO del camino OFFSCREEN. Histórico y default: 1/60 s fijo (goldens, --render-frames y el
    // export a 60 fps). Con el ancla de la física en 120 Hz (D-43) ese paso vale DOS cuadros de ancla, así
    // que el offscreen decae por `pow()` y no por el atajo exacto — el atajo vive en dt = 1/120. Existe para
    // PROBAR la invariancia al refresh: el mismo tramo de segundos simulado con dt = 1/60 y con dt = 1/120
    // tiene que dar el mismo movimiento (test [exportparity]). No está en IRenderer.h (congelado en M0): es
    // del backend, no del contrato.
    void   setOffscreenDt (double seconds);
    double offscreenDt() const;

    // QA/debug: copia las posiciones actuales (x,y por partícula, buffer Shared) a outXY (2*count floats).
    // Válido tras renderOffscreen (waitUntilCompleted → coherente). Para el test de contención [bounds].
    bool debugReadPositions (float* outXY, unsigned countPairs) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
