#pragma once

#include <stdint.h>
#include <stdbool.h>

// ===[ Butterscotch Shared Library API ]===
// Public C API for embedding the Butterscotch GameMaker runner in external applications.

#ifdef _WIN32
    #define BUTTERSCOTCH_API __declspec(dllexport)
#else
    #define BUTTERSCOTCH_API __attribute__((visibility("default")))
#endif

typedef struct ButterscotchContext ButterscotchContext;

// Creates a new Butterscotch context and loads the given data.win file.
// Returns nullptr on failure.
BUTTERSCOTCH_API ButterscotchContext* butterscotch_create(const char* dataWinPath);

// Frees the context and all associated resources.
BUTTERSCOTCH_API void butterscotch_free(ButterscotchContext* ctx);

// Executes one game frame (Begin Step, Alarms, Step, End Step, room transitions).
// Must be called before butterscotch_draw().
BUTTERSCOTCH_API void butterscotch_step(ButterscotchContext* ctx);

// Renders the current frame into the internal framebuffer.
// Must be called after butterscotch_step().
BUTTERSCOTCH_API void butterscotch_draw(ButterscotchContext* ctx);

// Returns a pointer to the RGBA framebuffer (4 bytes per pixel, top-down scanline order).
// The pointer is valid until the next butterscotch_draw() or butterscotch_free() call.
BUTTERSCOTCH_API const uint8_t* butterscotch_getFramebuffer(ButterscotchContext* ctx);

// Returns the framebuffer width in pixels.
BUTTERSCOTCH_API int32_t butterscotch_getFramebufferWidth(ButterscotchContext* ctx);

// Returns the framebuffer height in pixels.
BUTTERSCOTCH_API int32_t butterscotch_getFramebufferHeight(ButterscotchContext* ctx);

// Returns the current room speed (target frames per second).
BUTTERSCOTCH_API int32_t butterscotch_getRoomSpeed(ButterscotchContext* ctx);

// Returns true if the game has requested to exit.
BUTTERSCOTCH_API bool butterscotch_shouldExit(ButterscotchContext* ctx);

// Sends a key-down event. keyCode uses GML/Windows VK codes (same as Java KeyEvent VK constants).
BUTTERSCOTCH_API void butterscotch_keyDown(ButterscotchContext* ctx, int32_t keyCode);

// Sends a key-up event.
BUTTERSCOTCH_API void butterscotch_keyUp(ButterscotchContext* ctx, int32_t keyCode);

// Must be called at the start of each frame before processing input events.
// Clears per-frame pressed/released state.
BUTTERSCOTCH_API void butterscotch_beginFrame(ButterscotchContext* ctx);
