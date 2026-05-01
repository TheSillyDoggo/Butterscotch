#include "sdl2_renderer.h"
#include "common.h"
#include "matrix_math.h"
#include "text_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "stb_image.h"
#include "stb_ds.h"
#include "utils.h"
#include "image_decoder.h"
#include <SDL2/SDL.h>
// #include <SDL2/SDL_image.h>

#define MAX(a,b) (((a) > (b)) ? (a) : (b))
#define MIN(a,b) (((a) < (b)) ? (a) : (b))

static bool ensureTextureLoaded(SDL2Renderer* sdl, uint32_t pageId) {
    if (sdl->textureLoaded[pageId]) {
        return (sdl->textureWidths[pageId] != 0);
    }

    if (pageId < 0 || pageId >= sdl->base.dataWin->txtr.count) return false;

    sdl->textureLoaded[pageId] = true;

    DataWin* dw = sdl->base.dataWin;
    Texture* txtr = &dw->txtr.textures[pageId];

    int w, h;
    bool gm2022_5 = DataWin_isVersionAtLeast(dw, 2022, 5, 0, 0);
    uint8_t* pixels = ImageDecoder_decodeToRgba(txtr->blobData, (size_t) txtr->blobSize, gm2022_5, &w, &h);

    if (pixels == nullptr) {
        fprintf(stderr, "GL: Failed to decode TXTR page %u\n", pageId);
        return false;
    }

    fprintf(stderr, "SDL: decoded %d %d %d\n", w, h, 4 * w * h);

    sdl->textureWidths[pageId] = w;
    sdl->textureHeights[pageId] = h;

    /*SDL_Surface* src = SDL_CreateRGBSurfaceWithFormatFrom(
        pixels,
        w,
        h,
        32,
        w * 4,
        SDL_PIXELFORMAT_RGBA32
    );

    SDL_Surface* dst = SDL_CreateRGBSurfaceWithFormat(
        0,
        512,
        512,
        32,
        SDL_PIXELFORMAT_RGBA4444
    );

    SDL_FillRect(dst, nullptr,
        SDL_MapRGBA(dst->format, 0, 0, 0, 0)
    );

    SDL_Rect srcRect;
    srcRect.x = 0;
    srcRect.y = 0;
    srcRect.w = MIN(512, src->w - srcRect.x);
    srcRect.h = MIN(512, src->h - srcRect.y);

    SDL_Rect dstRect = {
        0, 0,
        srcRect.w,
        srcRect.h
    };

    SDL_BlitSurface(src, &srcRect, dst, &dstRect);

    SDL_Texture* tex = SDL_CreateTextureFromSurface(sdl->renderer, dst);

    SDL_SetTextureScaleMode(tex, SDL_ScaleModeNearest);

    SDL_FreeSurface(dst);
    SDL_FreeSurface(src);*/

    SDL_Texture* tex = SDL_CreateTexture(
        (SDL_Renderer*)sdl->renderer,
        SDL_PIXELFORMAT_RGBA32,
        SDL_TEXTUREACCESS_STATIC,
        w,
        h
    );

    if (!tex) {
        fprintf(stderr, "SDL: CreateTexture failed: %s\n", SDL_GetError());
        free(pixels);
        return false;
    }

    if (SDL_UpdateTexture(tex, NULL, pixels, w * 4) != 0) {
        fprintf(stderr, "SDL: UpdateTexture failed: %s\n", SDL_GetError());
        SDL_DestroyTexture(tex);
        free(pixels);
        return false;
    }

    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);

    sdl->textures[pageId] = tex;

    free(pixels);
    fprintf(stderr, "SDL: Loaded TXTR page %u (%dx%d)\n", pageId, w, h);
    return true;
}

SDL_Vertex sdlMakeVertex(SDL2Renderer* sdl, float x, float y, SDL_Color color, float u, float v) {
    SDL_Vertex vertex;

    x *= sdl->renderScale;
    y *= sdl->renderScale;

    vertex.position.x = x;
    vertex.position.y = y;
    vertex.color = color;
    vertex.tex_coord.x = u;
    vertex.tex_coord.y = v;

    return vertex;
}

// ===[ Vtable Implementations ]===

static void sdlInit(Renderer* renderer, DataWin* dataWin) {
    SDL2Renderer* sdl = (SDL2Renderer*) renderer;
    sdl->base.dataWin = dataWin;

    sdl->renderer = SDL_CreateRenderer((SDL_Window*)sdl->window, -1, SDL_RENDERER_ACCELERATED);

    sdl->textureCount = dataWin->txtr.count;
    sdl->textures = safeMalloc(sdl->textureCount * sizeof(void*));
    sdl->textureWidths = safeMalloc(sdl->textureCount * sizeof(int32_t));
    sdl->textureHeights = safeMalloc(sdl->textureCount * sizeof(int32_t));
    sdl->textureLoaded = safeMalloc(sdl->textureCount * sizeof(bool));

    for (uint32_t i = 0; sdl->textureCount > i; i++) {
        sdl->textureWidths[i] = 0;
        sdl->textureHeights[i] = 0;
        sdl->textureLoaded[i] = false;
    }
}

static void sdlDestroy(Renderer* renderer) {
    
}

static void sdlBeginFrame(Renderer* renderer, int32_t gameW, int32_t gameH, int32_t windowW, int32_t windowH) {
    SDL2Renderer* sdl = (SDL2Renderer*) renderer;

    SDL_SetWindowSize((SDL_Window*)sdl->window, gameW, gameH);
    SDL_SetRenderDrawColor((SDL_Renderer*)sdl->renderer, 0, 0, 0, 255);
    SDL_RenderClear((SDL_Renderer*)sdl->renderer);
}

static void sdlBeginView(Renderer* renderer, int32_t viewX, int32_t viewY, int32_t viewW, int32_t viewH, int32_t portX, int32_t portY, int32_t portW, int32_t portH, float viewAnsdle) {
    SDL2Renderer* sdl = (SDL2Renderer*) renderer;

    sdl->renderScale = MAX(floor((float)portH / (float)viewH), 1);
    sdl->viewX = viewX;
    sdl->viewY = viewY;
    sdl->portW = portW;
    sdl->portH = portH;

    SDL_Rect rect = {
        0, 0,
        viewW * sdl->renderScale,
        viewH * sdl->renderScale
    };
    SDL_RenderSetViewport(sdl->renderer, &rect);
}

static void sdlEndView(MAYBE_UNUSED Renderer* renderer) {
    
}

static void sdlBeginGUI(Renderer* renderer, int32_t guiW, int32_t guiH, int32_t portX, int32_t portY, int32_t portW, int32_t portH) {
    
}

static void sdlEndGUI(MAYBE_UNUSED Renderer* renderer) {
    
}

static void sdlEndFrame(MAYBE_UNUSED Renderer* renderer)
{
    SDL2Renderer* sdl = (SDL2Renderer*) renderer;
    SDL_RenderPresent((SDL_Renderer*)sdl->renderer);
}

static void SDL2RendererFlush(MAYBE_UNUSED Renderer* renderer) {}

static void sdlDrawSprite(Renderer* renderer, int32_t tpagIndex, float x, float y, float originX, float originY, float xscale, float yscale, float ansdleDeg, uint32_t color, float alpha) {
    alpha = SDL_clamp(alpha, 0, 1);
    SDL2Renderer* sdl = (SDL2Renderer*) renderer;
    DataWin* dw = renderer->dataWin;

    if (0 > tpagIndex || dw->tpag.count <= (uint32_t) tpagIndex) return;

    TexturePageItem* tpag = &dw->tpag.items[tpagIndex];
    int16_t pageId = tpag->texturePageId;
    if (0 > pageId || sdl->textureCount <= (uint32_t) pageId) return;
    if (!ensureTextureLoaded(sdl, (uint32_t) pageId)) return;

    SDL_Texture* texId = sdl->textures[pageId];
    int32_t texW = sdl->textureWidths[pageId];
    int32_t texH = sdl->textureHeights[pageId];

    // Compute normalized UVs from TPAG source rect
    float u0 = (float) tpag->sourceX / (float) texW;
    float v0 = (float) tpag->sourceY / (float) texH;
    float u1 = (float) (tpag->sourceX + tpag->sourceWidth) / (float) texW;
    float v1 = (float) (tpag->sourceY + tpag->sourceHeight) / (float) texH;

    // Compute local quad corners (relative to origin, with target offset)
    float localX0 = (float) tpag->targetX - originX;
    float localY0 = (float) tpag->targetY - originY;
    float localX1 = localX0 + (float) tpag->sourceWidth;
    float localY1 = localY0 + (float) tpag->sourceHeight;

    // Build 2D transform: T(x,y) * R(-angleDeg) * S(xscale, yscale)
    // GML rotation is counter-clockwise, OpenGL rotation is counter-clockwise, but
    // since we have Y-down, we negate the angle to get the correct visual rotation
    float angleRad = -ansdleDeg * ((float) M_PI / 180.0f);
    Matrix4f transform;
    Matrix4f_setTransform2D(&transform, x, y, xscale, yscale, angleRad);

    // Transform 4 corners
    float x0, y0, x1, y1, x2, y2, x3, y3;
    Matrix4f_transformPoint(&transform, localX0, localY0, &x0, &y0); // top-left
    Matrix4f_transformPoint(&transform, localX1, localY0, &x1, &y1); // top-right
    Matrix4f_transformPoint(&transform, localX1, localY1, &x2, &y2); // bottom-right
    Matrix4f_transformPoint(&transform, localX0, localY1, &x3, &y3); // bottom-left

    uint8_t r = BGR_R(color);
    uint8_t g = BGR_G(color);
    uint8_t b = BGR_B(color);
    uint8_t a = alpha * 255;

    SDL_Color color2 = { r, g, b, a };
    SDL_Vertex vertices[4];

    vertices[0] = sdlMakeVertex(sdl, x0, y0, color2, u0, v0); // top-left
    vertices[1] = sdlMakeVertex(sdl, x1, y1, color2, u1, v0); // top-right
    vertices[2] = sdlMakeVertex(sdl, x2, y2, color2, u1, v1); // bottom-right
    vertices[3] = sdlMakeVertex(sdl, x3, y3, color2, u0, v1); // bottom-left

    int indices[6] = { 0, 1, 3, 1, 2, 3 };

    SDL_SetRenderDrawBlendMode((SDL_Renderer*)sdl->renderer, SDL_BLENDMODE_BLEND);
    SDL_RenderGeometry((SDL_Renderer*)sdl->renderer, sdl->textures[tpag->texturePageId], vertices, 4, indices, 6);
}

static void sdlDrawSpritePart(Renderer* renderer, int32_t tpagIndex, int32_t srcOffX, int32_t srcOffY, int32_t srcW, int32_t srcH, float x, float y, float xscale, float yscale, uint32_t color, float alpha) {
    alpha = SDL_clamp(alpha, 0, 1);
    SDL2Renderer* sdl = (SDL2Renderer*) renderer;
    DataWin* dw = renderer->dataWin;

    // if (0 > tpagIndex || dw->tpag.count <= (uint32_t) tpagIndex) return;

    TexturePageItem* tpag = &dw->tpag.items[tpagIndex];
    int16_t pageId = tpag->texturePageId;
    if (0 > pageId || sdl->textureCount <= (uint32_t) pageId) return;
    if (!ensureTextureLoaded(sdl, (uint32_t) pageId)) return;

    SDL_Texture* texId = sdl->textures[pageId];
    int32_t texW = sdl->textureWidths[pageId];
    int32_t texH = sdl->textureHeights[pageId];

    // Compute UVs for the sub-region within the atlas
    float u0 = (float) (tpag->sourceX + srcOffX) / (float) texW;
    float v0 = (float) (tpag->sourceY + srcOffY) / (float) texH;
    float u1 = (float) (tpag->sourceX + srcOffX + srcW) / (float) texW;
    float v1 = (float) (tpag->sourceY + srcOffY + srcH) / (float) texH;

    // Quad corners (no origin offset, no transform - draw_sprite_part ignores sprite origin)
    float x0 = x;
    float y0 = y;
    float x1 = x + (float) srcW * xscale;
    float y1 = y + (float) srcH * yscale;

    uint8_t r = BGR_R(color);
    uint8_t g = BGR_G(color);
    uint8_t b = BGR_B(color);
    uint8_t a = alpha * 255;

    SDL_Color color2 = { r, g, b, a };
    SDL_Vertex vertices[4];

    vertices[0] = sdlMakeVertex(sdl, x0, y1, color2, u0, v1); // top-left
    vertices[1] = sdlMakeVertex(sdl, x1, y1, color2, u1, v1); // top-right
    vertices[2] = sdlMakeVertex(sdl, x1, y0, color2, u1, v0); // bottom-right
    vertices[3] = sdlMakeVertex(sdl, x0, y0, color2, u0, v0); // bottom-left

    int indices[6] = { 0, 1, 3, 1, 2, 3 };

    SDL_SetRenderDrawBlendMode((SDL_Renderer*)sdl->renderer, SDL_BLENDMODE_BLEND);
    SDL_RenderGeometry((SDL_Renderer*)sdl->renderer, sdl->textures[tpag->texturePageId], vertices, 4, indices, 6);
}

static void sdlDrawRectangle(Renderer* renderer, float x1, float y1, float x2, float y2, uint32_t color, float alpha, bool outline) {
    alpha = SDL_clamp(alpha, 0, 1);
    SDL2Renderer* sdl = (SDL2Renderer*)renderer;

    if (outline) {
        sdl->base.vtable->drawLine(renderer, x1, y1, x1, y2, 1, color, alpha);
        sdl->base.vtable->drawLine(renderer, x1, y2, x2, y2, 1, color, alpha);
        sdl->base.vtable->drawLine(renderer, x2, y2, x2, y1, 1, color, alpha);
        sdl->base.vtable->drawLine(renderer, x1, y1, x2, y1, 1, color, alpha);
    }
    else {
        SDL_Vertex vertices[4];

        uint8_t r = BGR_R(color);
        uint8_t g = BGR_G(color);
        uint8_t b = BGR_B(color);
        uint8_t a = alpha * 255;

        SDL_Color color = {
            r,
            g,
            b,
            a
        };

        vertices[0] = sdlMakeVertex(sdl, x1, y2, color, 0, 0);
        vertices[1] = sdlMakeVertex(sdl, x2, y2, color, 0, 0);
        vertices[2] = sdlMakeVertex(sdl, x2, y1, color, 0, 0);
        vertices[3] = sdlMakeVertex(sdl, x1, y1, color, 0, 0);

        int indices[6] = { 0, 1, 3, 1, 2, 3 };

        SDL_SetRenderDrawBlendMode((SDL_Renderer*)sdl->renderer, SDL_BLENDMODE_BLEND);
        SDL_RenderGeometry((SDL_Renderer*)sdl->renderer, nullptr, vertices, 4, indices, 6);
    }
}

// ===[ Line Drawing ]===

static void sdlDrawLineColor(Renderer* renderer, float x1, float y1, float x2, float y2, float width, uint32_t color1, uint32_t color2, float alpha) {
    alpha = SDL_clamp(alpha, 0, 1);
    SDL2Renderer* sdl = (SDL2Renderer*)renderer;
    SDL_Vertex vertices[4];

    SDL_Color _color1 = {
        BGR_R(color1),
        BGR_G(color1),
        BGR_B(color1),
        alpha * 255
    };
    
    SDL_Color _color2 = {
        BGR_R(color2),
        BGR_G(color2),
        BGR_B(color2),
        alpha * 255
    };

    vertices[0] = sdlMakeVertex(sdl, x1, y2, _color1, 0, 0);
    vertices[1] = sdlMakeVertex(sdl, x2, y2, _color2, 0, 0);
    vertices[2] = sdlMakeVertex(sdl, x2, y1, _color2, 0, 0);
    vertices[3] = sdlMakeVertex(sdl, x1, y1, _color1, 0, 0);

    int indices[6] = { 0, 1, 3, 1, 2, 3 };

    SDL_SetRenderDrawBlendMode((SDL_Renderer*)sdl->renderer, SDL_BLENDMODE_BLEND);
    SDL_RenderGeometry((SDL_Renderer*)sdl->renderer, nullptr, vertices, 4, indices, 6);
}

static void sdlDrawLine(Renderer* renderer, float x1, float y1, float x2, float y2, float width, uint32_t color, float alpha) {
    sdlDrawLineColor(renderer, x1, y1, x2, y2, width, color, color, alpha);
}

static void sdlDrawTriangle(Renderer *renderer, float x1, float y1, float x2, float y2, float x3, float y3, MAYBE_UNUSED bool outline) {
    SDL2Renderer* sdl = (SDL2Renderer*) renderer;

    SDL_Vertex vertices[3];

    SDL_Color color = {
        BGR_R(renderer->drawColor),
        BGR_G(renderer->drawColor),
        BGR_B(renderer->drawColor),
        SDL_clamp(renderer->drawAlpha, 0, 1) * 255
    };

    vertices[0] = sdlMakeVertex(sdl, x1, y1, color, 0, 0);
    vertices[1] = sdlMakeVertex(sdl, x2, y2, color, 0, 0);
    vertices[2] = sdlMakeVertex(sdl, x3, y3, color, 0, 0);

    SDL_SetRenderDrawBlendMode((SDL_Renderer*)sdl->renderer, SDL_BLENDMODE_BLEND);
    SDL_RenderGeometry((SDL_Renderer*)sdl->renderer, nullptr, vertices, 3, nullptr, 0);
}

// ===[ Text Drawing ]===

// Resolved font state shared between glDrawText and glDrawTextColor
typedef struct {
    Font* font;
    TexturePageItem* fontTpag; // single TPAG for regular fonts (nullptr for sprite fonts)
    SDL_Texture* texId;
    int32_t texW, texH;
    Sprite* spriteFontSprite; // source sprite for sprite fonts (nullptr for regular fonts)
} GlFontState;

// Resolves font texture state
// Returns false if the font can't be drawn
static bool glResolveFontState(SDL2Renderer* gl, DataWin* dw, Font* font, GlFontState* state) {
    state->font = font;
    state->fontTpag = nullptr;
    state->texId = 0;
    state->texW = 0;
    state->texH = 0;
    state->spriteFontSprite = nullptr;

    if (!font->isSpriteFont) {
        int32_t fontTpagIndex = DataWin_resolveTPAG(dw, font->textureOffset);
        if (0 > fontTpagIndex) return false;

        state->fontTpag = &dw->tpag.items[fontTpagIndex];
        int16_t pageId = state->fontTpag->texturePageId;
        if (0 > pageId || (uint32_t) pageId >= gl->textureCount) return false;
        if (!ensureTextureLoaded(gl, (uint32_t) pageId)) return false;

        state->texId = gl->textures[pageId];
        state->texW = gl->textureWidths[pageId];
        state->texH = gl->textureHeights[pageId];
    } else if (font->spriteIndex >= 0 && dw->sprt.count > (uint32_t) font->spriteIndex) {
        state->spriteFontSprite = &dw->sprt.sprites[font->spriteIndex];
    }
    return true;
}

// Resolves UV coordinates, texture ID, and local position for a single glyph
// Returns false if the glyph can't be drawn
static bool glResolveGlyph(SDL2Renderer* gl, DataWin* dw, GlFontState* state, FontGlyph* glyph, float cursorX, float cursorY, SDL_Texture** outTexId, float* outU0, float* outV0, float* outU1, float* outV1, float* outLocalX0, float* outLocalY0) {
    Font* font = state->font;
    if (font->isSpriteFont && state->spriteFontSprite != nullptr) {
        Sprite* sprite = state->spriteFontSprite;
        int32_t glyphIndex = (int32_t) (glyph - font->glyphs);
        if (0 > glyphIndex ||  glyphIndex >= (int32_t) sprite->textureCount) return false;

        uint32_t tpagOffset = sprite->textureOffsets[glyphIndex];
        int32_t tpagIdx = DataWin_resolveTPAG(dw, tpagOffset);
        if (0 > tpagIdx) return false;

        TexturePageItem* glyphTpag = &dw->tpag.items[tpagIdx];
        int16_t pid = glyphTpag->texturePageId;
        if (0 > pid || (uint32_t) pid >= gl->textureCount) return false;
        if (!ensureTextureLoaded(gl, (uint32_t) pid)) return false;

        *outTexId = gl->textures[pid];
        int32_t tw = gl->textureWidths[pid];
        int32_t th = gl->textureHeights[pid];

        *outU0 = (float) glyphTpag->sourceX / (float) tw;
        *outV0 = (float) glyphTpag->sourceY / (float) th;
        *outU1 = (float) (glyphTpag->sourceX + glyphTpag->sourceWidth) / (float) tw;
        *outV1 = (float) (glyphTpag->sourceY + glyphTpag->sourceHeight) / (float) th;

        *outLocalX0 = cursorX + (float) glyph->offset;
        *outLocalY0 = cursorY + (float) ((int32_t) glyphTpag->targetY - sprite->originY);
    } else {
        *outTexId = state->texId;
        *outU0 = (float) (state->fontTpag->sourceX + glyph->sourceX) / (float) state->texW;
        *outV0 = (float) (state->fontTpag->sourceY + glyph->sourceY) / (float) state->texH;
        *outU1 = (float) (state->fontTpag->sourceX + glyph->sourceX + glyph->sourceWidth) / (float) state->texW;
        *outV1 = (float) (state->fontTpag->sourceY + glyph->sourceY + glyph->sourceHeight) / (float) state->texH;

        *outLocalX0 = cursorX + glyph->offset;
        *outLocalY0 = cursorY;
    }
    return true;
}

static void sdlDrawText(Renderer* renderer, const char* text, float x, float y, float xscale, float yscale, float angleDeg) {
    SDL2Renderer* sdl = (SDL2Renderer*)renderer;
    DataWin* dw = renderer->dataWin;

    int32_t fontIndex = renderer->drawFont;
    if (0 > fontIndex || dw->font.count <= (uint32_t) fontIndex) return;

    Font* font = &dw->font.fonts[fontIndex];

    GlFontState fontState;
    if (!glResolveFontState(sdl, dw, font, &fontState)) return;

    uint32_t color = renderer->drawColor;
    uint8_t r = BGR_R(color);
    uint8_t g = BGR_G(color);
    uint8_t b = BGR_B(color);
    uint8_t a = SDL_clamp(renderer->drawAlpha, 0, 1) * 255;

    SDL_Color color2 = { r, g, b, a };
    SDL_Vertex vertices[4];
    int indices[6] = { 0, 1, 3, 1, 2, 3 };

    int32_t textLen = (int32_t) strlen(text);

    // Count lines, treating \r\n and \n\r as single breaks
    int32_t lineCount = TextUtils_countLines(text, textLen);

    float lineStride = TextUtils_lineStride(font);

    // Vertical alignment offset
    float totalHeight = (float) lineCount * lineStride;
    float valignOffset = 0;
    if (renderer->drawValign == 1) valignOffset = -totalHeight / 2.0f;
    else if (renderer->drawValign == 2) valignOffset = -totalHeight;

    // Build transform matrix
    float angleRad = -angleDeg * ((float) M_PI / 180.0f);
    Matrix4f transform;
    Matrix4f_setTransform2D(&transform, x, y, xscale * font->scaleX, yscale * font->scaleY, angleRad);

    // Iterate through lines. HTML5 subtracts ascenderOffset from per-line y offset.
    float cursorY = valignOffset - (float) font->ascenderOffset;
    int32_t lineStart = 0;

    for (int32_t lineIdx = 0; lineCount > lineIdx; lineIdx++) {
        // Find end of current line
        int32_t lineEnd = lineStart;
        while (textLen > lineEnd && !TextUtils_isNewlineChar(text[lineEnd])) {
            lineEnd++;
        }
        int32_t lineLen = lineEnd - lineStart;

        // Horizontal alignment offset for this line
        float lineWidth = TextUtils_measureLineWidth(font, text + lineStart, lineLen);
        float halignOffset = 0;
        if (renderer->drawHalign == 1) halignOffset = -lineWidth / 2.0f;
        else if (renderer->drawHalign == 2) halignOffset = -lineWidth;

        float cursorX = halignOffset;

        // Render each glyph in the line
        int32_t pos = 0;
        while (lineLen > pos) {
            uint16_t ch = TextUtils_decodeUtf8(text + lineStart, lineLen, &pos);
            FontGlyph* glyph = TextUtils_findGlyph(font, ch);
            if (glyph == nullptr) continue;
            if (glyph->sourceWidth == 0 || glyph->sourceHeight == 0) {
                cursorX += glyph->shift;
                continue;
            }

            float u0, v0, u1, v1;
            float localX0, localY0;
            SDL_Texture* glyphTexId;

            if (!glResolveGlyph(sdl, dw, &fontState, glyph, cursorX, cursorY, &glyphTexId, &u0, &v0, &u1, &v1, &localX0, &localY0)) {
                cursorX += glyph->shift;
                continue;
            }

            float localX1 = localX0 + (float) glyph->sourceWidth;
            float localY1 = localY0 + (float) glyph->sourceHeight;

            // Transform corners
            float px0, py0, px1, py1, px2, py2, px3, py3;
            Matrix4f_transformPoint(&transform, localX0, localY0, &px0, &py0);
            Matrix4f_transformPoint(&transform, localX1, localY0, &px1, &py1);
            Matrix4f_transformPoint(&transform, localX1, localY1, &px2, &py2);
            Matrix4f_transformPoint(&transform, localX0, localY1, &px3, &py3);

            vertices[0] = sdlMakeVertex(sdl, px0, py0, color2, u0, v0); // top-left
            vertices[1] = sdlMakeVertex(sdl, px1, py1, color2, u1, v0); // top-right
            vertices[2] = sdlMakeVertex(sdl, px2, py2, color2, u1, v1); // bottom-right
            vertices[3] = sdlMakeVertex(sdl, px3, py3, color2, u0, v1); // bottom-left

            SDL_SetRenderDrawBlendMode((SDL_Renderer*)sdl->renderer, SDL_BLENDMODE_BLEND);
            SDL_RenderGeometry((SDL_Renderer*)sdl->renderer, glyphTexId, vertices, 4, indices, 6);

            // Advance cursor by glyph shift + kerning
            cursorX += glyph->shift;
            if (lineLen > pos) {
                int32_t savedPos = pos;
                uint16_t nextCh = TextUtils_decodeUtf8(text + lineStart, lineLen, &pos);
                pos = savedPos;
                cursorX += TextUtils_getKerningOffset(glyph, nextCh);
            }
        }

        cursorY += lineStride;
        // Skip past the newline, treating \r\n and \n\r as single breaks
        if (textLen > lineEnd) {
            lineStart = TextUtils_skipNewline(text, lineEnd, textLen);
        } else {
            lineStart = lineEnd;
        }
    }
}

static void sdlDrawTextColor(Renderer* renderer, const char* text, float x, float y, float xscale, float yscale, float ansdleDeg, int32_t _c1, int32_t _c2, int32_t _c3, int32_t _c4, float alpha) {
    fprintf(stderr, "sdlDrawTextColor");
}

// ===[ Dynamic Sprite Creation/Deletion ]===

// Sentinel base for fake TPAG offsets used by dynamic sprites
#define DYNAMIC_TPAG_OFFSET_BASE 0xD0000000u

// Finds a free dynamic TPAG slot (texturePageId == -1), or appends a new one.
static uint32_t findOrAllocTpagSlot(DataWin* dw, uint32_t originalTpagCount) {
    return -1;
}

static int32_t sdlCreateSpriteFromSurface(Renderer* renderer, int32_t x, int32_t y, int32_t w, int32_t h, bool removeback, bool smooth, int32_t xorig, int32_t yorig) {
    fprintf(stderr, "sdlCreateSpriteFromSurface");
    return -1;
}

static void sdlDeleteSprite(Renderer* renderer, int32_t spriteIndex) {
    fprintf(stderr, "sdlDeleteSprite");
}

// ===[ Vtable ]===

static RendererVtable sdlVtable = {
    .init = sdlInit,
    .destroy = sdlDestroy,
    .beginFrame = sdlBeginFrame,
    .endFrame = sdlEndFrame,
    .beginView = sdlBeginView,
    .endView = sdlEndView,
    .beginGUI = sdlBeginGUI,
    .endGUI = sdlEndGUI,
    .drawSprite = sdlDrawSprite,
    .drawSpritePart = sdlDrawSpritePart,
    .drawRectangle = sdlDrawRectangle,
    .drawLine = sdlDrawLine,
    .drawLineColor = sdlDrawLineColor,
    .drawTriangle = sdlDrawTriangle,
    .drawText = sdlDrawText,
    .drawTextColor = sdlDrawTextColor,
    .flush = SDL2RendererFlush,
    .createSpriteFromSurface = sdlCreateSpriteFromSurface,
    .deleteSprite = sdlDeleteSprite,
    .drawTile = nullptr,
};

// ===[ Public API ]===

Renderer* SDL2Renderer_create(void* window) {
    SDL2Renderer* sdl = safeCalloc(1, sizeof(SDL2Renderer));
    sdl->base.vtable = &sdlVtable;
    sdl->base.drawColor = 0xFFFFFF; // white (BGR)
    sdl->base.drawAlpha = 1.0f;
    sdl->base.drawFont = -1;
    sdl->base.drawHalign = 0;
    sdl->base.drawValign = 0;
    sdl->window = window;
    return (Renderer*) sdl;
}
