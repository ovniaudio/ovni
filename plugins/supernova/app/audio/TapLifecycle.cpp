// El puente de TapLifecycle a libdispatch, y el ÚNICO lugar del ciclo de vida del tap donde se nombra un
// tipo de Objective-C. Va aparte a propósito: TapLifecycle.h lo incluyen TUs con ARC (los .mm de la app)
// y sin ARC (los .cpp de los tests), y un `dispatch_queue_t` como miembro de una struct del header le da
// destructor implícito en unas y no en otras — la misma struct con dos definiciones, o sea ODR.
//
// Este archivo es .cpp (no .mm) y no lleva -fobjc-arc: sin __OBJC__ los objetos de dispatch son structs
// de C puro, así que `dispatch_release` existe, es lo correcto, y la cola se libera UNA vez.
#include "TapLifecycle.h"

#include <dispatch/dispatch.h>

namespace supernova::tapqueue {

void* createSerial (const char* label) noexcept
{
    return (void*) dispatch_queue_create (label, DISPATCH_QUEUE_SERIAL);
}

void destroy (void* queue) noexcept
{
    if (queue != nullptr) dispatch_release ((dispatch_queue_t) queue);
}

void async (void* queue, void* context, void (*fn) (void*)) noexcept
{
    dispatch_async_f ((dispatch_queue_t) queue, context, fn);
}

} // namespace supernova::tapqueue
