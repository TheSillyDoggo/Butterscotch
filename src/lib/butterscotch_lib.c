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
