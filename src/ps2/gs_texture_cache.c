#include "gs_texture_cache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>

#include "stb_ds.h"
#include <gsTexture.h>

#include "utils.h"

// ===[ Constants ]===
#define GS_VRAM_SIZE (4 * 1024 * 1024)
#define BTX_HEADER_SIZE 64

// ===[ BTX Header ]===
// BTX v1: single texture (max 1024x1024)
// [0]   version (1)
// [1]   bpp (4 or 8)
// [2-3] original width (uint16 LE)
// [4-5] original height (uint16 LE)
// [6-7] padded width (uint16 LE, power-of-2)
// [8-9] padded height (uint16 LE, power-of-2)
// [10-63] reserved

// Build the tpagIndex -> file mapping by iterating all sprites and backgrounds.
static void buildFileMap(GsTextureCache* cache, DataWin* dataWin) {
    // Map sprite frames
    forEachIndexed(Sprite, sprite, si, dataWin->sprt.sprites, dataWin->sprt.count) {
        repeat(sprite->textureCount, fi) {
            int32_t tpagIndex = DataWin_resolveTPAG(dataWin, sprite->textureOffsets[fi]);
            if (0 > tpagIndex) continue;

            TpagFileMapping mapping = {
                .isBackground = false,
                .index = (int32_t) si,
                .frame = (int32_t) fi,
            };
            hmput(cache->fileMap, tpagIndex, mapping);
        }
    }

    // Map backgrounds
    forEachIndexed(Background, background, bi, dataWin->bgnd.backgrounds, dataWin->bgnd.count) {
        int32_t tpagIndex = DataWin_resolveTPAG(dataWin, background->textureOffset);
        if (0 > tpagIndex) continue;

        TpagFileMapping mapping = {
            .isBackground = true,
            .index = (int32_t) bi,
            .frame = 0,
        };
        hmput(cache->fileMap, tpagIndex, mapping);
    }

    // Map font textures
    forEachIndexed(Font, font, fi, dataWin->font.fonts, dataWin->font.count) {
        int32_t tpagIndex = DataWin_resolveTPAG(dataWin, font->textureOffset);
        if (0 > tpagIndex) continue;

        // Skip if already mapped (a font might share a TPAG with a sprite/bg)
        ptrdiff_t existing = hmgeti(cache->fileMap, tpagIndex);
        if (existing >= 0) continue;

        TpagFileMapping mapping = {
            .isBackground = false,
            .index = -1, // sentinel: font textures use font_{fi}.BTX
            .frame = (int32_t) fi,
        };
        hmput(cache->fileMap, tpagIndex, mapping);
    }

    printf("GsTextureCache: Built file map with %td entries\n", hmlen(cache->fileMap));
}

// Resolve a tpagIndex to a BTX file path. Returns false if no mapping exists.
static bool resolveFilePath(GsTextureCache* cache, int32_t tpagIndex, char* pathBuf, int32_t pathBufSize) {
    ptrdiff_t idx = hmgeti(cache->fileMap, tpagIndex);
    if (0 > idx) return false;

    TpagFileMapping* mapping = &cache->fileMap[idx].value;
    if (mapping->isBackground) {
        snprintf(pathBuf, pathBufSize, "mass:TEXTURES/bg_%ld.BTX", (long) mapping->index);
    } else if (0 > mapping->index) {
        // Font texture
        snprintf(pathBuf, pathBufSize, "mass:TEXTURES/font_%ld.BTX", (long) mapping->frame);
    } else {
        snprintf(pathBuf, pathBufSize, "mass:TEXTURES/%ld_%ld.BTX", (long) mapping->index, (long) mapping->frame);
    }
    return true;
}

GsTextureCache* GsTextureCache_create(GSGLOBAL* gsGlobal, uint32_t vramBase, DataWin* dataWin) {
    GsTextureCache* cache = calloc(1, sizeof(GsTextureCache));
    cache->gsGlobal = gsGlobal;
    cache->entries = nullptr;
    cache->indexMap = nullptr;
    cache->fileMap = nullptr;
    cache->vramBase = vramBase;
    cache->currentFrame = 0;

    uint32_t vramBudget = GS_VRAM_SIZE - vramBase;
    printf("GsTextureCache: vramBase=%lu, budget=%lu bytes (%.1f KB)\n",
           (unsigned long) vramBase, (unsigned long) vramBudget, (float) vramBudget / 1024.0f);

    buildFileMap(cache, dataWin);

    return cache;
}

void GsTextureCache_free(GsTextureCache* cache) {
    if (cache == nullptr) return;
    arrfree(cache->entries);
    hmfree(cache->indexMap);
    hmfree(cache->fileMap);
    free(cache);
}

void GsTextureCache_beginFrame(GsTextureCache* cache) {
    cache->currentFrame++;
}

// Evict all textures from VRAM and reset the bump allocator.
static void evictAll(GsTextureCache* cache) {
    printf("GsTextureCache: Evicting all textures (frame=%lu)\n", (unsigned long) cache->currentFrame);

    // Mark all entries as unloaded
    repeat(arrlen(cache->entries), i) {
        cache->entries[i].loaded = false;
        cache->entries[i].gsTexture.Vram = 0;
        cache->entries[i].gsTexture.VramClut = 0;
    }

    // Clear the index map so everything gets re-loaded on next access
    hmfree(cache->indexMap);
    cache->indexMap = nullptr;

    // Reset VRAM bump allocator to the base (preserves framebuffers + FONTM)
    cache->gsGlobal->CurrentPointer = cache->vramBase;
}

// Read a uint16 little-endian from a byte buffer
static uint16_t readU16LE(const uint8_t* data) {
    return (uint16_t) data[0] | ((uint16_t) data[1] << 8);
}

GSTEXTURE* GsTextureCache_get(GsTextureCache* cache, int32_t tpagIndex) {
    // Check if already cached
    ptrdiff_t mapIdx = hmgeti(cache->indexMap, tpagIndex);
    if (mapIdx >= 0) {
        int entryIdx = cache->indexMap[mapIdx].value;
        GsCachedTexture* entry = &cache->entries[entryIdx];
        if (entry->loaded) {
            return &entry->gsTexture;
        }
    }

    // Resolve tpagIndex to a BTX file path
    char path[256];
    if (!resolveFilePath(cache, tpagIndex, path, sizeof(path))) {
        return nullptr;
    }

    // Load BTX from disk
    FILE* f = fopen(path, "rb");
    if (f == nullptr) {
        // Only warn once per tpagIndex
        if (mapIdx < 0) {
            // Insert a sentinel so we don't warn again
            GsCachedTexture sentinel = {
                .tpagIndex = tpagIndex,
                .loaded = false,
                .width = 0,
                .height = 0,
            };
            memset(&sentinel.gsTexture, 0, sizeof(GSTEXTURE));
            int idx = (int) arrlen(cache->entries);
            arrput(cache->entries, sentinel);
            hmput(cache->indexMap, tpagIndex, idx);
        }
        return nullptr;
    }

    // Read header
    uint8_t header[BTX_HEADER_SIZE];
    if (fread(header, 1, BTX_HEADER_SIZE, f) != BTX_HEADER_SIZE) {
        fprintf(stderr, "GsTextureCache: Failed to read BTX header from %s\n", path);
        fclose(f);
        return nullptr;
    }

    uint8_t version = header[0];
    uint8_t bpp = header[1];

    if (version != 1) {
        fprintf(stderr, "GsTextureCache: Unsupported BTX version %d in %s (only v1 supported)\n", version, path);
        fclose(f);
        return nullptr;
    }

    if (bpp != 4 && bpp != 8) {
        fprintf(stderr, "GsTextureCache: Unsupported BPP %d in %s\n", bpp, path);
        fclose(f);
        return nullptr;
    }

    // uint16_t originalW = readU16LE(&header[2]);
    // uint16_t originalH = readU16LE(&header[4]);
    uint16_t paddedW = readU16LE(&header[6]);
    uint16_t paddedH = readU16LE(&header[8]);

    // Determine palette and pixel data sizes
    int32_t paletteColors = (bpp == 8) ? 256 : 16;
    uint32_t paletteBytes = (uint32_t) paletteColors * 4; // ABGR, 4 bytes per color
    uint32_t pixelBytes = (bpp == 8) ? ((uint32_t) paddedW * paddedH) : ((uint32_t) paddedW * paddedH / 2);

    // GS PSM for the texture
    uint8_t psm = (bpp == 8) ? GS_PSM_T8 : GS_PSM_T4;

    // CLUT dimensions for gsKit upload
    // T8: 16x16 in CT32, T4: 8x2 in CT32
    int32_t clutW = (bpp == 8) ? 16 : 8;
    int32_t clutH = (bpp == 8) ? 16 : 2;

    // Compute VRAM sizes
    uint32_t texVramSize = gsKit_texture_size(paddedW, paddedH, psm);
    uint32_t clutVramSize = gsKit_texture_size(clutW, clutH, GS_PSM_CT32);
    uint32_t totalVramSize = texVramSize + clutVramSize;

    // Check if it fits in the total VRAM budget
    uint32_t vramBudget = GS_VRAM_SIZE - cache->vramBase;
    if (totalVramSize > vramBudget) {
        fprintf(stderr, "GsTextureCache: Texture %s (%dx%d, %lu bytes) exceeds VRAM budget (%lu bytes), skipping\n", path, paddedW, paddedH, (unsigned long) totalVramSize, (unsigned long) vramBudget);
        fclose(f);
        return nullptr;
    }

    // Read palette into 128-byte aligned buffer (needed for DMA)
    u32* clutData = (u32*) memalign(128, paletteBytes);
    if (fread(clutData, 1, paletteBytes, f) != paletteBytes) {
        fprintf(stderr, "GsTextureCache: Failed to read palette from %s\n", path);
        free(clutData);
        fclose(f);
        return nullptr;
    }

    // Read pixel data into 128-byte aligned buffer
    uint32_t pixelAllocSize = (pixelBytes + 127) & ~127u; // round up to 128 for alignment
    uint8_t* pixelData = (uint8_t*) memalign(128, pixelAllocSize);
    if (fread(pixelData, 1, pixelBytes, f) != pixelBytes) {
        fprintf(stderr, "GsTextureCache: Failed to read pixel data from %s\n", path);
        free(clutData);
        free(pixelData);
        fclose(f);
        return nullptr;
    }

    fclose(f);

    // Allocate VRAM for texture
    uint32_t texVram = gsKit_vram_alloc(cache->gsGlobal, texVramSize, GSKIT_ALLOC_USERBUFFER);
    if (texVram == GSKIT_ALLOC_ERROR) {
        // VRAM full, evict all and retry
        evictAll(cache);
        texVram = gsKit_vram_alloc(cache->gsGlobal, texVramSize, GSKIT_ALLOC_USERBUFFER);
        if (texVram == GSKIT_ALLOC_ERROR) {
            fprintf(stderr, "GsTextureCache: VRAM alloc still failed after eviction for %s, skipping\n", path);
            free(clutData);
            free(pixelData);
            return nullptr;
        }
    }

    // Allocate VRAM for CLUT
    uint32_t clutVram = gsKit_vram_alloc(cache->gsGlobal, clutVramSize, GSKIT_ALLOC_USERBUFFER);
    if (clutVram == GSKIT_ALLOC_ERROR) {
        // VRAM full, evict all and retry (also need to re-alloc texture)
        evictAll(cache);
        texVram = gsKit_vram_alloc(cache->gsGlobal, texVramSize, GSKIT_ALLOC_USERBUFFER);
        clutVram = gsKit_vram_alloc(cache->gsGlobal, clutVramSize, GSKIT_ALLOC_USERBUFFER);
        if (texVram == GSKIT_ALLOC_ERROR || clutVram == GSKIT_ALLOC_ERROR) {
            fprintf(stderr, "GsTextureCache: VRAM alloc still failed after eviction for %s CLUT, skipping\n", path);
            free(clutData);
            free(pixelData);
            return nullptr;
        }
    }

    // Set up GSTEXTURE
    GSTEXTURE tex;
    memset(&tex, 0, sizeof(GSTEXTURE));
    tex.Width = paddedW;
    tex.Height = paddedH;
    tex.PSM = psm;
    tex.ClutPSM = GS_PSM_CT32;
    tex.Mem = (u32*) pixelData;
    tex.Clut = clutData;
    tex.Vram = texVram;
    tex.VramClut = clutVram;
    tex.Filter = GS_FILTER_NEAREST;

    // Upload texture + CLUT to VRAM
    gsKit_texture_upload(cache->gsGlobal, &tex);

    // Free EE-side buffers after upload (data is now in VRAM)
    free(pixelData);
    free(clutData);
    tex.Mem = nullptr;
    tex.Clut = nullptr;

    // Store in cache
    GsCachedTexture cached = {
        .tpagIndex = tpagIndex,
        .gsTexture = tex,
        .vramSize = totalVramSize,
        .loaded = true,
        .width = paddedW,
        .height = paddedH,
    };

    int entryIdx = (int) arrlen(cache->entries);
    arrput(cache->entries, cached);
    hmput(cache->indexMap, tpagIndex, entryIdx);

    printf("GsTextureCache: Loaded %s (%dx%d, %dbpp, %lu VRAM bytes)\n", path, paddedW, paddedH, bpp, (unsigned long) totalVramSize);

    return &cache->entries[entryIdx].gsTexture;
}
