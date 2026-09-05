#include "image/MediaThumbs.h"
#include "video/VideoSource.h"

namespace supernova
{
MediaThumbCache::MediaThumbCache()
    : juce::Thread ("supernova-thumbs"), alive (std::make_shared<std::atomic<bool>> (true))
{
    startThread (juce::Thread::Priority::low);
}

MediaThumbCache::~MediaThumbCache()
{
    alive->store (false);
    signalThreadShouldExit();
    notify();
    stopThread (4000);
}

std::shared_ptr<const MediaThumb> MediaThumbCache::peek (const juce::String& path, int rot) const
{
    const juce::ScopedLock sl (lock);
    const auto it = map.find (keyFor (path, rot).toStdString());
    return it != map.end() ? it->second : nullptr;
}

std::shared_ptr<const MediaThumb> MediaThumbCache::get (const juce::String& path, int rot, bool isVideo)
{
    const std::string key = keyFor (path, rot).toStdString();
    {
        const juce::ScopedLock sl (lock);
        const auto it = map.find (key);
        if (it != map.end()) return it->second;
        if (pending.insert (key).second)
            queue.push_back ({ key, path, rot, isVideo });
    }
    notify();
    return nullptr;
}

void MediaThumbCache::clear()
{
    const juce::ScopedLock sl (lock);
    map.clear();
    // lo encolado sigue: cuando llegue se guarda igual (barato) — evita carreras con el worker
}

void MediaThumbCache::run()
{
    while (! threadShouldExit())
    {
        Job job;
        bool have = false;
        {
            const juce::ScopedLock sl (lock);
            if (! queue.empty()) { job = queue.front(); queue.erase (queue.begin()); have = true; }
        }
        if (! have) { wait (200); continue; }

        auto thumb = std::make_shared<MediaThumb> (loadThumb (juce::File (job.path), job.rot, job.isVideo, kThumbSide));
        if (threadShouldExit()) break;
        {
            const juce::ScopedLock sl (lock);
            if ((int) map.size() >= kMaxEntries) map.clear();
            map[job.key] = thumb;
            pending.erase (job.key);
        }
        auto token = alive;
        juce::MessageManager::callAsync ([this, token]
        {
            if (token->load() && onThumbReady) onThumbReady();
        });
    }
}

// ---------------------------------------------------------------------------------------------- puro
juce::Image MediaThumbCache::makeThumbnail (const uint8_t* rgba, int w, int h, int maxSide)
{
    if (rgba == nullptr || w <= 0 || h <= 0 || maxSide <= 0) return {};
    const int side = juce::jmax (w, h);
    const double scale = side > maxSide ? (double) maxSide / (double) side : 1.0;
    const int tw = juce::jmax (1, (int) std::lround (w * scale));
    const int th = juce::jmax (1, (int) std::lround (h * scale));

    juce::Image out (juce::Image::ARGB, tw, th, true);
    juce::Image::BitmapData bd (out, juce::Image::BitmapData::writeOnly);
    for (int ty = 0; ty < th; ++ty)
    {
        const int y0 = (int) ((long long) ty * h / th);
        const int y1 = juce::jmax (y0 + 1, (int) ((long long) (ty + 1) * h / th));
        for (int tx = 0; tx < tw; ++tx)
        {
            const int x0 = (int) ((long long) tx * w / tw);
            const int x1 = juce::jmax (x0 + 1, (int) ((long long) (tx + 1) * w / tw));
            unsigned long r = 0, g = 0, b = 0, n = 0;
            for (int y = y0; y < y1; ++y)
            {
                const uint8_t* row = rgba + ((size_t) y * (size_t) w + (size_t) x0) * 4;
                for (int x = x0; x < x1; ++x, row += 4) { r += row[0]; g += row[1]; b += row[2]; ++n; }
            }
            if (n == 0) n = 1;
            bd.setPixelColour (tx, ty, juce::Colour ((juce::uint8) (r / n), (juce::uint8) (g / n), (juce::uint8) (b / n)));
        }
    }
    return out;
}

MediaThumb MediaThumbCache::loadThumb (const juce::File& file, int rot, bool isVideo, int maxSide)
{
    MediaThumb t;
    rot = ((rot % 4) + 4) % 4;
    if (! file.existsAsFile()) { t.failed = true; return t; }

    LoadedImage li;
    int srcW0 = 0, srcH0 = 0;   // dims de la FUENTE antes de reducir (el aspecto real del archivo)
    int preW  = 0, preH  = 0;   // dims tras reducir, antes de orientar/rotar (para detectar el swap w↔h)
    if (isVideo)
    {
        const juce::Image frame = VideoSource::firstFrameThumbnail (file, maxSide * 2);
        if (frame.isNull()) { t.failed = true; return t; }
        li = ImageLoader::fromImage (frame);     // ya enderezado por el preferredTransform
        srcW0 = preW = li.width; srcH0 = preH = li.height;
    }
    else
    {
        // Mismos bytes que el motor: decode JUCE (CoreImage en mac: PNG/JPEG/GIF/HEIC/WebP/TIFF) + la misma
        // orientación. Reducimos ANTES de extraer el RGBA (una foto de 48 MP no pasa entera por getPixelColour).
        juce::MemoryBlock mb;
        if (! file.loadFileAsData (mb) || mb.getSize() == 0) { t.failed = true; return t; }
        juce::Image img = juce::ImageFileFormat::loadFrom (mb.getData(), mb.getSize());   // PNG / JPEG / GIF
        if (img.isValid())
        {
            srcW0 = img.getWidth(); srcH0 = img.getHeight();
            const int f = ImageLoader::downsampleFactor (img.getWidth(), img.getHeight(), maxSide * 2);
            if (f > 1)
                img = img.rescaled (juce::jmax (1, img.getWidth() / f), juce::jmax (1, img.getHeight() / f),
                                    juce::Graphics::mediumResamplingQuality);
            li = ImageLoader::fromImage (img);
        }
        else
        {
            // HEIC / WebP / TIFF vía ImageIO (mac), YA reducido por el decoder al tamaño de miniatura (review):
            // srcW/srcH quedan reducidos pero con el aspecto exacto — que es lo único que usa el AUTO canvas.
            li = ImageLoader::fromEncodedData (mb.getData(), mb.getSize(), maxSide * 2);
            srcW0 = li.width; srcH0 = li.height;
        }
        preW = li.width; preH = li.height;
        if (li.valid())
            ImageLoader::applyOrientation (li, ImageLoader::orientationOf (mb.getData(), mb.getSize()));
    }
    if (! li.valid()) { t.failed = true; return t; }
    if (rot != 0) ImageLoader::rotate90 (li, rot);
    const bool swapped = (preW != preH) && (li.width == preH && li.height == preW);   // orientación/rotación impar
    t.srcW = swapped ? srcH0 : srcW0;
    t.srcH = swapped ? srcW0 : srcH0;
    t.image = makeThumbnail (li, maxSide);
    return t;
}
}
