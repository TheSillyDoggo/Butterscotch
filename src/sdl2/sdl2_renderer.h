#pragma once

#include "renderer.h"

// ===[ SDLRenderer Struct ]===
// Exposed in the header so platform-specific code (main.c) can access FBO fields for screenshots.
typedef struct {
    Renderer base; // Must be first field for struct embedding

    void** textures;
    int32_t* textureWidths;
    int32_t* textureHeights;
    bool* textureLoaded;
    uint32_t textureCount;

    int32_t viewX, viewY;
    int32_t portW, portH;
    int32_t portX, portY;
    float renderScale;

    void* window;
    void* renderer;
} SDL2Renderer;

Renderer* SDL2Renderer_create(void* window);
