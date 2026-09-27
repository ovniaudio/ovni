// T8 de la 0.2 · la búsqueda de EYEPIECE por bundle id, con LaunchServices. Sólo en la Mac, y SIN JUCE: los
// encabezados de CoreServices y los de JUCE chocan en nombres (Point, Component…), así que esto vive aparte y
// le devuelve al resto una ruta en texto. Ver ui/EyepieceIntro.h.
#if defined(__APPLE__)
#include <CoreServices/CoreServices.h>
#include <limits.h>
#include <string>

namespace telescope
{
std::string findApplicationByBundleId (const char* bundleId)
{
    std::string out;
    CFStringRef id = CFStringCreateWithCString (kCFAllocatorDefault, bundleId, kCFStringEncodingUTF8);
    if (id == nullptr) return out;
    CFArrayRef urls = LSCopyApplicationURLsForBundleIdentifier (id, nullptr);
    CFRelease (id);
    if (urls == nullptr) return out;
    if (CFArrayGetCount (urls) > 0)
    {
        const auto url = (CFURLRef) CFArrayGetValueAtIndex (urls, 0);
        char buf[PATH_MAX];
        if (CFURLGetFileSystemRepresentation (url, true, (UInt8*) buf, sizeof (buf)))
            out = buf;
    }
    CFRelease (urls);
    return out;
}
}
#endif
