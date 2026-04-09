#pragma once

#include "common.h"
#include "renderer.h"
#include "matrix_math.h"

// ===[ SoftwareRenderer Struct ]===

typedef struct {
    Renderer base; // Must be first field for struct embedding

    // Framebuffer (RGBA, top-down scanline order)
    uint8_t* framebuffer;
    int32_t fbWidth, fbHeight;

    // Decoded texture pages (RGBA pixel data)
    uint8_t** texturePixels;
    int32_t* textureWidths;
    int32_t* textureHeights;
    uint32_t textureCount;

    // View transform: world coords -> framebuffer pixel coords
    Matrix4f worldToScreen;

    // Scissor rect (framebuffer pixel coords)
    int32_t scissorX, scissorY, scissorW, scissorH;

    // Game dimensions (from beginFrame)
    int32_t gameW, gameH;
} SoftwareRenderer;

Renderer* SoftwareRenderer_create(void);
