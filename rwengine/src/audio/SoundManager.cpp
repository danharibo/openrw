#include "audio/SoundManager.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/avutil.h>
}

#include "audio/alCheck.hpp"

#include <rw/types.hpp>

Sound& SoundManager::getSoundRef(size_t name) {
    auto ref = buffers.find(name);
    if (ref != buffers.end()) {
        return ref->second;
    } else {
        createSfxInstance(name);
    }
    return buffers[name];
}

Sound& SoundManager::getSoundRef(const std::string& name) {
    return sounds[name];  // @todo reloading, how to check is it wav/mp3?
}

SoundManager::SoundManager() {
    initializeOpenAL();
    initializeAVCodec();
}

SoundManager::~SoundManager() {
    // De-initialize OpenAL
    if (alContext) {
        alcMakeContextCurrent(nullptr);
        alcDestroyContext(alContext);
    }

    if (alDevice) alcCloseDevice(alDevice);
}

bool SoundManager::initializeOpenAL() {
    alDevice = alcOpenDevice(nullptr);
    if (!alDevice) {
        RW_ERROR("Could not find OpenAL device!");
        return false;
    }

    alContext = alcCreateContext(alDevice, nullptr);
    if (!alContext) {
        RW_ERROR("Could not create OpenAL context!");
        return false;
    }

    if (!alcMakeContextCurrent(alContext)) {
        RW_ERROR("Unable to make OpenAL context current!");
        return false;
    }

    // Needed for max distance
    alDistanceModel(AL_LINEAR_DISTANCE_CLAMPED);

    return true;
}

bool SoundManager::initializeAVCodec() {
#if RW_DEBUG && RW_VERBOSE_DEBUG_MESSAGES
    av_log_set_level(AV_LOG_WARNING);
#else
    av_log_set_level(AV_LOG_ERROR);
#endif

    return true;
}

bool SoundManager::loadSound(const std::string& name,
                             const std::string& fileName) {
    Sound* sound = nullptr;
    auto sound_iter = sounds.find(name);

    if (sound_iter != sounds.end()) {
        sound = &sound_iter->second;
    } else {
        auto emplaced = sounds.emplace(std::piecewise_construct,
                                       std::forward_as_tuple(name),
                                       std::forward_as_tuple());
        sound = &emplaced.first->second;

        sound->source = std::make_shared<SoundSource>();
        sound->buffer = std::make_unique<SoundBuffer>();

        sound->source->loadFromFile(fileName);
        sound->isLoaded = sound->buffer->bufferData(*sound->source);
    }

    return sound->isLoaded;
}

void SoundManager::loadSfxSound(const rwfs::path& path, size_t index) {
    Sound* sound = nullptr;

    auto emplaced =
        sfx.emplace(std::piecewise_construct, std::forward_as_tuple(index),
                    std::forward_as_tuple());
    sound = &emplaced.first->second;

    sound->source = std::make_shared<SoundSource>();
    sound->source->loadSfx(path, sdt, index);
}

size_t SoundManager::createSfxInstance(size_t index) {
    Sound* sound = nullptr;
    auto soundRef = sfx.find(index);

    // Try to reuse first available buffer
    // (aka with stopped state)
    for (auto& sound : buffers) {
        if (sound.second.buffer && sound.second.isStopped()) {
            // Let's use this buffer
            sound.second.buffer = std::make_unique<SoundBuffer>();
            sound.second.source = soundRef->second.source;
            sound.second.isLoaded =
                sound.second.buffer->bufferData(*sound.second.source);
            return sound.first;
        }
    }
    // There's no available free buffer, so
    // we should create a new one.
    auto emplaced = buffers.emplace(std::piecewise_construct,
                                    std::forward_as_tuple(bufferNr),
                                    std::forward_as_tuple());
    sound = &emplaced.first->second;

    sound->id = bufferNr;
    sound->buffer = std::make_unique<SoundBuffer>();
    sound->source = soundRef->second.source;
    sound->isLoaded = sound->buffer->bufferData(*sound->source);
    bufferNr++;

    return sound->id;
}

bool SoundManager::isLoaded(const std::string& name) {
    auto sound = sounds.find(name);
    if (sound != sounds.end()) {
        return sound->second.isLoaded;
    }
    return false;
}

bool SoundManager::isPlaying(const std::string& name) {
    auto sound = sounds.find(name);
    if (sound != sounds.end()) {
        return sound->second.isPlaying();
    }
    return false;
}

bool SoundManager::isStopped(const std::string& name) {
    auto sound = sounds.find(name);
    if (sound != sounds.end()) {
        return sound->second.isStopped();
    }
    return false;
}

bool SoundManager::isPaused(const std::string& name) {
    auto sound = sounds.find(name);
    if (sound != sounds.end()) {
        return sound->second.isPaused();
    }
    return false;
}

void SoundManager::playSound(const std::string& name) {
    auto sound = sounds.find(name);
    if (sound != sounds.end()) {
        return sound->second.play();
    }
}

void SoundManager::playSfx(size_t name, const glm::vec3& position, bool looping,
                           int maxDist) {
    auto buffer = buffers.find(name);
    if (buffer != buffers.end()) {
        buffer->second.setPosition(position);
        if (looping) {
            buffer->second.setLooping(looping);
        }

        buffer->second.setPitch(1.f);
        buffer->second.setGain(1.f);
        if (maxDist != -1) {
            buffer->second.setMaxDistance(maxDist);
        }
        buffer->second.play();
    }
}

void SoundManager::pauseAllSounds() {
    for (auto& sound : sounds) {
        if (sound.second.isPlaying()) {
            sound.second.pause();
        }
    }
    for (auto& sound : buffers) {
        if (sound.second.isPlaying()) {
            sound.second.pause();
        }
    }
}

void SoundManager::resumeAllSounds() {
    for (auto& sound : sounds) {
        if (sound.second.isPaused()) {
            sound.second.play();
        }
    }
    for (auto& sound : buffers) {
        if (sound.second.isPaused()) {
            sound.second.play();
        }
    }
}

bool SoundManager::playBackground(const std::string& fileName) {
    if (this->loadSound(fileName, fileName)) {
        backgroundNoise = fileName;
        auto& sound = getSoundRef(fileName);
        sound.play();
        return true;
    }

    return false;
}

bool SoundManager::loadMusic(const std::string& name,
                             const std::string& fileName) {
    return loadSound(name, fileName);
}

void SoundManager::playMusic(const std::string& name) {
    auto sound = sounds.find(name);
    if (sound != sounds.end()) {
        sound->second.play();
    }
}

void SoundManager::stopMusic(const std::string& name) {
    auto sound = sounds.find(name);
    if (sound != sounds.end()) {
        sound->second.stop();
    }
}

void SoundManager::pause(bool p) {
    if (backgroundNoise.length() > 0) {
        if (p) {
            pauseAllSounds();
        } else {
            resumeAllSounds();
        }
    }
}

void SoundManager::setListenerPosition(const glm::vec3& position) {
    alListener3f(AL_POSITION, position.x, position.y, position.z);
}

void SoundManager::setListenerVelocity(const glm::vec3& vel) {
    alListener3f(AL_VELOCITY, vel.x, vel.y, vel.z);
}

void SoundManager::setListenerOrientation(const glm::vec3& at) {
    float v[6] = {0, at.y, 0, 0, 0, at.z};
    alListenerfv(AL_ORIENTATION, v);
}

void SoundManager::setSoundPosition(const std::string& name,
                                    const glm::vec3& position) {
    if (sounds.find(name) != sounds.end()) {
        alCheck(alSource3f(sounds[name].buffer->source, AL_POSITION, position.x,
                           position.y, position.z));
    }
}
