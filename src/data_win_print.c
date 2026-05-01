#include "data_win.h"

#include <stdio.h>
#include <string.h>

#include "utils.h"

void DataWin_printDebugSummary(DataWin* dataWin) {
    fprintf(stderr, "===== data.win Summary =====\n\n");

    // GEN8
    Gen8* g = &dataWin->gen8;
    fprintf(stderr, "-- GEN8 (General Info) --\n");
    fprintf(stderr, "  Game Name:        %s\n", g->name ? g->name : "(null)");
    fprintf(stderr, "  Display Name:     %s\n", g->displayName ? g->displayName : "(null)");
    fprintf(stderr, "  File Name:        %s\n", g->fileName ? g->fileName : "(null)");
    fprintf(stderr, "  Config:           %s\n", g->config ? g->config : "(null)");
    fprintf(stderr, "  Bytecode Version: %u\n", g->bytecodeVersion);
    fprintf(stderr, "  Game ID:          %u\n", g->gameID);
    fprintf(stderr, "  Version:          %u.%u.%u.%u\n", g->major, g->minor, g->release, g->build);
    fprintf(stderr, "  Window Size:      %ux%u\n", g->defaultWindowWidth, g->defaultWindowHeight);
    fprintf(stderr, "  Steam App ID:     %d\n", g->steamAppID);
    fprintf(stderr, "  Room Order:       %u rooms\n", g->roomOrderCount);
    fprintf(stderr, "\n");

    // OPTN
    fprintf(stderr, "-- OPTN (Options) --\n");
    fprintf(stderr, "  Constants:        %u\n", dataWin->optn.constantCount);
    if (dataWin->optn.constantCount > 0) {
        uint32_t show = dataWin->optn.constantCount < 3 ? dataWin->optn.constantCount : 3;
        forEachIndexed(OptnConstant, constant, idx, dataWin->optn.constants, show) {
            fprintf(stderr, "    [%u] %s = %s\n", idx, constant->name ? constant->name : "?", constant->value ? constant->value : "?");
        }
        if (dataWin->optn.constantCount > 3) fprintf(stderr, "    ... and %u more\n", dataWin->optn.constantCount - 3);
    }
    fprintf(stderr, "\n");

    // LANG
    fprintf(stderr, "-- LANG (Languages) --\n");
    fprintf(stderr, "  Languages:        %u\n", dataWin->lang.languageCount);
    fprintf(stderr, "  Entries:          %u\n", dataWin->lang.entryCount);
    fprintf(stderr, "\n");

    // EXTN
    fprintf(stderr, "-- EXTN (Extensions) --\n");
    fprintf(stderr, "  Extensions:       %u\n", dataWin->extn.count);
    forEachIndexed(Extension, ext, idx, dataWin->extn.extensions, dataWin->extn.count) {
        fprintf(stderr, "    [%u] %s (%u files)\n", idx, ext->name ? ext->name : "?", ext->fileCount);
    }
    fprintf(stderr, "\n");

    // SOND
    fprintf(stderr, "-- SOND (Sounds) --\n");
    fprintf(stderr, "  Sounds:           %u\n", dataWin->sond.count);
    if (dataWin->sond.count > 0) {
        uint32_t show = dataWin->sond.count < 3 ? dataWin->sond.count : 3;
        forEachIndexed(Sound, snd, idx, dataWin->sond.sounds, show) {
            fprintf(stderr, "    [%u] %s (%s)\n", idx, snd->name ? snd->name : "?", snd->type ? snd->type : "?");
        }
        if (dataWin->sond.count > 3) fprintf(stderr, "    ... and %u more\n", dataWin->sond.count - 3);
    }
    fprintf(stderr, "\n");

    // AGRP
    fprintf(stderr, "-- AGRP (Audio Groups) --\n");
    fprintf(stderr, "  Audio Groups:     %u\n", dataWin->agrp.count);
    forEachIndexed(AudioGroup, ag, idx, dataWin->agrp.audioGroups, dataWin->agrp.count) {
        fprintf(stderr, "    [%u] %s\n", idx, ag->name ? ag->name : "?");
    }
    fprintf(stderr, "\n");

    // SPRT
    fprintf(stderr, "-- SPRT (Sprites) --\n");
    fprintf(stderr, "  Sprites:          %u\n", dataWin->sprt.count);
    if (dataWin->sprt.count > 0) {
        uint32_t show = dataWin->sprt.count < 3 ? dataWin->sprt.count : 3;
        forEachIndexed(Sprite, spr, idx, dataWin->sprt.sprites, show) {
            fprintf(stderr, "    [%u] %s (%ux%u, %u frames)\n", idx, spr->name ? spr->name : "?", spr->width, spr->height, spr->textureCount);
        }
        if (dataWin->sprt.count > 3) fprintf(stderr, "    ... and %u more\n", dataWin->sprt.count - 3);
    }
    fprintf(stderr, "\n");

    // BGND
    fprintf(stderr, "-- BGND (Backgrounds) --\n");
    fprintf(stderr, "  Backgrounds:      %u\n", dataWin->bgnd.count);
    if (dataWin->bgnd.count > 0) {
        uint32_t show = dataWin->bgnd.count < 3 ? dataWin->bgnd.count : 3;
        forEachIndexed(Background, bg, idx, dataWin->bgnd.backgrounds, show) {
            fprintf(stderr, "    [%u] %s\n", idx, bg->name ? bg->name : "?");
        }
        if (dataWin->bgnd.count > 3) fprintf(stderr, "    ... and %u more\n", dataWin->bgnd.count - 3);
    }
    fprintf(stderr, "\n");

    // PATH
    fprintf(stderr, "-- PATH (Paths) --\n");
    fprintf(stderr, "  Paths:            %u\n", dataWin->path.count);
    fprintf(stderr, "\n");

    // SCPT
    fprintf(stderr, "-- SCPT (Scripts) --\n");
    fprintf(stderr, "  Scripts:          %u\n", dataWin->scpt.count);
    if (dataWin->scpt.count > 0) {
        uint32_t show = dataWin->scpt.count < 3 ? dataWin->scpt.count : 3;
        forEachIndexed(Script, scr, idx, dataWin->scpt.scripts, show) {
            fprintf(stderr, "    [%u] %s -> code[%d]\n", idx, scr->name ? scr->name : "?", scr->codeId);
        }
        if (dataWin->scpt.count > 3) fprintf(stderr, "    ... and %u more\n", dataWin->scpt.count - 3);
    }
    fprintf(stderr, "\n");

    // GLOB
    fprintf(stderr, "-- GLOB (Global Init Scripts) --\n");
    fprintf(stderr, "  Init Scripts:     %u\n", dataWin->glob.count);
    fprintf(stderr, "\n");

    // SHDR
    fprintf(stderr, "-- SHDR (Shaders) --\n");
    fprintf(stderr, "  Shaders:          %u\n", dataWin->shdr.count);
    forEachIndexed(Shader, shdr, idx, dataWin->shdr.shaders, dataWin->shdr.count) {
        fprintf(stderr, "    [%u] %s (version %d)\n", idx, shdr->name ? shdr->name : "?", shdr->version);
    }
    fprintf(stderr, "\n");

    // FONT
    fprintf(stderr, "-- FONT (Fonts) --\n");
    fprintf(stderr, "  Fonts:            %u\n", dataWin->font.count);
    forEachIndexed(Font, fnt, idx, dataWin->font.fonts, dataWin->font.count) {
        fprintf(stderr, "    [%u] %s (%s, em=%u, %u glyphs)\n", idx, fnt->name ? fnt->name : "?", fnt->displayName ? fnt->displayName : "?", fnt->emSize, fnt->glyphCount);
    }
    fprintf(stderr, "\n");

    // TMLN
    fprintf(stderr, "-- TMLN (Timelines) --\n");
    fprintf(stderr, "  Timelines:        %u\n", dataWin->tmln.count);
    fprintf(stderr, "\n");

    // OBJT
    fprintf(stderr, "-- OBJT (Game Objects) --\n");
    fprintf(stderr, "  Objects:          %u\n", dataWin->objt.count);
    if (dataWin->objt.count > 0) {
        uint32_t show = dataWin->objt.count < 3 ? dataWin->objt.count : 3;
        forEachIndexed(GameObject, obj, idx, dataWin->objt.objects, show) {
            uint32_t totalEvents = 0;
            repeat(OBJT_EVENT_TYPE_COUNT, e) {
                totalEvents += obj->eventLists[e].eventCount;
            }
            fprintf(stderr, "    [%u] %s (sprite=%d, depth=%d, %u events)\n", idx, obj->name ? obj->name : "?", obj->spriteId, obj->depth, totalEvents);
        }
        if (dataWin->objt.count > 3) fprintf(stderr, "    ... and %u more\n", dataWin->objt.count - 3);
    }
    fprintf(stderr, "\n");

    // ROOM
    fprintf(stderr, "-- ROOM (Rooms) --\n");
    fprintf(stderr, "  Rooms:            %u\n", dataWin->room.count);
    if (dataWin->room.count > 0) {
        uint32_t show = dataWin->room.count < 3 ? dataWin->room.count : 3;
        forEachIndexed(Room, rm, idx, dataWin->room.rooms, show) {
            if (rm->payloadLoaded) {
                fprintf(stderr, "    [%u] %s (%ux%u, %u objects, %u tiles)\n", idx, rm->name ? rm->name : "?", rm->width, rm->height, rm->gameObjectCount, rm->tileCount);
            } else {
                // Lazy room with payload not yet loaded: gameObjectCount/tileCount would be 0 and misleading.
                fprintf(stderr, "    [%u] %s (%ux%u, payload not loaded)\n", idx, rm->name ? rm->name : "?", rm->width, rm->height);
            }
        }
        if (dataWin->room.count > 3) fprintf(stderr, "    ... and %u more\n", dataWin->room.count - 3);
    }
    fprintf(stderr, "\n");

    // TPAG
    fprintf(stderr, "-- TPAG (Texture Page Items) --\n");
    fprintf(stderr, "  Items:            %u\n", dataWin->tpag.count);
    fprintf(stderr, "\n");

    // CODE
    fprintf(stderr, "-- CODE (Code Entries) --\n");
    fprintf(stderr, "  Entries:          %u\n", dataWin->code.count);
    if (dataWin->code.count > 0) {
        uint32_t show = dataWin->code.count < 3 ? dataWin->code.count : 3;
        forEachIndexed(CodeEntry, entry, idx, dataWin->code.entries, show) {
            fprintf(stderr, "    [%u] %s (%u bytes, %u locals, %u args)\n", idx, entry->name ? entry->name : "?", entry->length, entry->localsCount, entry->argumentsCount);
        }
        if (dataWin->code.count > 3) fprintf(stderr, "    ... and %u more\n", dataWin->code.count - 3);
    }
    fprintf(stderr, "\n");

    // VARI
    fprintf(stderr, "-- VARI (Variables) --\n");
    fprintf(stderr, "  Variables:        %u\n", dataWin->vari.variableCount);
    fprintf(stderr, "  Max Locals:       %u\n", dataWin->vari.maxLocalVarCount);
    if (dataWin->vari.variableCount > 0) {
        uint32_t show = dataWin->vari.variableCount < 3 ? dataWin->vari.variableCount : 3;
        forEachIndexed(Variable, var, idx, dataWin->vari.variables, show) {
            fprintf(stderr, "    [%u] %s (type=%d, id=%d, %u refs)\n", idx, var->name ? var->name : "?", var->instanceType, var->varID, var->occurrences);
        }
        if (dataWin->vari.variableCount > 3) fprintf(stderr, "    ... and %u more\n", dataWin->vari.variableCount - 3);
    }
    fprintf(stderr, "\n");

    // FUNC
    fprintf(stderr, "-- FUNC (Functions) --\n");
    fprintf(stderr, "  Functions:        %u\n", dataWin->func.functionCount);
    fprintf(stderr, "  Code Locals:      %u\n", dataWin->func.codeLocalsCount);
    if (dataWin->func.functionCount > 0) {
        uint32_t show = dataWin->func.functionCount < 3 ? dataWin->func.functionCount : 3;
        forEachIndexed(Function, fn, idx, dataWin->func.functions, show) {
            fprintf(stderr, "    [%u] %s (%u refs)\n", idx, fn->name ? fn->name : "?", fn->occurrences);
        }
        if (dataWin->func.functionCount > 3) fprintf(stderr, "    ... and %u more\n", dataWin->func.functionCount - 3);
    }
    fprintf(stderr, "\n");

    // STRG
    fprintf(stderr, "-- STRG (Strings) --\n");
    fprintf(stderr, "  Strings:          %u\n", dataWin->strg.count);
    if (dataWin->strg.count > 0) {
        uint32_t show = dataWin->strg.count < 5 ? dataWin->strg.count : 5;
        repeat(show, i) {
            const char* str = dataWin->strg.strings[i];
            // Truncate long strings for display
            if (str) {
                size_t len = strlen(str);
                if (len > 60) {
                    fprintf(stderr, "    [%u] \"%.60s...\" (%zu chars)\n", i, str, len);
                } else {
                    fprintf(stderr, "    [%u] \"%s\"\n", i, str);
                }
            } else {
                fprintf(stderr, "    [%u] (null)\n", i);
            }
        }
        if (dataWin->strg.count > 5) fprintf(stderr, "    ... and %u more\n", dataWin->strg.count - 5);
    }
    fprintf(stderr, "\n");

    // TXTR
    fprintf(stderr, "-- TXTR (Textures) --\n");
    fprintf(stderr, "  Textures:         %u\n", dataWin->txtr.count);
    if (dataWin->txtr.count > 0) {
        forEachIndexed(Texture, tex, idx, dataWin->txtr.textures, dataWin->txtr.count) {
            fprintf(stderr, "    [%u] offset=0x%08X size=%u bytes\n", idx, tex->blobOffset, tex->blobSize);
        }
    }
    fprintf(stderr, "\n");

    // AUDO
    fprintf(stderr, "-- AUDO (Audio) --\n");
    fprintf(stderr, "  Audio Entries:    %u\n", dataWin->audo.count);
    if (dataWin->audo.count > 0) {
        uint32_t show = dataWin->audo.count < 3 ? dataWin->audo.count : 3;
        forEachIndexed(AudioEntry, ae, idx, dataWin->audo.entries, show) {
            fprintf(stderr, "    [%u] offset=0x%08X size=%u bytes\n", idx, ae->dataOffset, ae->dataSize);
        }
        if (dataWin->audo.count > 3) fprintf(stderr, "    ... and %u more\n", dataWin->audo.count - 3);
    }
    fprintf(stderr, "\n");

    fprintf(stderr, "-- Room Instances --\n");
    forEach(Room, room, dataWin->room.rooms, dataWin->room.count) {
        fprintf(stderr, "Room %s\n", room->name);

        if (!room->payloadLoaded) {
            fprintf(stderr, "  (payload not loaded)\n");
            continue;
        }

        forEachIndexed(RoomGameObject, roomGameObject, idx, room->gameObjects, room->gameObjectCount) {
            int32_t objectDefinitionId = roomGameObject->objectDefinition;
            GameObject* objectDefinition = &dataWin->objt.objects[objectDefinitionId];
            fprintf(stderr, "  Object %d (%s, x=%d, y=%d)\n", objectDefinitionId, objectDefinition->name, roomGameObject->x, roomGameObject->y);
        }
    }

    // Overall summary
    fprintf(stderr, "===== DataWin parse complete =====\n");
}
