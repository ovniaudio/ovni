// SystemAudioSource — impl WINDOWS (M5): captura del audio del SISTEMA por WASAPI LOOPBACK, SIN drivers ni
// permisos (a diferencia de mac, Windows no pide TCC). Capturamos la salida del dispositivo de RENDER por
// defecto (loopback) → la app reacciona a cualquier cosa que suene (Spotify/YouTube/DAW), cero setup.
// Compila SOLO en Windows (guard). Patrón canónico: enumerar el endpoint de render → IAudioClient en modo
// shared + AUDCLNT_STREAMFLAGS_LOOPBACK → IAudioCaptureClient → hilo que hace polling GetBuffer/ReleaseBuffer,
// deinterleava a Float32 planar y entrega TODOS los frames (no latest-wins → sin cortes). El FIFO del
// processor absorbe el ritmo. Sin GPU/Windows local esto es WRITE-ONLY (lo valida CI windows-latest / smoke).
#if JUCE_WINDOWS
#include "SystemAudioSource.h"

#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>
#include <atomic>
#include <thread>
#include <vector>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "avrt.lib")

namespace supernova {

namespace {
constexpr int kMaxChannels = 8;
constexpr REFERENCE_TIME kBufferDuration = 2000000; // 200 ms de buffer WASAPI (unidades de 100ns)

// Un sample del mix format (interleaved) → Float32.
inline float sampleToFloat (const BYTE* p, WORD bits, bool isFloat) noexcept
{
    if (isFloat) return *reinterpret_cast<const float*> (p);
    switch (bits)
    {
        case 16: return (float) *reinterpret_cast<const int16_t*> (p) / 32768.0f;
        case 32: return (float) *reinterpret_cast<const int32_t*> (p) / 2147483648.0f;
        case 24: {
            int32_t v = (p[0]) | (p[1] << 8) | (p[2] << 16);
            if (v & 0x800000) v |= ~0xFFFFFF;   // sign-extend 24→32
            return (float) v / 8388608.0f;
        }
        default: return 0.0f;
    }
}
} // namespace

struct SystemAudioSource::Impl
{
    IMMDeviceEnumerator* enumr   = nullptr;
    IMMDevice*           device  = nullptr;
    IAudioClient*        client  = nullptr;
    IAudioCaptureClient* capture = nullptr;
    WAVEFORMATEX*        mixFmt  = nullptr;

    std::thread          worker;
    std::atomic<bool>    running { false };
    SampleCallback       cb;
    Status               status  = Status::idle;
    bool                 comInit = false;

    std::vector<std::vector<float>> planar;      // scratch planar por canal (reusable)
    std::vector<float*>             planarPtrs;

    ~Impl() { cleanup(); }

    void cleanup() noexcept
    {
        if (capture) { capture->Release(); capture = nullptr; }
        if (client)  { client->Release();  client  = nullptr; }
        if (mixFmt)  { CoTaskMemFree (mixFmt); mixFmt = nullptr; }
        if (device)  { device->Release(); device = nullptr; }
        if (enumr)   { enumr->Release();  enumr  = nullptr; }
        if (comInit) { CoUninitialize(); comInit = false; }
    }

    void run() noexcept
    {
        DWORD  taskIdx = 0;
        HANDLE mmcss = AvSetMmThreadCharacteristicsW (L"Pro Audio", &taskIdx);  // prioridad de audio pro

        const WORD   ch    = mixFmt->nChannels;
        const WORD   bits  = mixFmt->wBitsPerSample;
        const double sr    = (double) mixFmt->nSamplesPerSec;
        const WORD   block = mixFmt->nBlockAlign;
        bool isFloat = (mixFmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT);
        if (mixFmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
        {
            auto* ext = reinterpret_cast<WAVEFORMATEXTENSIBLE*> (mixFmt);
            isFloat = (ext->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
        }
        const int useCh = ch > kMaxChannels ? kMaxChannels : (int) ch;

        client->Start();
        while (running.load (std::memory_order_relaxed))
        {
            UINT32 packet = 0;
            if (FAILED (capture->GetNextPacketSize (&packet))) break;
            if (packet == 0) { Sleep (5); continue; }   // polling suave (loopback no señaliza eventos fiables)

            while (packet != 0 && running.load (std::memory_order_relaxed))
            {
                BYTE*  data   = nullptr;
                UINT32 frames = 0;
                DWORD  flags  = 0;
                if (FAILED (capture->GetBuffer (&data, &frames, &flags, nullptr, nullptr))) break;

                if (frames > 0 && cb)
                {
                    if ((int) planar.size() < useCh) { planar.resize (useCh); planarPtrs.resize (useCh); }
                    for (int c = 0; c < useCh; ++c)
                    {
                        if (planar[c].size() < frames) planar[c].resize (frames);
                        planarPtrs[c] = planar[c].data();
                    }
                    const bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
                    for (UINT32 f = 0; f < frames; ++f)
                        for (int c = 0; c < useCh; ++c)
                            planar[c][f] = silent ? 0.0f
                                : sampleToFloat (data + f * block + c * (bits / 8), bits, isFloat);
                    cb (planarPtrs.data(), useCh, (int) frames, sr);
                }
                capture->ReleaseBuffer (frames);
                if (FAILED (capture->GetNextPacketSize (&packet))) { packet = 0; break; }
            }
        }
        client->Stop();
        if (mmcss) AvRevertMmThreadCharacteristics (mmcss);
    }
};

SystemAudioSource::SystemAudioSource() : impl (std::make_unique<Impl>()) {}
SystemAudioSource::~SystemAudioSource() { stop(); }

bool SystemAudioSource::start (int /*sampleRate*/, int /*channels*/, SampleCallback onSamples) noexcept
{
    stop();
    impl->cb = std::move (onSamples);

    if (SUCCEEDED (CoInitializeEx (nullptr, COINIT_MULTITHREADED))) impl->comInit = true;

    if (FAILED (CoCreateInstance (__uuidof (MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  __uuidof (IMMDeviceEnumerator), (void**) &impl->enumr)))
    { impl->status = Status::error; impl->cleanup(); return false; }

    // Endpoint de RENDER por defecto → su salida es lo que capturamos (loopback).
    if (FAILED (impl->enumr->GetDefaultAudioEndpoint (eRender, eConsole, &impl->device)))
    { impl->status = Status::error; impl->cleanup(); return false; }

    if (FAILED (impl->device->Activate (__uuidof (IAudioClient), CLSCTX_ALL, nullptr, (void**) &impl->client)))
    { impl->status = Status::error; impl->cleanup(); return false; }

    if (FAILED (impl->client->GetMixFormat (&impl->mixFmt)) || impl->mixFmt == nullptr)
    { impl->status = Status::error; impl->cleanup(); return false; }

    if (FAILED (impl->client->Initialize (AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK,
                                          kBufferDuration, 0, impl->mixFmt, nullptr)))
    { impl->status = Status::error; impl->cleanup(); return false; }

    if (FAILED (impl->client->GetService (__uuidof (IAudioCaptureClient), (void**) &impl->capture)))
    { impl->status = Status::error; impl->cleanup(); return false; }

    impl->running.store (true);
    impl->status = Status::capturing;
    impl->worker = std::thread ([this] { impl->run(); });
    return true;
}

void SystemAudioSource::stop() noexcept
{
    if (impl->running.exchange (false))
        if (impl->worker.joinable()) impl->worker.join();
    impl->cleanup();
    impl->status = Status::idle;
}

SystemAudioSource::Status SystemAudioSource::status() const noexcept { return impl->status; }
bool SystemAudioSource::isSupported() noexcept   { return true; }   // WASAPI loopback en todo Windows moderno
bool SystemAudioSource::hasPermission() noexcept { return true; }   // loopback no requiere permiso (a dif. de mac)

} // namespace supernova
#endif
