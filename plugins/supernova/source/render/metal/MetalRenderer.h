#pragma once
#include "render/IRenderer.h"
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

    // VIDEO (spec §C): camino RÁPIDO — reescribe SOLO el buffer de colores (muestreo por celda + sRGB→lineal)
    // sin re-analizar geometría. La primera imagen del video se sube con uploadImage (fija flow/máscara);
    // los frames siguientes solo recolorean el lattice → la pantalla LED reproduce el video.
    void updateColors (const uint8_t* rgba, int w, int h);
    bool isSyphonActive() const;

    // QA/debug: copia las posiciones actuales (x,y por partícula, buffer Shared) a outXY (2*count floats).
    // Válido tras renderOffscreen (waitUntilCompleted → coherente). Para el test de contención [bounds].
    bool debugReadPositions (float* outXY, unsigned countPairs) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
