#include "audio/xaudio2/XAudio2Device.h"

#if defined(NARA_PLATFORM_WINDOWS)

#include "nara/core/Log.h"

#include <objbase.h>
#include <xaudio2.h>

namespace nara {

XAudio2Device::XAudio2Device() {
    const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    m_comInitialized = SUCCEEDED(co); // S_FALSE if already initialized on this thread

    HRESULT hr = XAudio2Create(&m_xaudio, 0, XAUDIO2_DEFAULT_PROCESSOR);
    if (FAILED(hr)) {
        NARA_ERROR("XAudio2Create failed (0x%08lX)", static_cast<unsigned long>(hr));
        return;
    }
    hr = m_xaudio->CreateMasteringVoice(&m_master);
    if (FAILED(hr)) {
        NARA_ERROR("CreateMasteringVoice failed (0x%08lX)", static_cast<unsigned long>(hr));
        m_xaudio->Release();
        m_xaudio = nullptr;
        return;
    }
    NARA_INFO("XAudio2 audio device created");
}

XAudio2Device::~XAudio2Device() {
    StopMusic();
    for (IXAudio2SourceVoice* voice : m_active) {
        voice->Stop(0);
        voice->DestroyVoice();
    }
    m_active.clear();
    if (m_master)
        m_master->DestroyVoice();
    if (m_xaudio)
        m_xaudio->Release();
    if (m_comInitialized)
        CoUninitialize();
}

IXAudio2SourceVoice* XAudio2Device::CreateVoice(const AudioClip& clip, bool loop) {
    if (!Valid() || !clip.Valid())
        return nullptr;

    WAVEFORMATEX wfx{};
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = clip.channels;
    wfx.nSamplesPerSec = clip.sampleRate;
    wfx.wBitsPerSample = clip.bitsPerSample;
    wfx.nBlockAlign = static_cast<WORD>(clip.channels * clip.bitsPerSample / 8);
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

    IXAudio2SourceVoice* voice = nullptr;
    if (FAILED(m_xaudio->CreateSourceVoice(&voice, &wfx)))
        return nullptr;

    XAUDIO2_BUFFER buffer{};
    buffer.AudioBytes = static_cast<UINT32>(clip.data.size());
    buffer.pAudioData = clip.data.data();
    buffer.Flags = XAUDIO2_END_OF_STREAM;
    buffer.LoopCount = loop ? XAUDIO2_LOOP_INFINITE : 0;
    if (FAILED(voice->SubmitSourceBuffer(&buffer))) {
        voice->DestroyVoice();
        return nullptr;
    }
    return voice;
}

void XAudio2Device::PlaySound(const AudioClip& clip, float volume) {
    Update(); // reclaim finished voices first
    IXAudio2SourceVoice* voice = CreateVoice(clip, false);
    if (voice == nullptr)
        return;
    voice->SetVolume(volume);
    voice->Start(0);
    m_active.push_back(voice);
}

void XAudio2Device::PlayMusic(const AudioClip& clip, float volume, bool loop) {
    StopMusic();
    m_music = CreateVoice(clip, loop);
    if (m_music != nullptr) {
        m_music->SetVolume(volume);
        m_music->Start(0);
    }
}

void XAudio2Device::StopMusic() {
    if (m_music != nullptr) {
        m_music->Stop(0);
        m_music->DestroyVoice();
        m_music = nullptr;
    }
}

void XAudio2Device::SetMasterVolume(float volume) {
    if (m_master)
        m_master->SetVolume(volume);
}

void XAudio2Device::Update() {
    for (size_t i = 0; i < m_active.size();) {
        XAUDIO2_VOICE_STATE state{};
        m_active[i]->GetState(&state);
        if (state.BuffersQueued == 0) { // finished playing
            m_active[i]->DestroyVoice();
            m_active[i] = m_active.back();
            m_active.pop_back();
        } else {
            ++i;
        }
    }
}

} // namespace nara

#endif // NARA_PLATFORM_WINDOWS
