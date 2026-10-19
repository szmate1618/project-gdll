#include "audio.hpp"
#include "audio_wave.hpp"

#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <unordered_set>

namespace viewer {
namespace {
constexpr std::size_t effectCount = static_cast<std::size_t>(AudioEffect::count);
constexpr std::array<const char*, effectCount> effectFiles{
    "footstep.wav", "landing.wav", "pistol.wav", "smg.wav", "sniper.wav",
    "shotgun.wav", "reload.wav", "zombie.wav", "footstep2.wav", "footstep3.wav", "footstep4.wav",
    "zombie2.wav", "zombie3.wav", "huh.wav", "intrigued.wav", "chase.wav", "chase2.wav",
    "door_latch.wav", "door_slam.wav", "empty_click.wav", "reload_rifle.wav", "reload_shotgun.wav", "explosion.wav"};
constexpr std::size_t streamBufferCount = 4, streamBufferFrames = 4096;

void checkAudio(const char* operation) {
    if (const auto error = alGetError(); error != AL_NO_ERROR)
        throw std::runtime_error(std::string(operation) + " (OpenAL error " + std::to_string(error) + ")");
}
bool finite(glm::vec3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
double distanceSquared(glm::vec3 a, glm::vec3 b) {
    const auto delta = glm::dvec3(a) - glm::dvec3(b);
    return glm::dot(delta, delta);
}
ALenum waveFormat(const AudioWave& wave) {
    return wave.channels() == 1 ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16;
}
void upload(ALuint buffer, const AudioWave& wave, const std::vector<std::int16_t>& samples) {
    alBufferData(buffer, waveFormat(wave), samples.data(),
                 static_cast<ALsizei>(samples.size() * sizeof(std::int16_t)), wave.sampleRate());
    checkAudio("Upload audio buffer");
}
ALint sourceState(ALuint source) {
    ALint state = AL_INITIAL;
    alGetSourcei(source, AL_SOURCE_STATE, &state);
    return state;
}
void positionSource(ALuint source, glm::vec3 position) {
    alSource3f(source, AL_POSITION, position.x, position.y, position.z);
}
}

struct AudioSystem::Impl {
    struct Sound { ALuint buffer = 0; double seconds = 0; };
    struct Voice {
        ALuint source = 0;
        std::optional<std::size_t> zombie;
        int priority = 0;
    };
    struct Stream {
        std::unique_ptr<AudioWave> wave;
        ALuint source = 0;
        std::array<ALuint, streamBufferCount> buffers{};
        std::uint64_t framesRead = 0;
        ~Stream() {
            if (source) alDeleteSources(1, &source);
            for (const auto buffer : buffers) if (buffer) alDeleteBuffers(1, &buffer);
        }
        void fill(ALuint buffer) {
            const auto samples = wave->read(streamBufferFrames, true);
            upload(buffer, *wave, samples);
            framesRead += samples.size() / wave->channels();
        }
        void update() {
            ALint processed = 0;
            alGetSourcei(source, AL_BUFFERS_PROCESSED, &processed);
            for (ALint i = 0; i < processed; ++i) {
                ALuint buffer = 0;
                alSourceUnqueueBuffers(source, 1, &buffer);
                checkAudio("Unqueue ambience");
                fill(buffer);
                alSourceQueueBuffers(source, 1, &buffer);
            }
            // All buffers may finish while the app is minimized or stalled.
            if (sourceState(source) != AL_PLAYING) alSourcePlay(source);
            checkAudio("Stream ambience");
        }
    };

    ALCdevice* device = nullptr;
    ALCcontext* context = nullptr;
    ALCcontext* previousContext = nullptr;
    bool ready = false, hrtf = false;
    glm::vec3 listener{0};
    std::array<Sound, effectCount> sounds{};
    std::vector<Voice> effects, zombies;
    std::vector<std::unique_ptr<Stream>> streams;

    ~Impl() {
        if (context) {
            alcMakeContextCurrent(context);
            streams.clear();
            for (const auto& voice : effects) if (voice.source) alDeleteSources(1, &voice.source);
            for (const auto& voice : zombies) if (voice.source) alDeleteSources(1, &voice.source);
            for (const auto& sound : sounds) if (sound.buffer) alDeleteBuffers(1, &sound.buffer);
            alcMakeContextCurrent(previousContext);
            alcDestroyContext(context);
        }
        if (device) alcCloseDevice(device);
    }

    void loadEffect(std::size_t index, const std::filesystem::path& path) {
        try {
            AudioWave wave(path);
            // Positional effects must be mono. Bound preload memory, even for
            // a mistakenly supplied long recording; ambience has its own path.
            if (wave.channels() != 1 || wave.seconds() > 10)
                throw std::runtime_error("Effects require mono PCM16 WAV, at most 10 seconds: " + path.string());
            const auto samples = wave.read(static_cast<std::size_t>(wave.frames()));
            auto& sound = sounds[index];
            alGenBuffers(1, &sound.buffer);
            checkAudio("Create effect buffer");
            upload(sound.buffer, wave, samples);
            sound.seconds = wave.seconds();
        } catch (const std::exception& error) {
            auto& sound = sounds[index];
            if (sound.buffer) alDeleteBuffers(1, &sound.buffer);
            sound = {};
            std::cerr << "Audio effect skipped: " << error.what() << '\n';
        }
    }

    void addStream(const std::filesystem::path& path, float gain) {
        try {
            auto stream = std::make_unique<Stream>();
            stream->wave = std::make_unique<AudioWave>(path);
            alGenSources(1, &stream->source);
            checkAudio("Create ambience source");
            alSourcei(stream->source, AL_SOURCE_RELATIVE, AL_TRUE);
            alSourcef(stream->source, AL_ROLLOFF_FACTOR, 0);
            alSourcef(stream->source, AL_GAIN, gain);
            alGenBuffers(static_cast<ALsizei>(stream->buffers.size()), stream->buffers.data());
            checkAudio("Create ambience buffers");
            for (const auto buffer : stream->buffers) stream->fill(buffer);
            alSourceQueueBuffers(stream->source, static_cast<ALsizei>(stream->buffers.size()), stream->buffers.data());
            alSourcePlay(stream->source);
            checkAudio("Start ambience");
            streams.push_back(std::move(stream));
        } catch (const std::exception& error) {
            std::cerr << "Audio ambience skipped: " << error.what() << '\n';
        }
    }

    void initialize(const AudioOptions& options) {
        previousContext = alcGetCurrentContext();
        device = alcOpenDevice(nullptr);
        if (!device) throw std::runtime_error("Cannot open output device");
        const bool supportsHrtf = alcIsExtensionPresent(device, "ALC_SOFT_HRTF") == ALC_TRUE;
        const ALCint attributes[]{ALC_HRTF_SOFT, options.hrtf ? ALC_TRUE : ALC_FALSE, 0};
        context = alcCreateContext(device, supportsHrtf ? attributes : nullptr);
        if (!context && supportsHrtf) {
            alcGetError(device);
            context = alcCreateContext(device, nullptr);
        }
        if (!context || !alcMakeContextCurrent(context)) throw std::runtime_error("Cannot create audio context");
        if (supportsHrtf) {
            ALCint enabled = ALC_FALSE;
            alcGetIntegerv(device, ALC_HRTF_SOFT, 1, &enabled);
            hrtf = enabled == ALC_TRUE;
        }
        alDistanceModel(AL_INVERSE_DISTANCE_CLAMPED);
        alDopplerFactor(0); // Camera teleports/mode switches never create pitch spikes.
        alListenerf(AL_GAIN, options.gain);
        for (std::size_t i = 0; i < sounds.size(); ++i) loadEffect(i, options.directory / effectFiles[i]);
        effects.resize(options.effectVoices);
        zombies.resize(options.zombieVoices);
        for (auto* pool : {&effects, &zombies}) {
            for (auto& voice : *pool) {
                alGenSources(1, &voice.source);
                checkAudio("Create voice pool");
                alSourcef(voice.source, AL_REFERENCE_DISTANCE, 3);
                alSourcef(voice.source, AL_MAX_DISTANCE, 40);
            }
        }
        addStream(options.directory / "wind.wav", 0.25f);
        if (options.rain) addStream(options.directory / "rain.wav", 0.4f);
        checkAudio("Initialize audio");
        ready = true;
        std::cout << "Audio: " << reinterpret_cast<const char*>(alGetString(AL_RENDERER))
                  << "; HRTF " << (hrtf ? "enabled" : "unavailable/disabled")
                  << "; " << effects.size() << " effect / " << zombies.size() << " zombie voices\n";
    }

    void play(AudioEffect effect, glm::vec3 position, float gain, float pitch, bool local) {
        const auto index = static_cast<std::size_t>(effect);
        if (!ready || index >= sounds.size() || !sounds[index].buffer ||
            !finite(position) || !std::isfinite(gain) || gain <= 0 ||
            !std::isfinite(pitch) || pitch < 0.5f || pitch > 2) return;
        Voice* selected = nullptr;
        double farthest = local ? -1 : distanceSquared(listener, position);
        if (!local && farthest > 40 * 40) return;
        for (auto& voice : effects) {
            if (sourceState(voice.source) != AL_PLAYING) { selected = &voice; break; }
            ALint relative = AL_FALSE;
            alGetSourcei(voice.source, AL_SOURCE_RELATIVE, &relative);
            if (relative) continue; // World effects cannot evict own-player sounds.
            glm::vec3 existing;
            alGetSource3f(voice.source, AL_POSITION, &existing.x, &existing.y, &existing.z);
            const auto distance = distanceSquared(listener, existing);
            if (distance > farthest) { selected = &voice; farthest = distance; }
        }
        // If every slot is local, reuse local effect sources in round-robin
        // order so sustained automatic fire keeps up without allocating sources.
        if (!selected && local && !effects.empty()) {
            selected = &effects[nextLocal++ % effects.size()];
        }
        if (!selected) return;
        const auto source = selected->source;
        alSourceStop(source);
        alSourcei(source, AL_BUFFER, static_cast<ALint>(sounds[index].buffer));
        alSourcei(source, AL_LOOPING, AL_FALSE);
        alSourcei(source, AL_SOURCE_RELATIVE, local ? AL_TRUE : AL_FALSE);
        positionSource(source, position);
        alSourcef(source, AL_ROLLOFF_FACTOR, local ? 0 : 1);
        alSourcef(source, AL_GAIN, std::min(gain, 1.0f));
        alSourcef(source, AL_PITCH, pitch);
        alSourcePlay(source);
    }
    std::size_t nextLocal = 0;
};

AudioSystem::AudioSystem(const AudioOptions& options) : impl_(std::make_unique<Impl>()) {
    if (!std::isfinite(options.gain) || options.gain < 0 || options.gain > 1 ||
        options.effectVoices > 128 || options.zombieVoices > 32)
        throw std::invalid_argument("Invalid audio gain or voice limits");
    if (options.enabled) {
        try { impl_->initialize(options); }
        catch (const std::exception& error) {
            std::cerr << "Audio disabled: " << error.what() << '\n';
            impl_.reset(); // Release partial initialization immediately.
        }
    }
}
AudioSystem::~AudioSystem() = default;
bool AudioSystem::enabled() const { return impl_ && impl_->ready; }
bool AudioSystem::hrtfEnabled() const { return enabled() && impl_->hrtf; }

void AudioSystem::setListener(glm::vec3 eye, glm::vec3 forward, glm::vec3 up) {
    if (!enabled() || !finite(eye) || !finite(forward) || !finite(up) ||
        glm::length(forward) < 1e-5f || glm::length(glm::cross(forward, up)) < 1e-5f) return;
    forward = glm::normalize(forward);
    up = glm::normalize(up - forward * glm::dot(up, forward));
    // Runtime and OpenAL use the same right-handed frame; do not negate Z.
    const ALfloat orientation[]{forward.x, forward.y, forward.z, up.x, up.y, up.z};
    impl_->listener = eye;
    alListener3f(AL_POSITION, eye.x, eye.y, eye.z);
    alListenerfv(AL_ORIENTATION, orientation);
}

void AudioSystem::play(AudioEffect effect, glm::vec3 position, float gain, float pitch) {
    if (enabled()) impl_->play(effect, position, gain, pitch, false);
}
void AudioSystem::playLocal(AudioEffect effect, float gain) {
    if (enabled()) impl_->play(effect, {0, 0, -0.5f}, gain, 1, true);
}

std::vector<std::size_t> AudioSystem::setZombieEmitters(const std::vector<AudioEmitter>& candidates) {
    if (!enabled()) return {};
    auto& state = *impl_;
    std::vector<AudioEmitter> selected;
    for (const auto& candidate : candidates)
        if (finite(candidate.position) && distanceSquared(candidate.position, state.listener) <= 40 * 40)
            selected.push_back(candidate);
    std::sort(selected.begin(), selected.end(), [&](const auto& a, const auto& b) {
        const auto da = distanceSquared(a.position, state.listener), db = distanceSquared(b.position, state.listener);
        return da == db ? a.id < b.id : da < db;
    });
    std::unordered_set<std::size_t> ids;
    selected.erase(std::remove_if(selected.begin(), selected.end(), [&](const auto& emitter) {
        return !ids.insert(emitter.id).second;
    }), selected.end());
    if (selected.size() > state.zombies.size()) selected.resize(state.zombies.size());
    for (auto& voice : state.zombies) {
        if (!voice.zombie) continue;
        if (std::none_of(selected.begin(), selected.end(), [&](const auto& emitter) { return emitter.id == *voice.zombie; })) {
            alSourceStop(voice.source);
            voice.zombie.reset();
            voice.priority = 0;
        }
    }
    for (const auto& emitter : selected) {
        auto found = std::find_if(state.zombies.begin(), state.zombies.end(), [&](const auto& voice) {
            return voice.zombie == emitter.id;
        });
        if (found == state.zombies.end()) {
            found = std::find_if(state.zombies.begin(), state.zombies.end(), [](const auto& voice) { return !voice.zombie; });
            if (found == state.zombies.end()) continue;
            found->zombie = emitter.id;
            found->priority = 0;
        }
        positionSource(found->source, emitter.position);
    }
    std::vector<std::size_t> result;
    for (const auto& emitter : selected) result.push_back(emitter.id);
    return result;
}

bool AudioSystem::playZombie(std::size_t id, AudioEffect effect, int priority, float gain, float pitch) {
    if (!enabled()) return false;
    const auto index = static_cast<std::size_t>(effect);
    if (index >= impl_->sounds.size() || !impl_->sounds[index].buffer || priority < 1 ||
        !std::isfinite(gain) || gain <= 0 || !std::isfinite(pitch) || pitch < .5f || pitch > 2) return false;
    auto found = std::find_if(impl_->zombies.begin(), impl_->zombies.end(), [&](const auto& voice) {
        return voice.zombie == id;
    });
    if (found == impl_->zombies.end()) return false;
    if (sourceState(found->source) == AL_PLAYING && priority <= found->priority) return false;
    alSourceStop(found->source);
    alSourcei(found->source, AL_BUFFER, static_cast<ALint>(impl_->sounds[index].buffer));
    alSourcei(found->source, AL_LOOPING, AL_FALSE);
    alSourcef(found->source, AL_GAIN, std::min(gain, 1.f));
    alSourcef(found->source, AL_PITCH, pitch);
    alSourcePlay(found->source);
    found->priority = priority;
    return true;
}

void AudioSystem::update() {
    if (!enabled()) return;
    auto& streams = impl_->streams;
    for (auto it = streams.begin(); it != streams.end();) {
        try { (*it)->update(); ++it; }
        catch (const std::exception& error) {
            std::cerr << "Audio stream stopped: " << error.what() << '\n';
            it = streams.erase(it);
        }
    }
    try { checkAudio("Update audio"); }
    catch (const std::exception& error) {
        std::cerr << "Audio disabled: " << error.what() << '\n';
        impl_.reset();
    }
}

AudioStats AudioSystem::stats() const {
    AudioStats result;
    if (!enabled()) return result;
    for (const auto& voice : impl_->effects) if (sourceState(voice.source) == AL_PLAYING) ++result.effects;
    for (const auto& voice : impl_->zombies) {
        if (!voice.zombie) continue;
        result.zombieEmitters.push_back(*voice.zombie);
        if (sourceState(voice.source) == AL_PLAYING) result.zombies.push_back(*voice.zombie);
    }
    for (const auto& stream : impl_->streams) {
        ALint queued = 0;
        alGetSourcei(stream->source, AL_BUFFERS_QUEUED, &queued);
        result.streamBuffers += static_cast<std::size_t>(queued);
        if (sourceState(stream->source) == AL_PLAYING) ++result.playingStreams;
        result.streamFramesRead += stream->framesRead;
    }
    return result;
}

} // namespace viewer
