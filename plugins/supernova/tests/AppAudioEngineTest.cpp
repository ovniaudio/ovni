// [supernova][app] — AppAudioEngine::pollSystemAudio: la reconciliación entre el GATE de permiso y lo que
// de verdad hizo el backend. Hasta 0.3.0 no había NI UN test de este camino, y el hueco se veía: un
// `error` del HAL (sin salida default, aggregate caído) dejaba el gate clavado en `requesting` para
// siempre — sin aviso, sin botón, sin audio, y con el tap/aggregate que el intento hubiera creado
// colgando del HAL. El backend real entra por un doble: SystemCapture ya es la interfaz que hace falta.
#include <catch2/catch_test_macros.hpp>

#include "PluginProcessor.h"
#include "app/AppAudioEngine.h"

using supernova::SystemCaptureStatus;
using supernova::SystemAudioBackend;
using State = supernova::SystemAudioPermissionGate::State;

namespace {

// Doble del backend: devuelve el status que le pidamos y cuenta start/stop. Sin Cocoa, sin TCC.
class FakeCapture final : public supernova::SystemCapture
{
public:
    bool start (int, int, SampleCallback cb) noexcept override
    {
        ++startCalls; sink = std::move (cb); status_ = next; return true;
    }
    void stop() noexcept override { ++stopCalls; sink = nullptr; status_ = SystemCaptureStatus::idle; }
    SystemCaptureStatus status() const noexcept override  { return status_; }
    bool isAuthorized() const noexcept override           { return authorized; }
    SystemAudioBackend backend() const noexcept override  { return SystemAudioBackend::processTap; }

    void setStatus (SystemCaptureStatus s) noexcept { status_ = s; }

    SystemCaptureStatus next = SystemCaptureStatus::idle;   // lo que devuelve el próximo start()
    bool authorized = false;
    int  startCalls = 0, stopCalls = 0;
    SampleCallback sink;

private:
    SystemCaptureStatus status_ = SystemCaptureStatus::idle;
};

// Arnés: processor + settings en un temp propio + engine con el doble adentro.
struct Harness
{
    explicit Harness (std::unique_ptr<FakeCapture> dbl)
        : fake (dbl.get()),
          settings (makeOptions()),
          engine (proc, settings, std::move (dbl)) {}

    ~Harness() { settings.getFile().deleteFile(); }

    static juce::PropertiesFile::Options makeOptions()
    {
        juce::PropertiesFile::Options o;
        o.applicationName     = "OvniSupernovaTests-" + juce::Uuid().toDashedString();
        o.filenameSuffix      = "settings";
        o.folderName          = "OvniSupernovaTests";
        o.osxLibrarySubFolder = "Application Support";
        return o;
    }

    FakeCapture*                fake;
    supernova::SupernovaProcessor proc;
    juce::PropertiesFile          settings;
    supernova::AppAudioEngine     engine;
};

} // namespace

TEST_CASE ("poll: el backend arrancó a capturar → el gate pasa a granted y el aviso se va", "[supernova][app]")
{
    auto dbl = std::make_unique<FakeCapture>();
    dbl->next = SystemCaptureStatus::capturing;
    Harness h { std::move (dbl) };

    h.engine.useSystemAudio();
    REQUIRE (h.engine.permissionState() == State::needsPermission);   // abrir la app NO pide nada (D-33)
    REQUIRE (h.fake->startCalls == 0);

    h.engine.requestSystemAudioPermission();                          // el click de ALLOW
    REQUIRE (h.engine.permissionState() == State::requesting);
    REQUIRE (h.fake->startCalls == 1);

    h.engine.pollSystemAudio();
    REQUIRE (h.engine.permissionState() == State::granted);
}

TEST_CASE ("poll: permiso denegado → estado denied y la captura se suelta", "[supernova][app]")
{
    auto dbl = std::make_unique<FakeCapture>();
    dbl->next = SystemCaptureStatus::permissionDenied;
    Harness h { std::move (dbl) };

    h.engine.useSystemAudio();
    h.engine.requestSystemAudioPermission();
    h.engine.pollSystemAudio();

    REQUIRE (h.engine.permissionState() == State::denied);
    REQUIRE (h.fake->stopCalls >= 1);          // nada del HAL queda colgando
}

TEST_CASE ("poll: un error del HAL NO deja el gate clavado en requesting", "[supernova][app]")
{
    // El bug de 0.3.0: 'error' no era permiso faltante, así que el poll no hacía NADA. El gate se quedaba
    // en `requesting` para siempre (sin aviso, sin ALLOW, sin audio) y lo que el intento hubiera creado en
    // el HAL quedaba vivo. Ahora vuelve a needsPermission: el usuario ve el aviso y puede reintentar.
    auto dbl = std::make_unique<FakeCapture>();
    dbl->next = SystemCaptureStatus::error;
    Harness h { std::move (dbl) };

    h.engine.useSystemAudio();
    h.engine.requestSystemAudioPermission();
    REQUIRE (h.engine.permissionState() == State::requesting);

    h.engine.pollSystemAudio();

    REQUIRE (h.engine.permissionState() == State::needsPermission);
    REQUIRE (h.fake->stopCalls >= 1);          // y se suelta lo que el intento hubiera creado

    // Y el aviso vuelve con ALLOW: se puede reintentar sin reabrir la app.
    h.fake->next = SystemCaptureStatus::capturing;
    h.engine.requestSystemAudioPermission();
    h.engine.pollSystemAudio();
    REQUIRE (h.engine.permissionState() == State::granted);
}

TEST_CASE ("poll: un backend 'unsupported' tampoco deja el gate clavado", "[supernova][app]")
{
    // macOS 11/12: ni process taps (14.2+) ni ScreenCaptureKit (13+). Mismo agujero que 'error' — el gate
    // se quedaba en `requesting` sin nada en pantalla. Mejor el aviso con ALLOW (reintentable, y no
    // miente diciendo "prendelo en Ajustes") que una app muda para siempre.
    auto dbl = std::make_unique<FakeCapture>();
    dbl->next = SystemCaptureStatus::unsupported;
    Harness h { std::move (dbl) };

    h.engine.useSystemAudio();
    h.engine.requestSystemAudioPermission();
    h.engine.pollSystemAudio();

    REQUIRE (h.engine.permissionState() == State::needsPermission);
}

TEST_CASE ("poll: si la captura se cae con el permiso ya dado, se rearma sola y callada", "[supernova][app]")
{
    // El caso de todos los días: el usuario enchufa los auriculares. El aggregate quedó atado al UID de
    // la salida vieja y el formato se leyó una sola vez, así que la captura se muere. El gate NO puede
    // volver a pedir permiso — TCC ya dijo que sí — pero tampoco puede quedarse en `granted` mirando un
    // backend muerto: hasta 0.3.0 la app se quedaba muda hasta que la reabrías.
    auto dbl = std::make_unique<FakeCapture>();
    dbl->next = SystemCaptureStatus::capturing;
    Harness h { std::move (dbl) };

    h.engine.useSystemAudio();
    h.engine.requestSystemAudioPermission();
    h.engine.pollSystemAudio();
    REQUIRE (h.engine.permissionState() == State::granted);
    const int startsBefore = h.fake->startCalls;

    h.fake->setStatus (SystemCaptureStatus::idle);            // los auriculares se enchufaron
    for (int i = 0; i < 60; ++i) h.engine.pollSystemAudio();  // 2s de poll a 30Hz

    REQUIRE (h.engine.permissionState() == State::granted);   // ni un cartel, ni un aviso
    REQUIRE (h.fake->startCalls > startsBefore);              // se levantó sola, con formato nuevo
    REQUIRE (h.engine.systemStatus() == SystemCaptureStatus::capturing);
}

TEST_CASE ("poll: si el usuario APAGA el permiso en Ajustes con la captura andando → denied",
           "[supernova][app]")
{
    auto dbl = std::make_unique<FakeCapture>();
    dbl->next = SystemCaptureStatus::capturing;
    Harness h { std::move (dbl) };

    h.engine.useSystemAudio();
    h.engine.requestSystemAudioPermission();
    h.engine.pollSystemAudio();
    REQUIRE (h.engine.permissionState() == State::granted);

    h.fake->setStatus (SystemCaptureStatus::permissionDenied);
    h.engine.pollSystemAudio();

    REQUIRE (h.engine.permissionState() == State::denied);    // el aviso pasa a decir dónde prenderlo
}

TEST_CASE ("poll: con la fuente en Input Device el gate no se toca", "[supernova][app]")
{
    auto dbl = std::make_unique<FakeCapture>();
    Harness h { std::move (dbl) };

    h.engine.useSystemAudio();
    h.engine.requestSystemAudioPermission();
    h.fake->setStatus (SystemCaptureStatus::error);

    h.engine.useInputDevice ("");            // el usuario se fue a MIC
    h.engine.pollSystemAudio();
    REQUIRE (h.engine.permissionState() == State::requesting);   // intacto: no es asunto de esta fuente
}
