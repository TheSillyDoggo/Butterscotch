#include "sdl2_gamepad.h"

#include <SDL2/SDL.h>
#include <string.h>
#include <stdio.h>

// ===[ Internal helpers ]===

static float applyDeadzone(float value, float deadzone) {
    if (value < 0.0f) {
        if (value > -deadzone) return 0.0f;
        return (value + deadzone) / (1.0f - deadzone);
    } else {
        if (value < deadzone) return 0.0f;
        return (value - deadzone) / (1.0f - deadzone);
    }
}

enum {
    IDX_LT = 6,
    IDX_RT = 7,
};

#define GLFW_GAMEPAD_BUTTON_A               0
#define GLFW_GAMEPAD_BUTTON_B               1
#define GLFW_GAMEPAD_BUTTON_X               2
#define GLFW_GAMEPAD_BUTTON_Y               3
#define GLFW_GAMEPAD_BUTTON_LEFT_BUMPER     4
#define GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER    5
#define GLFW_GAMEPAD_BUTTON_BACK            6
#define GLFW_GAMEPAD_BUTTON_START           7
#define GLFW_GAMEPAD_BUTTON_GUIDE           8
#define GLFW_GAMEPAD_BUTTON_LEFT_THUMB      9
#define GLFW_GAMEPAD_BUTTON_RIGHT_THUMB     10
#define GLFW_GAMEPAD_BUTTON_DPAD_UP         12
#define GLFW_GAMEPAD_BUTTON_DPAD_RIGHT      11
#define GLFW_GAMEPAD_BUTTON_DPAD_DOWN       13
#define GLFW_GAMEPAD_BUTTON_DPAD_LEFT       14

static void mapSdl2ToGml(const SDL_GameController* sdlState, GamepadSlot* slot) {
    slot->connected = true;

    slot->buttonDown[0] = SDL_GameControllerGetButton(sdlState, SDL_CONTROLLER_BUTTON_A);
    slot->buttonDown[1] = SDL_GameControllerGetButton(sdlState, SDL_CONTROLLER_BUTTON_B);
    slot->buttonDown[2] = SDL_GameControllerGetButton(sdlState, SDL_CONTROLLER_BUTTON_X);
    slot->buttonDown[3] = SDL_GameControllerGetButton(sdlState, SDL_CONTROLLER_BUTTON_Y);
    
    slot->buttonDown[GLFW_GAMEPAD_BUTTON_LEFT_BUMPER] = SDL_GameControllerGetButton(sdlState, SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
    slot->buttonDown[GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER] = SDL_GameControllerGetButton(sdlState, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);

    slot->buttonDown[GLFW_GAMEPAD_BUTTON_BACK] = SDL_GameControllerGetButton(sdlState, SDL_CONTROLLER_BUTTON_BACK);
    slot->buttonDown[GLFW_GAMEPAD_BUTTON_START] = SDL_GameControllerGetButton(sdlState, SDL_CONTROLLER_BUTTON_START);
    slot->buttonDown[GLFW_GAMEPAD_BUTTON_GUIDE] = SDL_GameControllerGetButton(sdlState, SDL_CONTROLLER_BUTTON_GUIDE);

    slot->buttonDown[GLFW_GAMEPAD_BUTTON_DPAD_UP] = SDL_GameControllerGetButton(sdlState, SDL_CONTROLLER_BUTTON_DPAD_UP);
    slot->buttonDown[GLFW_GAMEPAD_BUTTON_DPAD_DOWN] = SDL_GameControllerGetButton(sdlState, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    slot->buttonDown[GLFW_GAMEPAD_BUTTON_DPAD_LEFT] = SDL_GameControllerGetButton(sdlState, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
    slot->buttonDown[GLFW_GAMEPAD_BUTTON_DPAD_RIGHT] = SDL_GameControllerGetButton(sdlState, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);

    float lt = SDL_GameControllerGetAxis(sdlState, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
    float rt = SDL_GameControllerGetAxis(sdlState, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);

    slot->buttonDown[IDX_LT] = lt > slot->triggerThreshold;
    slot->buttonDown[IDX_RT] = rt > slot->triggerThreshold;
    slot->buttonValue[IDX_LT] = lt;
    slot->buttonValue[IDX_RT] = rt;


    float lh = SDL_GameControllerGetAxis(sdlState, SDL_CONTROLLER_AXIS_LEFTX);
    float lv = SDL_GameControllerGetAxis(sdlState, SDL_CONTROLLER_AXIS_LEFTY);
    float rh = SDL_GameControllerGetAxis(sdlState, SDL_CONTROLLER_AXIS_RIGHTX);
    float rv = SDL_GameControllerGetAxis(sdlState, SDL_CONTROLLER_AXIS_RIGHTY);

    slot->axisValue[0] = applyDeadzone(lh, slot->deadzone);
    slot->axisValue[1] = applyDeadzone(lv, slot->deadzone);
    slot->axisValue[2] = applyDeadzone(rh, slot->deadzone);
    slot->axisValue[3] = applyDeadzone(rv, slot->deadzone);

    for (int i = 0; GP_BUTTON_COUNT > i; i++) {
        if (i == IDX_LT || i == IDX_RT) continue;
        slot->buttonValue[i] = slot->buttonDown[i] ? 1.0f : 0.0f;
    }
}

// ===[ Public API ]===

void SDL2Gamepad_loadMappings(const char* mappings) {
    if (mappings != NULL && mappings[0] != '\0') {
        if (SDL_GameControllerAddMapping(mappings) != -1) {
            fprintf(stderr, "Gamepad: Loaded SDL gamecontroller mappings successfully\n");
        } else {
            fprintf(stderr, "Gamepad: Failed to load SDL gamecontroller mappings\n");
        }
    }
}

void SDL2Gamepad_poll(RunnerGamepadState* gp) {
    gp->connectedCount = 0;

    int slot = 0;
    for (size_t i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            SDL_GameController* gamepad = SDL_GameControllerOpen(i);

            if (!gamepad)
                continue;

            mapSdl2ToGml(gamepad, &gp->slots[slot]);

            gp->connectedCount++;
            slot++;
        }
    }
}
