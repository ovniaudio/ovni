#include "MeasureCore.h"
#include "TelescopeMeasureVersion.h"   // generado en cada build (EngineVersion.cmake)

namespace telescope::measure
{
EngineInfo builtEngine()
{
    return { TELESCOPE_MEASURE_VERSION, TELESCOPE_MEASURE_SHA };
}
}
