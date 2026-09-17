#include "audio_backend.h"

#include "runtime_log.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>

#if defined(__SWITCH__)
namespace {
constexpr uint32_t kSwitchAudioBufferBytes = 0x1000;
constexpr uint32_t kSwitchAudioBufferSamples = kSwitchAudioBufferBytes / sizeof(int16_t);
// Four 0x1000-byte buffers: about 85 ms of stereo 48 kHz audio queued at most.
constexpr uint32_t kSwitchAudioBufferCount = 4;
constexpr uint64_t kSwitchReclaimTimeout = 10'000'000ULL; // 10 ms

alignas(0x1000) int16_t s_audioSampleData[kSwitchAudioBufferCount][kSwitchAudioBufferSamples];
AudioOutBuffer s_audioHeaders[kSwitchAudioBufferCount];
}  // namespace
#else
#include <SDL3/SDL_init.h>
#endif  // __SWITCH__

AudioBackend& AudioBackend::Instance() {
    static AudioBackend instance;
    return instance;
}

float AudioBackend::EffectiveGainLocked() const {
    return m_muted ? 0.0f : m_masterVolume;
}

void AudioBackend::ApplyGainLocked() {
#if defined(__SWITCH__)
    // audoutSetAudioOutVolume takes a 0.0-1.0 float (libnx converts to 0-100).
    audoutSetAudioOutVolume(EffectiveGainLocked());
#else
    if (m_stream && !SDL_SetAudioStreamGain(m_stream, EffectiveGainLocked())) {
        RT_LOG(RT_TAG_AUDIO) << "SDL_SetAudioStreamGain failed: " << SDL_GetError() << std::endl;
    }
#endif
}

void AudioBackend::SetMasterVolume(float volume) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_masterVolume = std::clamp(volume, 0.0f, 1.0f);
    ApplyGainLocked();
}

void AudioBackend::SetMuted(bool muted) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_muted = muted;
    ApplyGainLocked();
}

bool AudioBackend::EnsureInitializedLocked(uint32_t sampleRate, uint32_t channels) {
    if (m_initialized && m_sampleRate == sampleRate && m_channels == channels) {
        return true;
    }

    m_initialized = false;
    m_sampleRate = 0;
    m_channels = 0;
    m_reportedDroppedBlock = false;

#if defined(__SWITCH__)
    if (sampleRate != audoutGetSampleRate() || channels != audoutGetChannelCount()) {
        // audout is fixed at 48 kHz stereo s16 on-device; Wii AI DMA matches
        // this exactly, so no conversion is needed here.
        RT_LOG(RT_TAG_AUDIO) << "audout fixed at 48 kHz stereo; requested " << sampleRate << " Hz / "
                  << channels << " ch ignored" << std::endl;
    }

    const Result initRc = audoutInitialize();
    if (R_FAILED(initRc)) {
        RT_LOG(RT_TAG_AUDIO) << "audoutInitialize failed: " << std::hex << initRc << std::dec << std::endl;
        return false;
    }
    const Result startRc = audoutStartAudioOut();
    if (R_FAILED(startRc)) {
        RT_LOG(RT_TAG_AUDIO) << "audoutStartAudioOut failed: " << std::hex << startRc << std::dec << std::endl;
        audoutExit();
        return false;
    }

    for (uint32_t i = 0; i < kSwitchAudioBufferCount; ++i) {
        s_audioHeaders[i].next = nullptr;
        s_audioHeaders[i].buffer = s_audioSampleData[i];
        s_audioHeaders[i].buffer_size = kSwitchAudioBufferBytes;
        s_audioHeaders[i].data_size = 0;
        s_audioHeaders[i].data_offset = 0;
        std::memset(s_audioSampleData[i], 0, sizeof(s_audioSampleData[i]));
    }
    m_nxWriteSlot = 0;
    m_nxWriteBytes = 0;

    ApplyGainLocked();
    m_sampleRate = sampleRate;
    m_channels = channels;
    m_initialized = true;
    return true;
#else
    if (m_stream) {
        SDL_DestroyAudioStream(m_stream);
        m_stream = nullptr;
    }

    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        RT_LOG(RT_TAG_AUDIO) << "SDL_InitSubSystem(SDL_INIT_AUDIO) failed: " << SDL_GetError() << std::endl;
        return false;
    }

    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_S16LE;
    spec.channels = static_cast<int>(channels);
    spec.freq = static_cast<int>(sampleRate);

    SDL_AudioStream* stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!stream) {
        RT_LOG(RT_TAG_AUDIO) << "SDL_OpenAudioDeviceStream failed: " << SDL_GetError() << std::endl;
        return false;
    }

    if (!SDL_ResumeAudioStreamDevice(stream)) {
        RT_LOG(RT_TAG_AUDIO) << "SDL_ResumeAudioStreamDevice failed: " << SDL_GetError() << std::endl;
        SDL_DestroyAudioStream(stream);
        return false;
    }

    if (!SDL_SetAudioStreamGain(stream, EffectiveGainLocked())) {
        RT_LOG(RT_TAG_AUDIO) << "SDL_SetAudioStreamGain failed: " << SDL_GetError() << std::endl;
        SDL_DestroyAudioStream(stream);
        return false;
    }

    m_stream = stream;
    m_spec = spec;
    m_sampleRate = sampleRate;
    m_channels = channels;
    m_initialized = true;
    return true;
#endif  // __SWITCH__
}

bool AudioBackend::Init(uint32_t sampleRate, uint32_t channels) {
    std::lock_guard<std::mutex> lock(m_mutex);
    return EnsureInitializedLocked(sampleRate, channels);
}

void AudioBackend::Shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);
#if defined(__SWITCH__)
    if (!m_initialized) {
        return;
    }
    audoutSetAudioOutVolume(0.0f);
    // Wait for every submitted buffer to finish before tearing down.
    AudioOutBuffer* released = nullptr;
    u32 releasedCount = 0;
    audoutWaitPlayFinish(&released, &releasedCount, UINT64_MAX);
    audoutStopAudioOut();
    audoutExit();
#else
    if (m_stream) {
        SDL_DestroyAudioStream(m_stream);
        m_stream = nullptr;
    }
    if (m_initialized) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
#endif  // __SWITCH__
    m_initialized = false;
    m_sampleRate = 0;
    m_channels = 0;
    m_nxWriteSlot = 0;
    m_nxWriteBytes = 0;
    m_reportedDroppedBlock = false;
    m_convertBuffer.clear();
}

uint32_t AudioBackend::QueuedBytesLocked() const {
#if defined(__SWITCH__)
    uint32_t queued = 0;
    for (uint32_t i = 0; i < kSwitchAudioBufferCount; ++i) {
        if (s_audioHeaders[i].data_size != 0) {
            queued += static_cast<uint32_t>(s_audioHeaders[i].data_size);
        }
    }
    return queued;
#else
    if (!m_stream) {
        return 0;
    }
    const int queued = SDL_GetAudioStreamQueued(m_stream);
    return queued > 0 ? static_cast<uint32_t>(queued) : 0;
#endif  // __SWITCH__
}

uint32_t AudioBackend::QueueLimitBytesLocked() const {
    constexpr uint32_t queueMs = 120;

    const uint64_t bytesPerSecond = static_cast<uint64_t>(m_sampleRate != 0 ? m_sampleRate : 48000) *
                                    static_cast<uint64_t>(m_channels != 0 ? m_channels : 2) *
                                    sizeof(int16_t);
    return static_cast<uint32_t>((bytesPerSecond * queueMs) / 1000u);
}

bool AudioBackend::QueueHasCapacityLocked(int incomingBytes) {
#if defined(__SWITCH__)
    const uint32_t maxQueued = QueueLimitBytesLocked();
    // The four buffered slots hold at most kSwitchAudioBufferCount * 0x1000
    // bytes of live audio; that is well under the 120 ms limit.
    if (static_cast<uint64_t>(QueuedBytesLocked()) + static_cast<uint64_t>(std::max(incomingBytes, 0)) >
        maxQueued) {
        if (!m_reportedDroppedBlock) {
            m_reportedDroppedBlock = true;
            RT_LOG(RT_TAG_AUDIO) << "output queue full; dropping blocks to preserve continuity" << std::endl;
        }
        return false;
    }
    return true;
#else
    if (!m_stream) {
        return false;
    }
    const int queued = SDL_GetAudioStreamQueued(m_stream);
    if (queued < 0) {
        return true;
    }
    const uint32_t maxQueued = QueueLimitBytesLocked();
    if (static_cast<uint64_t>(queued) + static_cast<uint64_t>(std::max(incomingBytes, 0)) > maxQueued) {
        // Match Dolphin's FIFO overflow behavior: preserve the continuous audio
        // already queued and discard the new block.  Clearing SDL's entire
        // stream creates an audible discontinuity (the severe crackle seen when
        // VI-batched DMA briefly outran playback).
        // A drop is audible; report the first one so it is not invisible.
        if (!m_reportedDroppedBlock) {
            m_reportedDroppedBlock = true;
            RT_LOG(RT_TAG_AUDIO) << "output queue full (" << queued << "/" << maxQueued
                      << " bytes); dropping blocks to preserve continuity" << std::endl;
        }
        return false;
    }
    return true;
#endif  // __SWITCH__
}

#if defined(__SWITCH__)
bool AudioBackend::AppendSamplesLocked(const int16_t* samples, size_t sampleCount) {
    size_t srcIndex = 0;
    while (srcIndex < sampleCount) {
        AudioOutBuffer& header = s_audioHeaders[m_nxWriteSlot];
        if (header.data_size != 0) {
            // The current slot is still queued with playback; reclaim finished
            // buffers. This is the only place playback pulls released buffers.
            AudioOutBuffer* released = nullptr;
            u32 releasedCount = 0;
            const Result rc = audoutWaitPlayFinish(&released, &releasedCount, kSwitchReclaimTimeout);
            if (R_SUCCEEDED(rc) && released != nullptr) {
                for (uint32_t i = 0; i < kSwitchAudioBufferCount; ++i) {
                    if (s_audioHeaders[i].data_size != 0 &&
                        (released == &s_audioHeaders[i] || released->next == &s_audioHeaders[i])) {
                        s_audioHeaders[i].data_size = 0;
                        released = released->next;
                    }
                }
            }
            if (header.data_size != 0) {
                // Playback has not caught up (~85 ms of audio in flight). Drop
                // the remainder of this push to preserve continuity.
                return true;
            }
        }

        if (m_nxWriteBytes == kSwitchAudioBufferBytes) {
            const Result rc = audoutAppendAudioOutBuffer(&header);
            if (R_FAILED(rc)) {
                RT_LOG(RT_TAG_AUDIO) << "audoutAppendAudioOutBuffer failed: " << std::hex << rc << std::dec
                          << std::endl;
                return false;
            }
            m_nxWriteSlot = (m_nxWriteSlot + 1) % kSwitchAudioBufferCount;
            m_nxWriteBytes = 0;
            header.data_size = 0;
            continue;
        }

        const size_t roomSamples = kSwitchAudioBufferSamples - (m_nxWriteBytes / sizeof(int16_t));
        const size_t copySamples = std::min(roomSamples, sampleCount - srcIndex);
        std::memcpy(&s_audioSampleData[m_nxWriteSlot][m_nxWriteBytes / sizeof(int16_t)],
                    &samples[srcIndex], copySamples * sizeof(int16_t));
        m_nxWriteBytes += static_cast<uint32_t>(copySamples * sizeof(int16_t));
        s_audioHeaders[m_nxWriteSlot].data_size = m_nxWriteBytes;
        srcIndex += copySamples;
    }

    // A push that exactly filled the last slot never looped back to submit it.
    if (m_nxWriteBytes == kSwitchAudioBufferBytes) {
        AudioOutBuffer& header = s_audioHeaders[m_nxWriteSlot];
        const Result rc = audoutAppendAudioOutBuffer(&header);
        if (R_FAILED(rc)) {
            RT_LOG(RT_TAG_AUDIO) << "audoutAppendAudioOutBuffer failed: " << std::hex << rc << std::dec
                      << std::endl;
            return false;
        }
        m_nxWriteSlot = (m_nxWriteSlot + 1) % kSwitchAudioBufferCount;
        m_nxWriteBytes = 0;
        header.data_size = 0;
    }

    return true;
}
#else
bool AudioBackend::AppendSamplesLocked(const int16_t* samples, size_t sampleCount) {
    const int lenBytes = static_cast<int>(sampleCount * sizeof(int16_t));
    if (!SDL_PutAudioStreamData(m_stream, samples, lenBytes)) {
        RT_LOG(RT_TAG_AUDIO) << "SDL_PutAudioStreamData failed: " << SDL_GetError() << std::endl;
        return false;
    }
    return true;
}
#endif  // __SWITCH__

bool AudioBackend::PushWiiAiSamplesBE16(const uint8_t* data, size_t bytes) {
    if (!data || bytes == 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized) {
        return false;
    }

    const size_t sampleCount = bytes / sizeof(int16_t);
    if (sampleCount == 0) {
        return false;
    }

    if (m_convertBuffer.size() < sampleCount) {
        m_convertBuffer.resize(sampleCount);
    }

    const size_t frameCount = sampleCount / 2;
    for (size_t frame = 0; frame < frameCount; ++frame) {
        const size_t rightOffset = frame * 4;
        const size_t leftOffset = rightOffset + 2;
        const uint16_t right = static_cast<uint16_t>(data[rightOffset]) << 8 |
                               static_cast<uint16_t>(data[rightOffset + 1]);
        const uint16_t left = static_cast<uint16_t>(data[leftOffset]) << 8 |
                              static_cast<uint16_t>(data[leftOffset + 1]);
        m_convertBuffer[frame * 2] = static_cast<int16_t>(left);
        m_convertBuffer[frame * 2 + 1] = static_cast<int16_t>(right);
    }

    const size_t lenSamples = frameCount * 2;
    const int lenBytes = static_cast<int>(lenSamples * sizeof(int16_t));
    if (!QueueHasCapacityLocked(lenBytes)) {
        return true;
    }
    return AppendSamplesLocked(m_convertBuffer.data(), lenSamples);
}

bool AudioBackend::PushSamplesLE16(const int16_t* samples, size_t sampleCount) {
    if (!samples || sampleCount == 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized) {
        return false;
    }

    const int lenBytes = static_cast<int>(sampleCount * sizeof(int16_t));
    if (!QueueHasCapacityLocked(lenBytes)) {
        return true;
    }
    return AppendSamplesLocked(samples, sampleCount);
}