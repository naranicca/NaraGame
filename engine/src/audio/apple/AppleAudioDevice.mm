#include "audio/apple/AppleAudioDevice.h"

#if defined(NARAGAME_PLATFORM_MACOS) || defined(NARAGAME_PLATFORM_IOS)

#include "NaraGame/core/Log.h"

#import <AVFoundation/AVFoundation.h>
#import <TargetConditionals.h>

#include <algorithm>
#include <atomic>

@interface NaraGameAudioToken : NSObject
@property(atomic, assign) BOOL finished;
@end
@implementation NaraGameAudioToken
@end

namespace NaraGame {

struct AppleAudioDevice::ActiveVoice {
    void* node = nullptr;
    void* buffer = nullptr;
    void* token = nullptr;
    ~ActiveVoice() {
        (void)(__bridge_transfer NaraGameAudioToken*)token;
        (void)(__bridge_transfer AVAudioPCMBuffer*)buffer;
        (void)(__bridge_transfer AVAudioPlayerNode*)node;
    }
};

AppleAudioDevice::AppleAudioDevice() {
    @autoreleasepool {
#if TARGET_OS_IPHONE
        AVAudioSession* session = AVAudioSession.sharedInstance;
        NSError* sessionError = nil;
        [session setCategory:AVAudioSessionCategoryAmbient error:&sessionError];
        [session setActive:YES error:&sessionError];
#endif
        AVAudioEngine* engine = [AVAudioEngine new];
        NSError* error = nil;
        if (![engine startAndReturnError:&error]) {
            NARAGAME_ERROR("AVAudioEngine start failed: %s",
                error.localizedDescription.UTF8String);
            return;
        }
        m_engine = (__bridge_retained void*)engine;
        NARAGAME_INFO("AVAudioEngine audio device created");
    }
}

AppleAudioDevice::~AppleAudioDevice() {
    StopMusic();
    for (auto& voice : m_active) DestroyVoice(voice);
    m_active.clear();
    AVAudioEngine* engine = (__bridge id)m_engine;
    [engine stop];
    (void)(__bridge_transfer AVAudioEngine*)m_engine;
}

bool AppleAudioDevice::Valid() const {
    return m_engine != nullptr && [(__bridge AVAudioEngine*)m_engine isRunning];
}

std::unique_ptr<AppleAudioDevice::ActiveVoice> AppleAudioDevice::CreateVoice(
        const AudioClip& clip, float volume, bool loop) {
    if (!Valid() || !clip.Valid() ||
        (clip.bitsPerSample != 8 && clip.bitsPerSample != 16) ||
        (clip.channels != 1 && clip.channels != 2)) return nullptr;

    const uint32_t bytesPerSample = clip.bitsPerSample / 8u;
    const AVAudioFrameCount frames = static_cast<AVAudioFrameCount>(
        clip.data.size() / (clip.channels * bytesPerSample));
    AVAudioFormat* format = [[AVAudioFormat alloc]
        initStandardFormatWithSampleRate:clip.sampleRate channels:clip.channels];
    AVAudioPCMBuffer* buffer = [[AVAudioPCMBuffer alloc]
        initWithPCMFormat:format frameCapacity:frames];
    if (buffer == nil || buffer.floatChannelData == nullptr) return nullptr;
    buffer.frameLength = frames;
    for (uint32_t channel = 0; channel < clip.channels; ++channel) {
        float* destination = buffer.floatChannelData[channel];
        for (AVAudioFrameCount frame = 0; frame < frames; ++frame) {
            const size_t sample = size_t(frame) * clip.channels + channel;
            if (clip.bitsPerSample == 16) {
                const size_t byte = sample * 2;
                const int16_t value = static_cast<int16_t>(
                    uint16_t(clip.data[byte]) | (uint16_t(clip.data[byte + 1]) << 8));
                destination[frame] = float(value) / 32768.0f;
            } else {
                destination[frame] = (float(clip.data[sample]) - 128.0f) / 128.0f;
            }
        }
    }

    AVAudioEngine* engine = (__bridge AVAudioEngine*)m_engine;
    AVAudioPlayerNode* node = [AVAudioPlayerNode new];
    NaraGameAudioToken* token = [NaraGameAudioToken new];
    [engine attachNode:node];
    [engine connect:node to:engine.mainMixerNode format:format];
    node.volume = std::clamp(volume, 0.0f, 1.0f);
    const AVAudioPlayerNodeBufferOptions options = loop
        ? AVAudioPlayerNodeBufferLoops : AVAudioPlayerNodeBufferInterrupts;
    [node scheduleBuffer:buffer atTime:nil options:options
        completionHandler:^{ token.finished = YES; }];
    [node play];

    auto voice = std::make_unique<ActiveVoice>();
    voice->node = (__bridge_retained void*)node;
    voice->buffer = (__bridge_retained void*)buffer;
    voice->token = (__bridge_retained void*)token;
    return voice;
}

void AppleAudioDevice::DestroyVoice(std::unique_ptr<ActiveVoice>& voice) {
    if (!voice) return;
    AVAudioPlayerNode* node = (__bridge AVAudioPlayerNode*)voice->node;
    [node stop];
    [(__bridge AVAudioEngine*)m_engine detachNode:node];
    voice.reset();
}

void AppleAudioDevice::PlaySound(const AudioClip& clip, float volume) {
    Update();
    if (auto voice = CreateVoice(clip, volume, false))
        m_active.push_back(std::move(voice));
}
void AppleAudioDevice::PlayMusic(const AudioClip& clip, float volume, bool loop) {
    StopMusic();
    m_music = CreateVoice(clip, volume, loop);
}
void AppleAudioDevice::StopMusic() { DestroyVoice(m_music); }
void AppleAudioDevice::SetMasterVolume(float volume) {
    if (m_engine)
        ((__bridge AVAudioEngine*)m_engine).mainMixerNode.outputVolume =
            std::clamp(volume, 0.0f, 1.0f);
}
void AppleAudioDevice::Update() {
    AVAudioEngine* engine = (__bridge AVAudioEngine*)m_engine;
    for (size_t i = 0; i < m_active.size();) {
        auto* token = (__bridge NaraGameAudioToken*)m_active[i]->token;
        if (token.finished) {
            AVAudioPlayerNode* node = (__bridge AVAudioPlayerNode*)m_active[i]->node;
            [node stop];
            [engine detachNode:node];
            m_active.erase(m_active.begin() + static_cast<ptrdiff_t>(i));
        } else {
            ++i;
        }
    }
    if (m_music) {
        auto* token = (__bridge NaraGameAudioToken*)m_music->token;
        if (token.finished) DestroyVoice(m_music);
    }
}

} // namespace NaraGame

#endif
