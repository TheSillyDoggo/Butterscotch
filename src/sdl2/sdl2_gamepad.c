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

/*static void mapGlfwToGml(const GLFWgamepadstate* glfwState, GamepadSlot* slot) {
    memcpy(slot->buttonDownPrev, slot->buttonDown, sizeof(slot->buttonDown));
    memset(slot->buttonDown, 0, sizeof(slot->buttonDown));
    memset(slot->buttonPressed, 0, sizeof(slot->buttonPressed));
    memset(slot->buttonReleased, 0, sizeof(slot->buttonReleased));
    memset(slot->buttonValue, 0, sizeof(slot->buttonValue));
    memset(slot->axisValue, 0, sizeof(slot->axisValue));

    if (glfwState->buttons[GLFW_GAMEPAD_BUTTON_A]) slot->buttonDown[0] = true;
    if (glfwState->buttons[GLFW_GAMEPAD_BUTTON_B]) slot->buttonDown[1] = true;
    if (glfwState->buttons[GLFW_GAMEPAD_BUTTON_X]) slot->buttonDown[2] = true;
    if (glfwState->buttons[GLFW_GAMEPAD_BUTTON_Y]) slot->buttonDown[3] = true;

    if (glfwState->buttons[GLFW_GAMEPAD_BUTTON_LEFT_BUMPER]) slot->buttonDown[4] = true;
    if (glfwState->buttons[GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER]) slot->buttonDown[5] = true;

    if (glfwState->buttons[GLFW_GAMEPAD_BUTTON_BACK]) slot->buttonDown[8] = true;
    if (glfwState->buttons[GLFW_GAMEPAD_BUTTON_START]) slot->buttonDown[9] = true;
    if (glfwState->buttons[GLFW_GAMEPAD_BUTTON_GUIDE]) slot->buttonDown[16] = true;

    if (glfwState->buttons[GLFW_GAMEPAD_BUTTON_LEFT_THUMB]) slot->buttonDown[10] = true;
    if (glfwState->buttons[GLFW_GAMEPAD_BUTTON_RIGHT_THUMB]) slot->buttonDown[11] = true;

    if (glfwState->buttons[GLFW_GAMEPAD_BUTTON_DPAD_UP]) slot->buttonDown[12] = true;
    if (glfwState->buttons[GLFW_GAMEPAD_BUTTON_DPAD_DOWN]) slot->buttonDown[13] = true;
    if (glfwState->buttons[GLFW_GAMEPAD_BUTTON_DPAD_LEFT]) slot->buttonDown[14] = true;
    if (glfwState->buttons[GLFW_GAMEPAD_BUTTON_DPAD_RIGHT]) slot->buttonDown[15] = true;

    float lt = glfwState->axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER];
    float rt = glfwState->axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER];
    if (lt < 0.0f) lt = 0.0f;
    if (rt < 0.0f) rt = 0.0f;
    slot->buttonValue[IDX_LT] = lt;
    slot->buttonValue[IDX_RT] = rt;
    if (lt >= slot->triggerThreshold) slot->buttonDown[IDX_LT] = true;
    if (rt >= slot->triggerThreshold) slot->buttonDown[IDX_RT] = true;

    float lh = glfwState->axes[GLFW_GAMEPAD_AXIS_LEFT_X];
    float lv = glfwState->axes[GLFW_GAMEPAD_AXIS_LEFT_Y];
    float rh = glfwState->axes[GLFW_GAMEPAD_AXIS_RIGHT_X];
    float rv = glfwState->axes[GLFW_GAMEPAD_AXIS_RIGHT_Y];

    slot->axisValue[0] = applyDeadzone(lh, slot->deadzone);
    slot->axisValue[1] = applyDeadzone(lv, slot->deadzone);
    slot->axisValue[2] = applyDeadzone(rh, slot->deadzone);
    slot->axisValue[3] = applyDeadzone(rv, slot->deadzone);

    for (int i = 0; GP_BUTTON_COUNT > i; i++) {
        if (i == IDX_LT || i == IDX_RT) continue;
        slot->buttonValue[i] = slot->buttonDown[i] ? 1.0f : 0.0f;
    }
}*/

// ===[ Public API ]===

void Sdl2Gamepad_loadMappings(const char* mappings) {
    /*if (mappings != NULL && mappings[0] != '\0') {
        if (glfwUpdateGamepadMappings(mappings)) {
            fprintf(stderr, "Gamepad: Loaded SDL gamecontroller mappings successfully\n");
        } else {
            fprintf(stderr, "Gamepad: Failed to load SDL gamecontroller mappings\n");
        }
    }*/
}

void Sdl2Gamepad_poll(RunnerGamepadState* gp) {
    gp->connectedCount = 0;

    for (int i = 0; i < MAX_GAMEPADS; i++) {
        GamepadSlot* slot = &gp->slots[i];

        if (!slot->connected || slot->controller == NULL) {
            int id = findFirstController();
            if (id < 0) {
                slot->connected = false;
                continue;
            }

            slot->controller = SDL_GameControllerOpen(id);
            slot->connected = (slot->controller != NULL);

            if (!slot->connected) continue;

            SDL_Joystick* joy = SDL_GameControllerGetJoystick(slot->controller);
            SDL_JoystickGUID guid = SDL_JoystickGetGUID(joy);

            SDL_JoystickName(joy); // optional name

            SDL_JoystickGetGUIDString(guid, slot->guid, sizeof(slot->guid));

            const char* name = SDL_GameControllerName(slot->controller);
            if (name) {
                strncpy(slot->description, name, sizeof(slot->description) - 1);
                slot->description[sizeof(slot->description) - 1] = '\0';
            }
        }

        mapSDLToGamepad(slot->controller, slot);

        // edge detection
        for (int btn = 0; btn < GP_BUTTON_COUNT; btn++) {
            bool wasDown = slot->buttonDownPrev[btn];
            if (slot->buttonDown[btn] && !wasDown) slot->buttonPressed[btn] = true;
            if (!slot->buttonDown[btn] && wasDown) slot->buttonReleased[btn] = true;
        }

        gp->connectedCount++;
    }
}