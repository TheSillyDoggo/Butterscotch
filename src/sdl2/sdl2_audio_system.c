#include "sdl2_audio_system.h"
#include <SDL2/SDL_mixer.h>
#include "stb_ds.h"

// ===[ Helpers ]===

static SoundInstance* findFreeSlot(SDL2AudioSystem* sdl) {
    // First pass: find an inactive slot
    repeat(MAX_SOUND_INSTANCES, i) {
        if (!sdl->instances[i].active) {
            return &sdl->instances[i];
        }
    }

    // Second pass: evict the lowest-priority ended sound
    SoundInstance* best = nullptr;
    repeat(MAX_SOUND_INSTANCES, i) {
        SoundInstance* inst = &sdl->instances[i];
        if (!Mix_Playing(inst->channel)) {
            if (best == nullptr || best->priority > inst->priority) {
                best = inst;
            }
        }
    }

    return best;
}

static SoundInstance* findInstanceById(SDL2AudioSystem* ma, int32_t instanceId) {
    int32_t slotIndex = instanceId - SOUND_INSTANCE_ID_BASE;
    if (0 > slotIndex || slotIndex >= MAX_SOUND_INSTANCES) return nullptr;
    SoundInstance* inst = &ma->instances[slotIndex];
    if (!inst->active || inst->instanceId != instanceId) return nullptr;
    return inst;
}

void ChannelFinishedCallback(int channel)
{
    // Mix_FreeChunk(Mix_GetChunk(channel));
}

static void sdlInit(AudioSystem* audio, DataWin* dataWin, FileSystem* fileSystem) {
    SDL2AudioSystem* sdl = (SDL2AudioSystem*)audio;
    arrput(sdl->base.audioGroups, dataWin);
    sdl->fileSystem = fileSystem;
    sdl->dataWin = dataWin;

    Mix_OpenAudio(44100,
        MIX_DEFAULT_FORMAT,
        MIX_DEFAULT_CHANNELS,
        2048
    );

    Mix_ChannelFinished(ChannelFinishedCallback);

    memset(sdl->instances, 0, sizeof(sdl->instances));
    sdl->nextInstanceCounter = 0;

    fprintf(stderr, "Audio: SDLMixer audio engine initialized\n");
}

static void sdlDestroy(AudioSystem* audio) {
    SDL2AudioSystem* sdl = (SDL2AudioSystem*)audio;

    /*repeat(MAX_SOUND_INSTANCES, i) {
        if (sdl->instances[i].active) {
            ma_sound_uninit(&sdl->instances[i].maSound);
            if (ma->instances[i].ownsDecoder) {
                ma_decoder_uninit(&ma->instances[i].decoder);
            }
            ma->instances[i].active = false;
        }
    }

    // Free stream entries
    repeat(MAX_AUDIO_STREAMS, i) {
        if (sdl->streams[i].active) {
            free(sdl->streams[i].filePath);
        }
    }*/

    Mix_CloseAudio();
    free(sdl);
}

static void sdlUpdate(AudioSystem* audio, float deltaTime) {
    SDL2AudioSystem* sdl = (SDL2AudioSystem*)audio;

    repeat(MAX_SOUND_INSTANCES, i) {
        SoundInstance* inst = &sdl->instances[i];
        if (!inst->active) continue;

        // Handle gain fading (for cases where we do manual fading)
        if (inst->fadeTimeRemaining > 0.0f) {
            inst->fadeTimeRemaining -= deltaTime;
            if (0.0f >= inst->fadeTimeRemaining) {
                inst->fadeTimeRemaining = 0.0f;
                inst->currentGain = inst->targetGain;
            } else {
                float t = 1.0f - (inst->fadeTimeRemaining / inst->fadeTotalTime);
                inst->currentGain = inst->startGain + (inst->targetGain - inst->startGain) * t;
            }
            Mix_Volume(inst->channel, inst->currentGain * MIX_MAX_VOLUME);
        }
    }
}

static int32_t sdlPlaySound(AudioSystem* audio, int32_t soundIndex, MAYBE_UNUSED int32_t priority, MAYBE_UNUSED bool loop) {
    SDL2AudioSystem* sdl = (SDL2AudioSystem*)audio;

    // Check if this is a stream index (created by audio_create_stream)
    bool isStream = (soundIndex >= AUDIO_STREAM_INDEX_BASE);
    Sound* sound = nullptr;
    Mix_Chunk* chunk = nullptr;

    if (isStream) {
        int32_t streamSlot = soundIndex - AUDIO_STREAM_INDEX_BASE;
        if (0 > streamSlot || streamSlot >= MAX_AUDIO_STREAMS || !sdl->streams[streamSlot].active) {
            fprintf(stderr, "Audio: Invalid stream index %d\n", soundIndex);
            return -1;
        }

        chunk = sdl->streams[streamSlot].chunk;
    } else {
        DataWin* dw = sdl->dataWin; // Audio Group 0 should always be data.win
        if (0 > soundIndex || (uint32_t) soundIndex >= dw->sond.count) {
            fprintf(stderr, "Audio: Invalid sound index %d\n", soundIndex);
            return -1;
        }
        sound = &dw->sond.sounds[soundIndex];

        bool isEmbedded = (sound->flags & 0x01) != 0;
        bool isCompressed = (sound->flags & 0x02) != 0;

        if (isEmbedded || isCompressed) {
            // Embedded audio: decode from AUDO chunk memory
            if (0 > sound->audioFile || (uint32_t) sound->audioFile >= sdl->base.audioGroups[sound->audioGroup]->audo.count) {
                fprintf(stderr, "Audio: Invalid audio file index %d for sound '%s'\n", sound->audioFile, sound->name);
                return -1;
            }

            AudioEntry* entry = &sdl->base.audioGroups[sound->audioGroup]->audo.entries[sound->audioFile];
            chunk = Mix_LoadWAV_RW(SDL_RWFromMem(entry->data, entry->dataSize), 1);
        }
        else
        {
            return -1;
        }
    }

    // Embedded audio: decode from AUDO chunk memory
    /*if (0 > sound->audioFile || (uint32_t) sound->audioFile >= sdl->base.audioGroups[sound->audioGroup]->audo.count) {
        fprintf(stderr, "Audio: Invalid audio file index %d for sound '%s'\n", sound->audioFile, sound->name);
        return -1;
    }

    return -1;

    AudioEntry* entry = &sdl->base.audioGroups[sound->audioGroup]->audo.entries[sound->audioFile];*/

    // AudioEntry* entry = &(*sdl->base.audioGroups)->audo.entries[soundIndex];
    SoundInstance* slot = findFreeSlot(sdl);
    int32_t slotIndex = (int32_t) (slot - sdl->instances);

    float volume = isStream ? 1.0f : sound->volume;
    float pitch = isStream ? 1.0f : sound->pitch;
    slot->channel = Mix_PlayChannel(-1, chunk, loop ? -1 : 0);
    Mix_Volume(slot->channel, volume * MIX_MAX_VOLUME);

    // Set up instance tracking
    slot->active = true;
    slot->soundIndex = soundIndex;
    slot->instanceId = SOUND_INSTANCE_ID_BASE + slotIndex;
    slot->currentGain = volume;
    slot->targetGain = volume;
    slot->fadeTimeRemaining = 0.0f;
    slot->fadeTotalTime = 0.0f;
    slot->startGain = volume;
    slot->priority = priority;

    // Track unique IDs for disambiguation
    sdl->nextInstanceCounter++;
    return slot->instanceId;
}

static void sdlStopSound(MAYBE_UNUSED AudioSystem* audio, MAYBE_UNUSED int32_t soundOrInstance) {
    fprintf(stderr, "sdlStopSound: %d", soundOrInstance);
}

static void sdlStopAll(MAYBE_UNUSED AudioSystem* audio) {}

static bool sdlIsPlaying(AudioSystem* audio, int32_t soundOrInstance) {
    SDL2AudioSystem* sdl = (SDL2AudioSystem*)audio;

    if (soundOrInstance >= SOUND_INSTANCE_ID_BASE) {
        SoundInstance* inst = findInstanceById(sdl, soundOrInstance);
        return inst != nullptr && Mix_Playing(inst->channel);
    } else {
        // Check if any instance of this sound resource is playing
        repeat(MAX_SOUND_INSTANCES, i) {
            SoundInstance* inst = &sdl->instances[i];
            if (inst->active && inst->soundIndex == soundOrInstance && Mix_Playing(inst->channel)) {
                return true;
            }
        }
        return false;
    }
}

static void sdlPauseSound(AudioSystem* audio, int32_t soundOrInstance) {
    SDL2AudioSystem* sdl = (SDL2AudioSystem*) audio;

    if (soundOrInstance >= SOUND_INSTANCE_ID_BASE) {
        SoundInstance* inst = findInstanceById(sdl, soundOrInstance);
        if (inst != nullptr) {
            Mix_Pause(inst->channel);
        }
    } else {
        repeat(MAX_SOUND_INSTANCES, i) {
            SoundInstance* inst = &sdl->instances[i];
            if (inst->active && inst->soundIndex == soundOrInstance) {
                Mix_Pause(inst->channel);
            }
        }
    }
}

static void sdlResumeSound(MAYBE_UNUSED AudioSystem* audio, MAYBE_UNUSED int32_t soundOrInstance) {
    SDL2AudioSystem* sdl = (SDL2AudioSystem*) audio;

    if (soundOrInstance >= SOUND_INSTANCE_ID_BASE) {
        SoundInstance* inst = findInstanceById(sdl, soundOrInstance);
        if (inst != nullptr) {
            Mix_Resume(inst->channel);
        }
    } else {
        repeat(MAX_SOUND_INSTANCES, i) {
            SoundInstance* inst = &sdl->instances[i];
            if (inst->active && inst->soundIndex == soundOrInstance) {
                Mix_Resume(inst->channel);
            }
        }
    }
}

static void sdlPauseAll(MAYBE_UNUSED AudioSystem* audio) {}

static void sdlResumeAll(MAYBE_UNUSED AudioSystem* audio) {}

static void sdlSetSoundGain(AudioSystem* audio, int32_t soundOrInstance, float gain, uint32_t timeMs) {
    fprintf(stderr, "sdlSetSoundGain: %d", soundOrInstance);
    SDL2AudioSystem* sdl = (SDL2AudioSystem*)audio;
    SoundInstance* instance = findInstanceById(sdl, soundOrInstance);
    
    if (soundOrInstance >= SOUND_INSTANCE_ID_BASE) {
        SoundInstance* inst = findInstanceById(sdl, soundOrInstance);
        if (inst != nullptr) {
            if (timeMs == 0) {
                inst->currentGain = gain;
                inst->targetGain = gain;
                inst->fadeTimeRemaining = 0.0f;
                Mix_Volume(inst->channel, gain * MIX_MAX_VOLUME);
            } else {
                inst->startGain = inst->currentGain;
                inst->targetGain = gain;
                inst->fadeTotalTime = (float) timeMs / 1000.0f;
                inst->fadeTimeRemaining = inst->fadeTotalTime;
            }
        }
    } else {
        repeat(MAX_SOUND_INSTANCES, i) {
            SoundInstance* inst = &sdl->instances[i];
            if (inst->active && inst->soundIndex == soundOrInstance) {
                if (timeMs == 0) {
                    inst->currentGain = gain;
                    inst->targetGain = gain;
                    inst->fadeTimeRemaining = 0.0f;
                    Mix_Volume(inst->channel, gain * MIX_MAX_VOLUME);
                } else {
                    inst->startGain = inst->currentGain;
                    inst->targetGain = gain;
                    inst->fadeTotalTime = (float) timeMs / 1000.0f;
                    inst->fadeTimeRemaining = inst->fadeTotalTime;
                }
            }
        }
    }
}

static float sdlGetSoundGain(AudioSystem* audio, int32_t soundOrInstance) {
    fprintf(stderr, "sdlGetSoundGain: %d", soundOrInstance);
    return 1.0f;
}

static void sdlSetSoundPitch(MAYBE_UNUSED AudioSystem* audio, MAYBE_UNUSED int32_t soundOrInstance, MAYBE_UNUSED float pitch) {
    fprintf(stderr, "sdlSetSoundPitch: %d, %d", soundOrInstance, pitch);
}

static float sdlGetSoundPitch(MAYBE_UNUSED AudioSystem* audio, MAYBE_UNUSED int32_t soundOrInstance) {
    fprintf(stderr, "sdlGetSoundPitch: %d", soundOrInstance);
    return 1.0f;
}

static float sdlGetTrackPosition(MAYBE_UNUSED AudioSystem* audio, MAYBE_UNUSED int32_t soundOrInstance) {
    fprintf(stderr, "sdlGetTrackPosition: %d", soundOrInstance);
    return 0.0f;
}

static void sdlSetTrackPosition(MAYBE_UNUSED AudioSystem* audio, MAYBE_UNUSED int32_t soundOrInstance, MAYBE_UNUSED float positionSeconds) {
    fprintf(stderr, "sdlSetTrackPosition: %d, %d", soundOrInstance, positionSeconds);
}

// Return 1.0s (not 0) so GML code that divides by audio length doesn't hit division-by-zero.
static float sdlGetSoundLength(MAYBE_UNUSED AudioSystem* audio, MAYBE_UNUSED int32_t soundOrInstance) {
    fprintf(stderr, "sdlGetSoundLength: %d", soundOrInstance);
    return 1.0f;
}

static void sdlSetMasterGain(MAYBE_UNUSED AudioSystem* audio, MAYBE_UNUSED float gain) {
    fprintf(stderr, "sdlSetMasterGain: %d", gain);
}

static void sdlSetChannelCount(MAYBE_UNUSED AudioSystem* audio, MAYBE_UNUSED int32_t count) {
    fprintf(stderr, "sdlSetChannelCount: %d", count);
}

static void sdlGroupLoad(AudioSystem* audio, int32_t groupIndex) {
    if (groupIndex > 0) {
        int sz = snprintf(nullptr, 0, "audiogroup%d.dat", groupIndex);
        char buf[sz + 1];
        snprintf(buf, sizeof(buf), "audiogroup%d.dat", groupIndex);
        DataWin *audioGroup = DataWin_parse(((SDL2AudioSystem*)audio)->fileSystem->vtable->resolvePath(((SDL2AudioSystem*)audio)->fileSystem, buf),
        (DataWinParserOptions) {
            .parseAudo = true,
        });
        arrput(audio->audioGroups, audioGroup);
    }
}

static bool sdlGroupIsLoaded(AudioSystem* audio, int32_t groupIndex) {
    return (arrlen(audio->audioGroups) > groupIndex);
}

static int32_t sdlCreateStream(AudioSystem* audio, const char* filename) {
    SDL2AudioSystem* sdl = (SDL2AudioSystem*)audio;

    // Find a free stream slot
    int32_t freeSlot = -1;
    repeat(MAX_AUDIO_STREAMS, i) {
        if (!sdl->streams[i].active) {
            freeSlot = (int32_t) i;
            break;
        }
    }

    if (0 > freeSlot) {
        fprintf(stderr, "Audio: No free stream slots for '%s'\n", filename);
        return -1;
    }

    char* resolved = sdl->fileSystem->vtable->resolvePath(sdl->fileSystem, filename);
    if (resolved == nullptr) {
        fprintf(stderr, "Audio: Could not resolve path for stream '%s'\n", filename);
        return -1;
    }

    sdl->streams[freeSlot].active = true;
    sdl->streams[freeSlot].filePath = resolved;
    sdl->streams[freeSlot].chunk = Mix_LoadWAV(resolved);

    int32_t streamIndex = AUDIO_STREAM_INDEX_BASE + freeSlot;
    fprintf(stderr, "Audio: Created stream %d for '%s' -> '%s'\n", streamIndex, resolved, resolved);
    return streamIndex;
}

static bool sdlDestroyStream(MAYBE_UNUSED AudioSystem* audio, MAYBE_UNUSED int32_t streamIndex) {
    SDL2AudioSystem* sdl = (SDL2AudioSystem*)audio;

    int32_t slotIndex = streamIndex - AUDIO_STREAM_INDEX_BASE;
    if (0 > slotIndex || slotIndex >= MAX_AUDIO_STREAMS) {
        fprintf(stderr, "Audio: Invalid stream index %d for destroy\n", streamIndex);
        return false;
    }

    AudioStreamEntry* entry = &sdl->streams[slotIndex];
    if (!entry->active) return false;

    // Stop all sound instances that were playing this stream
    repeat(MAX_SOUND_INSTANCES, i) {
        SoundInstance* inst = &sdl->instances[i];
        if (inst->active && inst->soundIndex == streamIndex) {
            Mix_HaltChannel(inst->channel);
            inst->active = false;
        }
    }

    free(entry->filePath);
    entry->filePath = nullptr;
    entry->active = false;
    fprintf(stderr, "Audio: Destroyed stream %d\n", streamIndex);
    return true;
}

static AudioSystemVtable sdlVtable = {
    .init = sdlInit,
    .destroy = sdlDestroy,
    .update = sdlUpdate,
    .playSound = sdlPlaySound,
    .stopSound = sdlStopSound,
    .stopAll = sdlStopAll,
    .isPlaying = sdlIsPlaying,
    .pauseSound = sdlPauseSound,
    .resumeSound = sdlResumeSound,
    .pauseAll = sdlPauseAll,
    .resumeAll = sdlResumeAll,
    .setSoundGain = sdlSetSoundGain,
    .getSoundGain = sdlGetSoundGain,
    .setSoundPitch = sdlSetSoundPitch,
    .getSoundPitch = sdlGetSoundPitch,
    .getTrackPosition = sdlGetTrackPosition,
    .setTrackPosition = sdlSetTrackPosition,
    .getSoundLength = sdlGetSoundLength,
    .setMasterGain = sdlSetMasterGain,
    .setChannelCount = sdlSetChannelCount,
    .groupLoad = sdlGroupLoad,
    .groupIsLoaded = sdlGroupIsLoaded,
    .createStream = sdlCreateStream,
    .destroyStream = sdlDestroyStream,
};

SDL2AudioSystem* SDL2AudioSystem_create(void) {
    SDL2AudioSystem* audio = calloc(1, sizeof(SDL2AudioSystem));
    audio->base.vtable = &sdlVtable;
    return audio;
}