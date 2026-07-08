#include "presets/ABState.h"

namespace ovni::presets
{
void ABState::toggle()
{
    (onA ? a : b) = apvts.copyState();              // guardar el slot activo
    onA = ! onA;
    apvts.replaceState ((onA ? a : b).createCopy()); // cargar el otro
}

void ABState::copyActiveToOther()
{
    (onA ? b : a) = apvts.copyState();              // el otro arranca desde el activo
}
}
