#pragma once
#include <memory>

// MetalSyphonServer — fachada C++ pura sobre SyphonMetalServer (Obj-C, BSD). Aísla el import de Syphon del resto
// del renderer y acorrala cualquier falla (RNF3): si el server no se puede crear o el publish tira, se desarma
// solo y la ventana sigue. Todo por el message thread (VBlank + botón) → sin sincronización.
namespace supernova
{
class MetalSyphonServer
{
public:
    MetalSyphonServer (void* mtlDevice, const char* name);   // crea SyphonMetalServer; puede quedar inválido
    ~MetalSyphonServer();                                     // stop + release

    bool isValid()    const noexcept;                         // server != nil
    bool hasClients() const noexcept;                         // para saltear el publish si nadie mira

    // Publica una textura BGRA8 legible en el command buffer dado. Llamar ANTES del commit. No-op si !isValid().
    void publish (void* mtlTexture, void* mtlCommandBuffer, int w, int h, bool flipped) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
