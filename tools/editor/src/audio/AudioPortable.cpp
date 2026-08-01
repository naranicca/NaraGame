#include "Audio.h"

#include <NaraGame/audio/AudioClip.h>
#include <NaraGame/audio/AudioDevice.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>

#if defined(NARAGAME_PORTABLE_HAS_STB_VORBIS)
extern "C" int stb_vorbis_decode_memory(const unsigned char*, int,
    int*, int*, short**);
#endif
#if defined(NARAGAME_PORTABLE_HAS_MINIMP3)
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"
#include "minimp3_ex.h"
#endif

namespace SceneEditor {
namespace {
std::unique_ptr<NaraGame::AudioDevice> g_device;
std::unordered_map<std::string, NaraGame::AudioClip> g_clips;
float g_master = 1.0f;
float g_sfx = 1.0f;
float g_music = 1.0f;
float g_reverb = 0.0f;
Audio::VoiceHandle g_nextHandle = 1;

NaraGame::AudioDevice* Device() {
    if (!g_device) {
        g_device = NaraGame::AudioDevice::Create();
        g_device->SetMasterVolume(g_master);
    }
    return g_device.get();
}

std::string LowerExtension(const std::string& path) {
    const size_t dot = path.find_last_of('.');
    std::string ext = dot == std::string::npos ? std::string{} : path.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return ext;
}

NaraGame::AudioClip* LoadClip(const std::string& path) {
    auto found = g_clips.find(path);
    if (found != g_clips.end()) return &found->second;
    const std::string ext = LowerExtension(path);
    NaraGame::AudioClip clip;
    if (ext == "wav") {
        clip = NaraGame::LoadWav(path);
    } else {
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input) return nullptr;
        const std::streamsize size = input.tellg();
        if (size <= 0) return nullptr;
        input.seekg(0, std::ios::beg);
        std::vector<uint8_t> bytes(static_cast<size_t>(size));
        if (!input.read(reinterpret_cast<char*>(bytes.data()), size)) return nullptr;
#if defined(NARAGAME_PORTABLE_HAS_STB_VORBIS)
        if (ext == "ogg") {
            short* pcm = nullptr;
            int channels = 0, sampleRate = 0;
            const int samplesPerChannel = stb_vorbis_decode_memory(bytes.data(),
                static_cast<int>(bytes.size()), &channels, &sampleRate, &pcm);
            if (samplesPerChannel > 0 && pcm != nullptr) {
                clip.channels = static_cast<uint16_t>(channels);
                clip.sampleRate = static_cast<uint32_t>(sampleRate);
                clip.bitsPerSample = 16;
                const size_t byteCount = size_t(samplesPerChannel) * channels * 2;
                clip.data.resize(byteCount);
                std::memcpy(clip.data.data(), pcm, byteCount);
                std::free(pcm);
            }
        }
#endif
#if defined(NARAGAME_PORTABLE_HAS_MINIMP3)
        if (ext == "mp3") {
            mp3dec_t decoder{};
            mp3dec_file_info_t info{};
            if (mp3dec_load_buf(&decoder, bytes.data(), bytes.size(), &info,
                                nullptr, nullptr) == 0 && info.buffer != nullptr) {
                clip.channels = static_cast<uint16_t>(info.channels);
                clip.sampleRate = static_cast<uint32_t>(info.hz);
                clip.bitsPerSample = 16;
                const size_t byteCount = info.samples * sizeof(mp3d_sample_t);
                clip.data.resize(byteCount);
                std::memcpy(clip.data.data(), info.buffer, byteCount);
                std::free(info.buffer);
            }
        }
#endif
    }
    auto [it, inserted] = g_clips.emplace(path, std::move(clip));
    (void)inserted;
    return it->second.Valid() ? &it->second : nullptr;
}
}

bool Audio::Play(const std::string& path, float volume) {
    auto* clip = LoadClip(path);
    if (!clip) return false;
    Device()->PlaySound(*clip, std::clamp(volume, 0.0f, 1.0f) * g_sfx);
    return true;
}

bool Audio::PlayPositional(const std::string& path, float volume,
                           float, float) {
    return Play(path, volume);
}

bool Audio::PlayFromMemory(const std::string& cacheKey, const uint8_t* data,
                           size_t size, const std::string& extension,
                           float volume) {
    std::string ext = extension;
    if (!ext.empty() && ext.front() == '.') ext.erase(ext.begin());
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    auto found = g_clips.find(cacheKey);
    if (found == g_clips.end()) {
        NaraGame::AudioClip clip;
        if (ext == "wav") {
            clip = NaraGame::LoadWavMemory(data, size, cacheKey);
        }
#if defined(NARAGAME_PORTABLE_HAS_STB_VORBIS)
        else if (ext == "ogg") {
            short* pcm = nullptr;
            int channels = 0, sampleRate = 0;
            const int samplesPerChannel = stb_vorbis_decode_memory(data,
                static_cast<int>(size), &channels, &sampleRate, &pcm);
            if (samplesPerChannel > 0 && pcm != nullptr) {
                clip.channels = static_cast<uint16_t>(channels);
                clip.sampleRate = static_cast<uint32_t>(sampleRate);
                clip.bitsPerSample = 16;
                const size_t byteCount = size_t(samplesPerChannel) * channels * 2;
                clip.data.resize(byteCount);
                std::memcpy(clip.data.data(), pcm, byteCount);
                std::free(pcm);
            }
        }
#endif
#if defined(NARAGAME_PORTABLE_HAS_MINIMP3)
        else if (ext == "mp3") {
            mp3dec_t decoder{};
            mp3dec_file_info_t info{};
            if (mp3dec_load_buf(&decoder, data, size, &info, nullptr, nullptr) == 0 &&
                info.buffer != nullptr) {
                clip.channels = static_cast<uint16_t>(info.channels);
                clip.sampleRate = static_cast<uint32_t>(info.hz);
                clip.bitsPerSample = 16;
                const size_t byteCount = info.samples * sizeof(mp3d_sample_t);
                clip.data.resize(byteCount);
                std::memcpy(clip.data.data(), info.buffer, byteCount);
                std::free(info.buffer);
            }
        }
#endif
        if (!clip.Valid()) return false;
        found = g_clips.emplace(cacheKey, std::move(clip)).first;
    }
    Device()->PlaySound(found->second,
        std::clamp(volume, 0.0f, 1.0f) * g_sfx);
    return true;
}

void Audio::Stop() {
    g_device.reset();
}

bool Audio::PlayMusic(const std::string& path, float volume, bool loop) {
    auto* clip = LoadClip(path);
    if (!clip) return false;
    Device()->PlayMusic(*clip,
        std::clamp(volume, 0.0f, 1.0f) * g_music, loop);
    return true;
}

void Audio::StopMusic() { if (g_device) g_device->StopMusic(); }
void Audio::UpdateMusic() { if (g_device) g_device->Update(); }

void Audio::SetBusVolume(Bus bus, float volume) {
    volume = std::clamp(volume, 0.0f, 1.0f);
    if (bus == Bus::Master) {
        g_master = volume;
        if (g_device) g_device->SetMasterVolume(volume);
    } else if (bus == Bus::Sfx) {
        g_sfx = volume;
    } else {
        g_music = volume;
    }
}
float Audio::GetBusVolume(Bus bus) {
    if (bus == Bus::Master) return g_master;
    return bus == Bus::Sfx ? g_sfx : g_music;
}
void Audio::SetReverbWet(float wet) { g_reverb = std::clamp(wet, 0.0f, 1.0f); }
float Audio::GetReverbWet() { return g_reverb; }

Audio::VoiceHandle Audio::Spawn(const std::string& path, float volume) {
    if (!Play(path, volume)) return 0;
    if (++g_nextHandle == 0) ++g_nextHandle;
    return g_nextHandle;
}
bool Audio::StopVoice(VoiceHandle) { return false; }
bool Audio::PauseVoice(VoiceHandle) { return false; }
bool Audio::ResumeVoice(VoiceHandle) { return false; }
bool Audio::SetVoiceVolume(VoiceHandle, float) { return false; }
bool Audio::IsVoicePlaying(VoiceHandle) { return false; }

void Audio::Shutdown() {
    g_device.reset();
    g_clips.clear();
}

} // namespace SceneEditor
