#pragma once
// Fachada C++ para el NSView layer-hosting (Obj-C++ vive en el .mm). El caller recibe el NSView como void*
// con +1 retain: lo pasa a juce::NSViewComponent::setView (que retiene su copia) y luego lo suelta con
// destroySupernovaMTKView (balancea el +1 propio; el lado JUCE lo libera al soltar la view).
namespace supernova
{
void* createSupernovaMTKView (void* device);   // NSView* (+1 retained). device = MTLDevice* (void*).
void  destroySupernovaMTKView (void* nsView);  // suelta el +1 propio.
void* metalLayerOf (void* nsView);             // CAMetalLayer* (no-owning).
}
