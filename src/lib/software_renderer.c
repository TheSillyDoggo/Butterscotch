#include "software_renderer.h"
#include "matrix_math.h"
#include "text_utils.h"
#include "utils.h"

#include "stb_image.h"
#include "stb_ds.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// ===[ Pixel Helpers ]===

static inline void blendPixel(uint8_t* dst, uint8_t sr, uint8_t sg, uint8_t sb, uint8_t sa) {
    if (sa == 0) return;
    if (sa == 255) {
        dst[0] = sr;
        dst[1] = sg;
        dst[2] = sb;
        dst[3] = 255;
        return;
    }

    // Standard alpha blending: src_rgb * src_a + dst_rgb * (1 - src_a)
    uint32_t srcA = sa;
    uint32_t invA = 255 - sa;
    dst[0] = (uint8_t) ((sr * srcA + dst[0] * invA) / 255);
    dst[1] = (uint8_t) ((sg * srcA + dst[1] * invA) / 255);
    dst[2] = (uint8_t) ((sb * srcA + dst[2] * invA) / 255);
    dst[3] = (uint8_t) (srcA + dst[3] * invA / 255);
}

static inline void sampleTexture(const uint8_t* pixels, int32_t texW, int32_t texH, float u, float v, uint8_t* r, uint8_t* g, uint8_t* b, uint8_t* a) {
    int32_t tx = (int32_t) (u * (float) texW);
    int32_t ty = (int32_t) (v * (float) texH);

    // Clamp
    if (0 > tx) tx = 0;
    if (0 > ty) ty = 0;
    if (tx >= texW) tx = texW - 1;
    if (ty >= texH) ty = texH - 1;

    const uint8_t* p = pixels + (ty * texW + tx) * 4;
    *r = p[0];
    *g = p[1];
    *b = p[2];
    *a = p[3];
}

// ===[ Triangle Vertex ]===

typedef struct {
    float x, y;       // screen position
    float u, v;        // texture UV (normalized 0-1)
    float r, g, b, a;  // vertex color (0-1)
} SWVertex;

// ===[ Triangle Rasterizer ]===

static inline float edgeFunction(float ax, float ay, float bx, float by, float px, float py) {
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

static void rasterizeTriangle(SoftwareRenderer* sw, SWVertex v0, SWVertex v1, SWVertex v2, const uint8_t* texPixels, int32_t texW, int32_t texH) {
    // Compute bounding box
    float fminX = v0.x;
    if (v1.x < fminX) fminX = v1.x;
    if (v2.x < fminX) fminX = v2.x;
    float fmaxX = v0.x;
    if (v1.x > fmaxX) fmaxX = v1.x;
    if (v2.x > fmaxX) fmaxX = v2.x;
    float fminY = v0.y;
    if (v1.y < fminY) fminY = v1.y;
    if (v2.y < fminY) fminY = v2.y;
    float fmaxY = v0.y;
    if (v1.y > fmaxY) fmaxY = v1.y;
    if (v2.y > fmaxY) fmaxY = v2.y;

    int32_t minX = (int32_t) floorf(fminX);
    int32_t maxX = (int32_t) ceilf(fmaxX);
    int32_t minY = (int32_t) floorf(fminY);
    int32_t maxY = (int32_t) ceilf(fmaxY);

    // Clamp to scissor rect
    if (minX < sw->scissorX) minX = sw->scissorX;
    if (minY < sw->scissorY) minY = sw->scissorY;
    if (maxX > sw->scissorX + sw->scissorW) maxX = sw->scissorX + sw->scissorW;
    if (maxY > sw->scissorY + sw->scissorH) maxY = sw->scissorY + sw->scissorH;

    // Clamp to framebuffer
    if (0 > minX) minX = 0;
    if (0 > minY) minY = 0;
    if (maxX > sw->fbWidth) maxX = sw->fbWidth;
    if (maxY > sw->fbHeight) maxY = sw->fbHeight;

    if (minX >= maxX || minY >= maxY) return;

    // Triangle area (2x, for barycentric normalization)
    float area = edgeFunction(v0.x, v0.y, v1.x, v1.y, v2.x, v2.y);
    if (fabsf(area) < 0.001f) return; // degenerate triangle

    // Ensure consistent winding (CCW)
    if (area < 0) {
        SWVertex tmp = v1;
        v1 = v2;
        v2 = tmp;
        area = -area;
    }

    float invArea = 1.0f / area;

    // Edge function increments per pixel
    float dw0_dx = v1.y - v2.y;
    float dw0_dy = v2.x - v1.x;
    float dw1_dx = v2.y - v0.y;
    float dw1_dy = v0.x - v2.x;
    float dw2_dx = v0.y - v1.y;
    float dw2_dy = v1.x - v0.x;

    // Edge functions at (minX + 0.5, minY + 0.5) - sample at pixel center
    float px = (float) minX + 0.5f;
    float py = (float) minY + 0.5f;
    float w0_row = edgeFunction(v1.x, v1.y, v2.x, v2.y, px, py);
    float w1_row = edgeFunction(v2.x, v2.y, v0.x, v0.y, px, py);
    float w2_row = edgeFunction(v0.x, v0.y, v1.x, v1.y, px, py);

    for (int32_t y = minY; maxY > y; y++) {
        float w0 = w0_row;
        float w1 = w1_row;
        float w2 = w2_row;

        for (int32_t x = minX; maxX > x; x++) {
            if (w0 >= 0 && w1 >= 0 && w2 >= 0) {
                // Barycentric weights
                float b0 = w0 * invArea;
                float b1 = w1 * invArea;
                float b2 = w2 * invArea;

                // Interpolate color
                float cr = b0 * v0.r + b1 * v1.r + b2 * v2.r;
                float cg = b0 * v0.g + b1 * v1.g + b2 * v2.g;
                float cb = b0 * v0.b + b1 * v1.b + b2 * v2.b;
                float ca = b0 * v0.a + b1 * v1.a + b2 * v2.a;
                if (0.0f > ca) ca = 0.0f;
                if (ca > 1.0f) ca = 1.0f;

                uint8_t finalR, finalG, finalB, finalA;

                if (texPixels != nullptr) {
                    // Interpolate UV and sample texture
                    float u = b0 * v0.u + b1 * v1.u + b2 * v2.u;
                    float v = b0 * v0.v + b1 * v1.v + b2 * v2.v;

                    uint8_t texR, texG, texB, texA;
                    sampleTexture(texPixels, texW, texH, u, v, &texR, &texG, &texB, &texA);

                    // Modulate texture by vertex color
                    finalR = (uint8_t) (texR * cr);
                    finalG = (uint8_t) (texG * cg);
                    finalB = (uint8_t) (texB * cb);
                    finalA = (uint8_t) (texA * ca);
                } else {
                    finalR = (uint8_t) (cr * 255.0f);
                    finalG = (uint8_t) (cg * 255.0f);
                    finalB = (uint8_t) (cb * 255.0f);
                    finalA = (uint8_t) (ca * 255.0f);
                }

                uint8_t* dst = sw->framebuffer + (y * sw->fbWidth + x) * 4;
                blendPixel(dst, finalR, finalG, finalB, finalA);
            }

            w0 += dw0_dx;
            w1 += dw1_dx;
            w2 += dw2_dx;
        }

        w0_row += dw0_dy;
        w1_row += dw1_dy;
        w2_row += dw2_dy;
    }
}

// Rasterizes a quad as two triangles: (v0, v1, v2) and (v2, v3, v0)
static void rasterizeQuad(SoftwareRenderer* sw, SWVertex v[4], const uint8_t* texPixels, int32_t texW, int32_t texH) {
    rasterizeTriangle(sw, v[0], v[1], v[2], texPixels, texW, texH);
    rasterizeTriangle(sw, v[2], v[3], v[0], texPixels, texW, texH);
}

// ===[ Vtable Implementations ]===

static void swInit(Renderer* renderer, DataWin* dataWin) {
    SoftwareRenderer* sw = (SoftwareRenderer*) renderer;
    renderer->dataWin = dataWin;

    // Decode all TXTR pages into RGBA pixel arrays
    sw->textureCount = dataWin->txtr.count;
    sw->texturePixels = safeCalloc(sw->textureCount, sizeof(uint8_t*));
    sw->textureWidths = safeCalloc(sw->textureCount, sizeof(int32_t));
    sw->textureHeights = safeCalloc(sw->textureCount, sizeof(int32_t));

    for (uint32_t i = 0; sw->textureCount > i; i++) {
        Texture* txtr = &dataWin->txtr.textures[i];
        int w, h, channels;
        uint8_t* pixels = stbi_load_from_memory(txtr->blobData, (int) txtr->blobSize, &w, &h, &channels, 4);
        if (pixels == nullptr) {
            fprintf(stderr, "SW: Failed to decode TXTR page %u\n", i);
            continue;
        }
        sw->texturePixels[i] = pixels;
        sw->textureWidths[i] = w;
        sw->textureHeights[i] = h;
        fprintf(stderr, "SW: Loaded TXTR page %u (%dx%d)\n", i, w, h);
    }

    // Framebuffer starts empty (allocated in beginFrame)
    sw->framebuffer = nullptr;
    sw->fbWidth = 0;
    sw->fbHeight = 0;

    fprintf(stderr, "SW: Software renderer initialized (%u texture pages)\n", sw->textureCount);
}

static void swDestroy(Renderer* renderer) {
    SoftwareRenderer* sw = (SoftwareRenderer*) renderer;

    for (uint32_t i = 0; sw->textureCount > i; i++) {
        if (sw->texturePixels[i] != nullptr) stbi_image_free(sw->texturePixels[i]);
    }
    free(sw->texturePixels);
    free(sw->textureWidths);
    free(sw->textureHeights);
    free(sw->framebuffer);
    free(sw);
}

static void swBeginFrame(Renderer* renderer, int32_t gameW, int32_t gameH, int32_t windowW, int32_t windowH) {
    SoftwareRenderer* sw = (SoftwareRenderer*) renderer;
    (void) windowW;
    (void) windowH;

    sw->gameW = gameW;
    sw->gameH = gameH;

    // Reallocate framebuffer if game dimensions changed
    if (gameW != sw->fbWidth || gameH != sw->fbHeight) {
        free(sw->framebuffer);
        sw->fbWidth = gameW;
        sw->fbHeight = gameH;
        sw->framebuffer = safeCalloc((size_t) gameW * (size_t) gameH, 4);
        fprintf(stderr, "SW: Framebuffer resized to %dx%d\n", gameW, gameH);
    }

    // Clear framebuffer to black
    memset(sw->framebuffer, 0, (size_t) sw->fbWidth * (size_t) sw->fbHeight * 4);
}

static void swEndFrame(Renderer* renderer) {
    (void) renderer;
    // Nothing to do - framebuffer is ready for reading
}

static void swBeginView(Renderer* renderer, int32_t viewX, int32_t viewY, int32_t viewW, int32_t viewH, int32_t portX, int32_t portY, int32_t portW, int32_t portH, float viewAngle) {
    SoftwareRenderer* sw = (SoftwareRenderer*) renderer;

    // Set scissor to port rectangle
    sw->scissorX = portX;
    sw->scissorY = portY;
    sw->scissorW = portW;
    sw->scissorH = portH;

    // Build projection matrix (same as GL renderer)
    Matrix4f projection;
    Matrix4f_identity(&projection);
    Matrix4f_ortho(&projection, (float) viewX, (float) (viewX + viewW), (float) (viewY + viewH), (float) viewY, -1.0f, 1.0f);

    if (viewAngle != 0.0f) {
        float cx = (float) viewX + (float) viewW / 2.0f;
        float cy = (float) viewY + (float) viewH / 2.0f;
        Matrix4f rot;
        Matrix4f_identity(&rot);
        Matrix4f_translate(&rot, cx, cy, 0.0f);
        float angleRad = viewAngle * (float) M_PI / 180.0f;
        Matrix4f_rotateZ(&rot, -angleRad);
        Matrix4f_translate(&rot, -cx, -cy, 0.0f);
        Matrix4f result;
        Matrix4f_multiply(&result, &projection, &rot);
        projection = result;
    }

    // Build viewport transform: NDC [-1,1] -> framebuffer pixels
    // x_screen = portX + (ndc_x + 1) * portW / 2
    // y_screen = portY + (1 - ndc_y) * portH / 2  (flip Y for top-down FB)
    Matrix4f viewport;
    memset(&viewport, 0, sizeof(Matrix4f));
    viewport.m[0] = (float) portW / 2.0f;      // scale X
    viewport.m[5] = -(float) portH / 2.0f;     // scale Y (flipped)
    viewport.m[10] = 1.0f;
    viewport.m[12] = (float) portX + (float) portW / 2.0f; // translate X
    viewport.m[13] = (float) portY + (float) portH / 2.0f; // translate Y
    viewport.m[15] = 1.0f;

    // Combined: world -> NDC -> screen pixels
    Matrix4f_multiply(&sw->worldToScreen, &viewport, &projection);
}

static void swEndView(Renderer* renderer) {
    (void) renderer;
    // Nothing to flush in immediate mode
}

// Transforms a world-space point to screen pixel coordinates
static inline void worldToScreen(SoftwareRenderer* sw, float wx, float wy, float* sx, float* sy) {
    Matrix4f_transformPoint(&sw->worldToScreen, wx, wy, sx, sy);
}

// ===[ Sprite Drawing ]===

static void swDrawSprite(Renderer* renderer, int32_t tpagIndex, float x, float y, float originX, float originY, float xscale, float yscale, float angleDeg, uint32_t color, float alpha) {
    SoftwareRenderer* sw = (SoftwareRenderer*) renderer;
    DataWin* dw = renderer->dataWin;

    if (0 > tpagIndex || dw->tpag.count <= (uint32_t) tpagIndex) return;

    TexturePageItem* tpag = &dw->tpag.items[tpagIndex];
    int16_t pageId = tpag->texturePageId;
    if (0 > pageId || sw->textureCount <= (uint32_t) pageId) return;

    uint8_t* texPixels = sw->texturePixels[pageId];
    int32_t texW = sw->textureWidths[pageId];
    int32_t texH = sw->textureHeights[pageId];
    if (texPixels == nullptr || texW == 0 || texH == 0) return;

    // Compute normalized UVs from TPAG source rect
    float u0 = (float) tpag->sourceX / (float) texW;
    float v0 = (float) tpag->sourceY / (float) texH;
    float u1 = (float) (tpag->sourceX + tpag->sourceWidth) / (float) texW;
    float v1 = (float) (tpag->sourceY + tpag->sourceHeight) / (float) texH;

    // Compute local quad corners (with target offset and origin)
    float localX0 = (float) tpag->targetX - originX;
    float localY0 = (float) tpag->targetY - originY;
    float localX1 = localX0 + (float) tpag->sourceWidth;
    float localY1 = localY0 + (float) tpag->sourceHeight;

    // Build 2D model transform
    float angleRad = -angleDeg * ((float) M_PI / 180.0f);
    Matrix4f transform;
    Matrix4f_setTransform2D(&transform, x, y, xscale, yscale, angleRad);

    // Transform 4 corners to world space
    float wx0, wy0, wx1, wy1, wx2, wy2, wx3, wy3;
    Matrix4f_transformPoint(&transform, localX0, localY0, &wx0, &wy0);
    Matrix4f_transformPoint(&transform, localX1, localY0, &wx1, &wy1);
    Matrix4f_transformPoint(&transform, localX1, localY1, &wx2, &wy2);
    Matrix4f_transformPoint(&transform, localX0, localY1, &wx3, &wy3);

    // Transform to screen space
    SWVertex verts[4];
    worldToScreen(sw, wx0, wy0, &verts[0].x, &verts[0].y);
    worldToScreen(sw, wx1, wy1, &verts[1].x, &verts[1].y);
    worldToScreen(sw, wx2, wy2, &verts[2].x, &verts[2].y);
    worldToScreen(sw, wx3, wy3, &verts[3].x, &verts[3].y);

    // Set UV coordinates
    verts[0].u = u0; verts[0].v = v0; // top-left
    verts[1].u = u1; verts[1].v = v0; // top-right
    verts[2].u = u1; verts[2].v = v1; // bottom-right
    verts[3].u = u0; verts[3].v = v1; // bottom-left

    // Convert BGR color to RGB floats
    float r = (float) BGR_R(color) / 255.0f;
    float g = (float) BGR_G(color) / 255.0f;
    float b = (float) BGR_B(color) / 255.0f;

    for (int i = 0; 4 > i; i++) {
        verts[i].r = r;
        verts[i].g = g;
        verts[i].b = b;
        verts[i].a = alpha;
    }

    rasterizeQuad(sw, verts, texPixels, texW, texH);
}

static void swDrawSpritePart(Renderer* renderer, int32_t tpagIndex, int32_t srcOffX, int32_t srcOffY, int32_t srcW, int32_t srcH, float x, float y, float xscale, float yscale, uint32_t color, float alpha) {
    SoftwareRenderer* sw = (SoftwareRenderer*) renderer;
    DataWin* dw = renderer->dataWin;

    if (0 > tpagIndex || dw->tpag.count <= (uint32_t) tpagIndex) return;

    TexturePageItem* tpag = &dw->tpag.items[tpagIndex];
    int16_t pageId = tpag->texturePageId;
    if (0 > pageId || sw->textureCount <= (uint32_t) pageId) return;

    uint8_t* texPixels = sw->texturePixels[pageId];
    int32_t texW = sw->textureWidths[pageId];
    int32_t texH = sw->textureHeights[pageId];
    if (texPixels == nullptr || texW == 0 || texH == 0) return;

    // Compute UVs for the sub-region within the atlas
    float u0 = (float) (tpag->sourceX + srcOffX) / (float) texW;
    float v0 = (float) (tpag->sourceY + srcOffY) / (float) texH;
    float u1 = (float) (tpag->sourceX + srcOffX + srcW) / (float) texW;
    float v1 = (float) (tpag->sourceY + srcOffY + srcH) / (float) texH;

    // Quad corners in world space (no rotation for sprite_part)
    float wx0 = x;
    float wy0 = y;
    float wx1 = x + (float) srcW * xscale;
    float wy1 = y + (float) srcH * yscale;

    // Transform to screen space
    SWVertex verts[4];
    worldToScreen(sw, wx0, wy0, &verts[0].x, &verts[0].y);
    worldToScreen(sw, wx1, wy0, &verts[1].x, &verts[1].y);
    worldToScreen(sw, wx1, wy1, &verts[2].x, &verts[2].y);
    worldToScreen(sw, wx0, wy1, &verts[3].x, &verts[3].y);

    verts[0].u = u0; verts[0].v = v0;
    verts[1].u = u1; verts[1].v = v0;
    verts[2].u = u1; verts[2].v = v1;
    verts[3].u = u0; verts[3].v = v1;

    float r = (float) BGR_R(color) / 255.0f;
    float g = (float) BGR_G(color) / 255.0f;
    float b = (float) BGR_B(color) / 255.0f;

    for (int i = 0; 4 > i; i++) {
        verts[i].r = r;
        verts[i].g = g;
        verts[i].b = b;
        verts[i].a = alpha;
    }

    rasterizeQuad(sw, verts, texPixels, texW, texH);
}

// ===[ Primitive Drawing ]===

static void emitColoredQuadSW(SoftwareRenderer* sw, float x0, float y0, float x1, float y1, float r, float g, float b, float a) {
    SWVertex verts[4];
    worldToScreen(sw, x0, y0, &verts[0].x, &verts[0].y);
    worldToScreen(sw, x1, y0, &verts[1].x, &verts[1].y);
    worldToScreen(sw, x1, y1, &verts[2].x, &verts[2].y);
    worldToScreen(sw, x0, y1, &verts[3].x, &verts[3].y);

    for (int i = 0; 4 > i; i++) {
        verts[i].u = 0; verts[i].v = 0;
        verts[i].r = r; verts[i].g = g; verts[i].b = b; verts[i].a = a;
    }

    rasterizeQuad(sw, verts, nullptr, 0, 0);
}

static void swDrawRectangle(Renderer* renderer, float x1, float y1, float x2, float y2, uint32_t color, float alpha, bool outline) {
    SoftwareRenderer* sw = (SoftwareRenderer*) renderer;

    float r = (float) BGR_R(color) / 255.0f;
    float g = (float) BGR_G(color) / 255.0f;
    float b = (float) BGR_B(color) / 255.0f;

    if (outline) {
        emitColoredQuadSW(sw, x1, y1, x2 + 1, y1 + 1, r, g, b, alpha);
        emitColoredQuadSW(sw, x1, y2, x2 + 1, y2 + 1, r, g, b, alpha);
        emitColoredQuadSW(sw, x1, y1 + 1, x1 + 1, y2, r, g, b, alpha);
        emitColoredQuadSW(sw, x2, y1 + 1, x2 + 1, y2, r, g, b, alpha);
    } else {
        emitColoredQuadSW(sw, x1, y1, x2 + 1, y2 + 1, r, g, b, alpha);
    }
}

static void swDrawLine(Renderer* renderer, float x1, float y1, float x2, float y2, float width, uint32_t color, float alpha) {
    SoftwareRenderer* sw = (SoftwareRenderer*) renderer;

    float r = (float) BGR_R(color) / 255.0f;
    float g = (float) BGR_G(color) / 255.0f;
    float b = (float) BGR_B(color) / 255.0f;

    // Compute perpendicular offset for line thickness
    float dx = x2 - x1;
    float dy = y2 - y1;
    float len = sqrtf(dx * dx + dy * dy);
    if (0.0001f > len) return;

    float halfW = width * 0.5f;
    float px = (-dy / len) * halfW;
    float py = (dx / len) * halfW;

    // Build quad from line endpoints with thickness
    SWVertex verts[4];
    worldToScreen(sw, x1 + px, y1 + py, &verts[0].x, &verts[0].y);
    worldToScreen(sw, x1 - px, y1 - py, &verts[1].x, &verts[1].y);
    worldToScreen(sw, x2 - px, y2 - py, &verts[2].x, &verts[2].y);
    worldToScreen(sw, x2 + px, y2 + py, &verts[3].x, &verts[3].y);

    for (int i = 0; 4 > i; i++) {
        verts[i].u = 0; verts[i].v = 0;
        verts[i].r = r; verts[i].g = g; verts[i].b = b; verts[i].a = alpha;
    }

    rasterizeQuad(sw, verts, nullptr, 0, 0);
}

static void swDrawLineColor(Renderer* renderer, float x1, float y1, float x2, float y2, float width, uint32_t color1, uint32_t color2, float alpha) {
    SoftwareRenderer* sw = (SoftwareRenderer*) renderer;

    float r1 = (float) BGR_R(color1) / 255.0f;
    float g1 = (float) BGR_G(color1) / 255.0f;
    float b1 = (float) BGR_B(color1) / 255.0f;

    float r2 = (float) BGR_R(color2) / 255.0f;
    float g2 = (float) BGR_G(color2) / 255.0f;
    float b2 = (float) BGR_B(color2) / 255.0f;

    float dx = x2 - x1;
    float dy = y2 - y1;
    float len = sqrtf(dx * dx + dy * dy);
    if (0.0001f > len) return;

    float halfW = width * 0.5f;
    float px = (-dy / len) * halfW;
    float py = (dx / len) * halfW;

    SWVertex verts[4];
    worldToScreen(sw, x1 + px, y1 + py, &verts[0].x, &verts[0].y);
    worldToScreen(sw, x1 - px, y1 - py, &verts[1].x, &verts[1].y);
    worldToScreen(sw, x2 - px, y2 - py, &verts[2].x, &verts[2].y);
    worldToScreen(sw, x2 + px, y2 + py, &verts[3].x, &verts[3].y);

    verts[0].u = 0; verts[0].v = 0; verts[0].r = r1; verts[0].g = g1; verts[0].b = b1; verts[0].a = alpha;
    verts[1].u = 0; verts[1].v = 0; verts[1].r = r1; verts[1].g = g1; verts[1].b = b1; verts[1].a = alpha;
    verts[2].u = 0; verts[2].v = 0; verts[2].r = r2; verts[2].g = g2; verts[2].b = b2; verts[2].a = alpha;
    verts[3].u = 0; verts[3].v = 0; verts[3].r = r2; verts[3].g = g2; verts[3].b = b2; verts[3].a = alpha;

    rasterizeQuad(sw, verts, nullptr, 0, 0);
}

static void swDrawTriangle(Renderer* renderer, float x1, float y1, float x2, float y2, float x3, float y3, bool outline) {
    SoftwareRenderer* sw = (SoftwareRenderer*) renderer;

    if (outline) {
        swDrawLine(renderer, x1, y1, x2, y2, 1, renderer->drawColor, 1.0f);
        swDrawLine(renderer, x2, y2, x3, y3, 1, renderer->drawColor, 1.0f);
        swDrawLine(renderer, x3, y3, x1, y1, 1, renderer->drawColor, 1.0f);
    } else {
        float r = (float) BGR_R(renderer->drawColor) / 255.0f;
        float g = (float) BGR_G(renderer->drawColor) / 255.0f;
        float b = (float) BGR_B(renderer->drawColor) / 255.0f;

        SWVertex v0, v1, v2;
        worldToScreen(sw, x1, y1, &v0.x, &v0.y);
        worldToScreen(sw, x2, y2, &v1.x, &v1.y);
        worldToScreen(sw, x3, y3, &v2.x, &v2.y);

        v0.u = 0; v0.v = 0; v0.r = r; v0.g = g; v0.b = b; v0.a = renderer->drawAlpha;
        v1.u = 0; v1.v = 0; v1.r = r; v1.g = g; v1.b = b; v1.a = renderer->drawAlpha;
        v2.u = 0; v2.v = 0; v2.r = r; v2.g = g; v2.b = b; v2.a = renderer->drawAlpha;

        rasterizeTriangle(sw, v0, v1, v2, nullptr, 0, 0);
    }
}

// ===[ Text Drawing ]===

// Resolves the texture pixel data for a font
typedef struct {
    Font* font;
    TexturePageItem* fontTpag; // single TPAG for regular fonts (nullptr for sprite fonts)
    uint8_t* texPixels;
    int32_t texW, texH;
    Sprite* spriteFontSprite; // source sprite for sprite fonts (nullptr for regular fonts)
} SWFontState;

static bool swResolveFontState(SoftwareRenderer* sw, DataWin* dw, Font* font, SWFontState* state) {
    state->font = font;
    state->fontTpag = nullptr;
    state->texPixels = nullptr;
    state->texW = 0;
    state->texH = 0;
    state->spriteFontSprite = nullptr;

    if (!font->isSpriteFont) {
        int32_t fontTpagIndex = DataWin_resolveTPAG(dw, font->textureOffset);
        if (0 > fontTpagIndex) return false;

        state->fontTpag = &dw->tpag.items[fontTpagIndex];
        int16_t pageId = state->fontTpag->texturePageId;
        if (0 > pageId || (uint32_t) pageId >= sw->textureCount) return false;

        state->texPixels = sw->texturePixels[pageId];
        state->texW = sw->textureWidths[pageId];
        state->texH = sw->textureHeights[pageId];
        if (state->texPixels == nullptr || state->texW == 0 || state->texH == 0) return false;
    } else if (font->spriteIndex >= 0 && dw->sprt.count > (uint32_t) font->spriteIndex) {
        state->spriteFontSprite = &dw->sprt.sprites[font->spriteIndex];
    }
    return true;
}

// Resolves glyph UV and position
static bool swResolveGlyph(SoftwareRenderer* sw, DataWin* dw, SWFontState* state, FontGlyph* glyph, float cursorX, float cursorY, uint8_t** outTexPixels, int32_t* outTexW, int32_t* outTexH, float* outU0, float* outV0, float* outU1, float* outV1, float* outLocalX0, float* outLocalY0) {
    Font* font = state->font;
    if (font->isSpriteFont && state->spriteFontSprite != nullptr) {
        Sprite* sprite = state->spriteFontSprite;
        int32_t glyphIndex = (int32_t) (glyph - font->glyphs);
        if (0 > glyphIndex || glyphIndex >= (int32_t) sprite->textureCount) return false;

        uint32_t tpagOffset = sprite->textureOffsets[glyphIndex];
        int32_t tpagIdx = DataWin_resolveTPAG(dw, tpagOffset);
        if (0 > tpagIdx) return false;

        TexturePageItem* glyphTpag = &dw->tpag.items[tpagIdx];
        int16_t pid = glyphTpag->texturePageId;
        if (0 > pid || (uint32_t) pid >= sw->textureCount) return false;

        *outTexPixels = sw->texturePixels[pid];
        *outTexW = sw->textureWidths[pid];
        *outTexH = sw->textureHeights[pid];
        if (*outTexPixels == nullptr || *outTexW == 0 || *outTexH == 0) return false;

        *outU0 = (float) glyphTpag->sourceX / (float) *outTexW;
        *outV0 = (float) glyphTpag->sourceY / (float) *outTexH;
        *outU1 = (float) (glyphTpag->sourceX + glyphTpag->sourceWidth) / (float) *outTexW;
        *outV1 = (float) (glyphTpag->sourceY + glyphTpag->sourceHeight) / (float) *outTexH;

        *outLocalX0 = cursorX + (float) glyph->offset;
        *outLocalY0 = cursorY + (float) ((int32_t) glyphTpag->targetY - sprite->originY);
    } else {
        *outTexPixels = state->texPixels;
        *outTexW = state->texW;
        *outTexH = state->texH;
        *outU0 = (float) (state->fontTpag->sourceX + glyph->sourceX) / (float) state->texW;
        *outV0 = (float) (state->fontTpag->sourceY + glyph->sourceY) / (float) state->texH;
        *outU1 = (float) (state->fontTpag->sourceX + glyph->sourceX + glyph->sourceWidth) / (float) state->texW;
        *outV1 = (float) (state->fontTpag->sourceY + glyph->sourceY + glyph->sourceHeight) / (float) state->texH;

        *outLocalX0 = cursorX + glyph->offset;
        *outLocalY0 = cursorY;
    }
    return true;
}

static void swDrawText(Renderer* renderer, const char* text, float x, float y, float xscale, float yscale, float angleDeg) {
    SoftwareRenderer* sw = (SoftwareRenderer*) renderer;
    DataWin* dw = renderer->dataWin;

    int32_t fontIndex = renderer->drawFont;
    if (0 > fontIndex || dw->font.count <= (uint32_t) fontIndex) return;

    Font* font = &dw->font.fonts[fontIndex];

    SWFontState fontState;
    if (!swResolveFontState(sw, dw, font, &fontState)) return;

    uint32_t color = renderer->drawColor;
    float alpha = renderer->drawAlpha;
    float cr = (float) BGR_R(color) / 255.0f;
    float cg = (float) BGR_G(color) / 255.0f;
    float cb = (float) BGR_B(color) / 255.0f;

    int32_t textLen = (int32_t) strlen(text);
    int32_t lineCount = TextUtils_countLines(text, textLen);

    float totalHeight = (float) lineCount * (float) font->emSize;
    float valignOffset = 0;
    if (renderer->drawValign == 1) valignOffset = -totalHeight / 2.0f;
    else if (renderer->drawValign == 2) valignOffset = -totalHeight;

    float angleRad = -angleDeg * ((float) M_PI / 180.0f);
    Matrix4f transform;
    Matrix4f_setTransform2D(&transform, x, y, xscale * font->scaleX, yscale * font->scaleY, angleRad);

    float cursorY = valignOffset;
    int32_t lineStart = 0;

    for (int32_t lineIdx = 0; lineCount > lineIdx; lineIdx++) {
        int32_t lineEnd = lineStart;
        while (textLen > lineEnd && !TextUtils_isNewlineChar(text[lineEnd])) lineEnd++;
        int32_t lineLen = lineEnd - lineStart;

        float lineWidth = TextUtils_measureLineWidth(font, text + lineStart, lineLen);
        float halignOffset = 0;
        if (renderer->drawHalign == 1) halignOffset = -lineWidth / 2.0f;
        else if (renderer->drawHalign == 2) halignOffset = -lineWidth;

        float cursorX = halignOffset;
        int32_t pos = 0;

        while (lineLen > pos) {
            uint16_t ch = TextUtils_decodeUtf8(text + lineStart, lineLen, &pos);
            FontGlyph* glyph = TextUtils_findGlyph(font, ch);
            if (glyph == nullptr) continue;
            if (glyph->sourceWidth == 0 || glyph->sourceHeight == 0) {
                cursorX += glyph->shift;
                continue;
            }

            uint8_t* glyphTexPixels;
            int32_t glyphTexW, glyphTexH;
            float u0, v0, u1, v1;
            float localX0, localY0;

            if (!swResolveGlyph(sw, dw, &fontState, glyph, cursorX, cursorY, &glyphTexPixels, &glyphTexW, &glyphTexH, &u0, &v0, &u1, &v1, &localX0, &localY0)) {
                cursorX += glyph->shift;
                continue;
            }

            float localX1 = localX0 + (float) glyph->sourceWidth;
            float localY1 = localY0 + (float) glyph->sourceHeight;

            // Transform corners through model matrix then to screen
            float wx0, wy0, wx1, wy1, wx2, wy2, wx3, wy3;
            Matrix4f_transformPoint(&transform, localX0, localY0, &wx0, &wy0);
            Matrix4f_transformPoint(&transform, localX1, localY0, &wx1, &wy1);
            Matrix4f_transformPoint(&transform, localX1, localY1, &wx2, &wy2);
            Matrix4f_transformPoint(&transform, localX0, localY1, &wx3, &wy3);

            SWVertex verts[4];
            worldToScreen(sw, wx0, wy0, &verts[0].x, &verts[0].y);
            worldToScreen(sw, wx1, wy1, &verts[1].x, &verts[1].y);
            worldToScreen(sw, wx2, wy2, &verts[2].x, &verts[2].y);
            worldToScreen(sw, wx3, wy3, &verts[3].x, &verts[3].y);

            verts[0].u = u0; verts[0].v = v0;
            verts[1].u = u1; verts[1].v = v0;
            verts[2].u = u1; verts[2].v = v1;
            verts[3].u = u0; verts[3].v = v1;

            for (int i = 0; 4 > i; i++) {
                verts[i].r = cr; verts[i].g = cg; verts[i].b = cb; verts[i].a = alpha;
            }

            rasterizeQuad(sw, verts, glyphTexPixels, glyphTexW, glyphTexH);

            cursorX += glyph->shift;
            if (lineLen > pos) {
                int32_t savedPos = pos;
                uint16_t nextCh = TextUtils_decodeUtf8(text + lineStart, lineLen, &pos);
                pos = savedPos;
                cursorX += TextUtils_getKerningOffset(glyph, nextCh);
            }
        }

        cursorY += (float) font->emSize;
        if (textLen > lineEnd) {
            lineStart = TextUtils_skipNewline(text, lineEnd, textLen);
        } else {
            lineStart = lineEnd;
        }
    }
}

static void swDrawTextColor(Renderer* renderer, const char* text, float x, float y, float xscale, float yscale, float angleDeg, int32_t _c1, int32_t _c2, int32_t _c3, int32_t _c4, float alpha) {
    SoftwareRenderer* sw = (SoftwareRenderer*) renderer;
    DataWin* dw = renderer->dataWin;

    int32_t fontIndex = renderer->drawFont;
    if (0 > fontIndex || dw->font.count <= (uint32_t) fontIndex) return;

    Font* font = &dw->font.fonts[fontIndex];

    SWFontState fontState;
    if (!swResolveFontState(sw, dw, font, &fontState)) return;

    char* processed = TextUtils_preprocessGmlText(text);
    int32_t textLen = (int32_t) strlen(processed);
    if (textLen == 0) { free(processed); return; }

    int32_t lineCount = TextUtils_countLines(processed, textLen);

    float totalHeight = (float) lineCount * (float) font->emSize;
    float valignOffset = 0;
    if (renderer->drawValign == 1) valignOffset = -totalHeight / 2.0f;
    else if (renderer->drawValign == 2) valignOffset = -totalHeight;

    float angleRad = -angleDeg * ((float) M_PI / 180.0f);
    Matrix4f transform;
    Matrix4f_setTransform2D(&transform, x, y, xscale * font->scaleX, yscale * font->scaleY, angleRad);

    float cursorY = valignOffset;
    int32_t lineStart = 0;

    // Color gradient deltas (16.16 fixed-point, matching GL renderer)
    int32_t left_r_dx = ((_c2 & 0xff0000) - (_c1 & 0xff0000)) / textLen;
    int32_t left_g_dx = ((((_c2 & 0xff00) << 8) - ((_c1 & 0xff00) << 8))) / textLen;
    int32_t left_b_dx = ((((_c2 & 0xff) << 16) - ((_c1 & 0xff) << 16))) / textLen;

    int32_t right_r_dx = ((_c3 & 0xff0000) - (_c4 & 0xff0000)) / textLen;
    int32_t right_g_dx = ((((_c3 & 0xff00) << 8) - ((_c4 & 0xff00) << 8))) / textLen;
    int32_t right_b_dx = ((((_c3 & 0xff) << 16) - ((_c4 & 0xff) << 16))) / textLen;

    int32_t left_delta_r = left_r_dx;
    int32_t left_delta_g = left_g_dx;
    int32_t left_delta_b = left_b_dx;
    int32_t right_delta_r = right_r_dx;
    int32_t right_delta_g = right_g_dx;
    int32_t right_delta_b = right_b_dx;

    int32_t c1 = _c1;
    int32_t c4 = _c4;

    for (int32_t lineIdx = 0; lineCount > lineIdx; lineIdx++) {
        int32_t lineEnd = lineStart;
        while (textLen > lineEnd && !TextUtils_isNewlineChar(processed[lineEnd])) lineEnd++;
        int32_t lineLen = lineEnd - lineStart;

        float lineWidth = TextUtils_measureLineWidth(font, processed + lineStart, lineLen);
        float halignOffset = 0;
        if (renderer->drawHalign == 1) halignOffset = -lineWidth / 2.0f;
        else if (renderer->drawHalign == 2) halignOffset = -lineWidth;

        float cursorX = halignOffset;
        int32_t pos = 0;

        while (lineLen > pos) {
            int32_t c2 = ((c1 & 0xff0000) + (left_delta_r & 0xff0000)) & 0xff0000;
            c2 |= ((c1 & 0xff00) + (left_delta_g >> 8) & 0xff00) & 0xff00;
            c2 |= ((c1 & 0xff) + (left_delta_b >> 16)) & 0xff;
            int32_t c3 = ((c4 & 0xff0000) + (right_delta_r & 0xff0000)) & 0xff0000;
            c3 |= ((c4 & 0xff00) + (right_delta_g >> 8) & 0xff00) & 0xff00;
            c3 |= ((c4 & 0xff) + (right_delta_b >> 16)) & 0xff;

            left_delta_r += left_r_dx;
            left_delta_g += left_g_dx;
            left_delta_b += left_b_dx;
            right_delta_r += right_r_dx;
            right_delta_g += right_g_dx;
            right_delta_b += right_b_dx;

            uint16_t ch = TextUtils_decodeUtf8(processed + lineStart, lineLen, &pos);
            FontGlyph* glyph = TextUtils_findGlyph(font, ch);
            if (glyph == nullptr) continue;
            if (glyph->sourceWidth == 0 || glyph->sourceHeight == 0) {
                cursorX += glyph->shift;
                continue;
            }

            uint8_t* glyphTexPixels;
            int32_t glyphTexW, glyphTexH;
            float u0, v0, u1, v1;
            float localX0, localY0;

            if (!swResolveGlyph(sw, dw, &fontState, glyph, cursorX, cursorY, &glyphTexPixels, &glyphTexW, &glyphTexH, &u0, &v0, &u1, &v1, &localX0, &localY0)) {
                cursorX += glyph->shift;
                continue;
            }

            float localX1 = localX0 + (float) glyph->sourceWidth;
            float localY1 = localY0 + (float) glyph->sourceHeight;

            float wx0, wy0, wx1, wy1, wx2, wy2, wx3, wy3;
            Matrix4f_transformPoint(&transform, localX0, localY0, &wx0, &wy0);
            Matrix4f_transformPoint(&transform, localX1, localY0, &wx1, &wy1);
            Matrix4f_transformPoint(&transform, localX1, localY1, &wx2, &wy2);
            Matrix4f_transformPoint(&transform, localX0, localY1, &wx3, &wy3);

            SWVertex verts[4];
            worldToScreen(sw, wx0, wy0, &verts[0].x, &verts[0].y);
            worldToScreen(sw, wx1, wy1, &verts[1].x, &verts[1].y);
            worldToScreen(sw, wx2, wy2, &verts[2].x, &verts[2].y);
            worldToScreen(sw, wx3, wy3, &verts[3].x, &verts[3].y);

            verts[0].u = u0; verts[0].v = v0;
            verts[1].u = u1; verts[1].v = v0;
            verts[2].u = u1; verts[2].v = v1;
            verts[3].u = u0; verts[3].v = v1;

            // Per-vertex colors (top-left=c1, top-right=c2, bottom-right=c3, bottom-left=c4)
            verts[0].r = (float) BGR_R(c1) / 255.0f; verts[0].g = (float) BGR_G(c1) / 255.0f; verts[0].b = (float) BGR_B(c1) / 255.0f; verts[0].a = alpha;
            verts[1].r = (float) BGR_R(c2) / 255.0f; verts[1].g = (float) BGR_G(c2) / 255.0f; verts[1].b = (float) BGR_B(c2) / 255.0f; verts[1].a = alpha;
            verts[2].r = (float) BGR_R(c3) / 255.0f; verts[2].g = (float) BGR_G(c3) / 255.0f; verts[2].b = (float) BGR_B(c3) / 255.0f; verts[2].a = alpha;
            verts[3].r = (float) BGR_R(c4) / 255.0f; verts[3].g = (float) BGR_G(c4) / 255.0f; verts[3].b = (float) BGR_B(c4) / 255.0f; verts[3].a = alpha;

            rasterizeQuad(sw, verts, glyphTexPixels, glyphTexW, glyphTexH);

            cursorX += glyph->shift;
            if (lineLen > pos) {
                int32_t savedPos = pos;
                uint16_t nextCh = TextUtils_decodeUtf8(processed + lineStart, lineLen, &pos);
                pos = savedPos;
                cursorX += TextUtils_getKerningOffset(glyph, nextCh);
            }
            c4 = c3;
            c1 = c2;
        }

        cursorY += (float) font->emSize;
        if (textLen > lineEnd) {
            lineStart = TextUtils_skipNewline(processed, lineEnd, textLen);
        } else {
            lineStart = lineEnd;
        }
    }

    free(processed);
}

// ===[ Stubs ]===

static void swFlush(Renderer* renderer) {
    (void) renderer;
    // Immediate mode - nothing to flush
}

static int32_t swCreateSpriteFromSurface(Renderer* renderer, int32_t x, int32_t y, int32_t w, int32_t h, bool removeback, bool smooth, int32_t xorig, int32_t yorig) {
    (void) renderer; (void) x; (void) y; (void) w; (void) h;
    (void) removeback; (void) smooth; (void) xorig; (void) yorig;
    fprintf(stderr, "SW: createSpriteFromSurface not implemented\n");
    return -1;
}

static void swDeleteSprite(Renderer* renderer, int32_t spriteIndex) {
    (void) renderer; (void) spriteIndex;
    fprintf(stderr, "SW: deleteSprite not implemented\n");
}

// ===[ Vtable ]===

static RendererVtable swVtable = {
    .init = swInit,
    .destroy = swDestroy,
    .beginFrame = swBeginFrame,
    .endFrame = swEndFrame,
    .beginView = swBeginView,
    .endView = swEndView,
    .drawSprite = swDrawSprite,
    .drawSpritePart = swDrawSpritePart,
    .drawRectangle = swDrawRectangle,
    .drawLine = swDrawLine,
    .drawLineColor = swDrawLineColor,
    .drawTriangle = swDrawTriangle,
    .drawText = swDrawText,
    .drawTextColor = swDrawTextColor,
    .flush = swFlush,
    .createSpriteFromSurface = swCreateSpriteFromSurface,
    .deleteSprite = swDeleteSprite,
    .drawTile = nullptr,
};

// ===[ Public API ]===

Renderer* SoftwareRenderer_create(void) {
    SoftwareRenderer* sw = safeCalloc(1, sizeof(SoftwareRenderer));
    sw->base.vtable = &swVtable;
    sw->base.drawColor = 0xFFFFFF; // white (BGR)
    sw->base.drawAlpha = 1.0f;
    sw->base.drawFont = -1;
    sw->base.drawHalign = 0;
    sw->base.drawValign = 0;
    return (Renderer*) sw;
}
