// [supernova][app] — elección del BACKEND de captura del audio del sistema (D-33).
// macOS 14.2 trae los Core Audio process taps (AudioHardwareCreateProcessTap): el permiso que pide
// macOS pasa a ser el CHICO, "System Audio Recording Only" (kTCCServiceAudioCapture), y la app deja de
// aparecer bajo "grabar la pantalla". Antes de 14.2 esa API NO existe → ScreenCaptureKit (permiso de
// PANTALLA) sigue siendo el único camino. La decisión es una función PURA para poder testearla sin Cocoa.
#include <catch2/catch_test_macros.hpp>
#include "app/audio/SystemCapture.h"

using supernova::SystemAudioBackend;
using supernova::pickBackend;

TEST_CASE ("system capture backend: macOS 14.2+ usa Core Audio process taps", "[supernova][app]")
{
    REQUIRE (pickBackend (14, 2) == SystemAudioBackend::processTap);   // primera versión con la API
    REQUIRE (pickBackend (14, 7) == SystemAudioBackend::processTap);
    REQUIRE (pickBackend (15, 0) == SystemAudioBackend::processTap);
    REQUIRE (pickBackend (26, 6) == SystemAudioBackend::processTap);
}

TEST_CASE ("system capture backend: macOS 13 … 14.1 cae a ScreenCaptureKit", "[supernova][app]")
{
    REQUIRE (pickBackend (13, 0) == SystemAudioBackend::screenCapture);
    REQUIRE (pickBackend (13, 6) == SystemAudioBackend::screenCapture);
    REQUIRE (pickBackend (14, 0) == SystemAudioBackend::screenCapture);
    REQUIRE (pickBackend (14, 1) == SystemAudioBackend::screenCapture);
}

TEST_CASE ("system capture backend: el panel de Ajustes es el del permiso que pide cada backend",
           "[supernova][app]")
{
    // 14.2+ → "Grabación de audio del sistema"; 13.x → "Grabación de pantalla" (el permiso viejo).
    REQUIRE (supernova::settingsPaneUrl (SystemAudioBackend::processTap)
             == juce::String ("x-apple.systempreferences:com.apple.preference.security?Privacy_AudioCapture"));
    REQUIRE (supernova::settingsPaneUrl (SystemAudioBackend::screenCapture)
             == juce::String ("x-apple.systempreferences:com.apple.preference.security?Privacy_ScreenCapture"));
}
