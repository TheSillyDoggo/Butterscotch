#include "gs_renderer.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "utils.h"
#include "text_utils.h"
#include <gsTexture.h>

// ===[ Vtable Implementations ]===

static void gsInit(Renderer* renderer, DataWin* dataWin) {
    GsRenderer* gs = (GsRenderer*) renderer;

    renderer->dataWin = dataWin;
    renderer->drawColor = 0xFFFFFF;
    renderer->drawAlpha = 1.0f;
    renderer->drawFont = -1;
    renderer->drawHalign = 0;
    renderer->drawValign = 0;

    // Enable alpha blending on all primitives (sets ABE bit in GS PRIM register)
    gs->gsGlobal->PrimAlphaEnable = GS_SETTING_ON;

    // Set alpha blend equation: (Cs - Cd) * As / 128 + Cd (standard src-over blend)
    gsKit_set_primalpha(gs->gsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);

    printf("GsRenderer: initialized with %u sprites, %u TPAG items\n", dataWin->sprt.count, dataWin->tpag.count);
}

static void gsDestroy(Renderer* renderer) {
    GsRenderer* gs = (GsRenderer*) renderer;
    GsTextureCache_free(gs->textureCache);
    free(gs);
}

static void gsBeginFrame(Renderer* renderer, [[maybe_unused]] int32_t gameW, [[maybe_unused]] int32_t gameH, [[maybe_unused]] int32_t windowW, [[maybe_unused]] int32_t windowH) {
    GsRenderer* gs = (GsRenderer*) renderer;
    gs->zCounter = 1;
    GsTextureCache_beginFrame(gs->textureCache);
}

static void gsEndFrame([[maybe_unused]] Renderer* renderer) {
    // No-op: flip happens in main loop
}

static void gsBeginView(Renderer* renderer, int32_t viewX, int32_t viewY, int32_t viewW, int32_t viewH, [[maybe_unused]] int32_t portX, [[maybe_unused]] int32_t portY, [[maybe_unused]] int32_t portW, [[maybe_unused]] int32_t portH, [[maybe_unused]] float viewAngle) {
    GsRenderer* gs = (GsRenderer*) renderer;
    gs->viewX = viewX;
    gs->viewY = viewY;

    // Scale game view to PS2 screen (640x448 NTSC interlaced)
    // Use uniform scale based on width (640/viewW) so pixels stay square.
    if (viewW > 0 && viewH > 0) {
        gs->scaleX = 640.0f / (float) viewW;
        gs->scaleY = gs->scaleX;
    } else {
        gs->scaleX = 2.0f;
        gs->scaleY = 2.0f;
    }

    // Center vertically: offset so the rendered image is centered on the 448px screen
    float renderedH = (float) viewH * gs->scaleY;
    gs->offsetX = 0.0f;
    gs->offsetY = (448.0f - renderedH) / 2.0f;
}

static void gsEndView([[maybe_unused]] Renderer* renderer) {
    // No-op
}

static void gsDrawSprite(Renderer* renderer, int32_t tpagIndex, float x, float y, float originX, float originY, float xscale, float yscale, [[maybe_unused]] float angleDeg, uint32_t color, float alpha) {
    GsRenderer* gs = (GsRenderer*) renderer;
    DataWin* dw = renderer->dataWin;

    if (0 > tpagIndex || (uint32_t) tpagIndex >= dw->tpag.count) return;

    TexturePageItem* tpag = &dw->tpag.items[tpagIndex];
    float w = (float) tpag->boundingWidth;
    float h = (float) tpag->boundingHeight;

    // Compute screen rect in game coordinates
    float gameX1 = x - originX * xscale;
    float gameY1 = y - originY * yscale;
    float gameX2 = x + (w - originX) * xscale;
    float gameY2 = y + (h - originY) * yscale;

    // Apply view offset
    gameX1 -= (float) gs->viewX;
    gameY1 -= (float) gs->viewY;
    gameX2 -= (float) gs->viewX;
    gameY2 -= (float) gs->viewY;

    // Scale to screen coordinates
    float sx1 = gameX1 * gs->scaleX + gs->offsetX;
    float sy1 = gameY1 * gs->scaleY + gs->offsetY;
    float sx2 = gameX2 * gs->scaleX + gs->offsetX;
    float sy2 = gameY2 * gs->scaleY + gs->offsetY;

    GSTEXTURE* tex = GsTextureCache_get(gs->textureCache, tpagIndex);
    if (tex == nullptr) {
        // Fallback: colored rectangle
        uint8_t r = BGR_R(color);
        uint8_t g = BGR_G(color);
        uint8_t b = BGR_B(color);
        uint8_t a = (uint8_t) (alpha * 128.0f);
        u64 fallbackColor = GS_SETREG_RGBAQ(r, g, b, a, 0x00);
        gsKit_prim_sprite(gs->gsGlobal, sx1, sy1, sx2, sy2, gs->zCounter, fallbackColor);
        gs->zCounter++;
        return;
    }

    // Color modulation: GS multiplies vertex color with texture color.
    // For indexed textures the palette already has correct colors,
    // so we use 0x80 (half, which maps to 1.0 after the GS doubles it).
    // Apply the draw color as BGR->RGB tint.
    uint8_t r = BGR_R(color);
    uint8_t g = BGR_G(color);
    uint8_t b = BGR_B(color);
    // Scale from 0-255 to 0-128 (GS color range)
    r = (uint8_t) ((uint32_t) r * 128 / 255);
    g = (uint8_t) ((uint32_t) g * 128 / 255);
    b = (uint8_t) ((uint32_t) b * 128 / 255);
    uint8_t a = (uint8_t) (alpha * 128.0f);
    u64 texColor = GS_SETREG_RGBAQ(r, g, b, a, 0x00);

    gsKit_prim_sprite_texture(gs->gsGlobal, tex,
        sx1, sy1, 0.0f, 0.0f,
        sx2, sy2, w, h,
        gs->zCounter, texColor);
    gs->zCounter++;
}

static void gsDrawSpritePart(Renderer* renderer, int32_t tpagIndex, int32_t srcOffX, int32_t srcOffY, int32_t srcW, int32_t srcH, float x, float y, float xscale, float yscale, uint32_t color, float alpha) {
    GsRenderer* gs = (GsRenderer*) renderer;
    DataWin* dw = renderer->dataWin;

    if (0 > tpagIndex || (uint32_t) tpagIndex >= dw->tpag.count) return;

    TexturePageItem* tpag = &dw->tpag.items[tpagIndex];

    // Compute screen rect
    float gameX1 = x - (float) gs->viewX;
    float gameY1 = y - (float) gs->viewY;
    float gameX2 = gameX1 + (float) srcW * xscale;
    float gameY2 = gameY1 + (float) srcH * yscale;

    float sx1 = gameX1 * gs->scaleX + gs->offsetX;
    float sy1 = gameY1 * gs->scaleY + gs->offsetY;
    float sx2 = gameX2 * gs->scaleX + gs->offsetX;
    float sy2 = gameY2 * gs->scaleY + gs->offsetY;

    GSTEXTURE* tex = GsTextureCache_get(gs->textureCache, tpagIndex);
    if (tex == nullptr) {
        uint8_t r = BGR_R(color);
        uint8_t g = BGR_G(color);
        uint8_t b = BGR_B(color);
        uint8_t a = (uint8_t) (alpha * 128.0f);
        u64 fallbackColor = GS_SETREG_RGBAQ(r, g, b, a, 0x00);
        gsKit_prim_sprite(gs->gsGlobal, sx1, sy1, sx2, sy2, gs->zCounter, fallbackColor);
        gs->zCounter++;
        return;
    }

    // The BTX image has content placed at (targetX, targetY) within the bounding rect.
    // srcOffX/srcOffY are content-relative (already adjusted by the shared helper in renderer.h),
    // so we add targetX/targetY to get pixel coords in the BTX image.
    float u0 = (float) (tpag->targetX + srcOffX);
    float v0 = (float) (tpag->targetY + srcOffY);
    float u1 = u0 + (float) srcW;
    float v1 = v0 + (float) srcH;

    uint8_t r = BGR_R(color);
    uint8_t g = BGR_G(color);
    uint8_t b = BGR_B(color);
    r = (uint8_t) ((uint32_t) r * 128 / 255);
    g = (uint8_t) ((uint32_t) g * 128 / 255);
    b = (uint8_t) ((uint32_t) b * 128 / 255);
    uint8_t a = (uint8_t) (alpha * 128.0f);
    u64 texColor = GS_SETREG_RGBAQ(r, g, b, a, 0x00);

    gsKit_prim_sprite_texture(gs->gsGlobal, tex, sx1, sy1, u0, v0, sx2, sy2, u1, v1, gs->zCounter, texColor);
    gs->zCounter++;
}

static void gsDrawRectangle(Renderer* renderer, float x1, float y1, float x2, float y2, uint32_t color, float alpha, [[maybe_unused]] bool outline) {
    GsRenderer* gs = (GsRenderer*) renderer;

    // BGR to RGB
    uint8_t r = BGR_R(color);
    uint8_t g = BGR_G(color);
    uint8_t b = BGR_B(color);
    uint8_t a = (uint8_t) (alpha * 128.0f);

    float sx1 = (x1 - (float) gs->viewX) * gs->scaleX + gs->offsetX;
    float sy1 = (y1 - (float) gs->viewY) * gs->scaleY + gs->offsetY;
    float sx2 = (x2 - (float) gs->viewX) * gs->scaleX + gs->offsetX;
    float sy2 = (y2 - (float) gs->viewY) * gs->scaleY + gs->offsetY;

    u64 rectColor = GS_SETREG_RGBAQ(r, g, b, a, 0x00);
    gsKit_prim_sprite(gs->gsGlobal, sx1, sy1, sx2, sy2, gs->zCounter, rectColor);
    gs->zCounter++;
}

static void gsDrawLine(Renderer* renderer, float x1, float y1, float x2, float y2, [[maybe_unused]] float width, uint32_t color, float alpha) {
    GsRenderer* gs = (GsRenderer*) renderer;

    uint8_t r = BGR_R(color);
    uint8_t g = BGR_G(color);
    uint8_t b = BGR_B(color);
    uint8_t a = (uint8_t) (alpha * 128.0f);

    float sx1 = (x1 - (float) gs->viewX) * gs->scaleX + gs->offsetX;
    float sy1 = (y1 - (float) gs->viewY) * gs->scaleY + gs->offsetY;
    float sx2 = (x2 - (float) gs->viewX) * gs->scaleX + gs->offsetX;
    float sy2 = (y2 - (float) gs->viewY) * gs->scaleY + gs->offsetY;

    u64 lineColor = GS_SETREG_RGBAQ(r, g, b, a, 0x00);
    gsKit_prim_line(gs->gsGlobal, sx1, sy1, sx2, sy2, gs->zCounter, lineColor);
    gs->zCounter++;
}

static void gsDrawText(Renderer* renderer, const char* text, float x, float y, float xscale, float yscale, [[maybe_unused]] float angleDeg) {
    GsRenderer* gs = (GsRenderer*) renderer;
    DataWin* dw = renderer->dataWin;

    if (0 > renderer->drawFont || (uint32_t) renderer->drawFont >= dw->font.count) return;

    Font* font = &dw->font.fonts[renderer->drawFont];

    // Resolve font TPAG and load texture
    int32_t fontTpagIndex = DataWin_resolveTPAG(dw, font->textureOffset);
    TexturePageItem* fontTpag = nullptr;
    GSTEXTURE* fontTex = nullptr;
    if (fontTpagIndex >= 0) {
        fontTpag = &dw->tpag.items[fontTpagIndex];
        fontTex = GsTextureCache_get(gs->textureCache, fontTpagIndex);
    }

    // BGR to RGB for text color
    uint32_t color = renderer->drawColor;
    uint8_t r = BGR_R(color);
    uint8_t g = BGR_G(color);
    uint8_t b = BGR_B(color);
    uint8_t a = (uint8_t) (renderer->drawAlpha * 128.0f);
    u64 textColor = GS_SETREG_RGBAQ(r, g, b, a, 0x00);

    // Preprocess GML text (# -> \n, \# -> #)
    char* processed = TextUtils_preprocessGmlText(text);
    int32_t textLen = (int32_t) strlen(processed);

    // Compute vertical alignment offset
    int32_t lineCount = TextUtils_countLines(processed, textLen);
    float totalHeight = (float) lineCount * (float) font->emSize;
    float valignOffset = 0;
    if (renderer->drawValign == 1) valignOffset = -totalHeight / 2.0f;
    else if (renderer->drawValign == 2) valignOffset = -totalHeight;

    float cursorY = valignOffset;
    int32_t lineStart = 0;

    while (textLen >= lineStart) {
        // Find end of current line
        int32_t lineEnd = lineStart;
        while (textLen > lineEnd && !TextUtils_isNewlineChar(processed[lineEnd])) {
            lineEnd++;
        }

        int32_t lineLen = lineEnd - lineStart;
        const char* line = processed + lineStart;

        // Measure line width for horizontal alignment
        float lineWidth = TextUtils_measureLineWidth(font, line, lineLen);
        float halignOffset = 0;
        if (renderer->drawHalign == 1) halignOffset = -lineWidth / 2.0f;
        else if (renderer->drawHalign == 2) halignOffset = -lineWidth;

        float cursorX = halignOffset;

        // Draw each glyph
        int32_t pos = 0;
        while (lineLen > pos) {
            uint16_t ch = TextUtils_decodeUtf8(line, lineLen, &pos);
            FontGlyph* glyph = TextUtils_findGlyph(font, ch);
            if (glyph == nullptr) continue;

            if (glyph->sourceWidth > 0 && glyph->sourceHeight > 0) {
                float glyphX = x + (cursorX + (float) glyph->offset) * xscale * font->scaleX;
                float glyphY = y + cursorY * yscale * font->scaleY;
                float glyphW = (float) glyph->sourceWidth * xscale * font->scaleX;
                float glyphH = (float) glyph->sourceHeight * yscale * font->scaleY;

                // Apply view offset and scale to screen coordinates
                float sx1 = (glyphX - (float) gs->viewX) * gs->scaleX + gs->offsetX;
                float sy1 = (glyphY - (float) gs->viewY) * gs->scaleY + gs->offsetY;
                float sx2 = (glyphX + glyphW - (float) gs->viewX) * gs->scaleX + gs->offsetX;
                float sy2 = (glyphY + glyphH - (float) gs->viewY) * gs->scaleY + gs->offsetY;

                if (fontTex != nullptr && fontTpag != nullptr) {
                    // Compute UVs: glyph sourceX/sourceY are relative to TPAG content,
                    // add targetX/targetY for BTX image pixel coords
                    float u0 = (float) (fontTpag->targetX + glyph->sourceX);
                    float v0 = (float) (fontTpag->targetY + glyph->sourceY);
                    float u1 = u0 + (float) glyph->sourceWidth;
                    float v1 = v0 + (float) glyph->sourceHeight;

                    gsKit_prim_sprite_texture(gs->gsGlobal, fontTex, sx1, sy1, u0, v0, sx2, sy2, u1, v1, gs->zCounter, textColor);
                } else {
                    // Fallback: colored rectangle
                    gsKit_prim_sprite(gs->gsGlobal, sx1, sy1, sx2, sy2, gs->zCounter, textColor);
                }
            }

            cursorX += (float) glyph->shift;

            // Apply kerning
            if (lineLen > pos) {
                int32_t savedPos = pos;
                uint16_t nextCh = TextUtils_decodeUtf8(line, lineLen, &pos);
                pos = savedPos;
                cursorX += TextUtils_getKerningOffset(glyph, nextCh);
            }
        }

        gs->zCounter++;

        // Advance to next line
        cursorY += (float) font->emSize;
        if (textLen > lineEnd) {
            lineStart = TextUtils_skipNewline(processed, lineEnd, textLen);
        } else {
            break;
        }
    }

    free(processed);
}

static void gsFlush([[maybe_unused]] Renderer* renderer) {
    // No-op: gsKit queues commands, executed in main loop via gsKit_queue_exec
}

static int32_t gsCreateSpriteFromSurface([[maybe_unused]] Renderer* renderer, [[maybe_unused]] int32_t x, [[maybe_unused]] int32_t y, [[maybe_unused]] int32_t w, [[maybe_unused]] int32_t h, [[maybe_unused]] bool removeback, [[maybe_unused]] bool smooth, [[maybe_unused]] int32_t xorig, [[maybe_unused]] int32_t yorig) {
    fprintf(stderr, "GsRenderer: createSpriteFromSurface not supported on PS2\n");
    return -1;
}

static void gsDeleteSprite([[maybe_unused]] Renderer* renderer, [[maybe_unused]] int32_t spriteIndex) {
    // No-op
}

// ===[ Constructor ]===

static RendererVtable gsVtable = {
    .init = gsInit,
    .destroy = gsDestroy,
    .beginFrame = gsBeginFrame,
    .endFrame = gsEndFrame,
    .beginView = gsBeginView,
    .endView = gsEndView,
    .drawSprite = gsDrawSprite,
    .drawSpritePart = gsDrawSpritePart,
    .drawRectangle = gsDrawRectangle,
    .drawLine = gsDrawLine,
    .drawText = gsDrawText,
    .flush = gsFlush,
    .createSpriteFromSurface = gsCreateSpriteFromSurface,
    .deleteSprite = gsDeleteSprite,
};

Renderer* GsRenderer_create(GSGLOBAL* gsGlobal, GsTextureCache* textureCache) {
    GsRenderer* gs = safeCalloc(1, sizeof(GsRenderer));
    gs->base.vtable = &gsVtable;
    gs->gsGlobal = gsGlobal;
    gs->textureCache = textureCache;
    gs->scaleX = 2.0f;
    gs->scaleY = 2.0f;
    gs->zCounter = 1;
    return (Renderer*) gs;
}
