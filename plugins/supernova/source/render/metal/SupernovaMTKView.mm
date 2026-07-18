#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>
#import <Metal/Metal.h>
#include "render/metal/SupernovaMTKView.h"

// NSView layer-hosting con CAMetalLayer. Patrón probado (JUCE forum + Apple Metal best practices):
//  · makeBackingLayer devuelve la CAMetalLayer  → wantsLayer=YES la vuelve HOSTING (nuestra layer es la del view)
//  · presentsWithTransaction + waitUntilScheduled (en el renderer) → resize sin tearing
//  · contentsScale = backingScaleFactor y drawableSize = bounds*scale en cada cambio de backing/tamaño (Retina)
@interface SupernovaMTKView : NSView
@end

@implementation SupernovaMTKView

- (CALayer*)makeBackingLayer { return [CAMetalLayer layer]; }
- (CAMetalLayer*)metalLayer  { return (CAMetalLayer*) self.layer; }

- (instancetype)initWithFrame:(NSRect)frame
{
    self = [super initWithFrame:frame];
    if (self != nil)
    {
        self.wantsLayer = YES;   // layer-HOSTING (usa makeBackingLayer)
        CAMetalLayer* ml = self.metalLayer;
        ml.pixelFormat = MTLPixelFormatBGRA8Unorm;
        ml.framebufferOnly = YES;
        ml.presentsWithTransaction = YES;
        self.layerContentsRedrawPolicy = NSViewLayerContentsRedrawDuringViewResize;
        [self updateDrawableSize];
    }
    return self;
}

- (void)viewDidChangeBackingProperties
{
    [super viewDidChangeBackingProperties];
    [self updateDrawableSize];
}

- (void)setFrameSize:(NSSize)newSize
{
    [super setFrameSize:newSize];
    [self updateDrawableSize];
}

- (void)updateDrawableSize
{
    CAMetalLayer* ml = self.metalLayer;
    CGFloat scale = 2.0;
    if (self.window != nil)             scale = self.window.backingScaleFactor;
    else if (NSScreen.mainScreen != nil) scale = NSScreen.mainScreen.backingScaleFactor;

    ml.contentsScale = scale;                                   // Retina
    NSSize backing = [self convertSizeToBacking:self.bounds.size];
    if (backing.width >= 1.0 && backing.height >= 1.0)
        ml.drawableSize = CGSizeMake (backing.width, backing.height);  // INVARIANTE: bounds * contentsScale
}

@end

namespace supernova
{
void* createSupernovaMTKView (void* device)
{
    SupernovaMTKView* v = [[SupernovaMTKView alloc] initWithFrame:NSMakeRect (0, 0, 100, 100)];
    v.metalLayer.device = (__bridge id<MTLDevice>) device;
    return (__bridge_retained void*) v;   // +1: el caller lo suelta con destroySupernovaMTKView
}

void destroySupernovaMTKView (void* nsView)
{
    if (nsView != nullptr)
    {
        SupernovaMTKView* v = (__bridge_transfer SupernovaMTKView*) nsView;  // recupera el +1 → ARC lo libera
        (void) v;
    }
}

void* metalLayerOf (void* nsView)
{
    if (nsView == nullptr) return nullptr;
    SupernovaMTKView* v = (__bridge SupernovaMTKView*) nsView;
    return (__bridge void*) v.metalLayer;
}
}
