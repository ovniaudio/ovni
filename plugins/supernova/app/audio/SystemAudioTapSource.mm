// SystemAudioTapSource — Core Audio process taps (macOS 14.2+). Ver el .h para el porqué (D-33).
//
// Cadena de objetos, y su orden de destrucción EXACTO al revés (IOProc → aggregate → tap):
//   1. CATapDescription global estéreo, privateTap=YES, muteBehavior=CATapUnmuted (el usuario sigue
//      escuchando su música: NO muteamos lo que grabamos).
//   2. AudioHardwareCreateProcessTap → AudioObjectID del tap. ACÁ es donde macOS pide el permiso de
//      "System Audio Recording Only" la PRIMERA vez. Si el usuario dijo que no, esta llamada falla
//      (kAudioHardwareIllegalOperationError 'nope') sin volver a mostrar cartel.
//   3. Aggregate device PRIVADO con ese tap en kAudioAggregateDeviceTapListKey y el output default como
//      sub-device/main (un aggregate sin sub-device real no bombea IO).
//   4. AudioDeviceIOProc sobre el aggregate, en una cola serial propia → empuja al callback.
//
// El FORMATO se LEE del tap (kAudioTapPropertyFormat): sample rate y canales NO se asumen — el tap sigue
// al output real del usuario (44.1k/48k/96k, estéreo o más).
//
// TCC: no hay preflight público para kTCCServiceAudioCapture (a diferencia de CGPreflightScreenCaptureAccess
// para pantalla). isAuthorized() reporta, entonces, el RESULTADO OBSERVADO del último intento de crear el
// tap: unknown antes del primer intento, granted si salió, denied si falló. Quién decide si conviene
// intentar (y disparar el cartel) es el gate de permiso de la app, no esta clase.
#include "SystemAudioTapSource.h"
#include "TapLifecycle.h"

#if JUCE_MAC

#import <Foundation/Foundation.h>
#import <CoreAudio/CoreAudio.h>
#import <CoreAudio/CATapDescription.h>
#import <CoreAudio/AudioHardwareTapping.h>
#import <AudioToolbox/AudioToolbox.h>

#include <atomic>
#include <cmath>
#include <limits>
#include <vector>
#include <cstdarg>
#include <cstdio>
#include <juce_audio_basics/juce_audio_basics.h>

namespace {

constexpr int kMaxCh = 8;

void tapLog (const char* fmt, ...)
{
    char buf[400];
    va_list ap; va_start (ap, fmt);
    std::vsnprintf (buf, sizeof buf, fmt, ap);
    va_end (ap);
    std::fprintf (stderr, "%s", buf);
}

// UID del device de salida por default: el aggregate necesita un sub-device REAL para bombear IO.
NSString* defaultOutputDeviceUID()
{
    AudioObjectPropertyAddress devAddr { kAudioHardwarePropertyDefaultOutputDevice,
                                         kAudioObjectPropertyScopeGlobal,
                                         kAudioObjectPropertyElementMain };
    AudioObjectID dev = kAudioObjectUnknown;
    UInt32 size = sizeof (dev);
    if (AudioObjectGetPropertyData (kAudioObjectSystemObject, &devAddr, 0, nullptr, &size, &dev) != noErr
        || dev == kAudioObjectUnknown)
        return nil;

    AudioObjectPropertyAddress uidAddr { kAudioDevicePropertyDeviceUID,
                                         kAudioObjectPropertyScopeGlobal,
                                         kAudioObjectPropertyElementMain };
    CFStringRef uid = nullptr;
    size = sizeof (uid);
    if (AudioObjectGetPropertyData (dev, &uidAddr, 0, nullptr, &size, &uid) != noErr || uid == nullptr)
        return nil;
    return (__bridge_transfer NSString*) uid;
}

} // namespace

namespace supernova {

struct SystemAudioTapSource::Impl : std::enable_shared_from_this<SystemAudioTapSource::Impl>
{
    std::atomic<SystemCaptureStatus> status { SystemCaptureStatus::idle };
    std::atomic<int> authorized { -1 };            // -1 sin datos · 0 denegado · 1 concedido (ver .h)
    std::atomic<int>  frameLogs { 0 };

    TapLifecycle life { "com.ovni.supernova.tap.setup" };   // generación + cola serial + stop que no espera
    dispatch_queue_t ioQ     = nil;
    dispatch_queue_t notifyQ = nil;   // avisos del HAL (cambio de salida / de sample rate)

    // Listeners guardados para poder sacarlos: sin esto el bloque queda registrado en el HAL apuntando a
    // un Impl muerto. Se instalan al empezar a capturar y se sacan en destroyChain().
    AudioObjectPropertyListenerBlock outListener API_AVAILABLE(macos(10.15)) = nil;
    AudioObjectPropertyListenerBlock srListener  API_AVAILABLE(macos(10.15)) = nil;

    AudioObjectID     tapID  = kAudioObjectUnknown;
    AudioObjectID     aggID  = kAudioObjectUnknown;
    AudioDeviceIOProcID procID = nullptr;

    SampleCallback cb;
    std::vector<float> planar;      // scratch para el caso interleaved (preasignado, no realoca en el IO)
    double tapSr = 48000.0;

    // Destruye lo que EXISTA, en el orden inverso exacto a la construcción (IOProc → aggregate → tap).
    // Idempotente, y por eso sirve para las dos cosas: el stop() normal y abortar un intento a medio
    // armar. Hasta 0.3.0 las salidas por error de start() no destruían nada: un `error` después de crear
    // el tap (o el aggregate) dejaba objetos del HAL vivos hasta cerrar la app, uno por reintento.
    void destroyChain() noexcept
    {
        removeDeviceListeners();
        if (@available (macOS 14.2, *))
        {
            if (aggID != kAudioObjectUnknown && procID != nullptr)
            {
                AudioDeviceStop (aggID, procID);
                AudioDeviceDestroyIOProcID (aggID, procID);   // espera a que el IOProc en vuelo termine
            }
            procID = nullptr;

            if (aggID != kAudioObjectUnknown) AudioHardwareDestroyAggregateDevice (aggID);
            aggID = kAudioObjectUnknown;

            if (tapID != kAudioObjectUnknown) AudioHardwareDestroyProcessTap (tapID);
            tapID = kAudioObjectUnknown;
        }
        cb = nullptr;               // recién acá: el IOProc ya no puede estar corriendo
        frameLogs.store (0);
    }

    // ---- cambio de salida / de sample rate mientras capturamos -------------------------------------
    // El aggregate queda atado al UID de la salida que había cuando se creó, y el formato del tap se lee
    // UNA sola vez. Enchufar auriculares (o cambiar de salida, o que la interfaz se desconecte) deja la
    // captura muda para siempre. Escuchamos los dos avisos del HAL y SOLTAMOS la captura; rearmarla es
    // asunto del poll del engine, que ya corre a 30Hz y sabe que el permiso sigue dado (no hay cartel).
    void installDeviceListeners() noexcept
    {
        if (notifyQ == nil)
            notifyQ = dispatch_queue_create ("com.ovni.supernova.tap.notify", DISPATCH_QUEUE_SERIAL);

        Impl* self = this;   // los listeners se sacan en destroyChain(), siempre antes de que muera el Impl

        AudioObjectPropertyAddress outAddr { kAudioHardwarePropertyDefaultOutputDevice,
                                             kAudioObjectPropertyScopeGlobal,
                                             kAudioObjectPropertyElementMain };
        outListener = ^(UInt32, const AudioObjectPropertyAddress*)
        {
            self->releaseForRestart ("cambió la salida por default");
        };
        const OSStatus outSt = AudioObjectAddPropertyListenerBlock (kAudioObjectSystemObject, &outAddr, notifyQ, outListener);
        if (outSt != noErr) tapLog ("[sysaudio] listener de salida-default FALLÓ st=%d (no se autorearma)\n", (int) outSt);

        if (aggID == kAudioObjectUnknown) return;

        AudioObjectPropertyAddress srAddr { kAudioDevicePropertyNominalSampleRate,
                                            kAudioObjectPropertyScopeGlobal,
                                            kAudioObjectPropertyElementMain };
        srListener = ^(UInt32, const AudioObjectPropertyAddress*)
        {
            // Sólo si CAMBIÓ de verdad: el aggregate avisa también al asentarse, y un rearme por cada
            // aviso sería un bucle stop/start eterno.
            Float64 sr = 0.0;
            UInt32 sz = sizeof (sr);
            AudioObjectPropertyAddress a { kAudioDevicePropertyNominalSampleRate,
                                           kAudioObjectPropertyScopeGlobal,
                                           kAudioObjectPropertyElementMain };
            if (AudioObjectGetPropertyData (self->aggID, &a, 0, nullptr, &sz, &sr) == noErr
                && sr > 0.0 && std::abs (sr - self->tapSr) > 1.0)
                self->releaseForRestart ("cambió el sample rate de la salida");
        };
        const OSStatus srSt = AudioObjectAddPropertyListenerBlock (aggID, &srAddr, notifyQ, srListener);
        if (srSt != noErr) tapLog ("[sysaudio] listener de sample-rate FALLÓ st=%d\n", (int) srSt);
    }

    void removeDeviceListeners() noexcept
    {
        if (outListener != nil)
        {
            AudioObjectPropertyAddress outAddr { kAudioHardwarePropertyDefaultOutputDevice,
                                                 kAudioObjectPropertyScopeGlobal,
                                                 kAudioObjectPropertyElementMain };
            AudioObjectRemovePropertyListenerBlock (kAudioObjectSystemObject, &outAddr, notifyQ, outListener);
            outListener = nil;
        }

        if (srListener != nil)
        {
            if (aggID != kAudioObjectUnknown)
            {
                AudioObjectPropertyAddress srAddr { kAudioDevicePropertyNominalSampleRate,
                                                    kAudioObjectPropertyScopeGlobal,
                                                    kAudioObjectPropertyElementMain };
                AudioObjectRemovePropertyListenerBlock (aggID, &srAddr, notifyQ, srListener);
            }
            srListener = nil;
        }
    }

    // Suelta la cadena (sin esperar a nadie) y deja el status en idle. El poll del engine la rearma.
    void releaseForRestart (const char* why) noexcept
    {
        if (status.load() != SystemCaptureStatus::capturing) return;   // ya está soltada
        tapLog ("[sysaudio] %s → suelto el tap; el poll lo vuelve a levantar con el formato nuevo\n", why);
        stopAsync();
    }

    // Invalida la generación en vuelo y ENCOLA la destrucción detrás de ella. No espera (ver TapLifecycle).
    void stopAsync() noexcept
    {
        status = SystemCaptureStatus::idle;   // para el que pregunte YA (pollSystemAudio a 30Hz)
        life.stop ([owner = shared_from_this()]
        {
            if (@available (macOS 14.2, *)) owner->destroyChain();
            owner->status = SystemCaptureStatus::idle;
        });
    }

    // Sólo escribe el status si esta generación sigue vigente (si hubo stop() en el medio, manda el stop).
    void setStatusIfCurrent (int gen, SystemCaptureStatus s) noexcept
    {
        if (life.isCurrent (gen)) status.store (s);
    }
};

SystemAudioTapSource::SystemAudioTapSource() : impl (std::make_shared<Impl>()) {}
SystemAudioTapSource::~SystemAudioTapSource() { stop(); }

bool SystemAudioTapSource::isSupported() noexcept
{
    if (@available (macOS 14.2, *)) return true;
    return false;
}

SystemCaptureStatus SystemAudioTapSource::status() const noexcept { return impl->status.load(); }

bool SystemAudioTapSource::isAuthorized() const noexcept { return impl->authorized.load() == 1; }

// ------------------------------------------------------------------------------------------- start
bool SystemAudioTapSource::start (int sampleRate, int channels, SampleCallback onSamples) noexcept
{
    if (! isSupported()) { impl->status = SystemCaptureStatus::unsupported; return false; }
    if (impl->status.load() == SystemCaptureStatus::capturing) return true;

    juce::ignoreUnused (sampleRate, channels);   // el formato lo manda el TAP, no nosotros

    if (impl->ioQ == nil) impl->ioQ = dispatch_queue_create ("com.ovni.supernova.tap.io", DISPATCH_QUEUE_SERIAL);

    // El setup va a una cola de fondo: la PRIMERA vez, AudioHardwareCreateProcessTap levanta el cartel de
    // TCC y bloquea hasta que el usuario contesta — en el message thread congelaría la ventana. La tarea
    // se lleva una referencia FUERTE al Impl: puede seguir viva después de que el dueño se haya ido.
    impl->life.start ([owner = impl, sink = std::move (onSamples)] (int gen)
    {
        Impl* I = owner.get();
        @autoreleasepool
        {
            if (@available (macOS 14.2, *))
            {
                if (! I->life.isCurrent (gen)) return;
                I->cb = sink;

                // 1) El tap: todo lo que suene en la Mac, mezclado a estéreo, sin excluir a nadie.
                CATapDescription* desc = [[CATapDescription alloc] initStereoGlobalTapButExcludeProcesses:@[]];
                desc.name         = @"SUPERNOVA System Audio";
                desc.muteBehavior = CATapUnmuted;      // el usuario sigue escuchando: no muteamos nada
                desc.privateTap   = YES;               // sólo este proceso ve el tap

                AudioObjectID tapID = kAudioObjectUnknown;
                const OSStatus tapSt = AudioHardwareCreateProcessTap (desc, &tapID);
                tapLog ("[sysaudio] createProcessTap st=%d tap=%u\n", (int) tapSt, (unsigned) tapID);

                if (tapSt != noErr || tapID == kAudioObjectUnknown)
                {
                    // 'nope' (kAudioHardwareIllegalOperationError) = TCC dijo que no. Cualquier otro código
                    // es un error real de la cadena, no un permiso faltante.
                    const bool denied = (tapSt == kAudioHardwareIllegalOperationError);
                    I->authorized.store (denied ? 0 : -1);
                    I->destroyChain();
                    I->setStatusIfCurrent (gen, denied ? SystemCaptureStatus::permissionDenied
                                                       : SystemCaptureStatus::error);
                    return;
                }
                I->authorized.store (1);               // el tap existe ⇒ el permiso está dado
                I->tapID = tapID;

                // Crear el tap es LO QUE BLOQUEA (el cartel de TCC espera al usuario). Si mientras tanto
                // hubo un stop() —cambio de SOURCE, cierre de la app—, cortamos acá en vez de seguir
                // armando un aggregate que vamos a tirar en el renglón siguiente.
                if (! I->life.isCurrent (gen)) { I->destroyChain(); return; }

                // 2) Formato REAL del tap (nada de asumir 48k estéreo).
                AudioObjectPropertyAddress fmtAddr { kAudioTapPropertyFormat,
                                                     kAudioObjectPropertyScopeGlobal,
                                                     kAudioObjectPropertyElementMain };
                AudioStreamBasicDescription asbd {};
                UInt32 fmtSize = sizeof (asbd);
                if (AudioObjectGetPropertyData (tapID, &fmtAddr, 0, nullptr, &fmtSize, &asbd) == noErr
                    && asbd.mSampleRate > 0.0)
                    I->tapSr = asbd.mSampleRate;
                // flags observadas en 14.2+: 0x9 = kAudioFormatFlagIsFloat (0x1) | …IsPacked (0x8).
                // NO trae kAudioFormatFlagIsNonInterleaved (0x20): el tap entrega float INTERLEAVED, así
                // que la rama que corre de verdad es la del de-interleave, no la planar. La planar queda
                // como fallback por contrato del HAL (mNumberBuffers>1), no porque sea "lo normal" acá.
                tapLog ("[sysaudio] tap format: %.0fHz %uch flags=0x%x (%s)\n",
                        I->tapSr, (unsigned) asbd.mChannelsPerFrame, (unsigned) asbd.mFormatFlags,
                        (asbd.mFormatFlags & kAudioFormatFlagIsNonInterleaved) ? "planar" : "interleaved");

                // 3) Aggregate PRIVADO con el tap + el output default como sub-device/main.
                NSString* outUID = defaultOutputDeviceUID();
                if (outUID == nil) { tapLog ("[sysaudio] sin output default\n");
                                     I->destroyChain(); I->setStatusIfCurrent (gen, SystemCaptureStatus::error); return; }

                NSString* aggUID = [NSString stringWithFormat:@"com.ovni.supernova.tap.%@", NSUUID.UUID.UUIDString];
                NSDictionary* aggDesc = @{
                    @kAudioAggregateDeviceNameKey:          @"SUPERNOVA System Audio",
                    @kAudioAggregateDeviceUIDKey:           aggUID,
                    @kAudioAggregateDeviceMainSubDeviceKey: outUID,
                    @kAudioAggregateDeviceIsPrivateKey:     @YES,
                    @kAudioAggregateDeviceIsStackedKey:     @NO,
                    @kAudioAggregateDeviceTapAutoStartKey:  @YES,
                    @kAudioAggregateDeviceSubDeviceListKey: @[ @{ @kAudioSubDeviceUIDKey: outUID } ],
                    @kAudioAggregateDeviceTapListKey:       @[ @{ @kAudioSubTapDriftCompensationKey: @YES,
                                                                 @kAudioSubTapUIDKey: desc.UUID.UUIDString } ],
                };

                AudioObjectID aggID = kAudioObjectUnknown;
                const OSStatus aggSt = AudioHardwareCreateAggregateDevice ((__bridge CFDictionaryRef) aggDesc, &aggID);
                tapLog ("[sysaudio] createAggregate st=%d agg=%u\n", (int) aggSt, (unsigned) aggID);
                if (aggSt != noErr || aggID == kAudioObjectUnknown) {
                    I->destroyChain(); I->setStatusIfCurrent (gen, SystemCaptureStatus::error); return; }
                I->aggID = aggID;

                // Scratch del de-interleave, dimensionado antes de que corra el IO (jamás realoca ahí).
                I->planar.assign ((size_t) kMaxCh * 8192u, 0.0f);

                // 4) IOProc: empuja TODOS los frames (no latest-wins).
                AudioDeviceIOProcID procID = nullptr;
                const OSStatus procSt = AudioDeviceCreateIOProcIDWithBlock (&procID, aggID, I->ioQ,
                    ^(const AudioTimeStamp*, const AudioBufferList* inData, const AudioTimeStamp*,
                      AudioBufferList*, const AudioTimeStamp*)
                    {
                        // Captura CRUDA del Impl a propósito: el bloque del IOProc lo destruye
                        // AudioDeviceDestroyIOProcID dentro de destroyChain(), que siempre corre antes
                        // de que muera el Impl (la tarea de limpieza se lo lleva por shared_ptr).
                        if (! I->life.isCurrent (gen) || ! I->cb || inData == nullptr) return;

                        const float* chans[kMaxCh] = {};
                        int nCh = 0, nFrames = 0;

                        const bool planarPath = inData->mNumberBuffers > 1
                                             || (inData->mNumberBuffers == 1 && inData->mBuffers[0].mNumberChannels == 1);
                        if (planarPath)
                        {
                            // Planar: un buffer por canal. FALLBACK — el tap de 14.2+ reporta 0x9, o sea
                            // interleaved (sin el flag 0x20), y entra por la rama de abajo.
                            nCh = juce::jmin ((int) inData->mNumberBuffers, kMaxCh);
                            nFrames = std::numeric_limits<int>::max();
                            for (int c = 0; c < nCh; ++c)
                            {
                                chans[c] = reinterpret_cast<const float*> (inData->mBuffers[c].mData);
                                nFrames  = juce::jmin (nFrames, (int) (inData->mBuffers[c].mDataByteSize / sizeof (float)));
                            }
                        }
                        else if (inData->mNumberBuffers == 1)
                        {
                            // Interleaved: de-interleave al scratch preasignado.
                            const auto& b = inData->mBuffers[0];
                            const float* inter = reinterpret_cast<const float*> (b.mData);
                            nCh = juce::jmin ((int) b.mNumberChannels, kMaxCh);
                            const int total = (int) (b.mDataByteSize / sizeof (float));
                            nFrames = nCh > 0 ? total / nCh : 0;
                            const int cap = (int) (I->planar.size() / (size_t) juce::jmax (1, nCh));
                            nFrames = juce::jmin (nFrames, cap);
                            for (int c = 0; c < nCh; ++c)
                            {
                                float* d = I->planar.data() + (size_t) c * (size_t) nFrames;
                                for (int f = 0; f < nFrames; ++f) d[f] = inter[f * nCh + c];
                                chans[c] = d;
                            }
                        }

                        if (nCh > 0 && nFrames > 0)
                        {
                            if (I->frameLogs.load() < 1)
                            {
                                I->frameLogs.fetch_add (1);
                                tapLog ("[sysaudio] audio fluyendo por TAP (%dch @%.0fHz, %s)\n",
                                        nCh, I->tapSr, planarPath ? "planar" : "de-interleave");
                            }
                            I->cb (chans, nCh, nFrames, I->tapSr);
                        }
                    });

                if (procSt != noErr || procID == nullptr) { tapLog ("[sysaudio] createIOProc FAILED st=%d\n", (int) procSt);
                                                            I->destroyChain();
                                                            I->setStatusIfCurrent (gen, SystemCaptureStatus::error); return; }
                I->procID = procID;

                const OSStatus startSt = AudioDeviceStart (aggID, procID);
                tapLog ("[sysaudio] deviceStart st=%d\n", (int) startSt);
                if (startSt != noErr) { I->destroyChain(); I->setStatusIfCurrent (gen, SystemCaptureStatus::error); return; }

                if (! I->life.isCurrent (gen)) { I->destroyChain(); return; }
                I->installDeviceListeners();
                I->status = SystemCaptureStatus::capturing;
                tapLog ("[sysaudio] CAPTURANDO por process tap (@%.0fHz) — permiso: audio del sistema\n", I->tapSr);
            }
            else
            {
                I->status = SystemCaptureStatus::unsupported;
            }
        }
    });   // false = ya había un setup en vuelo (el poll a 30Hz no apila carteles): tampoco es un error

    return true;
}

// -------------------------------------------------------------------------------------------- stop
// Orden EXACTO: IOProc (stop + destroy) → aggregate → tap. Al revés deja objetos del HAL colgados.
void SystemAudioTapSource::stop() noexcept
{
    if (! impl) return;

    // NO esperamos al setup en vuelo. Hasta 0.3.0 esto era un dispatch_sync contra la cola del setup, y
    // el setup puede estar bloqueado dentro de AudioHardwareCreateProcessTap con el cartel de TCC arriba:
    // cambiar de SOURCE o cerrar la app en ese momento congelaba el hilo de mensajes hasta que el usuario
    // contestara. TapLifecycle::stop invalida la generación y ENCOLA la limpieza detrás del setup; como la
    // cola es serial, se destruye exactamente lo que ese setup haya llegado a crear, y en el orden bueno.
    impl->stopAsync();
}

} // namespace supernova

#endif // JUCE_MAC
