#include "template/PluginEditorBase.h"

namespace ovni
{
//==================================================================================================
juce::PropertiesFile& PluginEditorBase::uiSettings()
{
    static juce::PropertiesFile file (
        []
        {
            juce::PropertiesFile::Options o;
            o.applicationName     = "OVNI";
            o.filenameSuffix      = "settings";       // -> ~/Library/Application Support/OVNI/OVNI.settings
            o.folderName          = "OVNI";
            o.osxLibrarySubFolder = "Application Support";
            return o;
        }());
    return file;
}

PluginEditorBase::PluginEditorBase (PluginProcessorBase& p, juce::String desig)
    : juce::AudioProcessorEditor (&p), processor (p), designation (std::move (desig))
{
    addAndMakeVisible (content);
    processor.presets().addChangeListener (this);   // refrescar el nombre cuando cambia el preset
    // NOTA: el plugin concreto llama setBaseSize(...) en SU constructor (corre después de éste).
}

PluginEditorBase::~PluginEditorBase()
{
    processor.presets().removeChangeListener (this);
}

//==================================================================================================
void PluginEditorBase::setBaseSize (int w, int h)
{
    baseW = w; baseH = h;
    content.setSize (w, h);                                  // coords base (el transform lo escala)
    const int z = uiSettings().getIntValue ("uiZoom", (int) Zoom::medium);
    zoom = (z >= (int) Zoom::small && z <= (int) Zoom::large) ? (Zoom) z : Zoom::medium;
    applyZoom (zoom);
}

void PluginEditorBase::addToCanvas (juce::Component& c)
{
    content.addAndMakeVisible (c);
}

void PluginEditorBase::applyZoom (Zoom z)
{
    zoom = z;
    const float f = zoomFactor (z);
    content.setTransform (juce::AffineTransform::scale (f));
    setSize (juce::roundToInt (baseW * f), juce::roundToInt (baseH * f));   // dispara resized() del editor
    repaint();
}

void PluginEditorBase::setZoom (Zoom z)
{
    applyZoom (z);
    uiSettings().setValue ("uiZoom", (int) z);
    uiSettings().saveIfNeeded();
}

void PluginEditorBase::resized()
{
    // El canvas vive en coords base; su transform de escala lo lleva al tamaño físico (= getWidth/Height).
    content.setBounds (0, 0, baseW, baseH);
}

void PluginEditorBase::paint (juce::Graphics& g)
{
    g.fillAll (ui::theme::bg0);   // respaldo bajo el canvas (cubre cualquier borde por redondeo)
}

//==================================================================================================
void PluginEditorBase::layoutCanvas()
{
    auto r = juce::Rectangle<int> (0, 0, baseW, baseH);
    headerArea = r.removeFromTop (headerHeight);

    // Zonas clickeables del header (mockup pulsar-a): power a la IZQUIERDA junto a la marca;
    // a la derecha, de afuera hacia adentro: [S·M·L] [A/B] [SAVE] [‹ nombre ›].
    auto hh = headerArea.reduced (12, 0);
    bypassZone = hh.removeFromLeft (30).withSizeKeepingCentre (30, 30);

    auto seg = hh.removeFromRight (69).withSizeKeepingCentre (69, 24);
    const int cw = seg.getWidth() / 3;
    zoomSZone = seg.removeFromLeft (cw);
    zoomMZone = seg.removeFromLeft (cw);
    zoomLZone = seg;
    hh.removeFromRight (13);
    presetAbZone = hh.removeFromRight (54).withSizeKeepingCentre (54, 24);
    hh.removeFromRight (13);
    presetSaveZone = hh.removeFromRight (46).withSizeKeepingCentre (46, 24);
    hh.removeFromRight (13);
    auto browser   = hh.removeFromRight (168).withSizeKeepingCentre (168, 28);
    presetPrevZone = browser.removeFromLeft (26);
    presetNextZone = browser.removeFromRight (26);
    presetNameZone = browser;

    // Bottom-bar contextual (si el plugin la habilitó): reserva su tira al PIE, antes del body.
    if (bottomBarOn)
        bar.setBounds (r.removeFromBottom (ui::theme::barH));

    layoutBody (r);   // PLUGIN: el resto de la ventana (coords base, ya sin la bottom-bar)
}

//==================================================================================================
void PluginEditorBase::paintCanvas (juce::Graphics& g)
{
    // Fondo atmósfera — horneado a resolución FÍSICA (nítido en Retina); el Panel cachea internamente.
    // El Panel respeta el COLOR de familia (mockup #plugin::before): cian/magenta/verde saturado.
    g.fillAll (ui::theme::bg0);
    panel.setHue (familyHue);
    const float scale = (float) g.getInternalContext().getPhysicalPixelScaleFactor();
    panel.render (baseW, baseH, scale);   // nitidez: el scale acumula host DPI × zoom
    panel.paint (g);

    paintHeader (g);
    paintBody (g);    // PLUGIN
}

void PluginEditorBase::paintHeader (juce::Graphics& g)
{
    using namespace ovni::ui;

    // ===== superficie del header (mockup): sheen superior + hairline inferior =====
    {
        auto hr = headerArea.toFloat();
        juce::ColourGradient sheen (juce::Colour (0x0ba0c0e0), 0.0f, hr.getY(),
                                    juce::Colour (0x00a0c0e0), 0.0f, hr.getY() + hr.getHeight() * 0.8f, false);
        g.setGradientFill (sheen);
        g.fillRect (hr);
        g.setColour (juce::Colour (0x0fbee1ff));
        g.fillRect (hr.getX(), hr.getY(), hr.getWidth(), 1.0f);          // inner-top sheen
        g.setColour (theme::lineSoft);
        g.fillRect (hr.getX(), hr.getBottom() - 1.0f, hr.getWidth(), 1.0f);   // hairline inferior
    }

    // ===== power (izquierda; hue de familia = activo / rojo = bypasseado) =====
    const bool bypassed = processor.apvts.getRawParameterValue (processor.bypassParamID())->load() >= 0.5f;
    const auto bcol = bypassed ? theme::red : familyHue;
    {
        auto byp = bypassZone.toFloat();
        g.setColour (theme::lineSoft);
        g.drawRoundedRectangle (byp, 3.0f, 1.0f);
        if (! bypassed)
        {
            g.setColour (familyHue.withAlpha (0.35f));
            g.drawRoundedRectangle (byp, 3.0f, 1.0f);                    // borde encendido
            g.setColour (familyHue.withAlpha (0.10f));
            g.fillRoundedRectangle (byp.expanded (1.5f), 4.0f);          // glow tenue
        }
        else
        {
            g.setColour (bcol.withAlpha (0.14f));
            g.fillRoundedRectangle (byp, 3.0f);
        }
        // glyph power (sin recuadro extra: el recuadro ya está dibujado acá)
        const auto  c  = byp.getCentre();
        const float rr = 5.5f;
        juce::Path pw;
        pw.addCentredArc (c.x, c.y + 1.0f, rr, rr, 0.0f,
                          juce::degreesToRadians (40.0f), juce::degreesToRadians (320.0f), true);
        g.setColour (bcol);
        g.strokePath (pw, juce::PathStrokeType (1.6f));
        g.drawLine (c.x, c.y - rr + 1.0f, c.x, c.y + 1.0f, 1.6f);
    }

    // ===== marca: OVNI · NOMBRE (cian, glow) · designación (badge mono) =====
    auto h = headerArea.reduced (12, 0).withTrimmedLeft (bypassZone.getRight() - 12 + 4);
    g.setColour (theme::lineSoft);
    g.fillRect (h.getX() + 8, h.getCentreY() - 10, 1, 20);               // hsep
    int tx = h.getX() + 21;

    g.setColour (theme::txt);
    g.setFont (fonts::display (17.0f).withExtraKerningFactor (0.30f));
    g.drawText ("OVNI", tx, h.getY(), 76, h.getHeight(), juce::Justification::centredLeft);
    tx += 86;

    const juce::String pluginName = processor.getName().toUpperCase();
    {
        const auto f = fonts::display (14.0f).withExtraKerningFactor (0.20f);
        g.setFont (f);
        const int nw = 26 + 13 * pluginName.length();                    // ancho generoso (kerning amplio)
        // glow del nombre (mockup: text-shadow del hue de familia) — 4 pasadas tenues + sólida encima
        const juce::Point<int> offs[] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
        g.setColour (familyHue.withAlpha (0.20f));
        for (const auto& off : offs)
            g.drawText (pluginName, tx + off.x, h.getY() + off.y, nw, h.getHeight(), juce::Justification::centredLeft);
        g.setColour (familyHue);
        g.drawText (pluginName, tx, h.getY(), nw, h.getHeight(), juce::Justification::centredLeft);
        tx += 13 + 11 * pluginName.length();
    }

    {
        g.setFont (fonts::mono (9.0f).withExtraKerningFactor (0.12f));
        const int bw = 18 + 7 * designation.length();
        auto badge = juce::Rectangle<int> (tx, h.getCentreY() - 9, bw, 17);
        g.setColour (theme::lineSoft);
        g.drawRoundedRectangle (badge.toFloat(), 2.0f, 1.0f);
        g.setColour (theme::fnt);
        g.drawText (designation, badge, juce::Justification::centred);
    }

    // ===== cluster derecho: ‹ nombre › · SAVE · A/B · S·M·L =====

    // ‹ nombre › (sin caja: cluster limpio como el mockup)
    {
        g.setColour (theme::mut);
        g.setFont (fonts::mono (13.0f));
        g.drawText (juce::String::fromUTF8 ("\xe2\x80\xb9"), presetPrevZone, juce::Justification::centred);
        g.drawText (juce::String::fromUTF8 ("\xe2\x80\xba"), presetNextZone, juce::Justification::centred);
        const auto pcur = processor.presets().current();
        juce::String pname = pcur.name.isEmpty() ? juce::String ("Init") : pcur.name;
        if (pcur.modified) pname += juce::String::fromUTF8 (" *");   // marca de modificado
        g.setColour (theme::txt);
        g.setFont (fonts::label (12.0f));
        g.drawText (pname, presetNameZone, juce::Justification::centred);
    }

    // SAVE
    g.setColour (theme::mut);
    g.setFont (fonts::mono (10.0f).withExtraKerningFactor (0.10f));
    g.drawText ("SAVE", presetSaveZone, juce::Justification::centred);

    // separadores verticales entre clusters
    g.setColour (theme::lineSoft);
    for (const int sx : { presetSaveZone.getX() - 7, presetAbZone.getX() - 7, zoomSZone.getX() - 7 })
        g.fillRect (sx, headerArea.getCentreY() - 10, 1, 20);

    // A/B: grupo borde hairline, slot activo RELLENO cian con texto oscuro (mockup #ab .on)
    {
        auto ab = presetAbZone;
        g.setColour (theme::lineSoft);
        g.drawRoundedRectangle (ab.toFloat(), 3.0f, 1.0f);
        const bool onA = processor.ab().activeSlot() == 'A';
        auto aCell = ab.removeFromLeft (ab.getWidth() / 2);
        auto bCell = ab;
        g.setFont (fonts::mono (10.0f));
        if (onA) { g.setColour (familyHue); g.fillRoundedRectangle (aCell.toFloat().reduced (1.0f), 2.0f); }
        else     { g.setColour (familyHue); g.fillRoundedRectangle (bCell.toFloat().reduced (1.0f), 2.0f); }
        g.setColour (onA ? juce::Colour (0xff031014) : theme::mut);
        g.drawText ("A", aCell, juce::Justification::centred);
        g.setColour (onA ? theme::mut : juce::Colour (0xff031014));
        g.drawText ("B", bCell, juce::Justification::centred);
    }

    // selector de tamaño S·M·L (grupo hairline; activo = cian sobre tinte)
    {
        auto grp = zoomSZone.getUnion (zoomLZone);
        g.setColour (theme::lineSoft);
        g.drawRoundedRectangle (grp.toFloat(), 3.0f, 1.0f);
        g.fillRect (zoomMZone.getX(), grp.getY() + 5, 1, grp.getHeight() - 10);
        g.fillRect (zoomLZone.getX(), grp.getY() + 5, 1, grp.getHeight() - 10);

        const Zoom        zs[]   = { Zoom::small, Zoom::medium, Zoom::large };
        const char* const lbls[] = { "S", "M", "L" };
        const juce::Rectangle<int> zones[] = { zoomSZone, zoomMZone, zoomLZone };
        g.setFont (fonts::mono (10.0f));
        for (int i = 0; i < 3; ++i)
        {
            if (zoom == zs[i])
            {
                g.setColour (familyHue.withAlpha (0.10f));
                g.fillRect (zones[i].reduced (1));
            }
            g.setColour (zoom == zs[i] ? familyHue : theme::mut);
            g.drawText (lbls[i], zones[i], juce::Justification::centred);
        }
    }
}

// Bisel del chasis (mockup #bezel): hairline inset 4px + corner-brackets en las 4 esquinas.
// Va ENCIMA de los hijos (paintOverChildren del canvas) para "cerrar" el instrumento.
void PluginEditorBase::paintBezel (juce::Graphics& g)
{
    const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) baseW, (float) baseH).reduced (4.0f);
    g.setColour (juce::Colour (0x0ea0c0e0));                 // hairline tenue (alpha ~0.055)
    g.drawRoundedRectangle (r, 3.0f, 1.0f);

    const juce::Colour bk (0x3396bee1);                      // brackets rgba(150,190,225,0.20)
    g.setColour (bk);
    const float L = 11.0f;
    // top-left · top-right · bottom-left · bottom-right
    g.fillRect (r.getX(), r.getY(), L, 1.0f);                g.fillRect (r.getX(), r.getY(), 1.0f, L);
    g.fillRect (r.getRight() - L, r.getY(), L, 1.0f);        g.fillRect (r.getRight() - 1.0f, r.getY(), 1.0f, L);
    g.fillRect (r.getX(), r.getBottom() - 1.0f, L, 1.0f);    g.fillRect (r.getX(), r.getBottom() - L, 1.0f, L);
    g.fillRect (r.getRight() - L, r.getBottom() - 1.0f, L, 1.0f);
    g.fillRect (r.getRight() - 1.0f, r.getBottom() - L, 1.0f, L);
}

//==================================================================================================
// Header browser: clicks por zona (sin botones) + selector S·M·L + menú por categoría
void PluginEditorBase::canvasMouseDown (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    if      (presetPrevZone.contains (p)) processor.presets().prev();
    else if (presetNextZone.contains (p)) processor.presets().next();
    else if (presetNameZone.contains (p)) showPresetMenu();
    else if (presetSaveZone.contains (p)) showSaveDialog();
    else if (zoomSZone.contains (p)) setZoom (Zoom::small);
    else if (zoomMZone.contains (p)) setZoom (Zoom::medium);
    else if (zoomLZone.contains (p)) setZoom (Zoom::large);
    else if (presetAbZone.contains (p))
    {
        if (juce::ModifierKeys::getCurrentModifiers().isAltDown()) processor.ab().copyActiveToOther();
        else                                                       processor.ab().toggle();
        content.repaint (headerArea);
    }
    else if (bypassZone.contains (p))
    {
        if (auto* bp = processor.apvts.getParameter (processor.bypassParamID()))
            bp->setValueNotifyingHost (bp->getValue() >= 0.5f ? 0.0f : 1.0f);
        content.repaint (headerArea);
    }
    else
        mouseDownBody (e);   // PLUGIN: el resto de la ventana
}

void PluginEditorBase::changeListenerCallback (juce::ChangeBroadcaster*)
{
    content.repaint (headerArea);   // refrescar el nombre del preset (+ marca de modificado)
}

void PluginEditorBase::showPresetMenu()
{
    juce::PopupMenu prod, sd, user, root;
    const auto& all = presets::factoryPresets();
    for (int i = 0; i < (int) all.size(); ++i)
    {
        auto& sub = (all[(size_t) i].category == presets::Category::Production) ? prod : sd;
        sub.addItem (i + 1, all[(size_t) i].name);
    }
    int uid = 10001;
    for (auto& f : processor.presets().userPresets())
        user.addItem (uid++, f.getFileNameWithoutExtension());

    root.addSubMenu ("PRODUCTION", prod);
    root.addSubMenu ("SOUND DESIGN", sd);
    if (processor.presets().userPresets().size() > 0)
        root.addSubMenu ("USER", user);

    root.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (content.localAreaToGlobal (presetNameZone)),
        [this] (int r)
        {
            const auto& fp = presets::factoryPresets();
            if (r >= 1 && r <= (int) fp.size())
                processor.presets().applyFactory (r - 1);
            else if (r >= 10001)
            {
                auto users = processor.presets().userPresets();
                const int idx = r - 10001;
                if (idx >= 0 && idx < users.size()) processor.presets().applyUserFile (users[idx]);
            }
        });
}

void PluginEditorBase::showSaveDialog()
{
    auto* w = new juce::AlertWindow ("Save Preset", "Preset name:",
                                     juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", "My Preset");
    w->addButton ("Save",   1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create (
        [this, w] (int res)
        {
            if (res == 1)
            {
                const auto nm = w->getTextEditorContents ("name").trim();
                if (nm.isNotEmpty()) processor.presets().saveUser (nm);
            }
        }), true);   // deleteWhenDismissed
}
}
