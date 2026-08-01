#include "audio/opensl/OpenSLAudioDevice.h"

#if defined(NARAGAME_PLATFORM_ANDROID)

#include "NaraGame/core/Log.h"

#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>

#include <algorithm>
#include <atomic>
#include <cmath>

namespace NaraGame {

struct OpenSLAudioDevice::Voice {
    SLObjectItf object = nullptr;
    SLPlayItf play = nullptr;
    SLAndroidSimpleBufferQueueItf queue = nullptr;
    SLVolumeItf volume = nullptr;
    AudioClip clip;
    std::atomic<bool> finished{false};
    bool loop = false;

    ~Voice() {
        if (object != nullptr) {
            (*object)->Destroy(object);
            object = nullptr;
        }
    }
};

namespace {
void BufferComplete(SLAndroidSimpleBufferQueueItf queue, void* context) {
    auto* voice = static_cast<OpenSLAudioDevice::Voice*>(context);
    if (voice == nullptr) return;
    if (voice->loop && !voice->clip.data.empty()) {
        (*queue)->Enqueue(queue, voice->clip.data.data(),
                          static_cast<SLuint32>(voice->clip.data.size()));
    } else {
        voice->finished.store(true, std::memory_order_release);
    }
}

SLmillibel ToMillibels(float volume) {
    if (volume <= 0.0001f) return SL_MILLIBEL_MIN;
    const float db = 20.0f * std::log10(std::clamp(volume, 0.0f, 1.0f));
    return static_cast<SLmillibel>(db * 100.0f);
}
}

OpenSLAudioDevice::OpenSLAudioDevice() {
    SLObjectItf engineObject = nullptr;
    if (slCreateEngine(&engineObject, 0, nullptr, 0, nullptr, nullptr) !=
        SL_RESULT_SUCCESS) return;
    if ((*engineObject)->Realize(engineObject, SL_BOOLEAN_FALSE) !=
        SL_RESULT_SUCCESS) {
        (*engineObject)->Destroy(engineObject); return;
    }
    SLEngineItf engine = nullptr;
    if ((*engineObject)->GetInterface(engineObject, SL_IID_ENGINE, &engine) !=
        SL_RESULT_SUCCESS) {
        (*engineObject)->Destroy(engineObject); return;
    }
    SLObjectItf outputMix = nullptr;
    if ((*engine)->CreateOutputMix(engine, &outputMix, 0, nullptr, nullptr) !=
            SL_RESULT_SUCCESS ||
        (*outputMix)->Realize(outputMix, SL_BOOLEAN_FALSE) != SL_RESULT_SUCCESS) {
        if (outputMix) (*outputMix)->Destroy(outputMix);
        (*engineObject)->Destroy(engineObject); return;
    }
    m_engineObject = engineObject;
    m_engine = engine;
    m_outputMix = outputMix;
    NARAGAME_INFO("OpenSL ES audio device created");
}

OpenSLAudioDevice::~OpenSLAudioDevice() {
    m_music.reset();
    m_active.clear();
    auto outputMix = m_outputMix;
    if (outputMix) (*outputMix)->Destroy(outputMix);
    auto engineObject = m_engineObject;
    if (engineObject) (*engineObject)->Destroy(engineObject);
}

std::unique_ptr<OpenSLAudioDevice::Voice> OpenSLAudioDevice::CreateVoice(
        const AudioClip& source, float volume, bool loop) {
    if (!Valid() || !source.Valid() ||
        (source.bitsPerSample != 8 && source.bitsPerSample != 16) ||
        (source.channels != 1 && source.channels != 2)) return nullptr;

    auto voice = std::make_unique<Voice>();
    voice->clip = source;
    voice->loop = loop;
    SLDataLocator_AndroidSimpleBufferQueue locator{
        SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 1};
    const SLuint32 mask = source.channels == 1
        ? SL_SPEAKER_FRONT_CENTER
        : (SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT);
    SLDataFormat_PCM format{SL_DATAFORMAT_PCM, source.channels,
        source.sampleRate * 1000u, source.bitsPerSample, source.bitsPerSample,
        mask, SL_BYTEORDER_LITTLEENDIAN};
    SLDataSource dataSource{&locator, &format};
    SLDataLocator_OutputMix outputLocator{SL_DATALOCATOR_OUTPUTMIX,
        m_outputMix};
    SLDataSink dataSink{&outputLocator, nullptr};
    const SLInterfaceID ids[] = {SL_IID_BUFFERQUEUE, SL_IID_VOLUME};
    const SLboolean required[] = {SL_BOOLEAN_TRUE, SL_BOOLEAN_TRUE};
    auto engine = m_engine;
    if ((*engine)->CreateAudioPlayer(engine, &voice->object, &dataSource,
            &dataSink, 2, ids, required) != SL_RESULT_SUCCESS ||
        (*voice->object)->Realize(voice->object, SL_BOOLEAN_FALSE) !=
            SL_RESULT_SUCCESS ||
        (*voice->object)->GetInterface(voice->object, SL_IID_PLAY,
            &voice->play) != SL_RESULT_SUCCESS ||
        (*voice->object)->GetInterface(voice->object, SL_IID_BUFFERQUEUE,
            &voice->queue) != SL_RESULT_SUCCESS ||
        (*voice->object)->GetInterface(voice->object, SL_IID_VOLUME,
            &voice->volume) != SL_RESULT_SUCCESS) return nullptr;

    (*voice->queue)->RegisterCallback(voice->queue, BufferComplete, voice.get());
    (*voice->volume)->SetVolumeLevel(voice->volume,
        ToMillibels(volume * m_masterVolume));
    (*voice->queue)->Enqueue(voice->queue, voice->clip.data.data(),
        static_cast<SLuint32>(voice->clip.data.size()));
    (*voice->play)->SetPlayState(voice->play, SL_PLAYSTATE_PLAYING);
    return voice;
}

void OpenSLAudioDevice::PlaySound(const AudioClip& clip, float volume) {
    Update();
    if (auto voice = CreateVoice(clip, volume, false))
        m_active.push_back(std::move(voice));
}
void OpenSLAudioDevice::PlayMusic(const AudioClip& clip, float volume, bool loop) {
    StopMusic();
    m_music = CreateVoice(clip, volume, loop);
}
void OpenSLAudioDevice::StopMusic() { m_music.reset(); }
void OpenSLAudioDevice::SetMasterVolume(float volume) {
    m_masterVolume = std::clamp(volume, 0.0f, 1.0f);
}
void OpenSLAudioDevice::Update() {
    std::erase_if(m_active, [](const auto& voice) {
        return voice->finished.load(std::memory_order_acquire);
    });
    if (m_music && m_music->finished.load(std::memory_order_acquire))
        m_music.reset();
}

} // namespace NaraGame

#endif
