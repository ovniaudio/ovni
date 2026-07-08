#include "dsp/SofaLoader.h"

extern "C" {
#include <mysofa.h>
}

namespace ovni::dsp {

SofaLoader::~SofaLoader() { close(); }

SofaLoader::SofaLoader (SofaLoader&& o) noexcept
    : handle (o.handle), filterLen (o.filterLen)
{
    o.handle    = nullptr;
    o.filterLen = 0;
}

SofaLoader& SofaLoader::operator= (SofaLoader&& o) noexcept
{
    if (this != &o)
    {
        close();
        handle      = o.handle;
        filterLen   = o.filterLen;
        o.handle    = nullptr;
        o.filterLen = 0;
    }
    return *this;
}

bool SofaLoader::open (const juce::File& sofaFile, double sampleRate)
{
    close();
    if (! sofaFile.existsAsFile() || sampleRate <= 0.0)
        return false;

    int err = 0, len = 0;
    MYSOFA_EASY* sofa = mysofa_open (sofaFile.getFullPathName().toRawUTF8(),
                                     (float) sampleRate, &len, &err);
    if (sofa == nullptr || err != MYSOFA_OK || len <= 0)
    {
        if (sofa != nullptr)
            mysofa_close (sofa);
        return false;
    }

    handle    = sofa;
    filterLen = len;
    return true;
}

bool SofaLoader::getFilter (float x, float y, float z,
                            float* irL, float* irR, float& delayL, float& delayR) const noexcept
{
    if (handle == nullptr || irL == nullptr || irR == nullptr)
        return false;

    mysofa_getfilter_float (static_cast<MYSOFA_EASY*> (handle), x, y, z, irL, irR, &delayL, &delayR);
    return true;
}

void SofaLoader::close() noexcept
{
    if (handle != nullptr)
    {
        mysofa_close (static_cast<MYSOFA_EASY*> (handle));
        handle = nullptr;
    }
    filterLen = 0;
}

} // namespace ovni::dsp
