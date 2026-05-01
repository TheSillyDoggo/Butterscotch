#include "data_win.h"
#include "vm.h"

#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <emscripten.h>

#include "runner_keyboard.h"
#include "runner.h"
#include "../sdl2/sdl2_file_system.h"
#include "../sdl2/sdl2_audio_system.h"
#include "../sdl2/sdl2_renderer.h"
#include "../sdl2/sdl2_gamepad.h"
#include "noop_audio_system.h"

#include "utils.h"
#include "profiler.h"

static Runner* runner;
static Renderer* renderer;
static SDL_Window* window;
static DataWin* dataWin;

static bool shouldClose = false;
static double lastFrameTime = 0.0;

static void unlockAudio(void)
{
    EM_ASM({
        if (typeof Module !== 'undefined' && Module.SDL2 && Module.SDL2.audioContext) {
            Module.SDL2.audioContext.resume();
        }
    });
}


static int32_t sdl2KeyToGml(int sdl2Key) {
    if (sdl2Key >= SDLK_a && sdl2Key <= SDLK_z) return sdl2Key - 32;
    if (sdl2Key >= SDLK_0 && sdl2Key <= SDLK_9) return sdl2Key;

    switch (sdl2Key) {
        case SDLK_ESCAPE: return VK_ESCAPE;
        case SDLK_RETURN: return VK_ENTER;
        case SDLK_TAB: return VK_TAB;
        case SDLK_BACKSPACE: return VK_BACKSPACE;
        case SDLK_SPACE: return VK_SPACE;
        case SDLK_LSHIFT:
        case SDLK_RSHIFT: return VK_SHIFT;
        case SDLK_LCTRL:
        case SDLK_RCTRL: return VK_CONTROL;
        case SDLK_LALT:
        case SDLK_RALT: return VK_ALT;
        case SDLK_UP: return VK_UP;
        case SDLK_DOWN: return VK_DOWN;
        case SDLK_LEFT: return VK_LEFT;
        case SDLK_RIGHT: return VK_RIGHT;
        default: return -1;
    }
}

static void keyCallback(Runner* runner, int key, int action) {
    int32_t gmlKey = sdl2KeyToGml(key);
    if (gmlKey < 0) return;

    if (action == 1) RunnerKeyboard_onKeyDown(runner->keyboard, gmlKey);
    else if (action == 0) RunnerKeyboard_onKeyUp(runner->keyboard, gmlKey);
}

static void setSDL2WindowTitle(void* window, const char* title) {
    SDL_SetWindowTitle((SDL_Window*)window, title);
}

static void mainLoop(void) {
    SDL_Event sdlEvent;

    RunnerKeyboard_beginFrame(runner->keyboard);
    RunnerGamepad_beginFrame(runner->gamepads);

    while (SDL_PollEvent(&sdlEvent)) {
        switch (sdlEvent.type) {
            case SDL_QUIT:
                shouldClose = true;
                break;

            case SDL_KEYDOWN:
                unlockAudio();
                keyCallback(runner, sdlEvent.key.keysym.sym, 1);
                break;

            case SDL_KEYUP:
                keyCallback(runner, sdlEvent.key.keysym.sym, 0);
                break;

            case SDL_CONTROLLERDEVICEADDED:
                SDL_GameControllerOpen(sdlEvent.cdevice.which);
                break;

            case SDL_MOUSEBUTTONDOWN:
                unlockAudio();
                break;
        }
    }

    SDL2Gamepad_poll(runner->gamepads);

    if (shouldClose || runner->shouldExit) {
        emscripten_cancel_main_loop();
        return;
    }

    // Run one game step (Begin Step, Keyboard, Alarms, Step, End Step, room transitions)
    Runner_step(runner);

    // Update audio system (gain fading, cleanup ended sounds)
    float dt = (float)(((double)SDL_GetTicks64() / 1000.0) - lastFrameTime);
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.1f) dt = 0.1f;

    runner->audioSystem->vtable->update(runner->audioSystem, dt);

    Room* activeRoom = runner->currentRoom;

    // Query actual framebuffer size (differs from window size on Wayland with fractional scaling)
    int fbWidth, fbHeight;
    SDL_GL_GetDrawableSize(window, &fbWidth, &fbHeight);

    Gen8* gen8 = &dataWin->gen8;
    int32_t gameW = (int32_t) gen8->defaultWindowWidth;
    int32_t gameH = (int32_t) gen8->defaultWindowHeight;

    // The application surface (FBO) is sized to defaultWindowWidth x defaultWindowHeight.
    // It is a bit hard to understand, but here's how it works:
    // The Port X/Port Y controls the position of the game viewport within the application surface.
    // The Port W/Port H controls the size of the game viewport within the application surface.
    // Think of it like if you had an image (or... well, a framebuffer) and you are "pasting" it over the application surface.
    // And the Port W/Port H are scaled by the window size too (set by the GEN8 chunk)
    float displayScaleX = 1.0f;
    float displayScaleY = 1.0f;
    bool viewsEnabled = (activeRoom->flags & 1) != 0;
    if (viewsEnabled) {
        int32_t minLeft = INT32_MAX, minTop = INT32_MAX;
        int32_t maxRight = INT32_MIN, maxBottom = INT32_MIN;
        repeat(MAX_VIEWS, vi) {
            RuntimeView* view = &runner->views[vi];
            if (!view->enabled) continue;
            if (minLeft > view->portX) minLeft = view->portX;
            if (minTop > view->portY) minTop = view->portY;
            int32_t right = view->portX + view->portWidth;
            int32_t bottom = view->portY + view->portHeight;
            if (right > maxRight) maxRight = right;
            if (bottom > maxBottom) maxBottom = bottom;
        }
        if (maxRight > minLeft && maxBottom > minTop) {
            displayScaleX = (float) gameW / (float) (maxRight - minLeft);
            displayScaleY = (float) gameH / (float) (maxBottom - minTop);
        }
    }

    renderer->vtable->beginFrame(renderer, gameW, gameH, fbWidth, fbHeight);

    // Render each enabled view (or a default full-screen view if views are disabled)
    bool anyViewRendered = false;

    if (viewsEnabled) {
        repeat(MAX_VIEWS, vi) {
            RuntimeView* view = &runner->views[vi];
            if (!view->enabled) continue;

            int32_t viewX = view->viewX;
            int32_t viewY = view->viewY;
            int32_t viewW = view->viewWidth;
            int32_t viewH = view->viewHeight;
            int32_t portX = (int32_t) ((float) view->portX * displayScaleX + 0.5f);
            int32_t portY = (int32_t) ((float) view->portY * displayScaleY + 0.5f);
            int32_t portW = (int32_t) ((float) view->portWidth * displayScaleX + 0.5f);
            int32_t portH = (int32_t) ((float) view->portHeight * displayScaleY + 0.5f);
            float viewAngle = view->viewAngle;

            runner->viewCurrent = vi;
            renderer->vtable->beginView(renderer, viewX, viewY, viewW, viewH, portX, portY, portW, portH, viewAngle);

            Runner_draw(runner);

            renderer->vtable->endView(renderer);

            int32_t guiW = runner->guiWidth > 0 ? runner->guiWidth : portW;
            int32_t guiH = runner->guiHeight > 0 ? runner->guiHeight : portH;
            renderer->vtable->beginGUI(renderer, guiW, guiH, portX, portY, portW, portH);
            Runner_drawGUI(runner);
            renderer->vtable->endGUI(renderer);

            anyViewRendered = true;
        }
    }

    if (!anyViewRendered) {
        // No views enabled or views disabled: render with default full-screen view
        runner->viewCurrent = 0;
        renderer->vtable->beginView(renderer, 0, 0, gameW, gameH, 0, 0, gameW, gameH, 0.0f);
        Runner_draw(runner);

        renderer->vtable->endView(renderer);

        int32_t guiW = runner->guiWidth > 0 ? runner->guiWidth : gameW;
        int32_t guiH = runner->guiHeight > 0 ? runner->guiHeight : gameH;
        renderer->vtable->beginGUI(renderer, guiW, guiH, 0, 0, gameW, gameH);
        Runner_drawGUI(runner);
        renderer->vtable->endGUI(renderer);
    }

    // Reset view_current to 0 so non-Draw events (Step, Alarm, Create) see view_current = 0
    runner->viewCurrent = 0;

    renderer->vtable->endFrame(renderer);


    // Limit frame rate to room speed
    if (runner->currentRoom->speed > 0) {
        double targetFrameTime = 1.0 / (runner->currentRoom->speed);
        double nextFrameTime = lastFrameTime + targetFrameTime;
        // Sleep for most of the remaining time, then spin-wait for precision
        double remaining = nextFrameTime - ((double)SDL_GetTicks64() / 1000.0);
        if (remaining > 0.002) {
            SDL_Delay((remaining - 0.001) * 1000);
        }
        while (((double)SDL_GetTicks64() / 1000.0) < nextFrameTime) {
            // Spin-wait for the remaining sub-millisecond
        }
        lastFrameTime = nextFrameTime;
    } else {
        lastFrameTime = ((double)SDL_GetTicks64() / 1000.0);
    }
}

int main(int argc, char* argv[]) {
    fprintf(stderr, "Loading data.win...\n");

    dataWin = DataWin_parse(
        "./game/data.win",
        (DataWinParserOptions){
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
            .skipLoadingPreciseMasksForNonPreciseSprites = true,
            .lazyLoadRooms = true,
            .eagerlyLoadedRooms = nullptr,
            .progressCallback = nullptr
        }
    );

    Gen8* gen8 = &dataWin->gen8;

    fprintf(stderr, "Loaded %s successfully\n", gen8->name);

    VMContext* vm = VM_create(dataWin);
    SDL2FileSystem* fs = SDL2FileSystem_create(dataWin->lazyLoadFilePath);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO) < 0) {
        fprintf(stderr, "SDL init failed\n");
        return 1;
    }

    window = SDL_CreateWindow(
        gen8->displayName,
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        (int)gen8->defaultWindowWidth,
        (int)gen8->defaultWindowHeight,
        SDL_WINDOW_OPENGL
    );

    renderer = SDL2Renderer_create(window);

    AudioSystem* audioSystem = (AudioSystem*)SDL2AudioSystem_create();

    runner = Runner_create(dataWin, vm, renderer, (FileSystem*)fs, audioSystem);

    runner->nativeWindow = window;
    runner->setWindowTitle = setSDL2WindowTitle;

    Runner_initFirstRoom(runner);

    lastFrameTime = (double)SDL_GetTicks64() / 1000.0;

    emscripten_set_main_loop(mainLoop, 0, 1);

    runner->audioSystem->vtable->destroy(runner->audioSystem);
    renderer->vtable->destroy(renderer);

    SDL_DestroyWindow(window);
    SDL_Quit();

    Runner_free(runner);
    SDL2FileSystem_destroy(fs);
    VM_free(vm);
    DataWin_free(dataWin);

    return 0;
}