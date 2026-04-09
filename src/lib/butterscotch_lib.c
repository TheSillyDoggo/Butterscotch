#include "butterscotch_lib.h"
#include "software_renderer.h"
#include "data_win.h"
#include "vm.h"
#include "runner.h"
#include "runner_keyboard.h"
#include "noop_audio_system.h"
#include "glfw_file_system.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ===[ Internal Context ]===

struct ButterscotchContext {
    DataWin* dataWin;
    VMContext* vm;
    Runner* runner;
    Renderer* renderer;
    AudioSystem* audioSystem;
    GlfwFileSystem* fileSystem;
    SoftwareRenderer* swRenderer; // same pointer as renderer, for framebuffer access
};

// ===[ Public API ]===

BUTTERSCOTCH_API ButterscotchContext* butterscotch_create(const char* dataWinPath) {
    if (dataWinPath == nullptr) return nullptr;

    fprintf(stderr, "Butterscotch: Loading %s...\n", dataWinPath);

    DataWin* dataWin = DataWin_parse(
        dataWinPath,
        (DataWinParserOptions) {
            .parseGen8 = true,
            .parseOptn = true,
            .parseLang = true,
            .parseExtn = true,
            .parseSond = true,
            .parseAgrp = true,
            .parseSprt = true,
            .parseBgnd = true,
            .parsePath = true,
            .parseScpt = true,
            .parseGlob = true,
            .parseShdr = true,
            .parseFont = true,
            .parseTmln = true,
            .parseObjt = true,
            .parseRoom = true,
            .parseTpag = true,
            .parseCode = true,
            .parseVari = true,
            .parseFunc = true,
            .parseStrg = true,
            .parseTxtr = true,
            .parseAudo = true,
            .skipLoadingPreciseMasksForNonPreciseSprites = true
        }
    );

    if (dataWin == nullptr) {
        fprintf(stderr, "Butterscotch: Failed to parse data.win\n");
        return nullptr;
    }

    fprintf(stderr, "Butterscotch: Loaded \"%s\" (%d) [Bytecode Version %u]\n",
        dataWin->gen8.name, dataWin->gen8.gameID, dataWin->gen8.bytecodeVersion);

    // Initialize VM
    VMContext* vm = VM_create(dataWin);

    // Initialize file system (POSIX-based, derived from data.win path)
    GlfwFileSystem* fileSystem = GlfwFileSystem_create(dataWinPath);

    // Initialize runner
    Runner* runner = Runner_create(dataWin, vm, (FileSystem*) fileSystem);

    // Initialize software renderer
    Renderer* renderer = SoftwareRenderer_create();
    renderer->vtable->init(renderer, dataWin);
    runner->renderer = renderer;

    // Initialize noop audio system
    NoopAudioSystem* noopAudio = NoopAudioSystem_create();
    AudioSystem* audioSystem = (AudioSystem*) noopAudio;
    audioSystem->vtable->init(audioSystem, dataWin, (FileSystem*) fileSystem);
    runner->audioSystem = audioSystem;

    // Initialize first room and fire Game Start / Room Start events
    Runner_initFirstRoom(runner);

    // Build context
    ButterscotchContext* ctx = safeMalloc(sizeof(ButterscotchContext));
    ctx->dataWin = dataWin;
    ctx->vm = vm;
    ctx->runner = runner;
    ctx->renderer = renderer;
    ctx->audioSystem = audioSystem;
    ctx->fileSystem = fileSystem;
    ctx->swRenderer = (SoftwareRenderer*) renderer;

    fprintf(stderr, "Butterscotch: Initialized successfully\n");
    return ctx;
}

BUTTERSCOTCH_API void butterscotch_free(ButterscotchContext* ctx) {
    if (ctx == nullptr) return;

    ctx->audioSystem->vtable->destroy(ctx->audioSystem);
    ctx->renderer->vtable->destroy(ctx->renderer);
    Runner_free(ctx->runner);
    GlfwFileSystem_destroy(ctx->fileSystem);
    VM_free(ctx->vm);
    DataWin_free(ctx->dataWin);
    free(ctx);
}

BUTTERSCOTCH_API void butterscotch_beginFrame(ButterscotchContext* ctx) {
    if (ctx == nullptr) return;
    RunnerKeyboard_beginFrame(ctx->runner->keyboard);
}

BUTTERSCOTCH_API void butterscotch_step(ButterscotchContext* ctx) {
    if (ctx == nullptr) return;
    Runner_step(ctx->runner);
    ctx->audioSystem->vtable->update(ctx->audioSystem, 1.0f / 30.0f);
}

BUTTERSCOTCH_API void butterscotch_draw(ButterscotchContext* ctx) {
    if (ctx == nullptr) return;

    Runner* runner = ctx->runner;
    Renderer* renderer = ctx->renderer;
    DataWin* dw = ctx->dataWin;
    Room* activeRoom = runner->currentRoom;

    // Compute game resolution
    int32_t gameW = (int32_t) dw->gen8.defaultWindowWidth;
    int32_t gameH = (int32_t) dw->gen8.defaultWindowHeight;

    bool viewsEnabled = (activeRoom->flags & 1) != 0;
    if (viewsEnabled) {
        int32_t maxRight = 0;
        int32_t maxBottom = 0;
        repeat(8, vi) {
            if (!activeRoom->views[vi].enabled) continue;
            int32_t right = activeRoom->views[vi].portX + activeRoom->views[vi].portWidth;
            int32_t bottom = activeRoom->views[vi].portY + activeRoom->views[vi].portHeight;
            if (right > maxRight) maxRight = right;
            if (bottom > maxBottom) maxBottom = bottom;
        }
        if (maxRight > 0 && maxBottom > 0) {
            gameW = maxRight;
            gameH = maxBottom;
        }
    }

    renderer->vtable->beginFrame(renderer, gameW, gameH, gameW, gameH);

    // Clear with room background color
    if (runner->drawBackgroundColor) {
        SoftwareRenderer* sw = ctx->swRenderer;
        uint8_t rr = BGR_R(runner->backgroundColor);
        uint8_t gg = BGR_G(runner->backgroundColor);
        uint8_t bb = BGR_B(runner->backgroundColor);
        size_t pixelCount = (size_t) sw->fbWidth * (size_t) sw->fbHeight;
        for (size_t i = 0; pixelCount > i; i++) {
            sw->framebuffer[i * 4 + 0] = rr;
            sw->framebuffer[i * 4 + 1] = gg;
            sw->framebuffer[i * 4 + 2] = bb;
            sw->framebuffer[i * 4 + 3] = 255;
        }
    }

    // Render views
    bool anyViewRendered = false;
    if (viewsEnabled) {
        repeat(8, vi) {
            if (!activeRoom->views[vi].enabled) continue;

            int32_t viewX = activeRoom->views[vi].viewX;
            int32_t viewY = activeRoom->views[vi].viewY;
            int32_t viewW = activeRoom->views[vi].viewWidth;
            int32_t viewH = activeRoom->views[vi].viewHeight;
            int32_t portX = activeRoom->views[vi].portX;
            int32_t portY = activeRoom->views[vi].portY;
            int32_t portW = activeRoom->views[vi].portWidth;
            int32_t portH = activeRoom->views[vi].portHeight;
            float viewAngle = runner->viewAngles[vi];

            runner->viewCurrent = vi;
            renderer->vtable->beginView(renderer, viewX, viewY, viewW, viewH, portX, portY, portW, portH, viewAngle);
            Runner_draw(runner);
            renderer->vtable->endView(renderer);
            anyViewRendered = true;
        }
    }

    if (!anyViewRendered) {
        runner->viewCurrent = 0;
        renderer->vtable->beginView(renderer, 0, 0, gameW, gameH, 0, 0, gameW, gameH, 0.0f);
        Runner_draw(runner);
        renderer->vtable->endView(renderer);
    }

    runner->viewCurrent = 0;
    renderer->vtable->endFrame(renderer);
}

BUTTERSCOTCH_API const uint8_t* butterscotch_getFramebuffer(ButterscotchContext* ctx) {
    if (ctx == nullptr) return nullptr;
    return ctx->swRenderer->framebuffer;
}

BUTTERSCOTCH_API int32_t butterscotch_getFramebufferWidth(ButterscotchContext* ctx) {
    if (ctx == nullptr) return 0;
    return ctx->swRenderer->fbWidth;
}

BUTTERSCOTCH_API int32_t butterscotch_getFramebufferHeight(ButterscotchContext* ctx) {
    if (ctx == nullptr) return 0;
    return ctx->swRenderer->fbHeight;
}

BUTTERSCOTCH_API int32_t butterscotch_getRoomSpeed(ButterscotchContext* ctx) {
    if (ctx == nullptr) return 30;
    if (ctx->runner->currentRoom == nullptr) return 30;
    return ctx->runner->currentRoom->speed;
}

BUTTERSCOTCH_API bool butterscotch_shouldExit(ButterscotchContext* ctx) {
    if (ctx == nullptr) return true;
    return ctx->runner->shouldExit;
}

BUTTERSCOTCH_API void butterscotch_keyDown(ButterscotchContext* ctx, int32_t keyCode) {
    if (ctx == nullptr) return;
    RunnerKeyboard_onKeyDown(ctx->runner->keyboard, keyCode);
}

BUTTERSCOTCH_API void butterscotch_keyUp(ButterscotchContext* ctx, int32_t keyCode) {
    if (ctx == nullptr) return;
    RunnerKeyboard_onKeyUp(ctx->runner->keyboard, keyCode);
}

// ===[ CallbackAudioSystem ]===

typedef struct {
    AudioSystem base;
    ButterscotchAudioCallbacks callbacks;
} CallbackAudioSystem;

static void callbackInit(MAYBE_UNUSED AudioSystem* audio, MAYBE_UNUSED DataWin* dataWin, MAYBE_UNUSED FileSystem* fileSystem) {}

static void callbackDestroy(AudioSystem* audio) {
    free(audio);
}

static void callbackUpdate(MAYBE_UNUSED AudioSystem* audio, MAYBE_UNUSED float deltaTime) {}

static int32_t callbackPlaySound(AudioSystem* audio, int32_t soundIndex, MAYBE_UNUSED int32_t priority, bool loop) {
    CallbackAudioSystem* cb = (CallbackAudioSystem*) audio;
    if (cb->callbacks.playSound != nullptr)
        return cb->callbacks.playSound(cb->callbacks.userData, soundIndex, loop);
    return -1;
}

static void callbackStopSound(AudioSystem* audio, int32_t soundOrInstance) {
    CallbackAudioSystem* cb = (CallbackAudioSystem*) audio;
    if (cb->callbacks.stopSound != nullptr)
        cb->callbacks.stopSound(cb->callbacks.userData, soundOrInstance);
}

static void callbackStopAll(AudioSystem* audio) {
    CallbackAudioSystem* cb = (CallbackAudioSystem*) audio;
    if (cb->callbacks.stopAll != nullptr)
        cb->callbacks.stopAll(cb->callbacks.userData);
}

static bool callbackIsPlaying(AudioSystem* audio, int32_t soundOrInstance) {
    CallbackAudioSystem* cb = (CallbackAudioSystem*) audio;
    if (cb->callbacks.isPlaying != nullptr)
        return cb->callbacks.isPlaying(cb->callbacks.userData, soundOrInstance);
    return false;
}

static void callbackPauseSound(AudioSystem* audio, int32_t soundOrInstance) {
    CallbackAudioSystem* cb = (CallbackAudioSystem*) audio;
    if (cb->callbacks.pauseSound != nullptr)
        cb->callbacks.pauseSound(cb->callbacks.userData, soundOrInstance);
}

static void callbackResumeSound(AudioSystem* audio, int32_t soundOrInstance) {
    CallbackAudioSystem* cb = (CallbackAudioSystem*) audio;
    if (cb->callbacks.resumeSound != nullptr)
        cb->callbacks.resumeSound(cb->callbacks.userData, soundOrInstance);
}

static void callbackPauseAll(AudioSystem* audio) {
    CallbackAudioSystem* cb = (CallbackAudioSystem*) audio;
    if (cb->callbacks.pauseAll != nullptr)
        cb->callbacks.pauseAll(cb->callbacks.userData);
}

static void callbackResumeAll(AudioSystem* audio) {
    CallbackAudioSystem* cb = (CallbackAudioSystem*) audio;
    if (cb->callbacks.resumeAll != nullptr)
        cb->callbacks.resumeAll(cb->callbacks.userData);
}

static void callbackSetSoundGain(AudioSystem* audio, int32_t soundOrInstance, float gain, uint32_t timeMs) {
    CallbackAudioSystem* cb = (CallbackAudioSystem*) audio;
    if (cb->callbacks.setSoundGain != nullptr)
        cb->callbacks.setSoundGain(cb->callbacks.userData, soundOrInstance, gain, timeMs);
}

static float callbackGetSoundGain(AudioSystem* audio, int32_t soundOrInstance) {
    CallbackAudioSystem* cb = (CallbackAudioSystem*) audio;
    if (cb->callbacks.getSoundGain != nullptr)
        return cb->callbacks.getSoundGain(cb->callbacks.userData, soundOrInstance);
    return 1.0f;
}

static void callbackSetSoundPitch(AudioSystem* audio, int32_t soundOrInstance, float pitch) {
    CallbackAudioSystem* cb = (CallbackAudioSystem*) audio;
    if (cb->callbacks.setSoundPitch != nullptr)
        cb->callbacks.setSoundPitch(cb->callbacks.userData, soundOrInstance, pitch);
}

static float callbackGetSoundPitch(AudioSystem* audio, int32_t soundOrInstance) {
    CallbackAudioSystem* cb = (CallbackAudioSystem*) audio;
    if (cb->callbacks.getSoundPitch != nullptr)
        return cb->callbacks.getSoundPitch(cb->callbacks.userData, soundOrInstance);
    return 1.0f;
}

static float callbackGetTrackPosition(AudioSystem* audio, int32_t soundOrInstance) {
    CallbackAudioSystem* cb = (CallbackAudioSystem*) audio;
    if (cb->callbacks.getTrackPosition != nullptr)
        return cb->callbacks.getTrackPosition(cb->callbacks.userData, soundOrInstance);
    return 0.0f;
}

static void callbackSetTrackPosition(AudioSystem* audio, int32_t soundOrInstance, float positionSeconds) {
    CallbackAudioSystem* cb = (CallbackAudioSystem*) audio;
    if (cb->callbacks.setTrackPosition != nullptr)
        cb->callbacks.setTrackPosition(cb->callbacks.userData, soundOrInstance, positionSeconds);
}

static void callbackSetMasterGain(AudioSystem* audio, float gain) {
    CallbackAudioSystem* cb = (CallbackAudioSystem*) audio;
    if (cb->callbacks.setMasterGain != nullptr)
        cb->callbacks.setMasterGain(cb->callbacks.userData, gain);
}

static void callbackSetChannelCount(MAYBE_UNUSED AudioSystem* audio, MAYBE_UNUSED int32_t count) {}
static void callbackGroupLoad(MAYBE_UNUSED AudioSystem* audio, MAYBE_UNUSED int32_t groupIndex) {}
static bool callbackGroupIsLoaded(MAYBE_UNUSED AudioSystem* audio, MAYBE_UNUSED int32_t groupIndex) { return true; }
static int32_t callbackCreateStream(MAYBE_UNUSED AudioSystem* audio, MAYBE_UNUSED const char* filename) { return -1; }
static bool callbackDestroyStream(MAYBE_UNUSED AudioSystem* audio, MAYBE_UNUSED int32_t streamIndex) { return false; }

static AudioSystemVtable callbackVtable = {
    .init = callbackInit,
    .destroy = callbackDestroy,
    .update = callbackUpdate,
    .playSound = callbackPlaySound,
    .stopSound = callbackStopSound,
    .stopAll = callbackStopAll,
    .isPlaying = callbackIsPlaying,
    .pauseSound = callbackPauseSound,
    .resumeSound = callbackResumeSound,
    .pauseAll = callbackPauseAll,
    .resumeAll = callbackResumeAll,
    .setSoundGain = callbackSetSoundGain,
    .getSoundGain = callbackGetSoundGain,
    .setSoundPitch = callbackSetSoundPitch,
    .getSoundPitch = callbackGetSoundPitch,
    .getTrackPosition = callbackGetTrackPosition,
    .setTrackPosition = callbackSetTrackPosition,
    .setMasterGain = callbackSetMasterGain,
    .setChannelCount = callbackSetChannelCount,
    .groupLoad = callbackGroupLoad,
    .groupIsLoaded = callbackGroupIsLoaded,
    .createStream = callbackCreateStream,
    .destroyStream = callbackDestroyStream,
};

BUTTERSCOTCH_API void butterscotch_setAudioCallbacks(ButterscotchContext* ctx, ButterscotchAudioCallbacks* callbacks) {
    if (ctx == nullptr || callbacks == nullptr) return;

    // Destroy the current audio system
    ctx->audioSystem->vtable->destroy(ctx->audioSystem);

    // Build a new CallbackAudioSystem with the provided callbacks (copied by value)
    CallbackAudioSystem* cb = calloc(1, sizeof(CallbackAudioSystem));
    cb->base.vtable = &callbackVtable;
    cb->callbacks = *callbacks;

    ctx->audioSystem = (AudioSystem*) cb;
    ctx->runner->audioSystem = ctx->audioSystem;
}

// ===[ Sound Info API ]===

BUTTERSCOTCH_API int32_t butterscotch_getSoundCount(ButterscotchContext* ctx) {
    if (ctx == nullptr) return 0;
    return (int32_t) ctx->dataWin->sond.count;
}

BUTTERSCOTCH_API void butterscotch_getSoundInfo(ButterscotchContext* ctx, int32_t soundIndex, ButterscotchSoundInfo* outInfo) {
    if (ctx == nullptr || outInfo == nullptr) return;
    if (0 > soundIndex || soundIndex >= (int32_t) ctx->dataWin->sond.count) return;

    Sound* snd = &ctx->dataWin->sond.sounds[soundIndex];
    outInfo->name = snd->name;
    outInfo->file = snd->file;
    outInfo->isEmbedded = (snd->flags & 0x01) != 0;
    outInfo->volume = snd->volume;
    outInfo->pitch = snd->pitch;
}

BUTTERSCOTCH_API const uint8_t* butterscotch_getSoundData(ButterscotchContext* ctx, int32_t soundIndex, int32_t* outSize) {
    if (outSize != nullptr) *outSize = 0;
    if (ctx == nullptr) return nullptr;
    if (0 > soundIndex || soundIndex >= (int32_t) ctx->dataWin->sond.count) return nullptr;

    Sound* snd = &ctx->dataWin->sond.sounds[soundIndex];
    if (0 > snd->audioFile || snd->audioFile >= (int32_t) ctx->dataWin->audo.count) return nullptr;

    AudioEntry* entry = &ctx->dataWin->audo.entries[snd->audioFile];
    if (outSize != nullptr) *outSize = (int32_t) entry->dataSize;
    return entry->data;
}
