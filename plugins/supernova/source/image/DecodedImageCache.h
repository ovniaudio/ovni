#pragma once
#include <juce_core/juce_core.h>
#include <memory>
#include <vector>
#include "image/ImageLoader.h"   // LoadedImage

// DecodedImageCache — el decode BASE de una foto (SIN la rotación manual del usuario) guardado por path,
// para que girar con el ⟳ sea instantáneo.
//
// El bug de campo: `showItem` volvía a llamar `decodeImageAsync (file, turns)` por cada cuarto de vuelta, y
// ese camino lee el archivo del disco, lo decodifica entero y corre DOS pasadas de Vision (saliencia +
// máscara del sujeto). Girar 90° no cambia ni los bytes del archivo ni el contenido de la imagen: lo único
// que cambia es una permutación de píxeles. Con una foto de 12 MP eso eran cientos de ms (segundos con la
// máquina ocupada) por cada toque del botón — "si doy vuelta la imagen tarda en ponerse en el preview".
//
// El decode ahora es SIEMPRE en rotación 0 (`decodeBaseImage`) y cualquier rotación se deriva con
// `rotatedCopy`. Además de ahorrar el trabajo, eso hace que las dos maneras de llegar a la misma foto
// girada — reabrir un proyecto guardado en esa rotación, o girarla a mano desde 0 — den EXACTAMENTE la
// misma imagen: Vision no es equivariante a la rotación (medido: las 262.144 celdas de la saliencia
// cambian, |Δ| hasta 0,42, si se la corre sobre la imagen ya girada) y además está entrenada con imágenes
// derechas, así que analizar SIEMPRE la foto enderezada por EXIF es la respuesta correcta, no sólo la rápida.
//
// NO es thread-safe, y no hace falta que lo sea: cada instancia vive en UN hilo. Hay dos, y nunca
// comparten objeto — la del editor, que sólo toca el MESSAGE THREAD (el hilo de decode produce la base y
// la entrega por callAsync), y una LOCAL al hilo del export (`PluginEditor.cpp`, desde 0.4.0) → sin locks. Acotado por bytes con expulsión LRU: una sesión de fotos de 12 MP no puede
// quedarse con toda la RAM.
namespace supernova
{
// Gira una grilla `w`×`h` de floats `turns` cuartos de vuelta CW — la misma rotación que
// ImageLoader::rotate90 aplica al RGBA. `turns` se toma mod 4. Devuelve una COPIA (la fuente no se toca).
std::vector<float> rotateGrid90 (const std::vector<float>& src, int w, int h, int turns);

// Copia de `base` girada `turns` cuartos CW: RGBA + saliencia + máscara del sujeto (las dos grillas son
// kParticleGrid², cuadradas, y giran igual que los píxeles). Una grilla vacía sigue vacía.
LoadedImage rotatedCopy (const LoadedImage& base, int turns);

// El decode BASE: RGBA enderezado por EXIF + saliencia (Vision) + máscara del sujeto (fondo plano → Vision
// → saliencia). BLOQUEA cientos de ms y corre Vision: va en un hilo de fondo, JAMÁS en el message thread.
LoadedImage decodeBaseImage (const juce::File& file);

// La foto de UN SLOT del EXPORT, decodificada COMO EN VIVO. El export corre en su propio hilo y no ve el
// caché del editor (que vive en el message thread). Desde 0.4.0 producción NO usa este overload: el export
// pasa por el de abajo, con su propio caché local al hilo, y éste queda para los tests y para un decode
// suelto. Decodifica por el MISMO camino:
// `decodeBaseImage` (RGBA enderezado + saliencia Vision + máscara del sujeto) y la rotación DERIVADA con
// `rotatedCopy`. Antes usaba `ImageLoader::fromFile` pelado: sin saliencia los pesos salían de la luma y sin
// máscara el depth caía al "modo foto" (relieve por luma) en vez de la almohada del sujeto → con DEPTH alto
// el MP4 tenía otro volumen que la pantalla. BLOQUEA (Vision): jamás en el message thread.
LoadedImage exportSlotImage (const juce::File& file, int turns);


class DecodedImageCache
{
public:
    static constexpr size_t kDefaultBudgetBytes = 256u * 1024u * 1024u;   // 256 MB ≈ 5 fotos de 12 MP

    explicit DecodedImageCache (size_t budget = kDefaultBudgetBytes) : budgetBytes (budget) {}

    // La base de `file` si está cacheada Y el archivo no cambió en disco (path + fecha de modificación);
    // nullptr si no. Una entrada cuyo archivo cambió se descarta acá mismo (no se puede confiar en ella).
    std::shared_ptr<const LoadedImage> get (const juce::File& file);
    void put (const juce::File& file, std::shared_ptr<const LoadedImage> base);
    void invalidate (const juce::String& path);   // relink / sacar de la sesión: ese path deja de valer
    void clear();                                 // vaciar la sesión / cargar otra

    // ¿Este path tiene una base cacheada? Consulta pura: no cuenta hit/miss ni mueve el LRU.
    bool   contains (const juce::String& path) const noexcept;

    size_t bytes()  const noexcept { return totalBytes; }
    size_t count()  const noexcept { return entries.size(); }
    size_t budget() const noexcept { return budgetBytes; }
    int    hits()   const noexcept { return hitCount; }     // diagnóstico (tests y QA)
    int    misses() const noexcept { return missCount; }

private:
    struct Entry
    {
        juce::String path;
        juce::int64  modMs = 0;      // fecha de modificación del archivo al cachear
        size_t       bytes = 0;
        uint64_t     lastUsed = 0;   // sello monótono para el LRU
        std::shared_ptr<const LoadedImage> image;
    };
    Entry* find (const juce::String& path) noexcept;
    void   evictToBudget();

    std::vector<Entry> entries;
    size_t   budgetBytes;
    size_t   totalBytes = 0;
    uint64_t stamp = 0;
    int      hitCount = 0, missCount = 0;
};

// La MISMA foto, reusando un caché LOCAL AL HILO del export. Sin esto, una secuencia que cicla A→B→A→B…
// re-decodifica (y re-corre Vision sobre) la misma foto en CADA vuelta: con dos fotos y un gap corto, un
// clip de 30 s son ~120 decodes en vez de 2 — con la cascada de Core ML, decenas de segundos con la UI
// aparentemente colgada (MEDIUM-1 del revisor del prompt 45). Se cachea la BASE (sin rotar), como en el
// editor, y la rotación se deriva encima: girar no vuelve a leer el disco ni a correr Vision. El caché es
// del hilo del export y muere con él; su presupuesto es el mismo del editor, así que una secuencia enorme
// degrada al comportamiento de antes en vez de comerse la RAM.
LoadedImage exportSlotImage (DecodedImageCache& cache, const juce::File& file, int turns);
}
