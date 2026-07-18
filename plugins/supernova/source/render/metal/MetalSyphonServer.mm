#import <Metal/Metal.h>
#import <Foundation/Foundation.h>
#import "SyphonMetalServer.h"
#include "render/metal/MetalSyphonServer.h"

// Único archivo que importa Syphon. La firma real del server Metal (master) es publishFrameTexture:
// onCommandBuffer:imageRegion:flipped: (la del SyphonServer GL clásico está deprecada). Syphon blitea a un
// IOSurface en ESE command buffer → el publish va DENTRO del frame, antes del commit; y la textura de origen
// NO puede ser framebufferOnly (por eso publicamos shareTex, no el drawable).
namespace supernova
{
struct MetalSyphonServer::Impl { SyphonMetalServer* server = nil; };

MetalSyphonServer::MetalSyphonServer (void* dev, const char* name) : impl (std::make_unique<Impl>())
{
    @try {
        impl->server = [[SyphonMetalServer alloc]
            initWithName: [NSString stringWithUTF8String: (name ? name : "SUPERNOVA")]
                  device: (__bridge id<MTLDevice>) dev
                 options: nil];               // visible (sin SyphonServerOptionIsPrivate)
    } @catch (NSException*) { impl->server = nil; }
}

MetalSyphonServer::~MetalSyphonServer()
{
    @try { [impl->server stop]; } @catch (...) {}
    impl->server = nil;
}

bool MetalSyphonServer::isValid()    const noexcept { return impl->server != nil; }
bool MetalSyphonServer::hasClients() const noexcept { return impl->server != nil && impl->server.hasClients; }

void MetalSyphonServer::publish (void* tex, void* cb, int w, int h, bool flipped) noexcept
{
    if (impl->server == nil || tex == nullptr || cb == nullptr) return;
    @try {
        [impl->server publishFrameTexture: (__bridge id<MTLTexture>) tex
                          onCommandBuffer: (__bridge id<MTLCommandBuffer>) cb
                              imageRegion: NSMakeRect (0, 0, w, h)
                                  flipped: flipped ? YES : NO];
    } @catch (NSException*) { impl->server = nil; }   // una falla desarma el server; la ventana sigue (RNF3)
}
}
