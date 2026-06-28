#include "Audio.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <xaudio2.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

// M8c3 vendored decoders -- only included when CMake sees
// the corresponding header / source in third_party/audio/.
#if defined(NARAGAME_HAS_STB_VORBIS) && NARAGAME_HAS_STB_VORBIS
extern "C" int stb_vorbis_decode_filename(
    const char* filename, int* channels, int* sample_rate,
    short** output);
#endif
#if defined(NARAGAME_HAS_MINIMP3) && NARAGAME_HAS_MINIMP3
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"
// Don't define MINIMP3_NO_STDIO -- the file-IO helpers
// (`mp3dec_load`) live behind `#ifndef MINIMP3_NO_STDIO` in
// minimp3_ex.h, so defining the macro even with value 0
// would still trip `#ifndef` + drop the impl.
#include "minimp3_ex.h"
#endif

namespace SceneEditor {

namespace {

struct SoundClip {
    std::vector<uint8_t> data;
    WAVEFORMATEX         format{};
};

// XAudio2 calls OnBufferEnd from its audio thread. Destroying
// the voice from there is supported -- the documentation says
// "May not be called recursively, but may overlap with main
// thread", which matches what we need. The voice pointer is
// passed in pBufferContext from XAUDIO2_BUFFER.pContext.
struct VoiceCallback : public IXAudio2VoiceCallback {
    void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32) override {}
    void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() override {}
    void STDMETHODCALLTYPE OnStreamEnd() override {}
    void STDMETHODCALLTYPE OnBufferStart(void*) override {}
    void STDMETHODCALLTYPE OnBufferEnd(void* pBufferContext) override {
        auto* v = reinterpret_cast<IXAudio2SourceVoice*>(pBufferContext);
        if (v) v->DestroyVoice();
    }
    void STDMETHODCALLTYPE OnLoopEnd(void*) override {}
    void STDMETHODCALLTYPE OnVoiceError(void*, HRESULT) override {}
};

// All static state lives in this anonymous namespace --
// Initialize() is called lazily from Play().
IXAudio2*                                  g_engine  = nullptr;
IXAudio2MasteringVoice*                    g_master  = nullptr;
VoiceCallback                              g_callback;
std::unordered_map<std::string, SoundClip> g_cache;
std::mutex                                 g_cacheMutex;
bool                                       g_initFailed = false;

bool Initialize() {
    if (g_engine) return true;
    if (g_initFailed) return false;
    // COINIT_MULTITHREADED matches XAudio2's threading model.
    // If COM is already initialized in a different mode the
    // call returns RPC_E_CHANGED_MODE; that's harmless.
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    HRESULT hr = XAudio2Create(&g_engine, 0,
                                XAUDIO2_DEFAULT_PROCESSOR);
    if (FAILED(hr) || !g_engine) {
        g_initFailed = true;
        return false;
    }
    hr = g_engine->CreateMasteringVoice(&g_master);
    if (FAILED(hr) || !g_master) {
        g_engine->Release();
        g_engine = nullptr;
        g_initFailed = true;
        return false;
    }
    return true;
}

// Helper to fill the SoundClip's WAVEFORMATEX from a
// channels + sample_rate pair (16-bit PCM). All decoders
// produce 16-bit PCM so this format header serves OGG /
// MP3 / FLAC etc the same way it serves WAV.
void FillPcmFormat(SoundClip& out, int channels, int sampleRate) {
    out.format.wFormatTag      = WAVE_FORMAT_PCM;
    out.format.nChannels       = WORD(channels);
    out.format.nSamplesPerSec  = DWORD(sampleRate);
    out.format.wBitsPerSample  = 16;
    out.format.nBlockAlign     = WORD(channels * 2);
    out.format.nAvgBytesPerSec =
        DWORD(sampleRate * channels * 2);
    out.format.cbSize          = 0;
}

// Minimum-viable WAV RIFF parser. Handles PCM `fmt ` +
// `data` chunks; skips JUNK / LIST / cue / etc. Returns
// false on malformed input or non-PCM compression.
bool LoadWav(const std::string& path, SoundClip& out) {
    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "rb") != 0 || !f) return false;
    char riff[12];
    if (fread(riff, 1, 12, f) != 12 ||
        std::memcmp(riff,     "RIFF", 4) != 0 ||
        std::memcmp(riff + 8, "WAVE", 4) != 0) {
        fclose(f); return false;
    }
    bool fmtRead = false;
    bool dataRead = false;
    while (!feof(f) && (!fmtRead || !dataRead)) {
        char hdr[8];
        if (fread(hdr, 1, 8, f) != 8) break;
        uint32_t size = 0;
        std::memcpy(&size, hdr + 4, 4);
        if (std::memcmp(hdr, "fmt ", 4) == 0) {
            if (size < 16) { fclose(f); return false; }
            std::vector<uint8_t> buf(size);
            if (fread(buf.data(), 1, size, f) != size) {
                fclose(f); return false;
            }
            const size_t copySize =
                std::min<size_t>(size, sizeof(WAVEFORMATEX));
            std::memcpy(&out.format, buf.data(), copySize);
            // Only PCM supported in this MVP; reject anything
            // else (IMA ADPCM, A-law etc) so callers know.
            if (out.format.wFormatTag != WAVE_FORMAT_PCM &&
                out.format.wFormatTag != WAVE_FORMAT_EXTENSIBLE) {
                fclose(f); return false;
            }
            fmtRead = true;
        } else if (std::memcmp(hdr, "data", 4) == 0) {
            out.data.resize(size);
            if (fread(out.data.data(), 1, size, f) != size) {
                fclose(f); return false;
            }
            dataRead = true;
        } else {
            // Skip JUNK / LIST / etc. WAV chunks are word-
            // aligned -- pad odd sizes by one byte.
            if (size & 1) ++size;
            fseek(f, size, SEEK_CUR);
        }
    }
    fclose(f);
    return fmtRead && dataRead;
}

#if defined(NARAGAME_HAS_STB_VORBIS) && NARAGAME_HAS_STB_VORBIS
// OGG Vorbis loader -- stb_vorbis decodes the entire file to
// int16 PCM in one shot. For long music tracks a streaming
// path would be friendlier; M8c3 buys SFX support first +
// leaves streaming for a follow-up.
bool LoadOgg(const std::string& path, SoundClip& out) {
    short* samples   = nullptr;
    int    channels  = 0;
    int    sampleRate = 0;
    const int sampleCount = stb_vorbis_decode_filename(
        path.c_str(), &channels, &sampleRate, &samples);
    if (sampleCount < 0 || !samples || channels <= 0 ||
        sampleRate <= 0) {
        if (samples) std::free(samples);
        return false;
    }
    const size_t bytes = size_t(sampleCount) *
                          size_t(channels) * 2;
    out.data.assign(
        reinterpret_cast<const uint8_t*>(samples),
        reinterpret_cast<const uint8_t*>(samples) + bytes);
    std::free(samples);
    FillPcmFormat(out, channels, sampleRate);
    return true;
}
#endif

#if defined(NARAGAME_HAS_MINIMP3) && NARAGAME_HAS_MINIMP3
// MP3 loader -- minimp3_ex's mp3dec_load reads + decodes the
// whole file in one call. Returns 0 on success; non-zero
// codes mean missing / malformed / unsupported.
bool LoadMp3(const std::string& path, SoundClip& out) {
    mp3dec_t            dec;
    mp3dec_file_info_t  info{};
    mp3dec_init(&dec);
    if (mp3dec_load(&dec, path.c_str(), &info, nullptr,
                     nullptr) != 0 ||
        !info.buffer || info.samples == 0 ||
        info.channels <= 0 || info.hz <= 0) {
        if (info.buffer) std::free(info.buffer);
        return false;
    }
    const size_t bytes = info.samples * sizeof(mp3d_sample_t);
    out.data.assign(
        reinterpret_cast<const uint8_t*>(info.buffer),
        reinterpret_cast<const uint8_t*>(info.buffer) + bytes);
    std::free(info.buffer);
    FillPcmFormat(out, info.channels, info.hz);
    return true;
}
#endif

// Dispatch the right loader by file extension. Falls back
// to `false` when the requested format wasn't vendored.
bool LoadSound(const std::string& path, SoundClip& out) {
    std::string lower = path;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                    [](unsigned char c) {
                        return std::tolower(c);
                    });
    auto endsWith = [&](const char* suffix) {
        const size_t n = std::strlen(suffix);
        if (lower.size() < n) return false;
        return std::memcmp(lower.data() + lower.size() - n,
                            suffix, n) == 0;
    };
    if (endsWith(".wav")) return LoadWav(path, out);
#if defined(NARAGAME_HAS_STB_VORBIS) && NARAGAME_HAS_STB_VORBIS
    if (endsWith(".ogg")) return LoadOgg(path, out);
#endif
#if defined(NARAGAME_HAS_MINIMP3) && NARAGAME_HAS_MINIMP3
    if (endsWith(".mp3")) return LoadMp3(path, out);
#endif
    return false;
}

} // namespace

bool Audio::Play(const std::string& absolutePath, float volume) {
    if (absolutePath.empty()) return false;
    if (!Initialize()) return false;
    SoundClip* clip = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_cacheMutex);
        auto it = g_cache.find(absolutePath);
        if (it == g_cache.end()) {
            SoundClip fresh;
            if (!LoadSound(absolutePath, fresh)) return false;
            it = g_cache.emplace(absolutePath,
                                  std::move(fresh)).first;
        }
        clip = &it->second;
    }
    IXAudio2SourceVoice* voice = nullptr;
    HRESULT hr = g_engine->CreateSourceVoice(
        &voice, &clip->format, 0,
        XAUDIO2_DEFAULT_FREQ_RATIO,
        &g_callback, nullptr, nullptr);
    if (FAILED(hr) || !voice) return false;
    // Clamp + apply volume. XAudio2's volume range is
    // [-XAUDIO2_MAX_VOLUME_LEVEL, XAUDIO2_MAX_VOLUME_LEVEL]
    // but we only expose [0, 1] in the Lua surface.
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;
    voice->SetVolume(volume);
    XAUDIO2_BUFFER buffer{};
    buffer.AudioBytes = UINT32(clip->data.size());
    buffer.pAudioData = clip->data.data();
    buffer.Flags      = XAUDIO2_END_OF_STREAM;
    // pContext rides into OnBufferEnd so the callback can
    // free the voice when it's done.
    buffer.pContext   = voice;
    hr = voice->SubmitSourceBuffer(&buffer);
    if (FAILED(hr)) { voice->DestroyVoice(); return false; }
    hr = voice->Start(0);
    if (FAILED(hr)) { voice->DestroyVoice(); return false; }
    return true;
}

void Audio::Stop() {
    if (!g_engine) return;
    // XAudio2 doesn't expose a "stop all source voices"
    // call directly; the master voice's StopEngine is the
    // hammer + matches the M8c1 PlaySound(nullptr) semantics.
    g_engine->StopEngine();
    g_engine->StartEngine();
}

void Audio::Shutdown() {
    if (g_master) {
        g_master->DestroyVoice();
        g_master = nullptr;
    }
    if (g_engine) {
        g_engine->Release();
        g_engine = nullptr;
    }
    {
        std::lock_guard<std::mutex> lock(g_cacheMutex);
        g_cache.clear();
    }
    g_initFailed = false;
}

} // namespace SceneEditor
