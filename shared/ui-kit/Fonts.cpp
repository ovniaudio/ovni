#include "Fonts.h"
#include "OvniUikitData.h"   // generado por juce_add_binary_data(ovni_uikit_assets) — ver CMakeLists.txt

namespace ovni::ui::fonts
{
namespace
{
    juce::Typeface::Ptr loadTf (const char* data, int size)
    {
        if (data == nullptr || size <= 0)
            return nullptr;
        return juce::Typeface::createSystemTypefaceFor (data, (size_t) size);
    }

    // Cacheadas en estáticos: se cargan una vez por proceso.
    const juce::Typeface::Ptr& clash()
    {
        static auto tf = loadTf (OvniUikitData::ClashGroteskSemibold_ttf, OvniUikitData::ClashGroteskSemibold_ttfSize);
        return tf;
    }
    const juce::Typeface::Ptr& general()
    {
        static auto tf = loadTf (OvniUikitData::GeneralSansRegular_ttf, OvniUikitData::GeneralSansRegular_ttfSize);
        return tf;
    }
    const juce::Typeface::Ptr& generalMed()
    {
        static auto tf = loadTf (OvniUikitData::GeneralSansMedium_ttf, OvniUikitData::GeneralSansMedium_ttfSize);
        return tf;
    }
    const juce::Typeface::Ptr& jbMono()
    {
        static auto tf = loadTf (OvniUikitData::JetBrainsMonoRegular_ttf, OvniUikitData::JetBrainsMonoRegular_ttfSize);
        return tf;
    }

    juce::Font make (const juce::Typeface::Ptr& tf, float height, bool boldFallback)
    {
        if (tf != nullptr)
            return juce::Font (juce::FontOptions().withTypeface (tf).withHeight (height));
        // fallback: system sans (no se debería llegar acá con las fuentes embebidas)
        auto opts = juce::FontOptions().withHeight (height);
        return juce::Font (boldFallback ? opts.withStyle ("Bold") : opts);
    }
}

juce::Font display (float h) { return make (clash(),      h, true);  }
juce::Font body    (float h) { return make (general(),    h, false); }
juce::Font label   (float h) { return make (generalMed(), h, false); }
juce::Font mono    (float h) { return make (jbMono(),     h, false); }
}
