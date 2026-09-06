// [supernova][app] — el GATE de permiso del audio del sistema (D-33). Reglas que fija Joaquín:
//   · Abrir la app NO puede levantar el cartel del sistema. Arranca sin capturar y con un aviso.
//   · El cartel lo dispara el CLICK del usuario ("Allow"), que es cuando entiende qué le están pidiendo.
//   · Si dice que no, la app sigue usable (MIC/MIDI) y el aviso pasa a explicar dónde prenderlo.
//   · Y NO se vuelve a pedir el cartel en cada arranque: se pidió una vez, después es sólo el aviso.
// La máquina de estados es PURA (sin Cocoa, sin TCC): el backend real entra por un doble.
#include <catch2/catch_test_macros.hpp>
#include "app/SystemAudioPermission.h"
#include "app/ui/SourcePermissionNotice.h"

using supernova::SystemAudioBackend;
using Gate  = supernova::SystemAudioPermissionGate;
using State = supernova::SystemAudioPermissionGate::State;

TEST_CASE ("permission gate: primer arranque sin permiso — ni captura ni cartel, sólo el aviso",
           "[supernova][app]")
{
    Gate g;
    g.begin (/*askedBefore*/ false, /*authorized*/ false);

    REQUIRE (g.state() == State::needsPermission);
    REQUIRE_FALSE (g.shouldStartCapture());   // ← acá vivía el cartel al abrir; ya no
    REQUIRE (g.noticeVisible());
    REQUIRE_FALSE (g.askedBefore());
}

TEST_CASE ("permission gate: el click de Allow es lo único que dispara el cartel del sistema",
           "[supernova][app]")
{
    Gate g;
    g.begin (false, false);

    REQUIRE (g.allowClicked());               // true = el caller debe arrancar la captura
    REQUIRE (g.state() == State::requesting);
    REQUIRE (g.shouldStartCapture());
    REQUIRE (g.askedBefore());                // se persiste: el cartel se pide UNA vez

    g.captureStarted();
    REQUIRE (g.state() == State::granted);
    REQUIRE (g.shouldStartCapture());
    REQUIRE_FALSE (g.noticeVisible());
}

TEST_CASE ("permission gate: denegar deja la app usable y el aviso explica dónde prenderlo",
           "[supernova][app]")
{
    Gate g;
    g.begin (false, false);
    g.allowClicked();
    g.captureDenied();

    REQUIRE (g.state() == State::denied);
    REQUIRE (g.noticeVisible());
    REQUIRE_FALSE (g.shouldStartCapture());   // no insistimos: MIC/MIDI siguen andando
    REQUIRE_FALSE (g.allowClicked());         // y un segundo click NO vuelve a pedir el cartel
    REQUIRE (g.state() == State::denied);
}

TEST_CASE ("permission gate: con el permiso ya dado arranca capturando, sin aviso", "[supernova][app]")
{
    Gate g;
    g.begin (/*askedBefore*/ true, /*authorized*/ true);

    REQUIRE (g.state() == State::granted);
    REQUIRE (g.shouldStartCapture());
    REQUIRE_FALSE (g.noticeVisible());
}

TEST_CASE ("permission gate: si ya se pidió antes, el arranque reintenta EN SILENCIO", "[supernova][app]")
{
    // macOS ya tiene una respuesta guardada en TCC → intentar NO levanta cartel: o sale, o falla callado.
    Gate g;
    g.begin (/*askedBefore*/ true, /*authorized*/ false);

    REQUIRE (g.state() == State::requesting);
    REQUIRE (g.shouldStartCapture());
    REQUIRE_FALSE (g.noticeVisible());        // nada parpadea mientras resuelve

    g.captureDenied();
    REQUIRE (g.state() == State::denied);
    REQUIRE (g.noticeVisible());
}

TEST_CASE ("permission gate: si el permiso aparece en Ajustes, el aviso se va solo", "[supernova][app]")
{
    Gate g;
    g.begin (true, false);
    g.captureDenied();
    REQUIRE (g.state() == State::denied);

    g.authorizationSeen();                    // el poll en vivo vio TCC encendido
    REQUIRE (g.state() == State::requesting);
    REQUIRE (g.shouldStartCapture());
    g.captureStarted();
    REQUIRE (g.state() == State::granted);
    REQUIRE_FALSE (g.noticeVisible());
}

// --------------------------------------------------------------------------------- macOS 11/12 (D-35)
// 0.3.1 hacía caer `unsupported` en `needsPermission`: la barra ofrecía ALLOW en una Mac donde ni los taps
// (14.2+) ni ScreenCaptureKit (13+) existen. El botón no podía resolver NADA — el cartel de macOS no sale,
// el intento vuelve a fallar, y el usuario queda clickeando algo que nunca va a funcionar. El cuarto
// estado dice la verdad y no ofrece nada que tocar: MIC/MIDI están al lado, en la misma barra.
TEST_CASE ("permission gate: en macOS 11/12 el estado es `unsupported`, no `needs permission`",
           "[supernova][app]")
{
    Gate g;
    g.begin (/*askedBefore*/ false, /*authorized*/ false);
    g.captureUnsupported();

    REQUIRE (g.state() == State::unsupported);
    REQUIRE_FALSE (g.shouldStartCapture());   // no hay nada que intentar en esta versión de macOS
    REQUIRE (g.noticeVisible());              // pero SÍ hay algo que explicar
    REQUIRE_FALSE (g.allowClicked());         // …y nada que clickear: el click no cambia el estado
    REQUIRE (g.state() == State::unsupported);
}

TEST_CASE ("permission gate: `unsupported` no se reintenta solo — la versión de macOS no cambia sola",
           "[supernova][app]")
{
    Gate g;
    g.begin (true, false);
    g.captureUnsupported();

    g.authorizationSeen();                    // el poll lento, si alguna vez llegara hasta acá
    REQUIRE (g.state() == State::unsupported);
    REQUIRE_FALSE (g.shouldStartCapture());
}

// --------------------------------------------------------------------------------- el aviso en el tiempo
// Visto en el smoke del 5-sep: con `sysAudioAsked=1` guardado y el permiso reseteado (usuario que limpia
// permisos, o reinstala macOS), la app arranca en `requesting`, el cartel del sistema sale SOLO… y la
// barra no muestra nada mientras espera. Nadie entiende qué está pasando ni de dónde salió ese cartel.
using supernova::NoticeMode;
using supernova::SourceNoticeModel;

TEST_CASE ("aviso: el reintento silencioso normal no hace parpadear nada", "[supernova][app]")
{
    SourceNoticeModel m;
    REQUIRE (m.update (State::requesting, 0.0)    == NoticeMode::hidden);
    REQUIRE (m.update (State::requesting, 1999.0) == NoticeMode::hidden);   // TCC contesta al toque
    REQUIRE (m.update (State::granted,    2500.0) == NoticeMode::hidden);
}

TEST_CASE ("aviso: si la espera pasa de 2s, la barra dice que está esperando a macOS", "[supernova][app]")
{
    SourceNoticeModel m;
    REQUIRE (m.update (State::requesting, 1000.0) == NoticeMode::hidden);
    REQUIRE (m.update (State::requesting, 3000.0) == NoticeMode::waiting);  // el cartel está abierto

    REQUIRE (supernova::noticeText (NoticeMode::waiting, SystemAudioBackend::processTap)
             == juce::String (juce::CharPointer_UTF8 ("Waiting for macOS permission\xe2\x80\xa6")));
    REQUIRE (supernova::noticeAction (NoticeMode::waiting).isEmpty());      // no hay nada que clickear
}

TEST_CASE ("aviso: esperando → el usuario dice que NO → OPEN", "[supernova][app]")
{
    SourceNoticeModel m;
    Gate g;
    g.begin (/*askedBefore*/ true, /*authorized*/ false);
    REQUIRE (m.update (g.state(), 0.0)    == NoticeMode::hidden);    // arranca el reloj de la espera
    REQUIRE (m.update (g.state(), 3000.0) == NoticeMode::waiting);

    g.captureDenied();
    REQUIRE (m.update (g.state(), 3100.0) == NoticeMode::denied);
    REQUIRE (supernova::noticeAction (NoticeMode::denied) == juce::String ("OPEN"));
}

TEST_CASE ("aviso: esperando → se cae el HAL → ALLOW (se puede reintentar)", "[supernova][app]")
{
    SourceNoticeModel m;
    Gate g;
    g.begin (true, false);
    REQUIRE (m.update (g.state(), 0.0)    == NoticeMode::hidden);
    REQUIRE (m.update (g.state(), 3000.0) == NoticeMode::waiting);

    g.captureFailed();                                    // ni permiso denegado ni éxito: error del HAL
    REQUIRE (m.update (g.state(), 3100.0) == NoticeMode::ask);
    REQUIRE (supernova::noticeAction (NoticeMode::ask) == juce::String ("ALLOW"));
}

TEST_CASE ("aviso: cada intento nuevo vuelve a arrancar el reloj de la espera", "[supernova][app]")
{
    SourceNoticeModel m;
    REQUIRE (m.update (State::requesting, 1000.0) == NoticeMode::hidden);
    REQUIRE (m.update (State::requesting, 4000.0) == NoticeMode::waiting);
    REQUIRE (m.update (State::needsPermission, 4100.0) == NoticeMode::ask);      // falló, volvemos al ALLOW
    REQUIRE (m.update (State::requesting, 4200.0) == NoticeMode::hidden);        // segundo intento: de cero
    REQUIRE (m.update (State::requesting, 6300.0) == NoticeMode::waiting);
}

TEST_CASE ("aviso: en macOS 11/12 la barra dice la verdad y no ofrece ningún bot\xc3\xb3n", "[supernova][app]")
{
    SourceNoticeModel m;
    Gate g;
    g.begin (false, false);
    g.captureUnsupported();

    // Sin reloj de por medio: no se está esperando a nadie, se está informando. Se ve YA.
    REQUIRE (m.update (g.state(), 0.0) == NoticeMode::unsupported);
    REQUIRE (m.update (g.state(), 9999.0) == NoticeMode::unsupported);

    REQUIRE (supernova::noticeText (NoticeMode::unsupported, SystemAudioBackend::screenCapture)
             == juce::String (juce::CharPointer_UTF8 (
                    "System Audio needs macOS 13 or later on this Mac \xe2\x80\x94 use an input device or MIDI")));
    REQUIRE (supernova::noticeText (NoticeMode::unsupported, SystemAudioBackend::processTap)
             == supernova::noticeText (NoticeMode::unsupported, SystemAudioBackend::screenCapture));
    REQUIRE (supernova::noticeAction (NoticeMode::unsupported).isEmpty());   // no hay nada que tocar
}

TEST_CASE ("permission gate: los textos nombran el permiso que pide CADA backend", "[supernova][app]")
{
    REQUIRE (supernova::noticeText (NoticeMode::ask, SystemAudioBackend::processTap)
             == juce::String ("System Audio needs permission"));
    REQUIRE (supernova::noticeAction (NoticeMode::ask) == juce::String ("ALLOW"));

    REQUIRE (supernova::noticeText (NoticeMode::denied, SystemAudioBackend::processTap)
             == juce::String (juce::CharPointer_UTF8 ("Enable in System Settings \xe2\x80\xba Privacy \xe2\x80\xba System Audio Recording")));
    REQUIRE (supernova::noticeText (NoticeMode::denied, SystemAudioBackend::screenCapture)
             == juce::String (juce::CharPointer_UTF8 ("Enable in System Settings \xe2\x80\xba Privacy \xe2\x80\xba Screen Recording")));
    REQUIRE (supernova::noticeAction (NoticeMode::denied) == juce::String ("OPEN"));
}

// [.uisnap] — los CUATRO estados del aviso de la barra SOURCE, para el ojo (y para las capturas del manual).
TEST_CASE ("uisnap: aviso de System Audio — pidiendo, esperando, denegado y sin soporte \xe2\x86\x92 /tmp/snv-perm-notice-*.png",
           "[supernova][.uisnap]")
{
    const struct { NoticeMode mode; const char* path; } cases[] = {
        { NoticeMode::ask,         "/tmp/snv-perm-notice-ask.png"         },
        { NoticeMode::waiting,     "/tmp/snv-perm-notice-waiting.png"     },
        { NoticeMode::denied,      "/tmp/snv-perm-notice-denied.png"      },
        { NoticeMode::unsupported, "/tmp/snv-perm-notice-unsupported.png" },
    };

    // El aviso es TRANSPARENTE (vive sobre la barra, que pinta su propio fondo): para que el PNG muestre
    // lo que ve Joaquín y no letras ámbar sobre blanco, lo montamos sobre un host con el bg1 de la barra.
    struct BarHost final : juce::Component
    {
        BarHost() { setOpaque (true); }
        void paint (juce::Graphics& g) override { g.fillAll (ovni::ui::theme::bg1); }
    };

    for (const auto& c : cases)
    {
        BarHost host;
        host.setSize (588, 38);

        auto notice = std::make_unique<supernova::SourcePermissionNotice>();
        notice->setState (c.mode, SystemAudioBackend::processTap);
        notice->setBounds (14, 6, 560, 26);   // el alto REAL de la fila 1 de la barra
        host.addAndMakeVisible (notice.get());

        const auto img = host.createComponentSnapshot (host.getLocalBounds(), true, 2.0f);
        REQUIRE (img.isValid());

        juce::File f (c.path);
        f.deleteFile();
        juce::FileOutputStream os (f);
        REQUIRE (os.openedOk());
        juce::PNGImageFormat png;
        REQUIRE (png.writeImageToStream (img, os));
    }
}
