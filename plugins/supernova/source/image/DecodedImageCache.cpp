#include "image/DecodedImageCache.h"
#include "image/ImageField.h"
#include "image/VisionField.h"
#include "render/IRenderer.h"   // kParticleGrid
#include <algorithm>

namespace supernova
{
std::vector<float> rotateGrid90 (const std::vector<float>& src, int w, int h, int turns)
{
    turns = ((turns % 4) + 4) % 4;
    if (turns == 0 || w <= 0 || h <= 0 || src.size() != (size_t) w * (size_t) h) return src;

    std::vector<float> cur = src;
    int cw = w, ch = h;
    for (int t = 0; t < turns; ++t)
    {
        std::vector<float> out (cur.size());
        const int ow = ch, oh = cw;                 // 90° CW intercambia los lados
        for (int oy = 0; oy < oh; ++oy)
            for (int ox = 0; ox < ow; ++ox)
                out[(size_t) oy * (size_t) ow + (size_t) ox]
                    = cur[(size_t) (ch - 1 - ox) * (size_t) cw + (size_t) oy];   // mismo mapeo que remapOrientation(6)
        cur = std::move (out);
        cw = ow; ch = oh;
    }
    return cur;
}

LoadedImage rotatedCopy (const LoadedImage& base, int turns)
{
    turns = ((turns % 4) + 4) % 4;
    LoadedImage out = base;                          // copia: la base del caché es inmutable
    if (turns == 0) return out;
    ImageLoader::rotate90 (out, turns);              // el MISMO remap de píxeles del camino de siempre
    out.saliency    = rotateGrid90 (base.saliency,    kParticleGrid, kParticleGrid, turns);
    out.subjectMask = rotateGrid90 (base.subjectMask, kParticleGrid, kParticleGrid, turns);
    return out;
}

LoadedImage decodeBaseImage (const juce::File& file)
{
    auto img = ImageLoader::fromFile (file);          // ya endereza por EXIF
    if (! img.valid()) return img;
    img.saliency = visionSaliency (img.rgba.data(), img.width, img.height, kParticleGrid, kParticleGrid);
    // Máscara del sujeto para el CUTOUT — cascada: (1) FONDO PLANO por color (logos/gráficos: la IA
    // fotográfica los confunde; el chroma-key los recorta perfecto) → (2) Vision (Quitar-fondo 14+ /
    // personas 12+) → (3) saliencia (universal).
    img.subjectMask = ImageField::maskFromFlatBackground (img.rgba.data(), img.width, img.height,
                                                          kParticleGrid, kParticleGrid);
    if (img.subjectMask.empty())
        img.subjectMask = visionSubjectMask (img.rgba.data(), img.width, img.height, kParticleGrid, kParticleGrid);
    if (img.subjectMask.empty() && ! img.saliency.empty())
        img.subjectMask = ImageField::maskFromSaliency (img.saliency, kParticleGrid, kParticleGrid);
    return img;
}

namespace
{
size_t bytesOf (const LoadedImage& im) noexcept
{
    return im.rgba.size() + (im.saliency.size() + im.subjectMask.size()) * sizeof (float);
}
}

DecodedImageCache::Entry* DecodedImageCache::find (const juce::String& path) noexcept
{
    for (auto& e : entries) if (e.path == path) return &e;
    return nullptr;
}

bool DecodedImageCache::contains (const juce::String& path) const noexcept
{
    for (const auto& e : entries) if (e.path == path) return true;
    return false;
}

std::shared_ptr<const LoadedImage> DecodedImageCache::get (const juce::File& file)
{
    auto* e = find (file.getFullPathName());
    if (e == nullptr) { ++missCount; return nullptr; }
    // El archivo cambió abajo nuestro (el usuario lo editó/reemplazó): la entrada ya no lo representa.
    if (file.getLastModificationTime().toMilliseconds() != e->modMs)
    {
        totalBytes -= e->bytes;
        entries.erase (entries.begin() + (e - entries.data()));
        ++missCount;
        return nullptr;
    }
    e->lastUsed = ++stamp;
    ++hitCount;
    return e->image;
}

void DecodedImageCache::put (const juce::File& file, std::shared_ptr<const LoadedImage> base)
{
    if (base == nullptr || ! base->valid()) return;
    invalidate (file.getFullPathName());
    Entry e;
    e.path     = file.getFullPathName();
    e.modMs    = file.getLastModificationTime().toMilliseconds();
    e.bytes    = bytesOf (*base);
    e.lastUsed = ++stamp;
    e.image    = std::move (base);
    totalBytes += e.bytes;
    entries.push_back (std::move (e));
    evictToBudget();
}

void DecodedImageCache::invalidate (const juce::String& path)
{
    for (size_t i = 0; i < entries.size(); ++i)
        if (entries[i].path == path)
        {
            totalBytes -= entries[i].bytes;
            entries.erase (entries.begin() + (long) i);
            return;
        }
}

void DecodedImageCache::clear() { entries.clear(); totalBytes = 0; }

// LRU: mientras se pase del presupuesto, se va la MENOS usada. La recién puesta es la más reciente, así que
// nunca se expulsa a sí misma (con una sola entrada, aunque sea más grande que el presupuesto, se queda:
// tirarla dejaría el caché inútil justo para la foto que se está mirando).
void DecodedImageCache::evictToBudget()
{
    while (totalBytes > budgetBytes && entries.size() > 1)
    {
        auto oldest = std::min_element (entries.begin(), entries.end(),
                                        [] (const Entry& a, const Entry& b) { return a.lastUsed < b.lastUsed; });
        totalBytes -= oldest->bytes;
        entries.erase (oldest);
    }
}

LoadedImage exportSlotImage (const juce::File& file, int turns)
{
    return rotatedCopy (decodeBaseImage (file), turns);
}

LoadedImage exportSlotImage (DecodedImageCache& cache, const juce::File& file, int turns)
{
    auto base = cache.get (file);
    if (base == nullptr)
    {
        base = std::make_shared<const LoadedImage> (decodeBaseImage (file));
        // `put` NO guarda un decode fallido (contrato del caché del editor: un archivo que falta puede
        // aparecer después, con un relink). Un slot roto vuelve a intentarse en cada vuelta, pero eso
        // cuesta un `fromFile` que falla al abrir — nunca Vision, que es lo caro.
        cache.put (file, base);
    }
    return rotatedCopy (*base, turns);
}
}
