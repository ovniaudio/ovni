#include "AutoreleasePool.h"

// @autoreleasepool funciona con o sin ARC (es un constructo del compilador). Este .mm no maneja ownership,
// así que no necesita -fobjc-arc; alcanza con que el glob lo compile como Obj-C++ por la extensión.
namespace supernova
{
void withAutoreleasePool (const std::function<void()>& body)
{
    @autoreleasepool { if (body) body(); }
}
}
