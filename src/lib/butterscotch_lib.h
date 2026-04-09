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

// ===[ Audio Callbacks ]===

// Callback table for audio playback. Set via butterscotch_setAudioCallbacks().
// All function pointers are optional (nullptr = no-op for that operation).
// userData is passed as the first argument to every callback.
typedef struct {
    void* userData;
    // Play a sound. Returns an instance ID used for subsequent operations. Return -1 on failure.
    int32_t (*playSound)(void* userData, int32_t soundIndex, bool loop);
    // Stop a specific sound instance (by instance ID returned from playSound).
    void (*stopSound)(void* userData, int32_t instanceId);
    // Stop all currently playing sounds.
    void (*stopAll)(void* userData);
    // Returns true if the given sound instance is still playing.
    bool (*isPlaying)(void* userData, int32_t instanceId);
    void (*pauseSound)(void* userData, int32_t instanceId);
    void (*resumeSound)(void* userData, int32_t instanceId);
    void (*pauseAll)(void* userData);
    void (*resumeAll)(void* userData);
    void (*setSoundGain)(void* userData, int32_t instanceId, float gain, uint32_t timeMs);
    float (*getSoundGain)(void* userData, int32_t instanceId);
    void (*setSoundPitch)(void* userData, int32_t instanceId, float pitch);
    float (*getSoundPitch)(void* userData, int32_t instanceId);
    float (*getTrackPosition)(void* userData, int32_t instanceId);
    void (*setTrackPosition)(void* userData, int32_t instanceId, float positionSeconds);
    void (*setMasterGain)(void* userData, float gain);
} ButterscotchAudioCallbacks;

// Installs audio callbacks, replacing the current audio system.
// The callbacks struct is copied by value; the caller does not need to keep it alive.
BUTTERSCOTCH_API void butterscotch_setAudioCallbacks(ButterscotchContext* ctx, ButterscotchAudioCallbacks* callbacks);

// ===[ Sound Info ]===

typedef struct {
    const char* name;     // sound name, e.g. "snd_attack" (pointer valid for ctx lifetime)
    const char* file;     // original filename field from data.win, e.g. "mus_toriel" (may be empty)
    bool isEmbedded;      // true = audio data lives in data.win (use butterscotch_getSoundData)
    float volume;         // default volume (0.0 - 1.0)
    float pitch;          // default pitch (1.0 = normal)
} ButterscotchSoundInfo;

// Returns the total number of sounds in the SOND chunk.
BUTTERSCOTCH_API int32_t butterscotch_getSoundCount(ButterscotchContext* ctx);

// Fills outInfo with metadata about a sound by index. String pointers inside are valid for the ctx lifetime.
// Does nothing if soundIndex is out of range or outInfo is nullptr.
BUTTERSCOTCH_API void butterscotch_getSoundInfo(ButterscotchContext* ctx, int32_t soundIndex, ButterscotchSoundInfo* outInfo);

// Returns a pointer to the raw audio bytes for an embedded sound (isEmbedded == true).
// Returns nullptr if the sound is not embedded or soundIndex is out of range.
// outSize is set to the byte count. The pointer is valid for the ctx lifetime.
BUTTERSCOTCH_API const uint8_t* butterscotch_getSoundData(ButterscotchContext* ctx, int32_t soundIndex, int32_t* outSize);
