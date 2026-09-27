#pragma once
// La SONDA de texto de [contraste] (F2 de la 0.2), compartida con el test del reacomodo de VERDICT.
// Envuelve el contexto de dibujo, ve pasar cada tanda de glifos (dónde cae, de qué color, de qué alto, qué
// dice) y puede pintar todo, nada, o una sola tanda. Ver el encabezado de ContrastTest.cpp.
#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <vector>
#include <juce_gui_basics/juce_gui_basics.h>

namespace telescope::test::probe
{
// ==== WCAG ================================================================================================
inline double channelLin (juce::uint8 c)
{
    const double v = c / 255.0;
    return v <= 0.04045 ? v / 12.92 : std::pow ((v + 0.055) / 1.055, 2.4);
}

inline double luminance (juce::Colour c)
{
    return 0.2126 * channelLin (c.getRed()) + 0.7152 * channelLin (c.getGreen()) + 0.0722 * channelLin (c.getBlue());
}

inline double contrast (juce::Colour a, juce::Colour b)
{
    const double la = luminance (a), lb = luminance (b);
    return (std::max (la, lb) + 0.05) / (std::min (la, lb) + 0.05);
}

// ==== LA SONDA ============================================================================================
struct GlyphRun
{
    int                    index = 0;
    juce::Colour           colour;          // el color declarado, con la opacidad del contexto
    bool                   gradient = false;
    float                  deviceHeight = 0.0f;   // alto de la fuente en píxeles de la imagen
    juce::Rectangle<float> box;             // caja en píxeles de la imagen
    juce::String           text;
};

// Glifo → caracter, por tipografía: la tanda llega como números de glifo y el reporte quiere el texto.
class GlyphNames
{
public:
    juce::String decode (const juce::Font& f, juce::Span<const uint16_t> glyphs)
    {
        auto& m = mapFor (f);
        juce::String s;
        for (const auto gl : glyphs)
        {
            const auto it = m.find (gl);
            s += it != m.end() ? it->second : juce::juce_wchar ('?');
        }
        return s;
    }

private:
    std::map<uint16_t, juce::juce_wchar>& mapFor (const juce::Font& f)
    {
        const auto key = f.getTypefaceName() + "/" + f.getTypefaceStyle();
        auto& m = maps[key];
        if (m.empty())
        {
            juce::String chars;
            for (juce::juce_wchar c = 32; c < 127; ++c) chars += c;
            chars += juce::String::fromUTF8 ("✓✔○●◆◇▲▼■□±·…–−°×→←↑↓µ√ÁÉÍÓÚáéíóúñÑüÜäöÄÖßàèìòùçÇâêôœ‹›«»"
                                              "ãõÃÕîïûëÀÈÂÊÎÔÛËÏŒ");   // F2b: las tildes de VERDICT (pt, fr)
            for (int i = 0; i < chars.length(); ++i)
            {
                juce::GlyphArrangement ga;
                ga.addLineOfText (f, juce::String::charToString (chars[i]), 0.0f, 0.0f);
                if (ga.getNumGlyphs() == 1)
                {
                    const auto gl = (uint16_t) ga.getGlyph (0).getGlyphIndex();
                    if (m.find (gl) == m.end()) m[gl] = chars[i];
                }
            }
        }
        return m;
    }

    std::map<juce::String, std::map<uint16_t, juce::juce_wchar>> maps;
};

class ProbeContext : public juce::LowLevelGraphicsContext
{
public:
    enum class Mode { all, none, only };

    ProbeContext (juce::LowLevelGraphicsContext& inner, Mode m, int onlyIndex, std::vector<GlyphRun>* sink,
                  GlyphNames* names)
        : in (inner), mode (m), target (onlyIndex), runs (sink), glyphNames (names) {}

    bool isVectorDevice() const override                        { return in.isVectorDevice(); }
    void setOrigin (juce::Point<int> o) override                { st.t = juce::AffineTransform::translation ((float) o.x, (float) o.y).followedBy (st.t); in.setOrigin (o); }
    void addTransform (const juce::AffineTransform& t) override { st.t = t.followedBy (st.t); in.addTransform (t); }
    float getPhysicalPixelScaleFactor() const override          { return in.getPhysicalPixelScaleFactor(); }
    bool clipToRectangle (const juce::Rectangle<int>& r) override              { return in.clipToRectangle (r); }
    bool clipToRectangleList (const juce::RectangleList<int>& r) override      { return in.clipToRectangleList (r); }
    void excludeClipRectangle (const juce::Rectangle<int>& r) override         { in.excludeClipRectangle (r); }
    void clipToPath (const juce::Path& p, const juce::AffineTransform& t) override      { in.clipToPath (p, t); }
    void clipToImageAlpha (const juce::Image& i, const juce::AffineTransform& t) override { in.clipToImageAlpha (i, t); }
    bool clipRegionIntersects (const juce::Rectangle<int>& r) override        { return in.clipRegionIntersects (r); }
    juce::Rectangle<int> getClipBounds() const override                        { return in.getClipBounds(); }
    bool isClipEmpty() const override                                          { return in.isClipEmpty(); }
    void saveState() override    { stack.push_back (st); in.saveState(); }
    void restoreState() override { if (! stack.empty()) { st = stack.back(); stack.pop_back(); } in.restoreState(); }
    void beginTransparencyLayer (float o) override { stack.push_back (st); st.opacity *= o; in.beginTransparencyLayer (o); }
    void endTransparencyLayer() override           { if (! stack.empty()) { st = stack.back(); stack.pop_back(); } in.endTransparencyLayer(); }
    void setFill (const juce::FillType& f) override { st.fill = f; in.setFill (f); }
    void setOpacity (float o) override              { st.opacity = o; in.setOpacity (o); }
    void setInterpolationQuality (juce::Graphics::ResamplingQuality q) override { in.setInterpolationQuality (q); }
    void fillAll() override                                            { in.fillAll(); }
    void fillRect (const juce::Rectangle<int>& r, bool rep) override   { in.fillRect (r, rep); }
    void fillRect (const juce::Rectangle<float>& r) override           { in.fillRect (r); }
    void fillRectList (const juce::RectangleList<float>& l) override   { in.fillRectList (l); }
    void fillPath (const juce::Path& p, const juce::AffineTransform& t) override { in.fillPath (p, t); }
    void drawRect (const juce::Rectangle<float>& r, float w) override  { in.drawRect (r, w); }
    void strokePath (const juce::Path& p, const juce::PathStrokeType& s, const juce::AffineTransform& t) override { in.strokePath (p, s, t); }
    void drawImage (const juce::Image& i, const juce::AffineTransform& t) override { in.drawImage (i, t); }
    void drawLine (const juce::Line<float>& l) override                { in.drawLine (l); }
    void drawLineWithThickness (const juce::Line<float>& l, float w) override { in.drawLineWithThickness (l, w); }
    void setFont (const juce::Font& f) override                        { st.font = f; in.setFont (f); }
    const juce::Font& getFont() override                               { return in.getFont(); }
    std::unique_ptr<juce::ImageType> getPreferredImageTypeForTemporaryImages() const override
    {
        return in.getPreferredImageTypeForTemporaryImages();
    }
    void drawRoundedRectangle (const juce::Rectangle<float>& r, float c, float w) override { in.drawRoundedRectangle (r, c, w); }
    void fillRoundedRectangle (const juce::Rectangle<float>& r, float c) override          { in.fillRoundedRectangle (r, c); }
    void drawEllipse (const juce::Rectangle<float>& r, float w) override                   { in.drawEllipse (r, w); }
    void fillEllipse (const juce::Rectangle<float>& r) override                            { in.fillEllipse (r); }
    uint64_t getFrameId() const override { return in.getFrameId(); }

    void drawGlyphs (juce::Span<const uint16_t> glyphs, juce::Span<const juce::Point<float>> pos,
                     const juce::AffineTransform& t) override
    {
        const int idx = counter++;
        if (runs != nullptr && ! glyphs.empty())
            runs->push_back (describe (idx, glyphs, pos, t));

        if (mode == Mode::all || (mode == Mode::only && idx == target))
            in.drawGlyphs (glyphs, pos, t);
    }

    int glyphRunCount() const noexcept { return counter; }

private:
    struct State
    {
        juce::AffineTransform t;
        juce::FillType        fill;
        float                 opacity = 1.0f;
        juce::Font            font { juce::FontOptions {} };
    };

    GlyphRun describe (int idx, juce::Span<const uint16_t> glyphs, juce::Span<const juce::Point<float>> pos,
                       const juce::AffineTransform& t) const
    {
        GlyphRun r;
        r.index    = idx;
        r.gradient = st.fill.isGradient();
        r.colour   = (st.fill.isColour() ? st.fill.colour : (r.gradient ? st.fill.gradient->getColour (0)
                                                                        : juce::Colours::transparentBlack))
                         .withMultipliedAlpha (st.opacity * st.fill.getOpacity());

        const auto full = t.followedBy (st.t);
        const float h = st.font.getHeight(), asc = st.font.getAscent(), desc = st.font.getDescent();
        juce::Rectangle<float> box;
        for (size_t i = 0; i < pos.size(); ++i)
        {
            const float x0  = pos[i].x;
            const float adv = i + 1 < pos.size() ? juce::jmax (0.0f, pos[i + 1].x - x0) : h * 0.62f;
            juce::Rectangle<float> g (x0, pos[i].y - asc, juce::jmax (adv, h * 0.2f), asc + desc);
            const auto gb = g.transformedBy (full);
            box = box.isEmpty() ? gb : box.getUnion (gb);
        }
        r.box = box;
        r.deviceHeight = h * std::sqrt (std::abs (full.getDeterminant()));
        if (glyphNames != nullptr) r.text = glyphNames->decode (st.font, glyphs);
        return r;
    }

    juce::LowLevelGraphicsContext& in;
    Mode                           mode;
    int                            target;
    std::vector<GlyphRun>*         runs;
    GlyphNames*                    glyphNames;
    State                          st;
    std::vector<State>             stack;
    int                            counter = 0;
};

inline juce::Image paintEditor (juce::Component& ed, ProbeContext::Mode mode, int only, std::vector<GlyphRun>* runs,
                                GlyphNames* names, int* count = nullptr, float kScale = 2.0f)
{
    juce::Image img (juce::Image::ARGB, juce::roundToInt ((float) ed.getWidth() * kScale),
                     juce::roundToInt ((float) ed.getHeight() * kScale), true);
    {
        auto ctx = img.createLowLevelContext();
        ProbeContext probe (*ctx, mode, only, runs, names);
        {
            juce::Graphics g (probe);
            g.addTransform (juce::AffineTransform::scale (kScale));
            ed.paintEntireComponent (g, false);
        }
        if (count != nullptr) *count = probe.glyphRunCount();
    }
    return img;
}

struct Measured
{
    double measured = 0.0, nominal = 0.0;
    int    pixels = 0;
    int    inkHeight = 0;   // alto de lo que se VE del rótulo, en píxeles de la imagen
    juce::Colour background, textPixel;
};

// El rótulo i: los píxeles donde C_i difiere de B. Del cuarto más cubierto (más diferencia de luminancia),
// el percentil 10 del contraste C_i contra B en el mismo píxel. Ver el encabezado.
inline Measured measureRun (const juce::Image& b, const juce::Image& c, const GlyphRun& run)
{
    const juce::Image::BitmapData bb (b, juce::Image::BitmapData::readOnly), cc (c, juce::Image::BitmapData::readOnly);
    const auto area = run.box.expanded (3.0f).getSmallestIntegerContainer()
                          .getIntersection ({ 0, 0, b.getWidth(), b.getHeight() });

    struct Px { double dl; double ratio; double nominal; juce::Colour bg, fg; };
    std::vector<Px> px;
    int yMin = 1 << 30, yMax = -1;
    for (int y = area.getY(); y < area.getBottom(); ++y)
        for (int x = area.getX(); x < area.getRight(); ++x)
        {
            const auto pb = bb.getPixelColour (x, y), pc = cc.getPixelColour (x, y);
            if (pb == pc) continue;
            yMin = std::min (yMin, y);
            yMax = std::max (yMax, y);
            const auto bg = juce::Colours::black.overlaidWith (pb);
            const auto fg = juce::Colours::black.overlaidWith (pc);
            px.push_back ({ std::abs (luminance (fg) - luminance (bg)), contrast (fg, bg),
                            contrast (bg.overlaidWith (run.colour), bg), bg, fg });
        }

    Measured m;
    m.pixels = (int) px.size();
    if (px.empty()) return m;
    m.inkHeight = yMax - yMin + 1;

    std::sort (px.begin(), px.end(), [] (const Px& a, const Px& z) { return a.dl > z.dl; });
    const size_t core = std::max<size_t> (1, px.size() / 4);
    std::vector<double> ratios, noms;
    for (size_t i = 0; i < core; ++i) ratios.push_back (px[i].ratio);
    for (const auto& p : px) noms.push_back (p.nominal);
    std::sort (ratios.begin(), ratios.end());
    std::sort (noms.begin(), noms.end());
    m.measured   = ratios[(size_t) ((double) (ratios.size() - 1) * 0.10)];
    m.nominal    = noms[(size_t) ((double) (noms.size() - 1) * 0.10)];
    m.background = px[core / 2].bg;
    m.textPixel  = px[0].fg;
    return m;
}
}
