#include "vm_builtins.h"
#include "instance.h"
#include "runner.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <time.h>

#include "stb_ds.h"
#include "text_utils.h"
#include "collision.h"

#define MAX_VIEWS 8
#define MAX_BACKGROUNDS 8

// ===[ BUILTIN FUNCTION REGISTRY ]===
typedef struct {
    char* key;
    BuiltinFunc value;
} BuiltinEntry;

static bool initialized = false;
static BuiltinEntry* builtinMap = nullptr;

static void registerBuiltin(const char* name, BuiltinFunc func) {
    requireMessage(shgeti(builtinMap, name) == -1, "Trying to register an already registered builtin function!");
    shput(builtinMap, (char*) name, func);
}

BuiltinFunc VMBuiltins_find(const char* name) {
    ptrdiff_t idx = shgeti(builtinMap, (char*) name);
    if (0 > idx) return nullptr;
    return builtinMap[idx].value;
}

// ===[ STUB LOGGING ]===

static void logStubbedFunction(VMContext* ctx, const char* funcName) {
    const char* callerName = VM_getCallerName(ctx);
    char* dedupKey = VM_createDedupKey(callerName, funcName);

    if (0 > shgeti(ctx->loggedStubbedFuncs, dedupKey)) {
        // shput stores the key pointer, so don't free it when inserting
        shput(ctx->loggedStubbedFuncs, dedupKey, true);
        fprintf(stderr, "VM: [%s] Stubbed function \"%s\"!\n", callerName, funcName);
    } else {
        free(dedupKey);
    }
}

// ===[ WORKING DIRECTORY ]===
static char* gameWorkingDirectory = nullptr;

void VMBuiltins_setWorkingDirectory(const char* gameFilePath) {
    if (gameWorkingDirectory != nullptr) free(gameWorkingDirectory);
    // Extract directory from the game file path
    const char* lastSlash = strrchr(gameFilePath, '/');
    if (lastSlash == nullptr) lastSlash = strrchr(gameFilePath, '\\');
    if (lastSlash != nullptr) {
        size_t dirLen = (size_t)(lastSlash - gameFilePath + 1); // include trailing slash
        gameWorkingDirectory = malloc(dirLen + 1);
        memcpy(gameWorkingDirectory, gameFilePath, dirLen);
        gameWorkingDirectory[dirLen] = '\0';
    } else {
        gameWorkingDirectory = strdup("./");
    }
}

// ===[ DS_MAP SYSTEM ]===

typedef struct {
    char* key;
    RValue value;
} DsMapEntry;

static DsMapEntry** dsMapPool = nullptr; // stb_ds array of shash maps

static int32_t dsMapCreate(void) {
    DsMapEntry* newMap = nullptr;
    int32_t id = (int32_t) arrlen(dsMapPool);
    arrput(dsMapPool, newMap);
    return id;
}

static DsMapEntry** dsMapGet(int32_t id) {
    if (id < 0 || (int32_t) arrlen(dsMapPool) <= id) return nullptr;
    return &dsMapPool[id];
}

// ===[ BUILT-IN VARIABLE GET/SET ]===

/**
 * Gets the argument number from the name
 *
 * If it returns -1, then the name is not an argument variable
 *
 * @param name The name
 * @return The argument number, -1 if it is not an argument variable
 */
static int extractArgumentNumber(const char* name) {
    if (strncmp(name, "argument", 8) == 0) {
        char* end;
        long argNumber = strtol(name + 8, &end, 10);
        if (end == name + 8 || *end != '\0' || 0 > argNumber || argNumber > 15) return -1;
        return (int) argNumber;
    }
    return -1;
}

static bool isValidAlarmIndex(int alarmIndex) {
    return alarmIndex >= 0 && GML_ALARM_COUNT > alarmIndex;
}

RValue VMBuiltins_getVariable(VMContext* ctx, const char* name, int32_t arrayIndex) {
    Instance* inst = (Instance*) ctx->currentInstance;
    Runner* runner = (Runner*) ctx->runner;

    // OS constants
    if (strcmp(name, "os_type") == 0) return RValue_makeReal(4.0); // os_linux
    if (strcmp(name, "os_windows") == 0) return RValue_makeReal(0.0);
    if (strcmp(name, "os_ps4") == 0) return RValue_makeReal(6.0);
    if (strcmp(name, "os_psvita") == 0) return RValue_makeReal(12.0);
    if (strcmp(name, "os_3ds") == 0) return RValue_makeReal(14.0);
    if (strcmp(name, "os_switch_") == 0) return RValue_makeReal(19.0);

    // Per-instance properties
    if (inst != nullptr) {
        if (strcmp(name, "image_speed") == 0) return RValue_makeReal(inst->imageSpeed);
        if (strcmp(name, "image_index") == 0) return RValue_makeReal(inst->imageIndex);
        if (strcmp(name, "image_xscale") == 0) return RValue_makeReal(inst->imageXscale);
        if (strcmp(name, "image_yscale") == 0) return RValue_makeReal(inst->imageYscale);
        if (strcmp(name, "image_angle") == 0) return RValue_makeReal(inst->imageAngle);
        if (strcmp(name, "image_alpha") == 0) return RValue_makeReal(inst->imageAlpha);
        if (strcmp(name, "image_blend") == 0) return RValue_makeReal((double) inst->imageBlend);
        if (strcmp(name, "image_number") == 0) {
            if (inst->spriteIndex >= 0) {
                Sprite* sprite = &ctx->runner->dataWin->sprt.sprites[inst->spriteIndex];
                return RValue_makeReal((double) sprite->textureCount);
            }
            return RValue_makeReal(0.0);
        }
        if (strcmp(name, "sprite_index") == 0) return RValue_makeReal((double) inst->spriteIndex);
        if (strcmp(name, "sprite_width") == 0) {
            if (inst->spriteIndex >= 0 && runner != nullptr && runner->dataWin->sprt.count > (uint32_t) inst->spriteIndex) {
                return RValue_makeReal((double) runner->dataWin->sprt.sprites[inst->spriteIndex].width * fabs(inst->imageXscale));
            }
            return RValue_makeReal(0.0);
        }
        if (strcmp(name, "sprite_height") == 0) {
            if (inst->spriteIndex >= 0 && runner != nullptr && runner->dataWin->sprt.count > (uint32_t) inst->spriteIndex) {
                return RValue_makeReal((double) runner->dataWin->sprt.sprites[inst->spriteIndex].height * fabs(inst->imageYscale));
            }
            return RValue_makeReal(0.0);
        }
        if (strcmp(name, "bbox_left") == 0 || strcmp(name, "bbox_right") == 0 || strcmp(name, "bbox_top") == 0 || strcmp(name, "bbox_bottom") == 0) {
            if (runner != nullptr) {
                InstanceBBox bbox = Collision_computeBBox(runner->dataWin, inst);
                if (bbox.valid) {
                    if (strcmp(name, "bbox_left") == 0) return RValue_makeReal(bbox.left);
                    if (strcmp(name, "bbox_right") == 0) return RValue_makeReal(bbox.right);
                    if (strcmp(name, "bbox_top") == 0) return RValue_makeReal(bbox.top);
                    return RValue_makeReal(bbox.bottom);
                }
            }
            if (strcmp(name, "bbox_left") == 0 || strcmp(name, "bbox_right") == 0) return RValue_makeReal(inst->x);
            return RValue_makeReal(inst->y);
        }
        if (strcmp(name, "visible") == 0) return RValue_makeBool(inst->visible);
        if (strcmp(name, "depth") == 0) return RValue_makeReal((double) inst->depth);
        if (strcmp(name, "x") == 0) return RValue_makeReal(inst->x);
        if (strcmp(name, "y") == 0) return RValue_makeReal(inst->y);
        if (strcmp(name, "xprevious") == 0) return RValue_makeReal(inst->xprevious);
        if (strcmp(name, "yprevious") == 0) return RValue_makeReal(inst->yprevious);
        if (strcmp(name, "xstart") == 0) return RValue_makeReal(inst->xstart);
        if (strcmp(name, "ystart") == 0) return RValue_makeReal(inst->ystart);
        if (strcmp(name, "mask_index") == 0) return RValue_makeReal((double) inst->maskIndex);
        if (strcmp(name, "id") == 0) return RValue_makeReal((double) inst->instanceId);
        if (strcmp(name, "object_index") == 0) return RValue_makeReal((double) inst->objectIndex);
        if (strcmp(name, "persistent") == 0) return RValue_makeBool(inst->persistent);
        if (strcmp(name, "solid") == 0) return RValue_makeBool(inst->solid);
        if (strcmp(name, "speed") == 0) return RValue_makeReal(inst->speed);
        if (strcmp(name, "direction") == 0) return RValue_makeReal(inst->direction);
        if (strcmp(name, "hspeed") == 0) return RValue_makeReal(inst->hspeed);
        if (strcmp(name, "vspeed") == 0) return RValue_makeReal(inst->vspeed);
        if (strcmp(name, "friction") == 0) return RValue_makeReal(inst->friction);
        if (strcmp(name, "gravity") == 0) return RValue_makeReal(inst->gravity);
        if (strcmp(name, "gravity_direction") == 0) return RValue_makeReal(inst->gravityDirection);
        if (strcmp(name, "alarm") == 0) {
            if (isValidAlarmIndex(arrayIndex)) {
                return RValue_makeReal((double) inst->alarm[arrayIndex]);
            }
            return RValue_makeReal(-1.0);
        }

        // Path instance variables
        if (strcmp(name, "path_index") == 0) return RValue_makeReal((double) inst->pathIndex);
        if (strcmp(name, "path_position") == 0) return RValue_makeReal(inst->pathPosition);
        if (strcmp(name, "path_positionprevious") == 0) return RValue_makeReal(inst->pathPositionPrevious);
        if (strcmp(name, "path_speed") == 0) return RValue_makeReal(inst->pathSpeed);
        if (strcmp(name, "path_scale") == 0) return RValue_makeReal(inst->pathScale);
        if (strcmp(name, "path_orientation") == 0) return RValue_makeReal(inst->pathOrientation);
        if (strcmp(name, "path_endaction") == 0) return RValue_makeReal((double) inst->pathEndAction);
    }

    // Room properties
    if (runner != nullptr) {
        if (strcmp(name, "room") == 0) return RValue_makeReal((double) runner->currentRoomIndex);
        if (strcmp(name, "room_speed") == 0) return RValue_makeReal((double) runner->currentRoom->speed);
        if (strcmp(name, "room_width") == 0) return RValue_makeReal((double) runner->currentRoom->width);
        if (strcmp(name, "room_height") == 0) return RValue_makeReal((double) runner->currentRoom->height);
        if (strcmp(name, "room_persistent") == 0) return RValue_makeBool(runner->currentRoom->persistent);
        if (strcmp(name, "view_current") == 0) return RValue_makeReal(0.0);
        if (strcmp(name, "view_xview") == 0) {
            if (arrayIndex >= 0 && MAX_VIEWS > arrayIndex) {
                return RValue_makeReal((double) runner->currentRoom->views[arrayIndex].viewX);
            }
            return RValue_makeReal(0.0);
        }
        if (strcmp(name, "view_yview") == 0) {
            if (arrayIndex >= 0 && MAX_VIEWS > arrayIndex) {
                return RValue_makeReal((double) runner->currentRoom->views[arrayIndex].viewY);
            }
            return RValue_makeReal(0.0);
        }
        if (strcmp(name, "view_wview") == 0) {
            if (arrayIndex >= 0 && MAX_VIEWS > arrayIndex) {
                return RValue_makeReal((double) runner->currentRoom->views[arrayIndex].viewWidth);
            }
            return RValue_makeReal(0.0);
        }
        if (strcmp(name, "view_hview") == 0) {
            if (arrayIndex >= 0 && MAX_VIEWS > arrayIndex) {
                return RValue_makeReal((double) runner->currentRoom->views[arrayIndex].viewHeight);
            }
            return RValue_makeReal(0.0);
        }
        if (strcmp(name, "view_object") == 0) {
            if (arrayIndex >= 0 && MAX_VIEWS > arrayIndex) {
                return RValue_makeReal((double) runner->currentRoom->views[arrayIndex].objectId);
            }
            return RValue_makeReal(-4.0);
        }

        // Background properties
        if (strcmp(name, "background_visible") == 0) {
            if (arrayIndex >= 0 && MAX_BACKGROUNDS > arrayIndex) return RValue_makeBool(runner->backgrounds[arrayIndex].visible);
            return RValue_makeBool(false);
        }
        if (strcmp(name, "background_index") == 0) {
            if (arrayIndex >= 0 && MAX_BACKGROUNDS > arrayIndex) return RValue_makeReal((double) runner->backgrounds[arrayIndex].backgroundIndex);
            return RValue_makeReal(-1.0);
        }
        if (strcmp(name, "background_x") == 0) {
            if (arrayIndex >= 0 && MAX_BACKGROUNDS > arrayIndex) return RValue_makeReal((double) runner->backgrounds[arrayIndex].x);
            return RValue_makeReal(0.0);
        }
        if (strcmp(name, "background_y") == 0) {
            if (arrayIndex >= 0 && MAX_BACKGROUNDS > arrayIndex) return RValue_makeReal((double) runner->backgrounds[arrayIndex].y);
            return RValue_makeReal(0.0);
        }
        if (strcmp(name, "background_hspeed") == 0) {
            if (arrayIndex >= 0 && MAX_BACKGROUNDS > arrayIndex) return RValue_makeReal((double) runner->backgrounds[arrayIndex].speedX);
            return RValue_makeReal(0.0);
        }
        if (strcmp(name, "background_vspeed") == 0) {
            if (arrayIndex >= 0 && MAX_BACKGROUNDS > arrayIndex) return RValue_makeReal((double) runner->backgrounds[arrayIndex].speedY);
            return RValue_makeReal(0.0);
        }
        if (strcmp(name, "background_width") == 0) {
            if (arrayIndex >= 0 && MAX_BACKGROUNDS > arrayIndex) {
                int32_t tpagIndex = Renderer_resolveBackgroundTPAGIndex(runner->dataWin, runner->backgrounds[arrayIndex].backgroundIndex);
                if (tpagIndex >= 0) return RValue_makeReal((double) runner->dataWin->tpag.items[tpagIndex].boundingWidth);
            }
            return RValue_makeReal(0.0);
        }
        if (strcmp(name, "background_height") == 0) {
            if (arrayIndex >= 0 && MAX_BACKGROUNDS > arrayIndex) {
                int32_t tpagIndex = Renderer_resolveBackgroundTPAGIndex(runner->dataWin, runner->backgrounds[arrayIndex].backgroundIndex);
                if (tpagIndex >= 0) return RValue_makeReal((double) runner->dataWin->tpag.items[tpagIndex].boundingHeight);
            }
            return RValue_makeReal(0.0);
        }
        if (strcmp(name, "background_color") == 0 || strcmp(name, "background_colour") == 0) {
            return RValue_makeReal((double) runner->backgroundColor);
        }
    }

    // Timing
    if (strcmp(name, "current_time") == 0) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        double ms = (double) ts.tv_sec * 1000.0 + (double) ts.tv_nsec / 1000000.0;
        return RValue_makeReal(ms);
    }

    // argument_count
    if (strcmp(name, "argument_count") == 0) return RValue_makeReal((double) ctx->scriptArgCount);

    // argument[N] - array-style access to script arguments
    if (strcmp(name, "argument") == 0) {
        if (ctx->scriptArgs != nullptr && ctx->scriptArgCount > arrayIndex && arrayIndex >= 0) {
            RValue val = ctx->scriptArgs[arrayIndex];
            val.ownsString = false;
            return val;
        }
        return RValue_makeUndefined();
    }

    // Argument variables (argument0..argument15 are built-in in GMS bytecode, stored in scriptArgs)
    const int argNumber = extractArgumentNumber(name);
    if (argNumber != -1) {
        if (ctx->scriptArgs != nullptr && ctx->scriptArgCount > argNumber) {
            RValue val = ctx->scriptArgs[argNumber];
            val.ownsString = false;
            return val;
        }
        return RValue_makeUndefined();
    }

    // Keyboard variables
    if (runner != nullptr) {
        if (strcmp(name, "keyboard_key") == 0) return RValue_makeReal((double) runner->keyboard->lastKey);
        if (strcmp(name, "keyboard_lastkey") == 0) return RValue_makeReal((double) runner->keyboard->lastKey);
    }

    // Surfaces
    if (strcmp(name, "application_surface") == 0) return RValue_makeReal(-1.0); // sentinel ID for the application surface

    // Constants that GMS defines
    if (strcmp(name, "true") == 0) return RValue_makeBool(true);
    if (strcmp(name, "false") == 0) return RValue_makeBool(false);
    if (strcmp(name, "pi") == 0) return RValue_makeReal(3.14159265358979323846);
    if (strcmp(name, "undefined") == 0) return RValue_makeUndefined();

    // Path action constants
    if (strcmp(name, "path_action_stop") == 0) return RValue_makeReal(0.0);
    if (strcmp(name, "path_action_restart") == 0) return RValue_makeReal(1.0);
    if (strcmp(name, "path_action_continue") == 0) return RValue_makeReal(2.0);
    if (strcmp(name, "path_action_reverse") == 0) return RValue_makeReal(3.0);

    // System info
    if (strcmp(name, "fps") == 0) return RValue_makeReal(30.0);
    if (strcmp(name, "fps_real") == 0) return RValue_makeReal(30.0);
    if (strcmp(name, "working_directory") == 0) return RValue_makeOwnedString(strdup(gameWorkingDirectory != nullptr ? gameWorkingDirectory : ""));
    if (strcmp(name, "program_directory") == 0) return RValue_makeOwnedString(strdup(""));
    if (strcmp(name, "temp_directory") == 0) return RValue_makeOwnedString(strdup(""));
    if (strcmp(name, "game_save_id") == 0) return RValue_makeOwnedString(strdup(""));
    if (strcmp(name, "os_type") == 0) return RValue_makeReal(1.0); // os_windows = 1

    fprintf(stderr, "VM: Unhandled built-in variable read '%s' (arrayIndex=%d)\n", name, arrayIndex);
    return RValue_makeReal(0.0);
}

void VMBuiltins_setVariable(VMContext* ctx, const char* name, RValue val, int32_t arrayIndex) {
    Instance* inst = (Instance*) ctx->currentInstance;
    Runner* runner = (Runner*) requireNotNullMessage(ctx->runner, "VM: setVariable called but no runner!");

    // Per-instance properties
    if (inst != nullptr) {
        if (strcmp(name, "image_speed") == 0) { inst->imageSpeed = RValue_toReal(val); return; }
        if (strcmp(name, "image_index") == 0) { inst->imageIndex = RValue_toReal(val); return; }
        if (strcmp(name, "image_xscale") == 0) { inst->imageXscale = RValue_toReal(val); return; }
        if (strcmp(name, "image_yscale") == 0) { inst->imageYscale = RValue_toReal(val); return; }
        if (strcmp(name, "image_angle") == 0) { inst->imageAngle = RValue_toReal(val); return; }
        if (strcmp(name, "image_alpha") == 0) { inst->imageAlpha = RValue_toReal(val); return; }
        if (strcmp(name, "image_blend") == 0) { inst->imageBlend = (uint32_t) RValue_toReal(val); return; }
        if (strcmp(name, "sprite_index") == 0) { inst->spriteIndex = RValue_toInt32(val); return; }
        if (strcmp(name, "visible") == 0) { inst->visible = RValue_toBool(val); return; }
        if (strcmp(name, "depth") == 0) { inst->depth = RValue_toInt32(val); return; }
        if (strcmp(name, "x") == 0) { inst->x = RValue_toReal(val); return; }
        if (strcmp(name, "y") == 0) { inst->y = RValue_toReal(val); return; }
        if (strcmp(name, "persistent") == 0) { inst->persistent = RValue_toBool(val); return; }
        if (strcmp(name, "solid") == 0) { inst->solid = RValue_toBool(val); return; }
        if (strcmp(name, "xprevious") == 0) { inst->xprevious = RValue_toReal(val); return; }
        if (strcmp(name, "yprevious") == 0) { inst->yprevious = RValue_toReal(val); return; }
        if (strcmp(name, "xstart") == 0) { inst->xstart = RValue_toReal(val); return; }
        if (strcmp(name, "ystart") == 0) { inst->ystart = RValue_toReal(val); return; }
        if (strcmp(name, "mask_index") == 0) { inst->maskIndex = RValue_toInt32(val); return; }
        if (strcmp(name, "speed") == 0) { inst->speed = RValue_toReal(val); Instance_computeComponentsFromSpeed(inst); return; }
        if (strcmp(name, "direction") == 0) {
            double d = fmod(RValue_toReal(val), 360.0);
            if (d < 0.0) d += 360.0;
            inst->direction = d;
            Instance_computeComponentsFromSpeed(inst);
            return;
        }
        if (strcmp(name, "hspeed") == 0) { inst->hspeed = RValue_toReal(val); Instance_computeSpeedFromComponents(inst); return; }
        if (strcmp(name, "vspeed") == 0) { inst->vspeed = RValue_toReal(val); Instance_computeSpeedFromComponents(inst); return; }
        if (strcmp(name, "friction") == 0) { inst->friction = RValue_toReal(val); return; }
        if (strcmp(name, "gravity") == 0) { inst->gravity = RValue_toReal(val); return; }
        if (strcmp(name, "gravity_direction") == 0) { inst->gravityDirection = RValue_toReal(val); return; }
        if (strcmp(name, "alarm") == 0) {
            if (isValidAlarmIndex(arrayIndex)) {
                int32_t newValue = RValue_toInt32(val);
                if (shgeti(ctx->alarmsToBeTraced, "*") != -1 || shgeti(ctx->alarmsToBeTraced, runner->dataWin->objt.objects[inst->objectIndex].name) != -1) {
                    fprintf(stderr, "VM: [%s] Setting Alarm[%d] = %d (instanceId=%d)\n", runner->dataWin->objt.objects[inst->objectIndex].name, arrayIndex, newValue, inst->instanceId);
                }
                inst->alarm[arrayIndex] = newValue;
            }
            return;
        }

        // Path instance variables (writable)
        if (strcmp(name, "path_position") == 0) { inst->pathPosition = RValue_toReal(val); return; }
        if (strcmp(name, "path_speed") == 0) { inst->pathSpeed = RValue_toReal(val); return; }
        if (strcmp(name, "path_scale") == 0) { inst->pathScale = RValue_toReal(val); return; }
        if (strcmp(name, "path_orientation") == 0) { inst->pathOrientation = RValue_toReal(val); return; }
        if (strcmp(name, "path_endaction") == 0) { inst->pathEndAction = RValue_toInt32(val); return; }
    }

    // Keyboard variables
    if (strcmp(name, "keyboard_key") == 0) {
        runner->keyboard->lastKey = RValue_toInt32(val);
        return;
    }
    if (strcmp(name, "keyboard_lastkey") == 0) {
        runner->keyboard->lastKey = RValue_toInt32(val);
        return;
    }

    // View properties
    if (strcmp(name, "view_xview") == 0) {
        if (arrayIndex >= 0 && MAX_VIEWS > arrayIndex) {
            runner->currentRoom->views[arrayIndex].viewX = RValue_toInt32(val);
        }
        return;
    }
    if (strcmp(name, "view_yview") == 0) {
        if (arrayIndex >= 0 && MAX_VIEWS > arrayIndex) {
            runner->currentRoom->views[arrayIndex].viewY = RValue_toInt32(val);
        }
        return;
    }
    if (strcmp(name, "view_wview") == 0) {
        if (arrayIndex >= 0 && MAX_VIEWS > arrayIndex) {
            runner->currentRoom->views[arrayIndex].viewWidth = RValue_toInt32(val);
        }
        return;
    }
    if (strcmp(name, "view_hview") == 0) {
        if (arrayIndex >= 0 && MAX_VIEWS > arrayIndex) {
            runner->currentRoom->views[arrayIndex].viewHeight = RValue_toInt32(val);
        }
        return;
    }
    if (strcmp(name, "view_object") == 0) {
        if (arrayIndex >= 0 && MAX_VIEWS > arrayIndex) {
            runner->currentRoom->views[arrayIndex].objectId = RValue_toInt32(val);
        }
        return;
    }

    // Background properties
    if (strcmp(name, "background_visible") == 0) {
        if (arrayIndex >= 0 && MAX_BACKGROUNDS > arrayIndex) runner->backgrounds[arrayIndex].visible = RValue_toBool(val);
        return;
    }
    if (strcmp(name, "background_index") == 0) {
        if (arrayIndex >= 0 && MAX_BACKGROUNDS > arrayIndex) runner->backgrounds[arrayIndex].backgroundIndex = RValue_toInt32(val);
        return;
    }
    if (strcmp(name, "background_x") == 0) {
        if (arrayIndex >= 0 && MAX_BACKGROUNDS > arrayIndex) runner->backgrounds[arrayIndex].x = (float) RValue_toReal(val);
        return;
    }
    if (strcmp(name, "background_y") == 0) {
        if (arrayIndex >= 0 && MAX_BACKGROUNDS > arrayIndex) runner->backgrounds[arrayIndex].y = (float) RValue_toReal(val);
        return;
    }
    if (strcmp(name, "background_hspeed") == 0) {
        if (arrayIndex >= 0 && MAX_BACKGROUNDS > arrayIndex) runner->backgrounds[arrayIndex].speedX = (float) RValue_toReal(val);
        return;
    }
    if (strcmp(name, "background_vspeed") == 0) {
        if (arrayIndex >= 0 && MAX_BACKGROUNDS > arrayIndex) runner->backgrounds[arrayIndex].speedY = (float) RValue_toReal(val);
        return;
    }
    if (strcmp(name, "background_color") == 0 || strcmp(name, "background_colour") == 0) {
        runner->backgroundColor = (uint32_t) RValue_toInt32(val);
        return;
    }

    // Room properties
    if (strcmp(name, "room_speed") == 0) {
        runner->currentRoom->speed = (uint32_t) RValue_toReal(val);
        return;
    }
    if (strcmp(name, "room_persistent") == 0) {
        runner->currentRoom->persistent = RValue_toBool(val);
        return;
    }

    // Read-only variables (silently ignore)
    if (strcmp(name, "os_type") == 0 || strcmp(name, "os_windows") == 0 ||
        strcmp(name, "os_ps4") == 0 || strcmp(name, "os_psvita") == 0 ||
        strcmp(name, "id") == 0 || strcmp(name, "object_index") == 0 ||
        strcmp(name, "current_time") == 0 || strcmp(name, "room") == 0 ||
        strcmp(name, "view_current") == 0 || strcmp(name, "path_index") == 0) {
        fprintf(stderr, "VM: Warning - attempted write to read-only built-in '%s'\n", name);
        return;
    }

    // argument[N] - array-style write to script arguments
    if (strcmp(name, "argument") == 0) {
        if (ctx->scriptArgs != nullptr && ctx->scriptArgCount > arrayIndex && arrayIndex >= 0) {
            RValue_free(&ctx->scriptArgs[arrayIndex]);
            ctx->scriptArgs[arrayIndex] = val;
        }
        return;
    }

    // Argument variables
    const int argNumber = extractArgumentNumber(name);
    if (argNumber != -1) {
        if (ctx->scriptArgs != nullptr && ctx->scriptArgCount > argNumber) {
            RValue_free(&ctx->scriptArgs[argNumber]);
            ctx->scriptArgs[argNumber] = val;
        }
        return;
    }

    fprintf(stderr, "VM: Unhandled built-in variable write '%s' (arrayIndex=%d)\n", name, arrayIndex);
}

// ===[ BUILTIN FUNCTION IMPLEMENTATIONS ]===

static RValue builtinShowDebugMessage([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) {
        fprintf(stderr, "[show_debug_message] Expected at least 1 argument\n");
        return RValue_makeUndefined();
    }

    char* val = RValue_toString(args[0]);
    printf("Game: %s\n", val);
    free(val);

    return RValue_makeUndefined();
}

static RValue builtinStringLength([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeInt32(0);
    // GML converts non-string arguments to string before measuring length
    char* str = RValue_toString(args[0]);
    int32_t len = (int32_t) strlen(str);
    free(str);
    return RValue_makeInt32(len);
}

static RValue builtinReal([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    return RValue_makeReal(RValue_toReal(args[0]));
}

static RValue builtinString([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeOwnedString(strdup(""));
    char* result = RValue_toString(args[0]);
    return RValue_makeOwnedString(result);
}

static RValue builtinFloor([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    return RValue_makeReal(floor(RValue_toReal(args[0])));
}

static RValue builtinCeil([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    return RValue_makeReal(ceil(RValue_toReal(args[0])));
}

static RValue builtinRound([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    return RValue_makeReal(round(RValue_toReal(args[0])));
}

static RValue builtinAbs([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    return RValue_makeReal(fabs(RValue_toReal(args[0])));
}

static RValue builtinSign([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    double val = RValue_toReal(args[0]);
    double result = (val > 0.0) ? 1.0 : ((0.0 > val) ? -1.0 : 0.0);
    return RValue_makeReal(result);
}

static RValue builtinMax([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    double result = -INFINITY;
    repeat(argCount, i) {
        double val = RValue_toReal(args[i]);
        if (val > result) result = val;
    }
    return RValue_makeReal(result);
}

static RValue builtinMin([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    double result = INFINITY;
    repeat(argCount, i) {
        double val = RValue_toReal(args[i]);
        if (result > val) result = val;
    }
    return RValue_makeReal(result);
}

static RValue builtinPower([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeReal(0.0);
    return RValue_makeReal(pow(RValue_toReal(args[0]), RValue_toReal(args[1])));
}

static RValue builtinSqrt([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    return RValue_makeReal(sqrt(RValue_toReal(args[0])));
}

static RValue builtinSqr([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    double val = RValue_toReal(args[0]);
    return RValue_makeReal(val * val);
}

static RValue builtinIsString([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeBool(false);
    return RValue_makeBool(args[0].type == RVALUE_STRING);
}

static RValue builtinIsReal([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeBool(false);
    bool result = args[0].type == RVALUE_REAL || args[0].type == RVALUE_INT32 || args[0].type == RVALUE_INT64 || args[0].type == RVALUE_BOOL;
    return RValue_makeBool(result);
}

static RValue builtinIsUndefined([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeBool(true);
    return RValue_makeBool(args[0].type == RVALUE_UNDEFINED);
}

// ===[ STRING FUNCTIONS ]===

static RValue builtinStringUpper([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount || args[0].type != RVALUE_STRING) return RValue_makeOwnedString(strdup(""));
    char* result = strdup(args[0].string != nullptr ? args[0].string : "");
    for (char* p = result; *p; p++) *p = (char) toupper((unsigned char) *p);
    return RValue_makeOwnedString(result);
}

static RValue builtinStringLower([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount || args[0].type != RVALUE_STRING) return RValue_makeOwnedString(strdup(""));
    char* result = strdup(args[0].string != nullptr ? args[0].string : "");
    for (char* p = result; *p; p++) *p = (char) tolower((unsigned char) *p);
    return RValue_makeOwnedString(result);
}

static RValue builtinStringCopy([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (3 > argCount || args[0].type != RVALUE_STRING) return RValue_makeOwnedString(strdup(""));
    const char* str = args[0].string != nullptr ? args[0].string : "";
    int32_t pos = RValue_toInt32(args[1]) - 1; // GMS is 1-based
    int32_t len = RValue_toInt32(args[2]);
    int32_t strLen = (int32_t) strlen(str);

    if (0 > pos) pos = 0;
    if (pos >= strLen || 0 >= len) return RValue_makeOwnedString(strdup(""));
    if (pos + len > strLen) len = strLen - pos;

    char* result = malloc(len + 1);
    memcpy(result, str + pos, len);
    result[len] = '\0';
    return RValue_makeOwnedString(result);
}

static RValue builtinOrd([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount || args[0].type != RVALUE_STRING || args[0].string == nullptr || args[0].string[0] == '\0') {
        return RValue_makeReal(0.0);
    }
    return RValue_makeReal((double) (unsigned char) args[0].string[0]);
}

static RValue builtinChr([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeOwnedString(strdup(""));
    char buf[2] = { (char) RValue_toInt32(args[0]), '\0' };
    return RValue_makeOwnedString(strdup(buf));
}

static RValue builtinStringPos([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount || args[0].type != RVALUE_STRING || args[1].type != RVALUE_STRING) return RValue_makeReal(0.0);
    const char* needle = args[0].string != nullptr ? args[0].string : "";
    const char* haystack = args[1].string != nullptr ? args[1].string : "";
    const char* found = strstr(haystack, needle);
    if (found == nullptr) return RValue_makeReal(0.0);
    return RValue_makeReal((double) (found - haystack + 1)); // 1-based
}

static RValue builtinStringCharAt([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount || args[0].type != RVALUE_STRING) return RValue_makeOwnedString(strdup(""));
    const char* str = args[0].string != nullptr ? args[0].string : "";
    int32_t pos = RValue_toInt32(args[1]) - 1; // 1-based
    int32_t strLen = (int32_t) strlen(str);
    if (0 > pos || pos >= strLen) return RValue_makeOwnedString(strdup(""));
    char buf[2] = { str[pos], '\0' };
    return RValue_makeOwnedString(strdup(buf));
}

static RValue builtinStringDelete([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (3 > argCount || args[0].type != RVALUE_STRING) return RValue_makeOwnedString(strdup(""));
    const char* str = args[0].string != nullptr ? args[0].string : "";
    int32_t pos = RValue_toInt32(args[1]) - 1; // 1-based
    int32_t count = RValue_toInt32(args[2]);
    int32_t strLen = (int32_t) strlen(str);

    if (0 > pos || pos >= strLen || 0 >= count) return RValue_makeOwnedString(strdup(str));
    if (pos + count > strLen) count = strLen - pos;

    char* result = malloc(strLen - count + 1);
    memcpy(result, str, pos);
    memcpy(result + pos, str + pos + count, strLen - pos - count);
    result[strLen - count] = '\0';
    return RValue_makeOwnedString(result);
}

static RValue builtinStringInsert([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (3 > argCount || args[0].type != RVALUE_STRING || args[1].type != RVALUE_STRING) return RValue_makeOwnedString(strdup(""));
    const char* substr = args[0].string != nullptr ? args[0].string : "";
    const char* str = args[1].string != nullptr ? args[1].string : "";
    int32_t pos = RValue_toInt32(args[2]) - 1; // 1-based
    int32_t strLen = (int32_t) strlen(str);
    int32_t subLen = (int32_t) strlen(substr);

    if (0 > pos) pos = 0;
    if (pos > strLen) pos = strLen;

    char* result = malloc(strLen + subLen + 1);
    memcpy(result, str, pos);
    memcpy(result + pos, substr, subLen);
    memcpy(result + pos + subLen, str + pos, strLen - pos);
    result[strLen + subLen] = '\0';
    return RValue_makeOwnedString(result);
}

static RValue builtinStringReplaceAll([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (3 > argCount || args[0].type != RVALUE_STRING || args[1].type != RVALUE_STRING || args[2].type != RVALUE_STRING) return RValue_makeOwnedString(strdup(""));
    const char* str = args[0].string != nullptr ? args[0].string : "";
    const char* needle = args[1].string != nullptr ? args[1].string : "";
    const char* replacement = args[2].string != nullptr ? args[2].string : "";
    int32_t needleLen = (int32_t) strlen(needle);
    if (0 == needleLen) return RValue_makeOwnedString(strdup(str));
    int32_t replacementLen = (int32_t) strlen(replacement);

    // Count occurrences to pre-allocate
    int32_t count = 0;
    const char* p = str;
    while ((p = strstr(p, needle)) != nullptr) { count++; p += needleLen; }

    int32_t strLen = (int32_t) strlen(str);
    int32_t resultLen = strLen + count * (replacementLen - needleLen);
    char* result = malloc(resultLen + 1);
    char* out = result;
    p = str;
    const char* match;
    while ((match = strstr(p, needle)) != nullptr) {
        int32_t before = (int32_t) (match - p);
        memcpy(out, p, before);
        out += before;
        memcpy(out, replacement, replacementLen);
        out += replacementLen;
        p = match + needleLen;
    }
    strcpy(out, p);
    return RValue_makeOwnedString(result);
}

// string_hash_to_newline: replaces # with \n (unless preceded by \)
static RValue builtinStringHashToNewline([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount || args[0].type != RVALUE_STRING) return RValue_makeOwnedString(strdup(""));
    const char* str = args[0].string != nullptr ? args[0].string : "";
    size_t len = strlen(str);
    // Worst case: every # becomes \r\n (2 chars)
    char* result = malloc(len * 2 + 1);
    size_t out = 0;
    for (size_t i = 0; len > i; i++) {
        if (str[i] == '#') {
            if (0 < i && str[i - 1] == '\\') {
                // Replace the backslash we already wrote with #
                result[out - 1] = '#';
            } else {
                result[out++] = '\n';
            }
        } else {
            result[out++] = str[i];
        }
    }
    result[out] = '\0';
    return RValue_makeOwnedString(result);
}

// ===[ MATH FUNCTIONS ]===

static RValue builtinDarctan2([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeReal(0.0);
    double y = RValue_toReal(args[0]);
    double x = RValue_toReal(args[1]);
    return RValue_makeReal(atan2(y, x) * (180.0 / M_PI));
}

static RValue builtinSin([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    return RValue_makeReal(sin(RValue_toReal(args[0])));
}

static RValue builtinCos([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    return RValue_makeReal(cos(RValue_toReal(args[0])));
}

static RValue builtinDegtorad([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    return RValue_makeReal(RValue_toReal(args[0]) * (M_PI / 180.0));
}

static RValue builtinRadtodeg([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    return RValue_makeReal(RValue_toReal(args[0]) * (180.0 / M_PI));
}

static RValue builtinClamp([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (3 > argCount) return RValue_makeReal(0.0);
    double val = RValue_toReal(args[0]);
    double lo = RValue_toReal(args[1]);
    double hi = RValue_toReal(args[2]);
    if (lo > val) val = lo;
    if (val > hi) val = hi;
    return RValue_makeReal(val);
}

static RValue builtinLerp([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (3 > argCount) return RValue_makeReal(0.0);
    double a = RValue_toReal(args[0]);
    double b = RValue_toReal(args[1]);
    double t = RValue_toReal(args[2]);
    return RValue_makeReal(a + (b - a) * t);
}

static RValue builtinPointDistance([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (4 > argCount) return RValue_makeReal(0.0);
    double dx = RValue_toReal(args[2]) - RValue_toReal(args[0]);
    double dy = RValue_toReal(args[3]) - RValue_toReal(args[1]);
    return RValue_makeReal(sqrt(dx * dx + dy * dy));
}

static RValue builtinDistanceToPoint(VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeReal(0.0);
    double px = RValue_toReal(args[0]);
    double py = RValue_toReal(args[1]);

    Instance* inst = ctx->currentInstance;
    int32_t sprIdx = (inst->maskIndex >= 0) ? inst->maskIndex : inst->spriteIndex;
    if (0 > sprIdx || (uint32_t) sprIdx >= ctx->dataWin->sprt.count) {
        return RValue_makeReal(0.0);
    }

    Sprite* spr = &ctx->dataWin->sprt.sprites[sprIdx];

    // Compute bounding box (no-rotation path)
    double bboxLeft = inst->x + inst->imageXscale * (spr->marginLeft - spr->originX);
    double bboxRight = inst->x + inst->imageXscale * ((spr->marginRight + 1) - spr->originX);
    if (bboxLeft > bboxRight) {
        double t = bboxLeft;
        bboxLeft = bboxRight;
        bboxRight = t;
    }

    double bboxTop = inst->y + inst->imageYscale * (spr->marginTop - spr->originY);
    double bboxBottom = inst->y + inst->imageYscale * ((spr->marginBottom + 1) - spr->originY);
    if (bboxTop > bboxBottom) {
        double t = bboxTop;
        bboxTop = bboxBottom;
        bboxBottom = t;
    }

    // Distance from point to nearest edge of bbox (0 if inside)
    double xd = 0.0;
    double yd = 0.0;
    if (px > bboxRight)  xd = px - bboxRight;
    if (px < bboxLeft)   xd = px - bboxLeft;
    if (py > bboxBottom) yd = py - bboxBottom;
    if (py < bboxTop)    yd = py - bboxTop;

    return RValue_makeReal(sqrt(xd * xd + yd * yd));
}

static RValue builtinPointDirection([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (4 > argCount) return RValue_makeReal(0.0);
    double dx = RValue_toReal(args[2]) - RValue_toReal(args[0]);
    double dy = RValue_toReal(args[3]) - RValue_toReal(args[1]);
    return RValue_makeReal(atan2(-dy, dx) * (180.0 / M_PI));
}

static RValue builtinMoveTowardsPoint(VMContext* ctx, RValue* args, [[maybe_unused]] int32_t argCount) {
    double targetX = RValue_toReal(args[0]);
    double targetY = RValue_toReal(args[1]);
    double spd = RValue_toReal(args[2]);
    Instance* inst = ctx->currentInstance;
    double dx = targetX - inst->x;
    double dy = targetY - inst->y;
    double dir = atan2(-dy, dx) * (180.0 / M_PI);
    if (dir < 0.0) dir += 360.0;
    inst->direction = dir;
    inst->speed = spd;
    Instance_computeComponentsFromSpeed(inst);
    return RValue_makeReal(0.0);
}

static RValue builtinLengthdir_x([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeReal(0.0);
    double len = RValue_toReal(args[0]);
    double dir = RValue_toReal(args[1]) * (M_PI / 180.0);
    return RValue_makeReal(len * cos(dir));
}

static RValue builtinLengthdir_y([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeReal(0.0);
    double len = RValue_toReal(args[0]);
    double dir = RValue_toReal(args[1]) * (M_PI / 180.0);
    return RValue_makeReal(-len * sin(dir));
}

// ===[ RANDOM FUNCTIONS ]===

static RValue builtinRandom([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    double n = RValue_toReal(args[0]);
    return RValue_makeReal(((double) rand() / (double) RAND_MAX) * n);
}

static RValue builtinRandomRange([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeReal(0.0);
    double lo = RValue_toReal(args[0]);
    double hi = RValue_toReal(args[1]);
    return RValue_makeReal(lo + ((double) rand() / (double) RAND_MAX) * (hi - lo));
}

static RValue builtinIrandom([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    int32_t n = RValue_toInt32(args[0]);
    if (0 >= n) return RValue_makeReal(0.0);
    return RValue_makeReal((double) (rand() % (n + 1)));
}

static RValue builtinIrandomRange([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeReal(0.0);
    int32_t lo = RValue_toInt32(args[0]);
    int32_t hi = RValue_toInt32(args[1]);
    if (lo > hi) { int32_t tmp = lo; lo = hi; hi = tmp; }
    int32_t range = hi - lo + 1;
    if (0 >= range) return RValue_makeReal((double) lo);
    return RValue_makeReal((double) (lo + rand() % range));
}

static RValue builtinChoose([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeUndefined();
    int32_t idx = rand() % argCount;
    // Must duplicate the value since args will be freed
    RValue val = args[idx];
    if (val.type == RVALUE_STRING && val.string != nullptr) {
        return RValue_makeOwnedString(strdup(val.string));
    }
    return val;
}

static RValue builtinRandomize(VMContext* ctx, [[maybe_unused]] RValue* args, [[maybe_unused]] int32_t argCount) {
    if (ctx->hasFixedSeed) return RValue_makeUndefined();
    srand((unsigned int) time(nullptr));
    return RValue_makeUndefined();
}

// ===[ ROOM FUNCTIONS ]===

static RValue builtinRoomGotoNext(VMContext* ctx, [[maybe_unused]] RValue* args, [[maybe_unused]] int32_t argCount) {
    Runner* runner = requireNotNullMessage(ctx->runner, "VM: room_goto_next called but no runner!");

    int32_t nextPos = runner->currentRoomOrderPosition + 1;
    if ((int32_t) runner->dataWin->gen8.roomOrderCount > nextPos) {
        runner->pendingRoom = runner->dataWin->gen8.roomOrder[nextPos];
    } else {
        fprintf(stderr, "VM: room_goto_next - already at last room!\n");
    }
    return RValue_makeUndefined();
}

static RValue builtinRoomGotoPrevious(VMContext* ctx, [[maybe_unused]] RValue* args, [[maybe_unused]] int32_t argCount) {
    Runner* runner = requireNotNullMessage(ctx->runner, "VM: room_goto_previous called but no runner!");

    int32_t previousPos = runner->currentRoomOrderPosition - 1;
    if (previousPos >= 0) {
        runner->pendingRoom = runner->dataWin->gen8.roomOrder[previousPos];
    } else {
        fprintf(stderr, "VM: room_goto_previous - already at first room!\n");
    }
    return RValue_makeUndefined();
}

static RValue builtinRoomGoto(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeUndefined();
    Runner* runner = requireNotNullMessage(ctx->runner, "VM: room_goto called but no runner!");
    runner->pendingRoom = RValue_toInt32(args[0]);
    return RValue_makeUndefined();
}

static RValue builtinRoomNext(VMContext* ctx, RValue* args, [[maybe_unused]] int32_t argCount) {
    Runner* runner = requireNotNullMessage(ctx->runner, "VM: room_next called but no runner!");
    int32_t roomId = RValue_toInt32(args[0]);
    DataWin* dw = runner->dataWin;
    repeat(dw->gen8.roomOrderCount, i) {
        if (dw->gen8.roomOrder[i] == roomId && dw->gen8.roomOrderCount > i + 1) {
            return RValue_makeReal(dw->gen8.roomOrder[i + 1]);
        }
    }
    return RValue_makeReal(-1);
}

static RValue builtinRoomPrevious(VMContext* ctx, RValue* args, [[maybe_unused]] int32_t argCount) {
    Runner* runner = requireNotNullMessage(ctx->runner, "VM: room_previous called but no runner!");
    int32_t roomId = RValue_toInt32(args[0]);
    DataWin* dw = runner->dataWin;
    repeat(dw->gen8.roomOrderCount, i) {
        if (dw->gen8.roomOrder[i] == roomId && i > 0) {
            return RValue_makeReal(dw->gen8.roomOrder[i - 1]);
        }
    }
    return RValue_makeReal(-1);
}

// GMS2 camera compatibility - we treat view index as camera ID
static RValue builtinViewGetCamera([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(-1);
    int32_t viewIndex = RValue_toInt32(args[0]);
    if (viewIndex >= 0 && MAX_VIEWS > viewIndex) {
        return RValue_makeReal((double) viewIndex);
    }
    return RValue_makeReal(-1);
}

static RValue builtinCameraGetViewX(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(-1);
    Runner* runner = requireNotNullMessage(ctx->runner, "VM: camera_get_view_x called but no runner!");
    int32_t cameraId = RValue_toInt32(args[0]);
    if (cameraId >= 0 && MAX_VIEWS > cameraId) {
        return RValue_makeReal((double) runner->currentRoom->views[cameraId].viewX);
    }
    return RValue_makeReal(-1);
}

static RValue builtinCameraGetViewY(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(-1);
    Runner* runner = requireNotNullMessage(ctx->runner, "VM: camera_get_view_y called but no runner!");
    int32_t cameraId = RValue_toInt32(args[0]);
    if (cameraId >= 0 && MAX_VIEWS > cameraId) {
        return RValue_makeReal((double) runner->currentRoom->views[cameraId].viewY);
    }
    return RValue_makeReal(-1);
}

static RValue builtinCameraGetViewWidth(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(-1);
    Runner* runner = requireNotNullMessage(ctx->runner, "VM: camera_get_view_width called but no runner!");
    int32_t cameraId = RValue_toInt32(args[0]);
    if (cameraId >= 0 && MAX_VIEWS > cameraId) {
        return RValue_makeReal((double) runner->currentRoom->views[cameraId].viewWidth);
    }
    return RValue_makeReal(-1);
}

static RValue builtinCameraGetViewHeight(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(-1);
    Runner* runner = requireNotNullMessage(ctx->runner, "VM: camera_get_view_height called but no runner!");
    int32_t cameraId = RValue_toInt32(args[0]);
    if (cameraId >= 0 && MAX_VIEWS > cameraId) {
        return RValue_makeReal((double) runner->currentRoom->views[cameraId].viewHeight);
    }
    return RValue_makeReal(-1);
}

// ===[ VARIABLE FUNCTIONS ]===

static RValue builtinVariableGlobalExists(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount || args[0].type != RVALUE_STRING) return RValue_makeReal(0.0);
    const char* name = args[0].string;
    ptrdiff_t idx = shgeti(ctx->globalVarNameMap, (char*) name);
    if (0 > idx) return RValue_makeReal(0.0);
    int32_t varID = ctx->globalVarNameMap[idx].value;
    if (ctx->globalVarCount > (uint32_t) varID && ctx->globalVars[varID].type != RVALUE_UNDEFINED) {
        return RValue_makeReal(1.0);
    }
    return RValue_makeReal(0.0);
}

static RValue builtinVariableGlobalGet(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount || args[0].type != RVALUE_STRING) return RValue_makeUndefined();
    const char* name = args[0].string;
    ptrdiff_t idx = shgeti(ctx->globalVarNameMap, (char*) name);
    if (0 > idx) return RValue_makeUndefined();
    int32_t varID = ctx->globalVarNameMap[idx].value;
    if (ctx->globalVarCount > (uint32_t) varID) {
        RValue val = ctx->globalVars[varID];
        // Duplicate owned strings
        if (val.type == RVALUE_STRING && val.ownsString && val.string != nullptr) {
            return RValue_makeOwnedString(strdup(val.string));
        }
        return val;
    }
    return RValue_makeUndefined();
}

static RValue builtinVariableGlobalSet(VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount || args[0].type != RVALUE_STRING) return RValue_makeUndefined();
    const char* name = args[0].string;
    ptrdiff_t idx = shgeti(ctx->globalVarNameMap, (char*) name);
    if (0 > idx) return RValue_makeUndefined();
    int32_t varID = ctx->globalVarNameMap[idx].value;
    if (ctx->globalVarCount > (uint32_t) varID) {
        RValue_free(&ctx->globalVars[varID]);
        RValue val = args[1];
        // Duplicate owned strings since args will be freed
        if (val.type == RVALUE_STRING && val.string != nullptr) {
            ctx->globalVars[varID] = RValue_makeOwnedString(strdup(val.string));
        } else {
            ctx->globalVars[varID] = val;
        }
    }
    return RValue_makeUndefined();
}

// ===[ SCRIPT EXECUTE ]===

static RValue builtinScriptExecute(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeUndefined();
    int32_t scriptIdx = RValue_toInt32(args[0]);

    // Look up the script to get its codeId
    if (scriptIdx < 0 || (uint32_t) scriptIdx >= ctx->dataWin->scpt.count) {
        fprintf(stderr, "VM: script_execute - invalid script index %d\n", scriptIdx);
        return RValue_makeUndefined();
    }

    int32_t codeId = ctx->dataWin->scpt.scripts[scriptIdx].codeId;
    if (0 > codeId || ctx->dataWin->code.count <= (uint32_t) codeId) {
        fprintf(stderr, "VM: script_execute - invalid codeId %d for script %d\n", codeId, scriptIdx);
        return RValue_makeUndefined();
    }

    // Pass remaining args (skip the script index)
    RValue* scriptArgs = (argCount > 1) ? &args[1] : nullptr;
    int32_t scriptArgCount = argCount - 1;

    return VM_callCodeIndex(ctx, codeId, scriptArgs, scriptArgCount);
}

// ===[ OS FUNCTIONS ]===

static RValue builtinOsGetLanguage([[maybe_unused]] VMContext* ctx, [[maybe_unused]] RValue* args, [[maybe_unused]] int32_t argCount) {
    return RValue_makeOwnedString(strdup("en"));
}

static RValue builtinOsGetRegion([[maybe_unused]] VMContext* ctx, [[maybe_unused]] RValue* args, [[maybe_unused]] int32_t argCount) {
    return RValue_makeOwnedString(strdup("US"));
}

// ===[ DS_MAP BUILTIN FUNCTIONS ]===

static RValue builtinDsMapCreate([[maybe_unused]] VMContext* ctx, [[maybe_unused]] RValue* args, [[maybe_unused]] int32_t argCount) {
    return RValue_makeReal((double) dsMapCreate());
}

static RValue builtinDsMapAdd([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (3 > argCount) return RValue_makeUndefined();
    int32_t id = RValue_toInt32(args[0]);
    DsMapEntry** mapPtr = dsMapGet(id);
    if (mapPtr == nullptr) return RValue_makeUndefined();

    char* key = RValue_toString(args[1]);

    // Only add if key doesn't exist
    bool exists = shgeti(*mapPtr, key) != -1;

    if (exists) {
        free(key); // Key already exists, we didn't insert it
    } else {
        RValue val = args[2];
        if (val.type == RVALUE_STRING && val.string != nullptr) {
            val = RValue_makeOwnedString(strdup(val.string));
        }
        shput(*mapPtr, key, val);
        // The RValue is now "owned" by the map, we do not need to free it!
    }

    return RValue_makeUndefined();
}

static RValue builtinDsMapSet([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (3 > argCount) return RValue_makeUndefined();
    int32_t id = RValue_toInt32(args[0]);
    DsMapEntry** mapPtr = dsMapGet(id);
    if (mapPtr == nullptr) return RValue_makeUndefined();

    char* key = RValue_toString(args[1]);

    ptrdiff_t existingKeyIndex = shgeti(*mapPtr, key);

    if (existingKeyIndex != -1) {
        // If it already exists, we'll get the current value and free it
        RValue_free(&(*mapPtr)[existingKeyIndex].value);
    }

    RValue val = args[2];
    if (val.type == RVALUE_STRING && val.string != nullptr) {
        val = RValue_makeOwnedString(strdup(val.string));
    }

    shput(*mapPtr, key, val);

    if (existingKeyIndex != -1) {
        // If it already existed, then shput still owns the old key
        // So we'll need to free the created key
        free(key);
    }

    return RValue_makeUndefined();
}

static RValue builtinDsMapReplace(VMContext* ctx, RValue* args, int32_t argCount) {
    // ds_map_replace is the same as ds_map_set in GMS 1.4
    return builtinDsMapSet(ctx, args, argCount);
}

static RValue builtinDsMapFindValue([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeUndefined();
    int32_t id = RValue_toInt32(args[0]);
    DsMapEntry** mapPtr = dsMapGet(id);
    if (mapPtr == nullptr) return RValue_makeUndefined();

    char* key = RValue_toString(args[1]);
    ptrdiff_t idx = shgeti(*mapPtr, key);
    free(key);
    if (0 > idx) return RValue_makeUndefined();
    RValue val = (*mapPtr)[idx].value;
    if (val.type == RVALUE_STRING && val.string != nullptr) {
        return RValue_makeOwnedString(strdup(val.string));
    }
    return val;
}

static RValue builtinDsMapExists([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeReal(0.0);
    int32_t id = RValue_toInt32(args[0]);
    DsMapEntry** mapPtr = dsMapGet(id);
    if (mapPtr == nullptr) return RValue_makeReal(0.0);

    char* key = RValue_toString(args[1]);
    ptrdiff_t idx = shgeti(*mapPtr, key);
    free(key);
    return RValue_makeReal(idx >= 0 ? 1.0 : 0.0);
}

static RValue builtinDsMapFindFirst([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeUndefined();
    int32_t id = RValue_toInt32(args[0]);
    DsMapEntry** mapPtr = dsMapGet(id);
    if (mapPtr == nullptr || shlen(*mapPtr) == 0) return RValue_makeUndefined();
    return RValue_makeOwnedString(strdup((*mapPtr)[0].key));
}

static RValue builtinDsMapFindNext([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeUndefined();
    int32_t id = RValue_toInt32(args[0]);
    DsMapEntry** mapPtr = dsMapGet(id);
    if (mapPtr == nullptr) return RValue_makeUndefined();

    char* prevKey = RValue_toString(args[1]);
    ptrdiff_t idx = shgeti(*mapPtr, prevKey);
    free(prevKey);
    if (0 > idx || idx + 1 >= shlen(*mapPtr)) return RValue_makeUndefined();
    return RValue_makeOwnedString(strdup((*mapPtr)[idx + 1].key));
}

static RValue builtinDsMapSize([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    int32_t id = RValue_toInt32(args[0]);
    DsMapEntry** mapPtr = dsMapGet(id);
    if (mapPtr == nullptr) return RValue_makeReal(0.0);
    return RValue_makeReal((double) shlen(*mapPtr));
}

static RValue builtinDsMapDestroy([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeUndefined();
    int32_t id = RValue_toInt32(args[0]);
    DsMapEntry** mapPtr = dsMapGet(id);
    if (mapPtr == nullptr) return RValue_makeUndefined();
    // Free all keys and values
    for (ptrdiff_t i = 0; shlen(*mapPtr) > i; i++) {
        free((*mapPtr)[i].key);
        RValue_free(&(*mapPtr)[i].value);
    }
    shfree(*mapPtr);
    *mapPtr = nullptr;
    return RValue_makeUndefined();
}

// ===[ DS_LIST STUBS ]===

static RValue builtinDsListCreate(VMContext* ctx, [[maybe_unused]] RValue* args, [[maybe_unused]] int32_t argCount) {
    logStubbedFunction(ctx, "ds_list_create");
    return RValue_makeReal(0.0);
}

static RValue builtinDsListAdd(VMContext* ctx, [[maybe_unused]] RValue* args, [[maybe_unused]] int32_t argCount) {
    logStubbedFunction(ctx, "ds_list_add");
    return RValue_makeUndefined();
}

static RValue builtinDsListSize(VMContext* ctx, [[maybe_unused]] RValue* args, [[maybe_unused]] int32_t argCount) {
    logStubbedFunction(ctx, "ds_list_size");
    return RValue_makeReal(0.0);
}

// ===[ ARRAY FUNCTIONS ]===

// Scans an ArrayMapEntry hashmap for the highest array index stored under the given varID.
// Returns maxIndex + 1 (the array length), or 0 if no entries found.
static int32_t arrayMapLengthForVar(ArrayMapEntry* map, int32_t varID) {
    int32_t maxIndex = -1;
    ptrdiff_t count = hmlen(map);
    for (ptrdiff_t i = 0; count > i; i++) {
        int64_t key = map[i].key;
        int32_t keyVarID = (int32_t) (key >> 32);
        if (keyVarID == varID) {
            int32_t arrayIndex = (int32_t) (key & 0xFFFFFFFF);
            if (arrayIndex > maxIndex) maxIndex = arrayIndex;
        }
    }
    return maxIndex + 1;
}

static RValue builtinArrayLengthId([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    // The argument should be an RVALUE_ARRAY_REF containing the source varID
    if (args[0].type != RVALUE_ARRAY_REF) return RValue_makeReal(0.0);
    int32_t varID = args[0].int32;
    // Check self array map on current instance
    Instance* inst = ctx->currentInstance;
    if (inst != nullptr) {
        int32_t len = arrayMapLengthForVar(inst->selfArrayMap, varID);
        if (0 < len) return RValue_makeReal((double) len);
    }
    // Check global array map
    int32_t len = arrayMapLengthForVar(ctx->globalArrayMap, varID);
    if (0 < len) return RValue_makeReal((double) len);
    // Check local array map
    len = arrayMapLengthForVar(ctx->localArrayMap, varID);
    if (0 < len) return RValue_makeReal((double) len);
    return RValue_makeReal(0.0);
}

// ===[ STUBBED FUNCTIONS ]===

#define STUB_RETURN_ZERO(name) \
    static RValue builtin_##name([[maybe_unused]] VMContext* ctx, [[maybe_unused]] RValue* args, [[maybe_unused]] int32_t argCount) { \
        logStubbedFunction(ctx, #name); \
        return RValue_makeReal(0.0); \
    }

#define STUB_RETURN_UNDEFINED(name) \
    static RValue builtin_##name([[maybe_unused]] VMContext* ctx, [[maybe_unused]] RValue* args, [[maybe_unused]] int32_t argCount) { \
        logStubbedFunction(ctx, #name); \
        return RValue_makeUndefined(); \
    }

// Steam stubs
STUB_RETURN_ZERO(steam_initialised)
STUB_RETURN_ZERO(steam_stats_ready)
STUB_RETURN_ZERO(steam_file_exists)
STUB_RETURN_UNDEFINED(steam_file_write)
STUB_RETURN_UNDEFINED(steam_file_read)
STUB_RETURN_ZERO(steam_get_persona_name)

// Audio stubs
STUB_RETURN_UNDEFINED(audio_channel_num)
STUB_RETURN_UNDEFINED(audio_play_sound)
STUB_RETURN_UNDEFINED(audio_stop_sound)
STUB_RETURN_UNDEFINED(audio_stop_all)
STUB_RETURN_ZERO(audio_is_playing)
STUB_RETURN_UNDEFINED(audio_sound_gain)
STUB_RETURN_UNDEFINED(audio_sound_pitch)
STUB_RETURN_ZERO(audio_sound_get_gain)
STUB_RETURN_ZERO(audio_sound_get_pitch)
STUB_RETURN_UNDEFINED(audio_master_gain)
STUB_RETURN_UNDEFINED(audio_group_load)
// Audio groups are always "loaded" since we don't have audio
static RValue builtin_audio_group_is_loaded([[maybe_unused]] VMContext* ctx, [[maybe_unused]] RValue* args, [[maybe_unused]] int32_t argCount) {
    return RValue_makeBool(true);
}
STUB_RETURN_UNDEFINED(audio_play_music)
STUB_RETURN_UNDEFINED(audio_stop_music)
STUB_RETURN_UNDEFINED(audio_music_gain)
STUB_RETURN_ZERO(audio_music_is_playing)
STUB_RETURN_UNDEFINED(audio_create_stream)
STUB_RETURN_UNDEFINED(audio_destroy_stream)
STUB_RETURN_UNDEFINED(audio_pause_sound)
STUB_RETURN_UNDEFINED(audio_resume_sound)
STUB_RETURN_ZERO(audio_sound_get_position)
STUB_RETURN_UNDEFINED(audio_sound_set_position)
STUB_RETURN_ZERO(audio_sound_length)

// Application surface stubs
STUB_RETURN_UNDEFINED(application_surface_enable)
STUB_RETURN_UNDEFINED(application_surface_draw_enable)

// ===[ GMS2 Layer System ]===

static RValue builtin_layer_force_draw_depth([[maybe_unused]] VMContext* ctx, [[maybe_unused]] RValue* args, [[maybe_unused]] int32_t argCount) {
    // Enable/disable depth-based layer drawing. We always draw by depth, so this is effectively a no-op.
    return RValue_makeUndefined();
}

// Helper: stores an array of RValues into the global scratch area and returns an RVALUE_ARRAY_REF.
// Local variables that receive this ref will have their array access redirected to globalArrayMap.
static RValue builtinReturnArray(VMContext* ctx, RValue* values, int32_t count) {
    // Clear old scratch data
    for (int32_t i = 0; i < 4096; i++) {
        int64_t k = ((int64_t) BUILTIN_SCRATCH_VARID << 32) | (uint32_t) i;
        ptrdiff_t idx = hmgeti(ctx->globalArrayMap, k);
        if (0 > idx) break;
        RValue_free(&ctx->globalArrayMap[idx].value);
        hmdel(ctx->globalArrayMap, k);
    }

    // Store new data
    repeat(count, i) {
        int64_t k = ((int64_t) BUILTIN_SCRATCH_VARID << 32) | (uint32_t) i;
        hmput(ctx->globalArrayMap, k, values[i]);
    }
    hmput(ctx->globalArrayVarTracker, BUILTIN_SCRATCH_VARID, 1);

    return RValue_makeArrayRef(BUILTIN_SCRATCH_VARID);
}

static RValue builtin_layer_get_all(VMContext* ctx, [[maybe_unused]] RValue* args, [[maybe_unused]] int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    int32_t count = (int32_t) arrlen(runner->layers);
    if (count == 0) return RValue_makeReal(0.0);

    RValue* values = malloc(count * sizeof(RValue));
    repeat(count, i) {
        values[i] = RValue_makeReal((double) runner->layers[i]->id);
    }
    RValue result = builtinReturnArray(ctx, values, count);
    free(values);
    return result;
}

static RValue builtin_layer_get_name(VMContext* ctx, RValue* args, [[maybe_unused]] int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    int32_t layerId = RValue_toInt32(args[0]);
    RuntimeLayer* layer = Runner_findLayerById(runner, layerId);
    if (layer == nullptr) return RValue_makeOwnedString(strdup(""));
    return RValue_makeOwnedString(strdup(layer->name));
}

static RValue builtin_layer_get_depth(VMContext* ctx, RValue* args, [[maybe_unused]] int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    int32_t layerId = RValue_toInt32(args[0]);
    RuntimeLayer* layer = Runner_findLayerById(runner, layerId);
    if (layer == nullptr) return RValue_makeReal(0.0);
    return RValue_makeReal((double) layer->depth);
}

static RValue builtin_layer_depth(VMContext* ctx, RValue* args, [[maybe_unused]] int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    int32_t layerId = RValue_toInt32(args[0]);
    int32_t depth = RValue_toInt32(args[1]);
    RuntimeLayer* layer = Runner_findLayerById(runner, layerId);
    if (layer != nullptr) layer->depth = depth;
    return RValue_makeUndefined();
}

static RValue builtin_layer_create(VMContext* ctx, RValue* args, int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    int32_t depth = RValue_toInt32(args[0]);
    const char* name = (argCount > 1) ? RValue_toString(args[1]) : "";

    RuntimeLayer* layer = calloc(1, sizeof(RuntimeLayer));
    layer->id = runner->nextLayerId++;
    layer->name = strdup(name);
    layer->depth = depth;
    layer->x = 0.0f;
    layer->y = 0.0f;
    layer->hSpeed = 0.0f;
    layer->vSpeed = 0.0f;
    layer->visible = true;
    layer->type = LAYER_TYPE_BACKGROUND;
    layer->bgElements = nullptr;
    arrput(runner->layers, layer);

    return RValue_makeReal((double) layer->id);
}

static RValue builtin_layer_destroy(VMContext* ctx, RValue* args, [[maybe_unused]] int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    int32_t layerId = RValue_toInt32(args[0]);
    int32_t count = (int32_t) arrlen(runner->layers);
    repeat(count, i) {
        if (runner->layers[i]->id == layerId) {
            RuntimeLayer* layer = runner->layers[i];
            arrfree(layer->bgElements);
            free(layer->name);
            free(layer);
            arrdel(runner->layers, i);
            break;
        }
    }
    return RValue_makeUndefined();
}

static RValue builtin_layer_x(VMContext* ctx, RValue* args, [[maybe_unused]] int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    int32_t layerId = RValue_toInt32(args[0]);
    RuntimeLayer* layer = Runner_findLayerById(runner, layerId);
    if (layer != nullptr) layer->x = (float) RValue_toReal(args[1]);
    return RValue_makeUndefined();
}

static RValue builtin_layer_y(VMContext* ctx, RValue* args, [[maybe_unused]] int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    int32_t layerId = RValue_toInt32(args[0]);
    RuntimeLayer* layer = Runner_findLayerById(runner, layerId);
    if (layer != nullptr) layer->y = (float) RValue_toReal(args[1]);
    return RValue_makeUndefined();
}

static RValue builtin_layer_get_x(VMContext* ctx, RValue* args, [[maybe_unused]] int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    int32_t layerId = RValue_toInt32(args[0]);
    RuntimeLayer* layer = Runner_findLayerById(runner, layerId);
    if (layer == nullptr) return RValue_makeReal(0.0);
    return RValue_makeReal((double) layer->x);
}

static RValue builtin_layer_get_y(VMContext* ctx, RValue* args, [[maybe_unused]] int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    int32_t layerId = RValue_toInt32(args[0]);
    RuntimeLayer* layer = Runner_findLayerById(runner, layerId);
    if (layer == nullptr) return RValue_makeReal(0.0);
    return RValue_makeReal((double) layer->y);
}

static RValue builtin_layer_hspeed(VMContext* ctx, RValue* args, [[maybe_unused]] int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    int32_t layerId = RValue_toInt32(args[0]);
    RuntimeLayer* layer = Runner_findLayerById(runner, layerId);
    if (layer != nullptr) layer->hSpeed = (float) RValue_toReal(args[1]);
    return RValue_makeUndefined();
}

static RValue builtin_layer_vspeed(VMContext* ctx, RValue* args, [[maybe_unused]] int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    int32_t layerId = RValue_toInt32(args[0]);
    RuntimeLayer* layer = Runner_findLayerById(runner, layerId);
    if (layer != nullptr) layer->vSpeed = (float) RValue_toReal(args[1]);
    return RValue_makeUndefined();
}

static RValue builtin_layer_get_hspeed(VMContext* ctx, RValue* args, [[maybe_unused]] int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    int32_t layerId = RValue_toInt32(args[0]);
    RuntimeLayer* layer = Runner_findLayerById(runner, layerId);
    if (layer == nullptr) return RValue_makeReal(0.0);
    return RValue_makeReal((double) layer->hSpeed);
}

static RValue builtin_layer_get_vspeed(VMContext* ctx, RValue* args, [[maybe_unused]] int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    int32_t layerId = RValue_toInt32(args[0]);
    RuntimeLayer* layer = Runner_findLayerById(runner, layerId);
    if (layer == nullptr) return RValue_makeReal(0.0);
    return RValue_makeReal((double) layer->vSpeed);
}

static RValue builtin_layer_get_visible(VMContext* ctx, RValue* args, [[maybe_unused]] int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    int32_t layerId = RValue_toInt32(args[0]);
    RuntimeLayer* layer = Runner_findLayerById(runner, layerId);
    if (layer == nullptr) return RValue_makeBool(false);
    return RValue_makeBool(layer->visible);
}

static RValue builtin_layer_set_visible(VMContext* ctx, RValue* args, [[maybe_unused]] int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    int32_t layerId = RValue_toInt32(args[0]);
    bool visible = RValue_toInt32(args[1]) != 0;
    RuntimeLayer* layer = Runner_findLayerById(runner, layerId);
    if (layer != nullptr) layer->visible = visible;
    return RValue_makeUndefined();
}

// ===[ Layer Background Element Functions ]===

static RValue builtin_layer_background_create(VMContext* ctx, RValue* args, [[maybe_unused]] int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    int32_t layerId = RValue_toInt32(args[0]);
    int32_t spriteIndex = RValue_toInt32(args[1]);
    RuntimeLayer* layer = Runner_findLayerById(runner, layerId);
    if (layer == nullptr) return RValue_makeReal(-1.0);

    RuntimeLayerBgElement elem = {0};
    elem.id = runner->nextElementId++;
    elem.layerId = layerId;
    elem.visible = true;
    elem.foreground = false;
    elem.spriteIndex = spriteIndex;
    elem.hTiled = false;
    elem.vTiled = false;
    elem.stretch = false;
    elem.color = 0xFFFFFFFF;
    elem.alpha = 1.0f;
    elem.xScale = 1.0f;
    elem.yScale = 1.0f;
    arrput(layer->bgElements, elem);

    return RValue_makeReal((double) elem.id);
}

static RValue builtin_layer_background_visible(VMContext* ctx, RValue* args, [[maybe_unused]] int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    int32_t elemId = RValue_toInt32(args[0]);
    bool visible = RValue_toInt32(args[1]) != 0;
    RuntimeLayerBgElement* elem = Runner_findBgElementById(runner, elemId);
    if (elem != nullptr) elem->visible = visible;
    return RValue_makeUndefined();
}

static RValue builtin_layer_background_htiled(VMContext* ctx, RValue* args, [[maybe_unused]] int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    int32_t elemId = RValue_toInt32(args[0]);
    RuntimeLayerBgElement* elem = Runner_findBgElementById(runner, elemId);
    if (elem != nullptr) elem->hTiled = RValue_toInt32(args[1]) != 0;
    return RValue_makeUndefined();
}

static RValue builtin_layer_background_vtiled(VMContext* ctx, RValue* args, [[maybe_unused]] int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    int32_t elemId = RValue_toInt32(args[0]);
    RuntimeLayerBgElement* elem = Runner_findBgElementById(runner, elemId);
    if (elem != nullptr) elem->vTiled = RValue_toInt32(args[1]) != 0;
    return RValue_makeUndefined();
}

static RValue builtin_layer_background_xscale(VMContext* ctx, RValue* args, [[maybe_unused]] int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    int32_t elemId = RValue_toInt32(args[0]);
    RuntimeLayerBgElement* elem = Runner_findBgElementById(runner, elemId);
    if (elem != nullptr) elem->xScale = (float) RValue_toReal(args[1]);
    return RValue_makeUndefined();
}

static RValue builtin_layer_background_yscale(VMContext* ctx, RValue* args, [[maybe_unused]] int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    int32_t elemId = RValue_toInt32(args[0]);
    RuntimeLayerBgElement* elem = Runner_findBgElementById(runner, elemId);
    if (elem != nullptr) elem->yScale = (float) RValue_toReal(args[1]);
    return RValue_makeUndefined();
}

static RValue builtin_layer_background_stretch(VMContext* ctx, RValue* args, [[maybe_unused]] int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    int32_t elemId = RValue_toInt32(args[0]);
    RuntimeLayerBgElement* elem = Runner_findBgElementById(runner, elemId);
    if (elem != nullptr) elem->stretch = RValue_toInt32(args[1]) != 0;
    return RValue_makeUndefined();
}

static RValue builtin_layer_background_blend(VMContext* ctx, RValue* args, [[maybe_unused]] int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    int32_t elemId = RValue_toInt32(args[0]);
    RuntimeLayerBgElement* elem = Runner_findBgElementById(runner, elemId);
    if (elem != nullptr) elem->color = (uint32_t) RValue_toInt32(args[1]);
    return RValue_makeUndefined();
}

static RValue builtin_layer_background_alpha(VMContext* ctx, RValue* args, [[maybe_unused]] int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    int32_t elemId = RValue_toInt32(args[0]);
    RuntimeLayerBgElement* elem = Runner_findBgElementById(runner, elemId);
    if (elem != nullptr) elem->alpha = (float) RValue_toReal(args[1]);
    return RValue_makeUndefined();
}

static RValue builtin_layer_background_change(VMContext* ctx, RValue* args, [[maybe_unused]] int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    int32_t elemId = RValue_toInt32(args[0]);
    int32_t spriteIndex = RValue_toInt32(args[1]);
    RuntimeLayerBgElement* elem = Runner_findBgElementById(runner, elemId);
    if (elem != nullptr) elem->spriteIndex = spriteIndex;
    return RValue_makeUndefined();
}

static RValue builtin_layer_background_exists(VMContext* ctx, RValue* args, [[maybe_unused]] int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    int32_t layerId = RValue_toInt32(args[0]);
    int32_t elemId = RValue_toInt32(args[1]);
    RuntimeLayer* layer = Runner_findLayerById(runner, layerId);
    if (layer == nullptr) return RValue_makeBool(false);
    int32_t elemCount = (int32_t) arrlen(layer->bgElements);
    repeat(elemCount, i) {
        if (layer->bgElements[i].id == elemId) return RValue_makeBool(true);
    }
    return RValue_makeBool(false);
}

// Background element getters
static RValue builtin_layer_background_get_sprite(VMContext* ctx, RValue* args, [[maybe_unused]] int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    RuntimeLayerBgElement* elem = Runner_findBgElementById(runner, RValue_toInt32(args[0]));
    if (elem == nullptr) return RValue_makeReal(-1.0);
    return RValue_makeReal((double) elem->spriteIndex);
}

static RValue builtin_layer_background_get_htiled(VMContext* ctx, RValue* args, [[maybe_unused]] int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    RuntimeLayerBgElement* elem = Runner_findBgElementById(runner, RValue_toInt32(args[0]));
    if (elem == nullptr) return RValue_makeBool(false);
    return RValue_makeBool(elem->hTiled);
}

static RValue builtin_layer_background_get_vtiled(VMContext* ctx, RValue* args, [[maybe_unused]] int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    RuntimeLayerBgElement* elem = Runner_findBgElementById(runner, RValue_toInt32(args[0]));
    if (elem == nullptr) return RValue_makeBool(false);
    return RValue_makeBool(elem->vTiled);
}

static RValue builtin_layer_background_get_stretch(VMContext* ctx, RValue* args, [[maybe_unused]] int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    RuntimeLayerBgElement* elem = Runner_findBgElementById(runner, RValue_toInt32(args[0]));
    if (elem == nullptr) return RValue_makeBool(false);
    return RValue_makeBool(elem->stretch);
}

static RValue builtin_layer_background_get_blend(VMContext* ctx, RValue* args, [[maybe_unused]] int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    RuntimeLayerBgElement* elem = Runner_findBgElementById(runner, RValue_toInt32(args[0]));
    if (elem == nullptr) return RValue_makeReal(0.0);
    return RValue_makeReal((double) (elem->color & 0x00FFFFFF));
}

static RValue builtin_layer_background_get_alpha(VMContext* ctx, RValue* args, [[maybe_unused]] int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    RuntimeLayerBgElement* elem = Runner_findBgElementById(runner, RValue_toInt32(args[0]));
    if (elem == nullptr) return RValue_makeReal(1.0);
    return RValue_makeReal((double) elem->alpha);
}

static RValue builtin_layer_background_get_xscale(VMContext* ctx, RValue* args, [[maybe_unused]] int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    RuntimeLayerBgElement* elem = Runner_findBgElementById(runner, RValue_toInt32(args[0]));
    if (elem == nullptr) return RValue_makeReal(1.0);
    return RValue_makeReal((double) elem->xScale);
}

static RValue builtin_layer_background_get_yscale(VMContext* ctx, RValue* args, [[maybe_unused]] int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    RuntimeLayerBgElement* elem = Runner_findBgElementById(runner, RValue_toInt32(args[0]));
    if (elem == nullptr) return RValue_makeReal(1.0);
    return RValue_makeReal((double) elem->yScale);
}

// layer_background_get_index is an alias for layer_background_get_sprite
static RValue builtin_layer_background_get_index(VMContext* ctx, RValue* args, int32_t argCount) {
    return builtin_layer_background_get_sprite(ctx, args, argCount);
}

// ===[ Layer Element Query Functions ]===

static RValue builtin_layer_get_all_elements(VMContext* ctx, RValue* args, [[maybe_unused]] int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    int32_t layerId = RValue_toInt32(args[0]);
    RuntimeLayer* layer = Runner_findLayerById(runner, layerId);
    if (layer == nullptr) return RValue_makeReal(0.0);

    int32_t elemCount = (int32_t) arrlen(layer->bgElements);
    if (elemCount == 0) return RValue_makeReal(0.0);

    RValue* values = malloc(elemCount * sizeof(RValue));
    repeat(elemCount, i) {
        values[i] = RValue_makeReal((double) layer->bgElements[i].id);
    }
    RValue result = builtinReturnArray(ctx, values, elemCount);
    free(values);
    return result;
}

static RValue builtin_layer_get_element_type(VMContext* ctx, RValue* args, [[maybe_unused]] int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    int32_t elemId = RValue_toInt32(args[0]);
    // Check if it's a background element
    RuntimeLayerBgElement* bgElem = Runner_findBgElementById(runner, elemId);
    if (bgElem != nullptr) return RValue_makeReal(1.0); // 1 = background element
    return RValue_makeReal(0.0); // unknown
}

// Stubs for element types we don't fully implement yet
static RValue builtin_layer_instance_get_instance([[maybe_unused]] VMContext* ctx, RValue* args, [[maybe_unused]] int32_t argCount) {
    return RValue_makeReal((double) RValue_toInt32(args[0])); // element ID is the instance ID for instance elements
}

static RValue builtin_layer_sprite_get_sprite([[maybe_unused]] VMContext* ctx, [[maybe_unused]] RValue* args, [[maybe_unused]] int32_t argCount) {
    return RValue_makeReal(-1.0);
}

static RValue builtin_layer_sprite_destroy([[maybe_unused]] VMContext* ctx, [[maybe_unused]] RValue* args, [[maybe_unused]] int32_t argCount) {
    return RValue_makeUndefined();
}

// Tile element stubs
static RValue builtin_layer_tile_visible([[maybe_unused]] VMContext* ctx, [[maybe_unused]] RValue* args, [[maybe_unused]] int32_t argCount) {
    return RValue_makeUndefined();
}

static RValue builtin_layer_tile_x([[maybe_unused]] VMContext* ctx, [[maybe_unused]] RValue* args, [[maybe_unused]] int32_t argCount) {
    return RValue_makeUndefined();
}

static RValue builtin_layer_tile_y([[maybe_unused]] VMContext* ctx, [[maybe_unused]] RValue* args, [[maybe_unused]] int32_t argCount) {
    return RValue_makeUndefined();
}

static RValue builtin_layer_tile_get_x([[maybe_unused]] VMContext* ctx, [[maybe_unused]] RValue* args, [[maybe_unused]] int32_t argCount) {
    return RValue_makeReal(0.0);
}

static RValue builtin_layer_tile_get_y([[maybe_unused]] VMContext* ctx, [[maybe_unused]] RValue* args, [[maybe_unused]] int32_t argCount) {
    return RValue_makeReal(0.0);
}

static RValue builtin_layer_tile_alpha([[maybe_unused]] VMContext* ctx, [[maybe_unused]] RValue* args, [[maybe_unused]] int32_t argCount) {
    return RValue_makeUndefined();
}

// GMS2 stubs (DELTARUNE uses some GMS2 features)
STUB_RETURN_ZERO(camera_create)
STUB_RETURN_ZERO(vertex_format_begin)
STUB_RETURN_ZERO(vertex_format_add_position_3d)
STUB_RETURN_ZERO(vertex_format_add_normal)
STUB_RETURN_ZERO(vertex_format_add_colour)
STUB_RETURN_ZERO(vertex_format_add_textcoord)
STUB_RETURN_ZERO(vertex_format_end)
STUB_RETURN_ZERO(vertex_create_buffer)

// font_add_sprite_ext: returns a font index (stub as -1)
static RValue builtin_font_add_sprite_ext([[maybe_unused]] VMContext* ctx, [[maybe_unused]] RValue* args, [[maybe_unused]] int32_t argCount) {
    logStubbedFunction(ctx, "font_add_sprite_ext");
    return RValue_makeReal(-1.0);
}

// ===[ JSON DECODE ]===
// Simple JSON parser for flat objects with string/number values (covers DELTARUNE lang files).
// GML json_decode returns a ds_map ID.

// Parse a JSON string literal starting at pos (after the opening quote).
// Returns a malloc'd string. Advances *pos past the closing quote.
static char* jsonParseString(const char* json, size_t* pos) {
    size_t cap = 256;
    size_t len = 0;
    char* buf = malloc(cap);
    while (json[*pos] != '\0' && json[*pos] != '"') {
        if (json[*pos] == '\\' && json[*pos + 1] != '\0') {
            (*pos)++;
            switch (json[*pos]) {
                case '"': buf[len++] = '"'; break;
                case '\\': buf[len++] = '\\'; break;
                case '/': buf[len++] = '/'; break;
                case 'n': buf[len++] = '\n'; break;
                case 'r': buf[len++] = '\r'; break;
                case 't': buf[len++] = '\t'; break;
                case 'b': buf[len++] = '\b'; break;
                case 'f': buf[len++] = '\f'; break;
                default: buf[len++] = json[*pos]; break;
            }
        } else {
            buf[len++] = json[*pos];
        }
        (*pos)++;
        if (cap - 1 <= len) {
            cap *= 2;
            buf = realloc(buf, cap);
        }
    }
    if (json[*pos] == '"') (*pos)++; // skip closing quote
    buf[len] = '\0';
    return buf;
}

static RValue builtin_json_decode([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount || args[0].type != RVALUE_STRING || args[0].string == nullptr) {
        return RValue_makeReal(-1.0);
    }
    const char* json = args[0].string;
    int32_t mapId = dsMapCreate();
    DsMapEntry** mapPtr = dsMapGet(mapId);
    if (mapPtr == nullptr) return RValue_makeReal(-1.0);

    size_t pos = 0;
    size_t jsonLen = strlen(json);

    // Skip to opening brace
    while (jsonLen > pos && json[pos] != '{') pos++;
    if (jsonLen > pos) pos++; // skip '{'

    while (jsonLen > pos) {
        // Skip whitespace and commas
        while (jsonLen > pos && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r' || json[pos] == ',')) pos++;

        if (json[pos] == '}' || json[pos] == '\0') break;

        // Parse key (must be a string)
        if (json[pos] != '"') break;
        pos++; // skip opening quote
        char* key = jsonParseString(json, &pos);

        // Skip colon
        while (jsonLen > pos && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r')) pos++;
        if (json[pos] == ':') pos++;
        while (jsonLen > pos && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r')) pos++;

        // Parse value
        RValue val;
        if (json[pos] == '"') {
            pos++; // skip opening quote
            char* strVal = jsonParseString(json, &pos);
            val = RValue_makeOwnedString(strVal);
        } else if (json[pos] == 't' && strncmp(json + pos, "true", 4) == 0) {
            val = RValue_makeBool(true);
            pos += 4;
        } else if (json[pos] == 'f' && strncmp(json + pos, "false", 5) == 0) {
            val = RValue_makeBool(false);
            pos += 5;
        } else if (json[pos] == 'n' && strncmp(json + pos, "null", 4) == 0) {
            val = RValue_makeUndefined();
            pos += 4;
        } else {
            // Number
            char* end;
            double num = strtod(json + pos, &end);
            val = RValue_makeReal(num);
            pos = (size_t)(end - json);
        }

        shput(*mapPtr, key, val);
    }

    return RValue_makeReal((double) mapId);
}

// @@NewGMLArray@@: GMS2 array creation, returns an empty array
// In GMS2 bytecode, this creates a new array. We just return 0 since our VM handles arrays differently.
static RValue builtin_newGMLArray([[maybe_unused]] VMContext* ctx, [[maybe_unused]] RValue* args, [[maybe_unused]] int32_t argCount) {
    return RValue_makeReal(0.0);
}

// Gamepad stubs
STUB_RETURN_ZERO(gamepad_get_device_count)
STUB_RETURN_ZERO(gamepad_is_connected)
STUB_RETURN_ZERO(gamepad_button_check)
STUB_RETURN_ZERO(gamepad_button_check_pressed)
STUB_RETURN_ZERO(gamepad_button_check_released)
STUB_RETURN_ZERO(gamepad_axis_value)
STUB_RETURN_ZERO(gamepad_get_description)
STUB_RETURN_ZERO(gamepad_button_value)

// INI stubs
STUB_RETURN_UNDEFINED(ini_open)
STUB_RETURN_UNDEFINED(ini_close)
STUB_RETURN_UNDEFINED(ini_write_real)
STUB_RETURN_UNDEFINED(ini_write_string)

static RValue builtinIniReadString(VMContext* ctx, RValue* args, int32_t argCount) {
    logStubbedFunction(ctx, "ini_read_string");
    if (3 > argCount) return RValue_makeOwnedString(strdup(""));
    // Return the default value (3rd arg)
    if (args[2].type == RVALUE_STRING && args[2].string != nullptr) {
        return RValue_makeOwnedString(strdup(args[2].string));
    }
    char* str = RValue_toString(args[2]);
    return RValue_makeOwnedString(str);
}

static RValue builtinIniReadReal(VMContext* ctx, RValue* args, int32_t argCount) {
    logStubbedFunction(ctx, "ini_read_real");
    if (3 > argCount) return RValue_makeReal(0.0);
    return RValue_makeReal(RValue_toReal(args[2]));
}

// ===[ FILE I/O ]===
#define MAX_FILE_HANDLES 32

static FILE* fileHandles[MAX_FILE_HANDLES] = {0};

static int32_t allocFileHandle(FILE* f) {
    for (int32_t i = 0; MAX_FILE_HANDLES > i; i++) {
        if (fileHandles[i] == nullptr) {
            fileHandles[i] = f;
            return i;
        }
    }
    return -1;
}

static FILE* getFileHandle(int32_t handle) {
    if (0 > handle || handle >= MAX_FILE_HANDLES) return nullptr;
    return fileHandles[handle];
}

static void freeFileHandle(int32_t handle) {
    if (0 > handle || handle >= MAX_FILE_HANDLES) return;
    fileHandles[handle] = nullptr;
}

static RValue builtin_file_exists([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeBool(false);
    const char* filename = (args[0].type == RVALUE_STRING ? args[0].string : nullptr);
    if (filename == nullptr) return RValue_makeBool(false);
    FILE* f = fopen(filename, "r");
    if (f != nullptr) {
        fclose(f);
        return RValue_makeBool(true);
    }
    return RValue_makeBool(false);
}

static RValue builtin_file_text_open_read([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(-1.0);
    const char* filename = (args[0].type == RVALUE_STRING ? args[0].string : nullptr);
    if (filename == nullptr) return RValue_makeReal(-1.0);
    FILE* f = fopen(filename, "r");
    if (f == nullptr) return RValue_makeReal(-1.0);
    int32_t handle = allocFileHandle(f);
    if (0 > handle) {
        fclose(f);
        return RValue_makeReal(-1.0);
    }
    return RValue_makeReal((double) handle);
}

static RValue builtin_file_text_open_write([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(-1.0);
    const char* filename = (args[0].type == RVALUE_STRING ? args[0].string : nullptr);
    if (filename == nullptr) return RValue_makeReal(-1.0);
    FILE* f = fopen(filename, "w");
    if (f == nullptr) return RValue_makeReal(-1.0);
    int32_t handle = allocFileHandle(f);
    if (0 > handle) {
        fclose(f);
        return RValue_makeReal(-1.0);
    }
    return RValue_makeReal((double) handle);
}

static RValue builtin_file_text_close([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeUndefined();
    int32_t handle = RValue_toInt32(args[0]);
    FILE* f = getFileHandle(handle);
    if (f != nullptr) {
        fclose(f);
        freeFileHandle(handle);
    }
    return RValue_makeUndefined();
}

static RValue builtin_file_text_eof([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeBool(true);
    int32_t handle = RValue_toInt32(args[0]);
    FILE* f = getFileHandle(handle);
    if (f == nullptr) return RValue_makeBool(true);
    return RValue_makeBool(feof(f) != 0);
}

// file_text_readln: reads the rest of the current line (including the newline) and returns it as a string
// The HTML5 runner includes the newline characters in the returned string
static RValue builtin_file_text_readln([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeOwnedString(strdup(""));
    int32_t handle = RValue_toInt32(args[0]);
    FILE* f = getFileHandle(handle);
    if (f == nullptr) return RValue_makeOwnedString(strdup(""));
    char buf[4096];
    if (fgets(buf, sizeof(buf), f) == nullptr) {
        return RValue_makeOwnedString(strdup(""));
    }
    return RValue_makeOwnedString(strdup(buf));
}

// file_text_read_string: reads until whitespace or newline
static RValue builtinFileTextReadString([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeOwnedString(strdup(""));
    int32_t handle = RValue_toInt32(args[0]);
    FILE* f = getFileHandle(handle);
    if (f == nullptr) return RValue_makeOwnedString(strdup(""));
    // Skip leading whitespace (but not newlines)
    int c;
    while ((c = fgetc(f)) != EOF) {
        if (c == '\n' || c == '\r') {
            ungetc(c, f);
            return RValue_makeOwnedString(strdup(""));
        }
        if (c != ' ' && c != '\t') {
            ungetc(c, f);
            break;
        }
    }
    char buf[4096];
    size_t i = 0;
    while ((c = fgetc(f)) != EOF && c != '\n' && c != '\r' && c != ' ' && c != '\t') {
        if (sizeof(buf) - 1 > i) {
            buf[i++] = (char) c;
        }
    }
    if (c != EOF) ungetc(c, f);
    buf[i] = '\0';
    return RValue_makeOwnedString(strdup(buf));
}

// file_text_read_real: reads a real number from the file
static RValue builtinFileTextReadReal([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    int32_t handle = RValue_toInt32(args[0]);
    FILE* f = getFileHandle(handle);
    if (f == nullptr) return RValue_makeReal(0.0);
    double value = 0.0;
    if (fscanf(f, "%lf", &value) != 1) {
        return RValue_makeReal(0.0);
    }
    return RValue_makeReal(value);
}

static RValue builtin_file_text_write_string([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeUndefined();
    int32_t handle = RValue_toInt32(args[0]);
    FILE* f = getFileHandle(handle);
    if (f == nullptr) return RValue_makeUndefined();
    const char* str = (args[1].type == RVALUE_STRING ? args[1].string : nullptr);
    if (str != nullptr) fputs(str, f);
    return RValue_makeUndefined();
}

static RValue builtin_file_text_writeln([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeUndefined();
    int32_t handle = RValue_toInt32(args[0]);
    FILE* f = getFileHandle(handle);
    if (f == nullptr) return RValue_makeUndefined();
    fputc('\n', f);
    return RValue_makeUndefined();
}

static RValue builtin_file_text_write_real([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeUndefined();
    int32_t handle = RValue_toInt32(args[0]);
    FILE* f = getFileHandle(handle);
    if (f == nullptr) return RValue_makeUndefined();
    double value = RValue_toReal(args[1]);
    fprintf(f, "%.6g", value);
    return RValue_makeUndefined();
}

static RValue builtin_file_delete([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeUndefined();
    const char* filename = (args[0].type == RVALUE_STRING ? args[0].string : nullptr);
    if (filename != nullptr) remove(filename);
    return RValue_makeUndefined();
}

static RValue builtin_file_copy([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeUndefined();
    const char* src = (args[0].type == RVALUE_STRING ? args[0].string : nullptr);
    const char* dst = (args[1].type == RVALUE_STRING ? args[1].string : nullptr);
    if (src == nullptr || dst == nullptr) return RValue_makeUndefined();
    FILE* fsrc = fopen(src, "rb");
    if (fsrc == nullptr) return RValue_makeUndefined();
    FILE* fdst = fopen(dst, "wb");
    if (fdst == nullptr) { fclose(fsrc); return RValue_makeUndefined(); }
    char buf[4096];
    size_t n;
    while (0 < (n = fread(buf, 1, sizeof(buf), fsrc))) {
        fwrite(buf, 1, n, fdst);
    }
    fclose(fsrc);
    fclose(fdst);
    return RValue_makeUndefined();
}

// Keyboard functions
static RValue builtinKeyboardCheck(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeBool(false);
    Runner* runner = (Runner*) ctx->runner;
    int32_t key = RValue_toInt32(args[0]);
    return RValue_makeBool(RunnerKeyboard_check(runner->keyboard, key));
}

static RValue builtinKeyboardCheckPressed(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeBool(false);
    Runner* runner = (Runner*) ctx->runner;
    int32_t key = RValue_toInt32(args[0]);
    return RValue_makeBool(RunnerKeyboard_checkPressed(runner->keyboard, key));
}

static RValue builtinKeyboardCheckReleased(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeBool(false);
    Runner* runner = (Runner*) ctx->runner;
    int32_t key = RValue_toInt32(args[0]);
    return RValue_makeBool(RunnerKeyboard_checkReleased(runner->keyboard, key));
}

static RValue builtinKeyboardCheckDirect(VMContext* ctx, RValue* args, int32_t argCount) {
    // keyboard_check_direct is the same as keyboard_check for our purposes
    return builtinKeyboardCheck(ctx, args, argCount);
}

static RValue builtinKeyboardKeyPress(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeUndefined();
    Runner* runner = (Runner*) ctx->runner;
    int32_t key = RValue_toInt32(args[0]);
    RunnerKeyboard_simulatePress(runner->keyboard, key);
    return RValue_makeUndefined();
}

static RValue builtinKeyboardKeyRelease(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeUndefined();
    Runner* runner = (Runner*) ctx->runner;
    int32_t key = RValue_toInt32(args[0]);
    RunnerKeyboard_simulateRelease(runner->keyboard, key);
    return RValue_makeUndefined();
}

static RValue builtinKeyboardClear(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeUndefined();
    Runner* runner = (Runner*) ctx->runner;
    int32_t key = RValue_toInt32(args[0]);
    RunnerKeyboard_clear(runner->keyboard, key);
    return RValue_makeUndefined();
}

// Joystick stubs
STUB_RETURN_ZERO(joystick_exists)
STUB_RETURN_ZERO(joystick_xpos)
STUB_RETURN_ZERO(joystick_ypos)
STUB_RETURN_ZERO(joystick_direction)
STUB_RETURN_ZERO(joystick_pov)
STUB_RETURN_ZERO(joystick_check_button)

// Window stubs
STUB_RETURN_ZERO(window_get_fullscreen)
STUB_RETURN_UNDEFINED(window_set_fullscreen)
STUB_RETURN_UNDEFINED(window_set_caption)
STUB_RETURN_UNDEFINED(window_set_size)
STUB_RETURN_UNDEFINED(window_center)
static RValue builtinWindowGetWidth(VMContext* ctx, [[maybe_unused]] RValue* args, [[maybe_unused]] int32_t argCount) {
    return RValue_makeReal((double) ctx->dataWin->gen8.defaultWindowWidth);
}

static RValue builtinWindowGetHeight(VMContext* ctx, [[maybe_unused]] RValue* args, [[maybe_unused]] int32_t argCount) {
    return RValue_makeReal((double) ctx->dataWin->gen8.defaultWindowHeight);
}

// Game stubs
STUB_RETURN_UNDEFINED(game_restart)
static RValue builtinGameEnd(VMContext* ctx, [[maybe_unused]] RValue* args, [[maybe_unused]] int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    runner->shouldExit = true;
    return RValue_makeUndefined();
}
STUB_RETURN_UNDEFINED(game_save)
STUB_RETURN_UNDEFINED(game_load)

static RValue builtinInstanceNumber(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    Runner* runner = (Runner*) ctx->runner;
    int32_t objectIndex = RValue_toInt32(args[0]);
    int32_t count = 0;
    int32_t instanceCount = (int32_t) arrlen(runner->instances);
    repeat(instanceCount, i) {
        Instance* inst = runner->instances[i];
        if (inst->active && VM_isObjectOrDescendant(ctx->dataWin, inst->objectIndex, objectIndex)) {
            count++;
        }
    }
    return RValue_makeReal((double) count);
}

static RValue builtinInstanceFind(VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeReal(-4.0); // noone
    Runner* runner = (Runner*) ctx->runner;
    int32_t objectIndex = RValue_toInt32(args[0]);
    int32_t n = RValue_toInt32(args[1]);
    int32_t count = 0;
    int32_t instanceCount = (int32_t) arrlen(runner->instances);
    repeat(instanceCount, i) {
        Instance* inst = runner->instances[i];
        if (inst->active && VM_isObjectOrDescendant(ctx->dataWin, inst->objectIndex, objectIndex)) {
            if (count == n) return RValue_makeReal((double) inst->instanceId);
            count++;
        }
    }
    return RValue_makeReal(-4.0); // noone
}

static RValue builtinInstanceExists(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeBool(false);
    Runner* runner = (Runner*) ctx->runner;
    int32_t id = RValue_toInt32(args[0]);
    bool found = false;
    int32_t instanceCount = (int32_t) arrlen(runner->instances);
    if (id >= 0 && runner->dataWin->objt.count > (uint32_t) id) {
        // Object type index: search for any active instance of this object (or descendants)
        repeat(instanceCount, i) {
            Instance* inst = runner->instances[i];
            if (inst->active && VM_isObjectOrDescendant(ctx->dataWin, inst->objectIndex, id)) {
                found = true;
                break;
            }
        }
    } else {
        // Instance ID: search for a specific instance
        repeat(instanceCount, i) {
            Instance* inst = runner->instances[i];
            if (inst->active && inst->instanceId == (uint32_t) id) {
                found = true;
                break;
            }
        }
    }
    return RValue_makeBool(found);
}

static RValue builtinInstanceDestroy(VMContext* ctx, RValue* args, int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    if (1 > argCount) {
        // No args: destroy the current instance
        if (ctx->currentInstance != nullptr) {
            Runner_destroyInstance(runner, (Instance*) ctx->currentInstance);
        }
        return RValue_makeUndefined();
    }
    // 1 arg: find and destroy matching instances
    int32_t id = RValue_toInt32(args[0]);
    int32_t instanceCount = (int32_t) arrlen(runner->instances);
    if (id >= 0 && runner->dataWin->objt.count > (uint32_t) id) {
        // Object type index: destroy all active instances of this object (or descendants)
        repeat(instanceCount, i) {
            Instance* inst = runner->instances[i];
            if (inst->active && VM_isObjectOrDescendant(ctx->dataWin, inst->objectIndex, id)) {
                Runner_destroyInstance(runner, inst);
            }
        }
    } else {
        // Instance ID: destroy that specific instance
        repeat(instanceCount, i) {
            Instance* inst = runner->instances[i];
            if (inst->active && inst->instanceId == (uint32_t) id) {
                Runner_destroyInstance(runner, inst);
                break;
            }
        }
    }
    return RValue_makeUndefined();
}

static RValue builtinInstanceCreate(VMContext* ctx, RValue* args, int32_t argCount) {
    if (3 > argCount) return RValue_makeReal(0.0);
    Runner* runner = (Runner*) ctx->runner;
    double x = RValue_toReal(args[0]);
    double y = RValue_toReal(args[1]);
    int32_t objectIndex = RValue_toInt32(args[2]);
    if (0 > objectIndex || runner->dataWin->objt.count <= (uint32_t) objectIndex) {
        fprintf(stderr, "VM: instance_create: objectIndex %d out of range\n", objectIndex);
        return RValue_makeReal(0.0);
    }
    Instance* callerInst = (Instance*) ctx->currentInstance;
    Instance* inst = Runner_createInstance(runner, x, y, objectIndex);
    if (callerInst != nullptr && ctx->creatorVarID >= 0 && inst->selfVarCount > (uint32_t) ctx->creatorVarID) {
        inst->selfVars[ctx->creatorVarID] = RValue_makeReal((double) callerInst->instanceId);
    }
    return RValue_makeReal((double) inst->instanceId);
}

static RValue builtinEventInherited(VMContext* ctx, [[maybe_unused]] RValue* args, [[maybe_unused]] int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    Instance* inst = (Instance*) ctx->currentInstance;
    if (inst == nullptr || 0 > ctx->currentEventObjectIndex || 0 > ctx->currentEventType) {
        fprintf(stderr, "VM: event_inherited called with no event context (inst=%p, eventObjIdx=%d, eventType=%d)\n", (void*) inst, ctx->currentEventObjectIndex, ctx->currentEventType);
        return RValue_makeReal(0.0);
    }

    DataWin* dataWin = ctx->dataWin;
    int32_t ownerObjectIndex = ctx->currentEventObjectIndex;
    if ((uint32_t) ownerObjectIndex >= dataWin->objt.count) {
        fprintf(stderr, "VM: event_inherited ownerObjectIndex %d out of range\n", ownerObjectIndex);
        return RValue_makeReal(0.0);
    }

    int32_t parentObjectIndex = dataWin->objt.objects[ownerObjectIndex].parentId;
    if (ctx->traceEventInherited) {
        fprintf(stderr, "VM: [%s] event_inherited owner=%s(%d) parent=%s(%d) event=%s (instanceId=%d)\n", dataWin->objt.objects[inst->objectIndex].name, dataWin->objt.objects[ownerObjectIndex].name, ownerObjectIndex, (0 > parentObjectIndex) ? "none" : dataWin->objt.objects[parentObjectIndex].name, parentObjectIndex, Runner_getEventName(ctx->currentEventType, ctx->currentEventSubtype), inst->instanceId);
    }
    if (0 > parentObjectIndex) return RValue_makeReal(0.0);

    Runner_executeEventFromObject(runner, inst, parentObjectIndex, ctx->currentEventType, ctx->currentEventSubtype);
    return RValue_makeReal(0.0);
}

static RValue builtinEventUser(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    Runner* runner = (Runner*) ctx->runner;
    Instance* inst = (Instance*) ctx->currentInstance;
    if (inst == nullptr) return RValue_makeReal(0.0);

    int32_t subevent = RValue_toInt32(args[0]);
    if (0 > subevent || 15 < subevent) return RValue_makeReal(0.0);

    Runner_executeEvent(runner, inst, EVENT_OTHER, OTHER_USER0 + subevent);
    return RValue_makeReal(0.0);
}

static RValue builtinEventPerform(VMContext* ctx, RValue* args, int32_t argCount) {
    if (2 > argCount) return RValue_makeReal(0.0);
    Runner* runner = (Runner*) ctx->runner;
    Instance* inst = (Instance*) ctx->currentInstance;
    if (inst == nullptr) return RValue_makeReal(0.0);

    int32_t eventType = RValue_toInt32(args[0]);
    int32_t eventSubtype = RValue_toInt32(args[1]);

    Runner_executeEvent(runner, inst, eventType, eventSubtype);
    return RValue_makeReal(0.0);
}

static RValue builtinActionKillObject(VMContext* ctx, [[maybe_unused]] RValue* args, [[maybe_unused]] int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    if (ctx->currentInstance != nullptr) {
        Runner_destroyInstance(runner, (Instance*) ctx->currentInstance);
    }
    return RValue_makeUndefined();
}

static RValue builtinActionCreateObject(VMContext* ctx, RValue* args, int32_t argCount) {
    if (3 > argCount) return RValue_makeUndefined();
    Runner* runner = (Runner*) ctx->runner;
    int32_t objectIndex = RValue_toInt32(args[0]);
    double x = RValue_toReal(args[1]);
    double y = RValue_toReal(args[2]);
    if (0 > objectIndex || runner->dataWin->objt.count <= (uint32_t) objectIndex) {
        fprintf(stderr, "VM: action_create_object: objectIndex %d out of range\n", objectIndex);
        return RValue_makeUndefined();
    }
    Instance* callerInst = (Instance*) ctx->currentInstance;
    if (ctx->actionRelativeFlag && callerInst != nullptr) {
        x += callerInst->x;
        y += callerInst->y;
    }
    Instance* inst = Runner_createInstance(runner, x, y, objectIndex);
    if (callerInst != nullptr && ctx->creatorVarID >= 0 && inst->selfVarCount > (uint32_t) ctx->creatorVarID) {
        inst->selfVars[ctx->creatorVarID] = RValue_makeReal((double) callerInst->instanceId);
    }
    return RValue_makeUndefined();
}

static RValue builtinActionSetRelative(VMContext* ctx, [[maybe_unused]] RValue* args, [[maybe_unused]] int32_t argCount) {
    ctx->actionRelativeFlag = RValue_toInt32(args[0]) != 0;
    return RValue_makeUndefined();
}

static RValue builtinActionMove(VMContext* ctx, [[maybe_unused]] RValue* args, [[maybe_unused]] int32_t argCount) {
    // action_move(direction_string, speed)
    // Direction string is 9 chars of '0'/'1' encoding a 3x3 direction grid:
    //   Pos: 0=UL(225) 1=U(270) 2=UR(315) 3=L(180) 4=STOP 5=R(0) 6=DL(135) 7=D(90) 8=DR(45)
    const char* dirs = RValue_toString(args[0]);
    double spd = RValue_toReal(args[1]);

    static const double angles[] = {225, 270, 315, 180, -1, 0, 135, 90, 45};

    // Collect all enabled directions
    int candidates[9];
    int count = 0;
    for (int i = 0; 9 > i && dirs[i] != '\0'; i++) {
        if (dirs[i] == '1') {
            candidates[count++] = i;
        }
    }

    if (0 == count) return RValue_makeUndefined();

    // Pick one at random
    int pick = candidates[0 == count - 1 ? 0 : rand() % count];

    if (ctx->currentInstance != nullptr) {
        Instance* inst = (Instance*) ctx->currentInstance;
        if (4 == pick) {
            // STOP
            if (ctx->actionRelativeFlag) {
                inst->speed += spd;
            } else {
                inst->speed = 0;
            }
        } else {
            double angle = angles[pick];
            if (ctx->actionRelativeFlag) {
                inst->direction += angle;
                inst->speed += spd;
            } else {
                inst->direction = angle;
                inst->speed = spd;
            }
        }
        Instance_computeComponentsFromSpeed(inst);
    }
    return RValue_makeUndefined();
}

static RValue builtinActionMoveTo(VMContext* ctx, [[maybe_unused]] RValue* args, [[maybe_unused]] int32_t argCount) {
    double ax = RValue_toReal(args[0]);
    double ay = RValue_toReal(args[1]);

    if (ctx->currentInstance != nullptr) {
        Instance* inst = (Instance*) ctx->currentInstance;
        if (ctx->actionRelativeFlag) {
            inst->x += ax;
            inst->y += ay;
        } else {
            inst->x = ax;
            inst->y = ay;
        }
    }
    return RValue_makeUndefined();
}

static RValue builtinActionSetFriction(VMContext* ctx, [[maybe_unused]] RValue* args, [[maybe_unused]] int32_t argCount) {
    double val = RValue_toReal(args[0]);

    if (ctx->currentInstance != nullptr) {
        Instance* inst = (Instance*) ctx->currentInstance;
        if (ctx->actionRelativeFlag) {
            inst->friction += val;
        } else {
            inst->friction = val;
        }
    }
    return RValue_makeUndefined();
}

// Buffer stubs
STUB_RETURN_ZERO(buffer_create)
STUB_RETURN_UNDEFINED(buffer_delete)
STUB_RETURN_UNDEFINED(buffer_write)
STUB_RETURN_ZERO(buffer_read)
STUB_RETURN_UNDEFINED(buffer_seek)
STUB_RETURN_ZERO(buffer_tell)
STUB_RETURN_ZERO(buffer_get_size)
STUB_RETURN_ZERO(buffer_base64_encode)

// PSN stubs
STUB_RETURN_UNDEFINED(psn_init)
STUB_RETURN_ZERO(psn_default_user)
STUB_RETURN_ZERO(psn_get_leaderboard_score)

// Draw functions
static RValue builtin_drawSprite(VMContext* ctx, RValue* args, [[maybe_unused]] int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    if (runner->renderer == nullptr) return RValue_makeUndefined();

    int32_t spriteIndex = RValue_toInt32(args[0]);
    int32_t subimg = RValue_toInt32(args[1]);
    float x = (float) RValue_toReal(args[2]);
    float y = (float) RValue_toReal(args[3]);

    // If subimg < 0, use the current instance's imageIndex
    if (0 > subimg && ctx->currentInstance != nullptr) {
        subimg = (int32_t) ((Instance*) ctx->currentInstance)->imageIndex;
    }

    Renderer_drawSprite(runner->renderer, spriteIndex, subimg, x, y);
    return RValue_makeUndefined();
}

static RValue builtin_drawSpriteExt(VMContext* ctx, RValue* args, [[maybe_unused]] int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    if (runner->renderer == nullptr) return RValue_makeUndefined();

    int32_t spriteIndex = RValue_toInt32(args[0]);
    int32_t subimg = RValue_toInt32(args[1]);
    float x = (float) RValue_toReal(args[2]);
    float y = (float) RValue_toReal(args[3]);
    float xscale = (float) RValue_toReal(args[4]);
    float yscale = (float) RValue_toReal(args[5]);
    float rot = (float) RValue_toReal(args[6]);
    uint32_t color = (uint32_t) RValue_toInt32(args[7]);
    float alpha = (float) RValue_toReal(args[8]);

    if (0 > subimg && ctx->currentInstance != nullptr) {
        subimg = (int32_t) ((Instance*) ctx->currentInstance)->imageIndex;
    }

    Renderer_drawSpriteExt(runner->renderer, spriteIndex, subimg, x, y, xscale, yscale, rot, color, alpha);
    return RValue_makeUndefined();
}

static RValue builtin_drawSpritePart(VMContext* ctx, RValue* args, [[maybe_unused]] int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    if (runner->renderer == nullptr) return RValue_makeUndefined();

    int32_t spriteIndex = RValue_toInt32(args[0]);
    int32_t subimg = RValue_toInt32(args[1]);
    int32_t left = RValue_toInt32(args[2]);
    int32_t top = RValue_toInt32(args[3]);
    int32_t width = RValue_toInt32(args[4]);
    int32_t height = RValue_toInt32(args[5]);
    float x = (float) RValue_toReal(args[6]);
    float y = (float) RValue_toReal(args[7]);

    // If subimg < 0, use the current instance's imageIndex
    if (0 > subimg && ctx->currentInstance != nullptr) {
        subimg = (int32_t) ((Instance*) ctx->currentInstance)->imageIndex;
    }

    Renderer_drawSpritePart(runner->renderer, spriteIndex, subimg, left, top, width, height, x, y);
    return RValue_makeUndefined();
}

// draw_sprite_part_ext(sprite, subimg, left, top, width, height, x, y, xscale, yscale, color, alpha)
static RValue builtinDrawSpritePartExt(VMContext* ctx, RValue* args, [[maybe_unused]] int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    if (runner->renderer == nullptr || 12 > argCount) return RValue_makeUndefined();

    int32_t spriteIndex = RValue_toInt32(args[0]);
    int32_t subimg = RValue_toInt32(args[1]);
    int32_t left = RValue_toInt32(args[2]);
    int32_t top = RValue_toInt32(args[3]);
    int32_t width = RValue_toInt32(args[4]);
    int32_t height = RValue_toInt32(args[5]);
    float x = (float) RValue_toReal(args[6]);
    float y = (float) RValue_toReal(args[7]);
    float xscale = (float) RValue_toReal(args[8]);
    float yscale = (float) RValue_toReal(args[9]);
    uint32_t color = (uint32_t) RValue_toInt32(args[10]);
    float alpha = (float) RValue_toReal(args[11]);

    // If subimg < 0, use the current instance's imageIndex
    if (0 > subimg && ctx->currentInstance != nullptr) {
        subimg = (int32_t) ((Instance*) ctx->currentInstance)->imageIndex;
    }

    DataWin* dw = runner->dataWin;
    int32_t tpagIndex = Renderer_resolveTPAGIndex(dw, spriteIndex, subimg);
    if (0 > tpagIndex) return RValue_makeUndefined();

    TexturePageItem* tpag = &dw->tpag.items[tpagIndex];

    // Clip region to TPAG bounds (same logic as Renderer_drawSpritePart)
    if (left < tpag->targetX) {
        int32_t off = tpag->targetX - left;
        x += (float) off * xscale;
        width -= off;
        left = 0;
    } else {
        left -= tpag->targetX;
    }

    if (top < tpag->targetY) {
        int32_t off = tpag->targetY - top;
        y += (float) off * yscale;
        height -= off;
        top = 0;
    } else {
        top -= tpag->targetY;
    }

    if (width > tpag->sourceWidth - left) width = tpag->sourceWidth - left;
    if (height > tpag->sourceHeight - top) height = tpag->sourceHeight - top;
    if (0 >= width || 0 >= height) return RValue_makeUndefined();

    runner->renderer->vtable->drawSpritePart(runner->renderer, tpagIndex, left, top, width, height, x, y, xscale, yscale, color, alpha);
    return RValue_makeUndefined();
}

static RValue builtin_drawRectangle(VMContext* ctx, RValue* args, [[maybe_unused]] int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    if (runner->renderer == nullptr) return RValue_makeUndefined();

    float x1 = (float) RValue_toReal(args[0]);
    float y1 = (float) RValue_toReal(args[1]);
    float x2 = (float) RValue_toReal(args[2]);
    float y2 = (float) RValue_toReal(args[3]);
    bool outline = RValue_toBool(args[4]);

    runner->renderer->vtable->drawRectangle(runner->renderer, x1, y1, x2, y2, runner->renderer->drawColor, runner->renderer->drawAlpha, outline);
    return RValue_makeUndefined();
}

static RValue builtin_drawSetColor(VMContext* ctx, RValue* args, [[maybe_unused]] int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    if (runner->renderer != nullptr) {
        runner->renderer->drawColor = (uint32_t) RValue_toInt32(args[0]);
    }
    return RValue_makeUndefined();
}

static RValue builtin_drawSetAlpha(VMContext* ctx, RValue* args, [[maybe_unused]] int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    if (runner->renderer != nullptr) {
        runner->renderer->drawAlpha = (float) RValue_toReal(args[0]);
    }
    return RValue_makeUndefined();
}

static RValue builtin_drawSetFont(VMContext* ctx, RValue* args, [[maybe_unused]] int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    if (runner->renderer != nullptr) {
        runner->renderer->drawFont = RValue_toInt32(args[0]);
    }
    return RValue_makeUndefined();
}

static RValue builtin_drawSetHalign(VMContext* ctx, RValue* args, [[maybe_unused]] int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    if (runner->renderer != nullptr) {
        runner->renderer->drawHalign = RValue_toInt32(args[0]);
    }
    return RValue_makeUndefined();
}

static RValue builtin_drawSetValign(VMContext* ctx, RValue* args, [[maybe_unused]] int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    if (runner->renderer != nullptr) {
        runner->renderer->drawValign = RValue_toInt32(args[0]);
    }
    return RValue_makeUndefined();
}

static RValue builtin_drawText(VMContext* ctx, RValue* args, [[maybe_unused]] int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    if (runner->renderer == nullptr) return RValue_makeUndefined();

    float x = (float) RValue_toReal(args[0]);
    float y = (float) RValue_toReal(args[1]);
    char* str = RValue_toString(args[2]);

    runner->renderer->vtable->drawText(runner->renderer, str, x, y, 1.0f, 1.0f, 0.0f);
    free(str);
    return RValue_makeUndefined();
}

static RValue builtin_drawTextTransformed(VMContext* ctx, RValue* args, [[maybe_unused]] int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    if (runner->renderer == nullptr) return RValue_makeUndefined();

    float x = (float) RValue_toReal(args[0]);
    float y = (float) RValue_toReal(args[1]);
    char* str = RValue_toString(args[2]);
    float xscale = (float) RValue_toReal(args[3]);
    float yscale = (float) RValue_toReal(args[4]);
    float angle = (float) RValue_toReal(args[5]);

    runner->renderer->vtable->drawText(runner->renderer, str, x, y, xscale, yscale, angle);
    free(str);
    return RValue_makeUndefined();
}
STUB_RETURN_UNDEFINED(draw_text_ext)
STUB_RETURN_UNDEFINED(draw_text_ext_transformed)
STUB_RETURN_UNDEFINED(draw_surface)
STUB_RETURN_UNDEFINED(draw_surface_ext)
static RValue builtin_drawBackground(VMContext* ctx, RValue* args, [[maybe_unused]] int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    if (runner->renderer == nullptr || 3 > argCount) return RValue_makeUndefined();

    int32_t bgIndex = RValue_toInt32(args[0]);
    float x = (float) RValue_toReal(args[1]);
    float y = (float) RValue_toReal(args[2]);

    int32_t tpagIndex = Renderer_resolveBackgroundTPAGIndex(runner->dataWin, bgIndex);
    if (0 > tpagIndex) return RValue_makeUndefined();

    runner->renderer->vtable->drawSprite(runner->renderer, tpagIndex, x, y, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0xFFFFFF, runner->renderer->drawAlpha);
    return RValue_makeUndefined();
}

static RValue builtin_drawBackgroundExt(VMContext* ctx, RValue* args, [[maybe_unused]] int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    if (runner->renderer == nullptr || 8 > argCount) return RValue_makeUndefined();

    int32_t bgIndex = RValue_toInt32(args[0]);
    float x = (float) RValue_toReal(args[1]);
    float y = (float) RValue_toReal(args[2]);
    float xscale = (float) RValue_toReal(args[3]);
    float yscale = (float) RValue_toReal(args[4]);
    float rot = (float) RValue_toReal(args[5]);
    uint32_t color = (uint32_t) RValue_toInt32(args[6]);
    float alpha = (float) RValue_toReal(args[7]);

    int32_t tpagIndex = Renderer_resolveBackgroundTPAGIndex(runner->dataWin, bgIndex);
    if (0 > tpagIndex) return RValue_makeUndefined();

    runner->renderer->vtable->drawSprite(runner->renderer, tpagIndex, x, y, 0.0f, 0.0f, xscale, yscale, rot, color, alpha);
    return RValue_makeUndefined();
}

static RValue builtin_drawBackgroundStretched(VMContext* ctx, RValue* args, [[maybe_unused]] int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    if (runner->renderer == nullptr || 5 > argCount) return RValue_makeUndefined();

    int32_t bgIndex = RValue_toInt32(args[0]);
    float x = (float) RValue_toReal(args[1]);
    float y = (float) RValue_toReal(args[2]);
    float w = (float) RValue_toReal(args[3]);
    float h = (float) RValue_toReal(args[4]);

    int32_t tpagIndex = Renderer_resolveBackgroundTPAGIndex(runner->dataWin, bgIndex);
    if (0 > tpagIndex) return RValue_makeUndefined();

    TexturePageItem* tpag = &runner->dataWin->tpag.items[tpagIndex];
    float xscale = w / (float) tpag->boundingWidth;
    float yscale = h / (float) tpag->boundingHeight;

    runner->renderer->vtable->drawSprite(runner->renderer, tpagIndex, x, y, 0.0f, 0.0f, xscale, yscale, 0.0f, 0xFFFFFF, runner->renderer->drawAlpha);
    return RValue_makeUndefined();
}

static RValue builtin_drawBackgroundPartExt(VMContext* ctx, RValue* args, [[maybe_unused]] int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    if (runner->renderer == nullptr || 11 > argCount) return RValue_makeUndefined();

    int32_t bgIndex = RValue_toInt32(args[0]);
    int32_t left = RValue_toInt32(args[1]);
    int32_t top = RValue_toInt32(args[2]);
    int32_t width = RValue_toInt32(args[3]);
    int32_t height = RValue_toInt32(args[4]);
    float x = (float) RValue_toReal(args[5]);
    float y = (float) RValue_toReal(args[6]);
    float xscale = (float) RValue_toReal(args[7]);
    float yscale = (float) RValue_toReal(args[8]);
    uint32_t color = (uint32_t) RValue_toInt32(args[9]);
    float alpha = (float) RValue_toReal(args[10]);

    int32_t tpagIndex = Renderer_resolveBackgroundTPAGIndex(runner->dataWin, bgIndex);
    if (0 > tpagIndex) return RValue_makeUndefined();

    runner->renderer->vtable->drawSpritePart(runner->renderer, tpagIndex, left, top, width, height, x, y, xscale, yscale, color, alpha);
    return RValue_makeUndefined();
}

static RValue builtinBackgroundGetWidth(VMContext* ctx, RValue* args, [[maybe_unused]] int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    int32_t bgIndex = RValue_toInt32(args[0]);
    int32_t tpagIndex = Renderer_resolveBackgroundTPAGIndex(ctx->dataWin, bgIndex);
    if (0 > tpagIndex) return RValue_makeReal(0.0);
    return RValue_makeReal((double) ctx->dataWin->tpag.items[tpagIndex].boundingWidth);
}

static RValue builtinBackgroundGetHeight(VMContext* ctx, RValue* args, [[maybe_unused]] int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    int32_t bgIndex = RValue_toInt32(args[0]);
    int32_t tpagIndex = Renderer_resolveBackgroundTPAGIndex(ctx->dataWin, bgIndex);
    if (0 > tpagIndex) return RValue_makeReal(0.0);
    return RValue_makeReal((double) ctx->dataWin->tpag.items[tpagIndex].boundingHeight);
}

static RValue builtin_draw_self(VMContext* ctx, [[maybe_unused]] RValue* args, [[maybe_unused]] int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    if (runner->renderer != nullptr && ctx->currentInstance != nullptr) {
        Renderer_drawSelf(runner->renderer, (Instance*) ctx->currentInstance);
    }
    return RValue_makeUndefined();
}

STUB_RETURN_UNDEFINED(draw_line)

static RValue builtin_draw_set_colour(VMContext* ctx, RValue* args, [[maybe_unused]] int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    if (runner->renderer != nullptr) {
        runner->renderer->drawColor = (uint32_t) RValue_toInt32(args[0]);
    }
    return RValue_makeUndefined();
}

static RValue builtin_draw_get_colour(VMContext* ctx, [[maybe_unused]] RValue* args, [[maybe_unused]] int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    if (runner->renderer != nullptr) {
        return RValue_makeReal((double) runner->renderer->drawColor);
    }
    return RValue_makeReal(0.0);
}

static RValue builtin_draw_get_color(VMContext* ctx, [[maybe_unused]] RValue* args, [[maybe_unused]] int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    if (runner->renderer != nullptr) {
        return RValue_makeReal((double) runner->renderer->drawColor);
    }
    return RValue_makeReal(0.0);
}

static RValue builtin_draw_get_alpha(VMContext* ctx, [[maybe_unused]] RValue* args, [[maybe_unused]] int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    if (runner->renderer != nullptr) {
        return RValue_makeReal((double) runner->renderer->drawAlpha);
    }
    return RValue_makeReal(0.0);
}

// Surface stubs
STUB_RETURN_ZERO(surface_create)
STUB_RETURN_UNDEFINED(surface_free)
STUB_RETURN_UNDEFINED(surface_set_target)
STUB_RETURN_UNDEFINED(surface_reset_target)
STUB_RETURN_ZERO(surface_exists)
// application_surface is surface ID -1 (sentinel); for it, return the window dimensions
static RValue builtinSurfaceGetWidth(VMContext* ctx, RValue* args, [[maybe_unused]] int32_t argCount) {
    int32_t surfaceId = (int32_t) RValue_toReal(args[0]);
    if (surfaceId == -1) {
        return RValue_makeReal((double) ctx->dataWin->gen8.defaultWindowWidth);
    }
    logStubbedFunction(ctx, "surface_get_width");
    return RValue_makeReal(0.0);
}

static RValue builtinSurfaceGetHeight(VMContext* ctx, RValue* args, [[maybe_unused]] int32_t argCount) {
    int32_t surfaceId = (int32_t) RValue_toReal(args[0]);
    if (surfaceId == -1) {
        return RValue_makeReal((double) ctx->dataWin->gen8.defaultWindowHeight);
    }
    logStubbedFunction(ctx, "surface_get_height");
    return RValue_makeReal(0.0);
}

// Sprite stubs
STUB_RETURN_ZERO(sprite_get_width)
STUB_RETURN_ZERO(sprite_get_height)
STUB_RETURN_ZERO(sprite_get_number)
STUB_RETURN_ZERO(sprite_get_xoffset)
STUB_RETURN_ZERO(sprite_get_yoffset)

// Font/text measurement
static RValue builtin_stringWidth(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    Runner* runner = (Runner*) ctx->runner;
    if (runner == nullptr || runner->renderer == nullptr) return RValue_makeReal(0.0);

    Renderer* renderer = runner->renderer;
    int32_t fontIndex = renderer->drawFont;
    if (0 > fontIndex || renderer->dataWin->font.count <= (uint32_t) fontIndex) return RValue_makeReal(0.0);

    Font* font = &renderer->dataWin->font.fonts[fontIndex];
    const char* str = RValue_toString(args[0]);

    char* processed = TextUtils_preprocessGmlText(str);
    int32_t textLen = (int32_t) strlen(processed);

    // Find the widest line
    float maxWidth = 0;
    int32_t lineStart = 0;
    while (textLen >= lineStart) {
        int32_t lineEnd = lineStart;
        while (textLen > lineEnd && !TextUtils_isNewlineChar(processed[lineEnd])) {
            lineEnd++;
        }
        int32_t lineLen = lineEnd - lineStart;

        float lineWidth = TextUtils_measureLineWidth(font, processed + lineStart, lineLen);
        if (lineWidth > maxWidth) maxWidth = lineWidth;

        if (textLen > lineEnd) {
            lineStart = TextUtils_skipNewline(processed, lineEnd, textLen);
        } else {
            break;
        }
    }

    free(processed);
    return RValue_makeReal((double) (maxWidth * font->scaleX));
}

static RValue builtin_stringHeight(VMContext* ctx, RValue* args, int32_t argCount) {
    if (1 > argCount) return RValue_makeReal(0.0);
    Runner* runner = (Runner*) ctx->runner;
    if (runner == nullptr || runner->renderer == nullptr) return RValue_makeReal(0.0);

    Renderer* renderer = runner->renderer;
    int32_t fontIndex = renderer->drawFont;
    if (0 > fontIndex || renderer->dataWin->font.count <= (uint32_t) fontIndex) return RValue_makeReal(0.0);

    Font* font = &renderer->dataWin->font.fonts[fontIndex];
    const char* str = RValue_toString(args[0]);

    char* processed = TextUtils_preprocessGmlText(str);
    int32_t textLen = (int32_t) strlen(processed);
    int32_t lineCount = TextUtils_countLines(processed, textLen);
    free(processed);

    return RValue_makeReal((double) ((float) lineCount * (float) font->emSize * font->scaleY));
}

STUB_RETURN_ZERO(string_width_ext)
STUB_RETURN_ZERO(string_height_ext)

// Color functions
static RValue builtinMakeColor([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (3 > argCount) return RValue_makeReal(0.0);
    int32_t r = RValue_toInt32(args[0]);
    int32_t g = RValue_toInt32(args[1]);
    int32_t b = RValue_toInt32(args[2]);
    return RValue_makeReal((double) (r | (g << 8) | (b << 16)));
}

static RValue builtinMakeColour(VMContext* ctx, RValue* args, int32_t argCount) {
    return builtinMakeColor(ctx, args, argCount);
}

static RValue builtinMergeColor([[maybe_unused]] VMContext* ctx, RValue* args, int32_t argCount) {
    if (3 > argCount) return RValue_makeReal(0.0);
    int32_t col1 = RValue_toInt32(args[0]);
    int32_t col2 = RValue_toInt32(args[1]);
    double amount = RValue_toReal(args[2]);
    if (0.0 > amount) amount = 0.0;
    if (1.0 < amount) amount = 1.0;
    int32_t r1 = col1 & 0xFF, g1 = (col1 >> 8) & 0xFF, b1 = (col1 >> 16) & 0xFF;
    int32_t r2 = col2 & 0xFF, g2 = (col2 >> 8) & 0xFF, b2 = (col2 >> 16) & 0xFF;
    int32_t r = (int32_t) (r1 + (r2 - r1) * amount);
    int32_t g = (int32_t) (g1 + (g2 - g1) * amount);
    int32_t b = (int32_t) (b1 + (b2 - b1) * amount);
    return RValue_makeReal((double) (r | (g << 8) | (b << 16)));
}

// display_get_width()
static RValue builtinDisplayGetWidth(VMContext* ctx, [[maybe_unused]] RValue* args, [[maybe_unused]] int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    if (runner->renderer == nullptr) return RValue_makeReal(0.0);
    return RValue_makeReal((double) runner->renderer->displayWidth);
}

// display_get_height()
static RValue builtinDisplayGetHeight(VMContext* ctx, [[maybe_unused]] RValue* args, [[maybe_unused]] int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    if (runner->renderer == nullptr) return RValue_makeReal(0.0);
    return RValue_makeReal((double) runner->renderer->displayHeight);
}

// place_meeting(x, y, obj)
static RValue builtinPlaceMeeting(VMContext* ctx, RValue* args, int32_t argCount) {
    if (3 > argCount) return RValue_makeBool(false);

    Runner* runner = (Runner*) ctx->runner;
    if (runner == nullptr) return RValue_makeBool(false);

    Instance* self = (Instance*) ctx->currentInstance;
    if (self == nullptr) return RValue_makeBool(false);

    double newX = RValue_toReal(args[0]);
    double newY = RValue_toReal(args[1]);
    int32_t targetObjIndex = RValue_toInt32(args[2]);

    // Temporarily move self to (newX, newY)
    double oldX = self->x;
    double oldY = self->y;
    self->x = newX;
    self->y = newY;

    bool found = false;
    InstanceBBox bboxSelf = Collision_computeBBox(ctx->dataWin, self);
    if (bboxSelf.valid) {
        int32_t count = (int32_t) arrlen(runner->instances);

        repeat(count, i) {
            Instance* inst = runner->instances[i];
            if (!inst->active) continue;
            if (inst == self) continue;

            if (!Collision_matchesTarget(ctx->dataWin, inst, targetObjIndex)) continue;

            InstanceBBox bboxOther = Collision_computeBBox(ctx->dataWin, inst);
            if (!bboxOther.valid) continue;

            // AABB overlap test
            if (bboxSelf.left >= bboxOther.right || bboxOther.left >= bboxSelf.right ||
                bboxSelf.top >= bboxOther.bottom || bboxOther.top >= bboxSelf.bottom) continue;

            // Precise collision check if either sprite needs it
            Sprite* sprSelf = Collision_getSprite(ctx->dataWin, self);
            Sprite* sprOther = Collision_getSprite(ctx->dataWin, inst);
            bool needsPrecise = (sprSelf != nullptr && sprSelf->sepMasks == 1) || (sprOther != nullptr && sprOther->sepMasks == 1);

            if (needsPrecise) {
                if (!Collision_instancesOverlapPrecise(ctx->dataWin, self, inst, bboxSelf, bboxOther)) continue;
            }

            found = true;
            break;
        }
    }

    // Restore position
    self->x = oldX;
    self->y = oldY;

    return RValue_makeBool(found);
}
// collision_line(x1, y1, x2, y2, obj, prec, notme)
static RValue builtinCollisionLine(VMContext* ctx, RValue* args, int32_t argCount) {
    if (7 > argCount) return RValue_makeReal((double) INSTANCE_NOONE);

    Runner* runner = (Runner*) ctx->runner;
    if (runner == nullptr) return RValue_makeReal((double) INSTANCE_NOONE);

    double lx1 = RValue_toReal(args[0]);
    double ly1 = RValue_toReal(args[1]);
    double lx2 = RValue_toReal(args[2]);
    double ly2 = RValue_toReal(args[3]);
    int32_t targetObjIndex = RValue_toInt32(args[4]);
    int32_t prec = RValue_toInt32(args[5]);
    int32_t notme = RValue_toInt32(args[6]);

    Instance* self = (Instance*) ctx->currentInstance;
    int32_t count = (int32_t) arrlen(runner->instances);

    repeat(count, i) {
        Instance* inst = runner->instances[i];
        if (!inst->active) continue;
        if (notme && inst == self) continue;

        if (!Collision_matchesTarget(ctx->dataWin, inst, targetObjIndex)) continue;

        InstanceBBox bbox = Collision_computeBBox(ctx->dataWin, inst);
        if (!bbox.valid) continue;

        // Fast reject: line's bounding rect vs instance bbox
        double lineLeft   = fmin(lx1, lx2);
        double lineRight  = fmax(lx1, lx2);
        double lineTop    = fmin(ly1, ly2);
        double lineBottom = fmax(ly1, ly2);
        if (bbox.left > lineRight || lineLeft > bbox.right || bbox.top > lineBottom || lineTop > bbox.bottom) continue;

        // Normalize line left-to-right for clipping
        double xl = lx1, yl = ly1, xr = lx2, yr = ly2;
        if (xl > xr) { double tmp = xl; xl = xr; xr = tmp; tmp = yl; yl = yr; yr = tmp; }

        double dx = xr - xl;
        double dy = yr - yl;

        // Clip line to bbox horizontally
        if (fabs(dx) > 0.0001) {
            if (bbox.left > xl) {
                double t = (bbox.left - xl) / dx;
                xl = bbox.left;
                yl = yl + t * dy;
            }
            if (xr > bbox.right) {
                double t = (bbox.right - xl) / (xr - xl);
                yr = yl + t * (yr - yl);
                xr = bbox.right;
            }
        }

        // Y-bounds check after horizontal clipping
        double clippedTop    = fmin(yl, yr);
        double clippedBottom = fmax(yl, yr);
        if (bbox.top > clippedBottom || clippedTop > bbox.bottom) continue;

        // Bbox-only mode: collision confirmed
        if (prec == 0) {
            return RValue_makeReal((double) inst->instanceId);
        }

        // Precise mode: walk line pixel-by-pixel within bbox
        Sprite* spr = Collision_getSprite(ctx->dataWin, inst);
        if (spr == nullptr || spr->sepMasks != 1 || spr->masks == nullptr || spr->maskCount == 0) {
            // No precise mask available, treat as bbox hit
            return RValue_makeReal((double) inst->instanceId);
        }

        // Recompute dx/dy for the clipped segment
        double cdx = xr - xl;
        double cdy = yr - yl;
        bool found = false;

        if (fabs(cdy) >= fabs(cdx)) {
            // Vertical-major: normalize top-to-bottom
            double xt = xl, yt = yl, xb = xr, yb = yr;
            if (yt > yb) { double tmp = xt; xt = xb; xb = tmp; tmp = yt; yt = yb; yb = tmp; }
            double vdx = xb - xt;
            double vdy = yb - yt;

            int32_t startY = (int32_t) fmax(bbox.top, yt);
            int32_t endY   = (int32_t) fmin(bbox.bottom, yb);
            for (int32_t py = startY; endY >= py && !found; py++) {
                double px = (fabs(vdy) > 0.0001) ? xt + ((double) py - yt) * vdx / vdy : xt;
                if (Collision_pointInMask(spr, inst, px + 0.5, (double) py + 0.5)) {
                    found = true;
                }
            }
        } else {
            // Horizontal-major
            int32_t startX = (int32_t) fmax(bbox.left, xl);
            int32_t endX   = (int32_t) fmin(bbox.right, xr);
            for (int32_t px = startX; endX >= px && !found; px++) {
                double py = (fabs(cdx) > 0.0001) ? yl + ((double) px - xl) * cdy / cdx : yl;
                if (Collision_pointInMask(spr, inst, (double) px + 0.5, py + 0.5)) {
                    found = true;
                }
            }
        }

        if (!found) continue;
        return RValue_makeReal((double) inst->instanceId);
    }

    return RValue_makeReal((double) INSTANCE_NOONE);
}

// collision_rectangle(x1, y1, x2, y2, obj, prec, notme)
static RValue builtinCollisionRectangle(VMContext* ctx, RValue* args, int32_t argCount) {
    if (7 > argCount) return RValue_makeReal((double) INSTANCE_NOONE);

    Runner* runner = (Runner*) ctx->runner;
    if (runner == nullptr) return RValue_makeReal((double) INSTANCE_NOONE);

    double x1 = RValue_toReal(args[0]);
    double y1 = RValue_toReal(args[1]);
    double x2 = RValue_toReal(args[2]);
    double y2 = RValue_toReal(args[3]);
    int32_t targetObjIndex = RValue_toInt32(args[4]);
    int32_t prec = RValue_toInt32(args[5]);
    int32_t notme = RValue_toInt32(args[6]);

    // Normalize rect
    if (x1 > x2) { double tmp = x1; x1 = x2; x2 = tmp; }
    if (y1 > y2) { double tmp = y1; y1 = y2; y2 = tmp; }

    Instance* self = (Instance*) ctx->currentInstance;
    int32_t count = (int32_t) arrlen(runner->instances);

    repeat(count, i) {
        Instance* inst = runner->instances[i];
        if (!inst->active) continue;
        if (notme && inst == self) continue;

        if (!Collision_matchesTarget(ctx->dataWin, inst, targetObjIndex)) continue;

        InstanceBBox bbox = Collision_computeBBox(ctx->dataWin, inst);
        if (!bbox.valid) continue;

        // AABB overlap test
        if (x1 >= bbox.right || bbox.left >= x2 || y1 >= bbox.bottom || bbox.top >= y2) continue;

        // Precise check if requested and sprite has precise masks
        if (prec != 0) {
            Sprite* spr = Collision_getSprite(ctx->dataWin, inst);
            if (spr != nullptr && spr->sepMasks == 1 && spr->masks != nullptr && spr->maskCount > 0) {
                // Check if any pixel in the overlap region hits the mask
                double iLeft   = fmax(x1, bbox.left);
                double iRight  = fmin(x2, bbox.right);
                double iTop    = fmax(y1, bbox.top);
                double iBottom = fmin(y2, bbox.bottom);

                bool found = false;
                int32_t startX = (int32_t) floor(iLeft);
                int32_t endX   = (int32_t) ceil(iRight);
                int32_t startY = (int32_t) floor(iTop);
                int32_t endY   = (int32_t) ceil(iBottom);

                for (int32_t py = startY; endY > py && !found; py++) {
                    for (int32_t px = startX; endX > px && !found; px++) {
                        if (Collision_pointInMask(spr, inst, (double) px + 0.5, (double) py + 0.5)) {
                            found = true;
                        }
                    }
                }
                if (!found) continue;
            }
        }

        return RValue_makeReal((double) inst->instanceId);
    }

    return RValue_makeReal((double) INSTANCE_NOONE);
}

// collision_point(x, y, obj, prec, notme)
static RValue builtinCollisionPoint(VMContext* ctx, RValue* args, int32_t argCount) {
    if (5 > argCount) return RValue_makeReal((double) INSTANCE_NOONE);

    Runner* runner = (Runner*) ctx->runner;
    if (runner == nullptr) return RValue_makeReal((double) INSTANCE_NOONE);

    double px = RValue_toReal(args[0]);
    double py = RValue_toReal(args[1]);
    int32_t targetObjIndex = RValue_toInt32(args[2]);
    int32_t prec = RValue_toInt32(args[3]);
    int32_t notme = RValue_toInt32(args[4]);

    Instance* self = (Instance*) ctx->currentInstance;
    int32_t count = (int32_t) arrlen(runner->instances);

    repeat(count, i) {
        Instance* inst = runner->instances[i];
        if (!inst->active) continue;
        if (notme && inst == self) continue;

        if (!Collision_matchesTarget(ctx->dataWin, inst, targetObjIndex)) continue;

        InstanceBBox bbox = Collision_computeBBox(ctx->dataWin, inst);
        if (!bbox.valid) continue;

        // Point-in-AABB test
        if (bbox.left > px || px >= bbox.right || bbox.top > py || py >= bbox.bottom) continue;

        // Precise check if requested
        if (prec != 0) {
            Sprite* spr = Collision_getSprite(ctx->dataWin, inst);
            if (spr != nullptr && spr->sepMasks == 1 && spr->masks != nullptr && spr->maskCount > 0) {
                if (!Collision_pointInMask(spr, inst, px, py)) continue;
            }
        }

        return RValue_makeReal((double) inst->instanceId);
    }

    return RValue_makeReal((double) INSTANCE_NOONE);
}

// instance_position(x, y, obj)
static RValue builtinInstancePosition(VMContext* ctx, RValue* args, int32_t argCount) {
    if (3 > argCount) return RValue_makeReal((double) INSTANCE_NOONE);

    Runner* runner = (Runner*) ctx->runner;
    if (runner == nullptr) return RValue_makeReal((double) INSTANCE_NOONE);

    double px = RValue_toReal(args[0]);
    double py = RValue_toReal(args[1]);
    int32_t targetObjIndex = RValue_toInt32(args[2]);

    int32_t count = (int32_t) arrlen(runner->instances);

    repeat(count, i) {
        Instance* inst = runner->instances[i];
        if (!inst->active) continue;

        if (!Collision_matchesTarget(ctx->dataWin, inst, targetObjIndex)) continue;

        InstanceBBox bbox = Collision_computeBBox(ctx->dataWin, inst);
        if (!bbox.valid) continue;

        // Point-in-AABB test (no precise, no notme)
        if (bbox.left > px || px >= bbox.right || bbox.top > py || py >= bbox.bottom) continue;

        return RValue_makeReal((double) inst->instanceId);
    }

    return RValue_makeReal((double) INSTANCE_NOONE);
}

// Misc stubs
STUB_RETURN_ZERO(get_timer)
static RValue builtinActionSetAlarm(VMContext* ctx, [[maybe_unused]] RValue* args, [[maybe_unused]] int32_t argCount) {
    int32_t steps = RValue_toInt32(args[0]);
    int32_t alarmIndex = RValue_toInt32(args[1]);

    if (0 > alarmIndex || alarmIndex >= GML_ALARM_COUNT) {
        return RValue_makeUndefined();
    }

    if (ctx->currentInstance != nullptr) {
        Instance* inst = (Instance*) ctx->currentInstance;
        Runner* runner = (Runner*) ctx->runner;

        if (shgeti(ctx->alarmsToBeTraced, "*") != -1 || shgeti(ctx->alarmsToBeTraced, runner->dataWin->objt.objects[inst->objectIndex].name) != -1) {
            fprintf(stderr, "VM: [%s] Setting Alarm[%d] = %d (instanceId=%d)\n", runner->dataWin->objt.objects[inst->objectIndex].name, alarmIndex, steps, inst->instanceId);
        }

        inst->alarm[alarmIndex] = steps;
    }

    return RValue_makeUndefined();
}

// ===[ Tile Layer Functions ]===

static TileLayerState* getOrCreateTileLayer(Runner* runner, int32_t depth) {
    ptrdiff_t idx = hmgeti(runner->tileLayerMap, depth);
    if (0 > idx) {
        TileLayerState defaultVal = { .visible = true, .offsetX = 0.0f, .offsetY = 0.0f };
        hmput(runner->tileLayerMap, depth, defaultVal);
        idx = hmgeti(runner->tileLayerMap, depth);
    }
    return &runner->tileLayerMap[idx].value;
}

static RValue builtinTileLayerHide([[maybe_unused]] VMContext* ctx, RValue* args, [[maybe_unused]] int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    int32_t depth = RValue_toInt32(args[0]);
    TileLayerState* layer = getOrCreateTileLayer(runner, depth);
    layer->visible = false;
    return RValue_makeUndefined();
}

static RValue builtinTileLayerShow([[maybe_unused]] VMContext* ctx, RValue* args, [[maybe_unused]] int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    int32_t depth = RValue_toInt32(args[0]);
    TileLayerState* layer = getOrCreateTileLayer(runner, depth);
    layer->visible = true;
    return RValue_makeUndefined();
}

static RValue builtinTileLayerShift([[maybe_unused]] VMContext* ctx, RValue* args, [[maybe_unused]] int32_t argCount) {
    Runner* runner = (Runner*) ctx->runner;
    int32_t depth = RValue_toInt32(args[0]);
    float dx = (float) RValue_toReal(args[1]);
    float dy = (float) RValue_toReal(args[2]);
    TileLayerState* layer = getOrCreateTileLayer(runner, depth);
    layer->offsetX += dx;
    layer->offsetY += dy;
    return RValue_makeUndefined();
}

// ===[ PATH FUNCTIONS ]===

// path_start(path, speed, endaction, absolute) - HTML5: Assign_Path (yyInstance.js:2695-2743)
static RValue builtinPathStart(VMContext* ctx, RValue* args, int32_t argCount) {
    if (4 > argCount) return RValue_makeUndefined();

    Instance* inst = (Instance*) ctx->currentInstance;
    if (inst == nullptr) return RValue_makeUndefined();

    Runner* runner = (Runner*) ctx->runner;
    if (runner == nullptr) return RValue_makeUndefined();

    int32_t pathIdx = RValue_toInt32(args[0]);
    double speed = RValue_toReal(args[1]);
    int32_t endAction = RValue_toInt32(args[2]);
    bool absolute = RValue_toBool(args[3]);

    // Validate path index
    inst->pathIndex = -1;
    if (0 > pathIdx) return RValue_makeUndefined();
    if ((uint32_t) pathIdx >= runner->dataWin->path.count) return RValue_makeUndefined();

    GamePath* path = &runner->dataWin->path.paths[pathIdx];
    if (0.0 >= path->length) return RValue_makeUndefined();

    inst->pathIndex = pathIdx;
    inst->pathSpeed = speed;

    if (inst->pathSpeed >= 0.0) {
        inst->pathPosition = 0.0;
    } else {
        inst->pathPosition = 1.0;
    }

    inst->pathPositionPrevious = inst->pathPosition;
    inst->pathScale = 1.0;
    inst->pathOrientation = 0.0;
    inst->pathEndAction = endAction;

    if (absolute) {
        PathPositionResult startPos = GamePath_getPosition(path, inst->pathSpeed >= 0.0 ? 0.0 : 1.0);
        inst->x = startPos.x;
        inst->y = startPos.y;

        PathPositionResult origin = GamePath_getPosition(path, 0.0);
        inst->pathXStart = origin.x;
        inst->pathYStart = origin.y;
    } else {
        inst->pathXStart = inst->x;
        inst->pathYStart = inst->y;
    }

    return RValue_makeUndefined();
}

// path_end() - HTML5: Assign_Path(-1,...)
static RValue builtinPathEnd(VMContext* ctx, [[maybe_unused]] RValue* args, [[maybe_unused]] int32_t argCount) {
    Instance* inst = (Instance*) ctx->currentInstance;
    if (inst != nullptr) {
        inst->pathIndex = -1;
    }
    return RValue_makeUndefined();
}

// ===[ REGISTRATION ]===

void VMBuiltins_registerAll(void) {
    requireMessage(!initialized, "Attempting to register all VMBuiltins, but it was already registered!");
    initialized = true;

    // Core output
    registerBuiltin("show_debug_message", builtinShowDebugMessage);

    // String functions
    registerBuiltin("string_length", builtinStringLength);
    registerBuiltin("string", builtinString);
    registerBuiltin("string_upper", builtinStringUpper);
    registerBuiltin("string_lower", builtinStringLower);
    registerBuiltin("string_copy", builtinStringCopy);
    registerBuiltin("substr", builtinStringCopy);
    registerBuiltin("string_pos", builtinStringPos);
    registerBuiltin("string_char_at", builtinStringCharAt);
    registerBuiltin("string_delete", builtinStringDelete);
    registerBuiltin("string_insert", builtinStringInsert);
    registerBuiltin("string_replace_all", builtinStringReplaceAll);
    registerBuiltin("string_hash_to_newline", builtinStringHashToNewline);
    registerBuiltin("ord", builtinOrd);
    registerBuiltin("chr", builtinChr);

    // Type functions
    registerBuiltin("real", builtinReal);
    registerBuiltin("is_string", builtinIsString);
    registerBuiltin("is_real", builtinIsReal);
    registerBuiltin("is_undefined", builtinIsUndefined);

    // Math functions
    registerBuiltin("floor", builtinFloor);
    registerBuiltin("ceil", builtinCeil);
    registerBuiltin("round", builtinRound);
    registerBuiltin("abs", builtinAbs);
    registerBuiltin("sign", builtinSign);
    registerBuiltin("max", builtinMax);
    registerBuiltin("min", builtinMin);
    registerBuiltin("power", builtinPower);
    registerBuiltin("sqrt", builtinSqrt);
    registerBuiltin("sqr", builtinSqr);
    registerBuiltin("sin", builtinSin);
    registerBuiltin("cos", builtinCos);
    registerBuiltin("darctan2", builtinDarctan2);
    registerBuiltin("degtorad", builtinDegtorad);
    registerBuiltin("radtodeg", builtinRadtodeg);
    registerBuiltin("clamp", builtinClamp);
    registerBuiltin("lerp", builtinLerp);
    registerBuiltin("point_distance", builtinPointDistance);
    registerBuiltin("point_direction", builtinPointDirection);
    registerBuiltin("distance_to_point", builtinDistanceToPoint);
    registerBuiltin("move_towards_point", builtinMoveTowardsPoint);
    registerBuiltin("lengthdir_x", builtinLengthdir_x);
    registerBuiltin("lengthdir_y", builtinLengthdir_y);

    // Random
    registerBuiltin("random", builtinRandom);
    registerBuiltin("random_range", builtinRandomRange);
    registerBuiltin("irandom", builtinIrandom);
    registerBuiltin("irandom_range", builtinIrandomRange);
    registerBuiltin("choose", builtinChoose);
    registerBuiltin("randomize", builtinRandomize);

    // Room
    registerBuiltin("room_goto_next", builtinRoomGotoNext);
    registerBuiltin("room_goto_previous", builtinRoomGotoPrevious);
    registerBuiltin("room_goto", builtinRoomGoto);
    registerBuiltin("room_next", builtinRoomNext);
    registerBuiltin("room_previous", builtinRoomPrevious);

    // GMS2 camera compatibility
    registerBuiltin("view_get_camera", builtinViewGetCamera);
    registerBuiltin("camera_get_view_x", builtinCameraGetViewX);
    registerBuiltin("camera_get_view_y", builtinCameraGetViewY);
    registerBuiltin("camera_get_view_width", builtinCameraGetViewWidth);
    registerBuiltin("camera_get_view_height", builtinCameraGetViewHeight);

    // Variables
    registerBuiltin("variable_global_exists", builtinVariableGlobalExists);
    registerBuiltin("variable_global_get", builtinVariableGlobalGet);
    registerBuiltin("variable_global_set", builtinVariableGlobalSet);

    // Script
    registerBuiltin("script_execute", builtinScriptExecute);

    // OS
    registerBuiltin("os_get_language", builtinOsGetLanguage);
    registerBuiltin("os_get_region", builtinOsGetRegion);

    // ds_map
    registerBuiltin("ds_map_create", builtinDsMapCreate);
    registerBuiltin("ds_map_add", builtinDsMapAdd);
    registerBuiltin("ds_map_set", builtinDsMapSet);
    registerBuiltin("ds_map_replace", builtinDsMapReplace);
    registerBuiltin("ds_map_find_value", builtinDsMapFindValue);
    registerBuiltin("ds_map_exists", builtinDsMapExists);
    registerBuiltin("ds_map_find_first", builtinDsMapFindFirst);
    registerBuiltin("ds_map_find_next", builtinDsMapFindNext);
    registerBuiltin("ds_map_size", builtinDsMapSize);
    registerBuiltin("ds_map_destroy", builtinDsMapDestroy);

    // ds_list stubs
    registerBuiltin("ds_list_create", builtinDsListCreate);
    registerBuiltin("ds_list_add", builtinDsListAdd);
    registerBuiltin("ds_list_size", builtinDsListSize);

    // Array
    registerBuiltin("array_length_1d", builtinArrayLengthId);

    // Steam stubs
    registerBuiltin("steam_initialised", builtin_steam_initialised);
    registerBuiltin("steam_stats_ready", builtin_steam_stats_ready);
    registerBuiltin("steam_file_exists", builtin_steam_file_exists);
    registerBuiltin("steam_file_write", builtin_steam_file_write);
    registerBuiltin("steam_file_read", builtin_steam_file_read);
    registerBuiltin("steam_get_persona_name", builtin_steam_get_persona_name);

    // Audio stubs
    registerBuiltin("audio_channel_num", builtin_audio_channel_num);
    registerBuiltin("audio_play_sound", builtin_audio_play_sound);
    registerBuiltin("audio_stop_sound", builtin_audio_stop_sound);
    registerBuiltin("audio_stop_all", builtin_audio_stop_all);
    registerBuiltin("audio_is_playing", builtin_audio_is_playing);
    registerBuiltin("audio_sound_gain", builtin_audio_sound_gain);
    registerBuiltin("audio_sound_pitch", builtin_audio_sound_pitch);
    registerBuiltin("audio_sound_get_gain", builtin_audio_sound_get_gain);
    registerBuiltin("audio_sound_get_pitch", builtin_audio_sound_get_pitch);
    registerBuiltin("audio_master_gain", builtin_audio_master_gain);
    registerBuiltin("audio_group_load", builtin_audio_group_load);
    registerBuiltin("audio_group_is_loaded", builtin_audio_group_is_loaded);
    registerBuiltin("audio_play_music", builtin_audio_play_music);
    registerBuiltin("audio_stop_music", builtin_audio_stop_music);
    registerBuiltin("audio_music_gain", builtin_audio_music_gain);
    registerBuiltin("audio_music_is_playing", builtin_audio_music_is_playing);
    registerBuiltin("audio_create_stream", builtin_audio_create_stream);
    registerBuiltin("audio_destroy_stream", builtin_audio_destroy_stream);
    registerBuiltin("audio_pause_sound", builtin_audio_pause_sound);
    registerBuiltin("audio_resume_sound", builtin_audio_resume_sound);
    registerBuiltin("audio_sound_get_position", builtin_audio_sound_get_position);
    registerBuiltin("audio_sound_set_position", builtin_audio_sound_set_position);
    registerBuiltin("audio_sound_length", builtin_audio_sound_length);

    // Application surface
    registerBuiltin("application_surface_enable", builtin_application_surface_enable);
    registerBuiltin("application_surface_draw_enable", builtin_application_surface_draw_enable);

    // GMS2 Layer system
    registerBuiltin("layer_force_draw_depth", builtin_layer_force_draw_depth);
    registerBuiltin("layer_get_all", builtin_layer_get_all);
    registerBuiltin("layer_get_name", builtin_layer_get_name);
    registerBuiltin("layer_get_depth", builtin_layer_get_depth);
    registerBuiltin("layer_depth", builtin_layer_depth);
    registerBuiltin("layer_create", builtin_layer_create);
    registerBuiltin("layer_destroy", builtin_layer_destroy);
    registerBuiltin("layer_x", builtin_layer_x);
    registerBuiltin("layer_y", builtin_layer_y);
    registerBuiltin("layer_get_x", builtin_layer_get_x);
    registerBuiltin("layer_get_y", builtin_layer_get_y);
    registerBuiltin("layer_hspeed", builtin_layer_hspeed);
    registerBuiltin("layer_vspeed", builtin_layer_vspeed);
    registerBuiltin("layer_get_hspeed", builtin_layer_get_hspeed);
    registerBuiltin("layer_get_vspeed", builtin_layer_get_vspeed);
    registerBuiltin("layer_get_visible", builtin_layer_get_visible);
    registerBuiltin("layer_set_visible", builtin_layer_set_visible);
    registerBuiltin("layer_background_create", builtin_layer_background_create);
    registerBuiltin("layer_background_visible", builtin_layer_background_visible);
    registerBuiltin("layer_background_htiled", builtin_layer_background_htiled);
    registerBuiltin("layer_background_vtiled", builtin_layer_background_vtiled);
    registerBuiltin("layer_background_xscale", builtin_layer_background_xscale);
    registerBuiltin("layer_background_yscale", builtin_layer_background_yscale);
    registerBuiltin("layer_background_stretch", builtin_layer_background_stretch);
    registerBuiltin("layer_background_blend", builtin_layer_background_blend);
    registerBuiltin("layer_background_alpha", builtin_layer_background_alpha);
    registerBuiltin("layer_background_change", builtin_layer_background_change);
    registerBuiltin("layer_background_exists", builtin_layer_background_exists);
    registerBuiltin("layer_background_get_sprite", builtin_layer_background_get_sprite);
    registerBuiltin("layer_background_get_index", builtin_layer_background_get_index);
    registerBuiltin("layer_background_get_htiled", builtin_layer_background_get_htiled);
    registerBuiltin("layer_background_get_vtiled", builtin_layer_background_get_vtiled);
    registerBuiltin("layer_background_get_stretch", builtin_layer_background_get_stretch);
    registerBuiltin("layer_background_get_blend", builtin_layer_background_get_blend);
    registerBuiltin("layer_background_get_alpha", builtin_layer_background_get_alpha);
    registerBuiltin("layer_background_get_xscale", builtin_layer_background_get_xscale);
    registerBuiltin("layer_background_get_yscale", builtin_layer_background_get_yscale);
    registerBuiltin("layer_get_all_elements", builtin_layer_get_all_elements);
    registerBuiltin("layer_get_element_type", builtin_layer_get_element_type);
    registerBuiltin("layer_instance_get_instance", builtin_layer_instance_get_instance);
    registerBuiltin("layer_sprite_get_sprite", builtin_layer_sprite_get_sprite);
    registerBuiltin("layer_sprite_destroy", builtin_layer_sprite_destroy);
    registerBuiltin("layer_tile_visible", builtin_layer_tile_visible);
    registerBuiltin("layer_tile_x", builtin_layer_tile_x);
    registerBuiltin("layer_tile_y", builtin_layer_tile_y);
    registerBuiltin("layer_tile_get_x", builtin_layer_tile_get_x);
    registerBuiltin("layer_tile_get_y", builtin_layer_tile_get_y);
    registerBuiltin("layer_tile_alpha", builtin_layer_tile_alpha);

    // GMS2 stubs
    registerBuiltin("camera_create", builtin_camera_create);
    registerBuiltin("vertex_format_begin", builtin_vertex_format_begin);
    registerBuiltin("vertex_format_add_position_3d", builtin_vertex_format_add_position_3d);
    registerBuiltin("vertex_format_add_normal", builtin_vertex_format_add_normal);
    registerBuiltin("vertex_format_add_colour", builtin_vertex_format_add_colour);
    registerBuiltin("vertex_format_add_textcoord", builtin_vertex_format_add_textcoord);
    registerBuiltin("vertex_format_end", builtin_vertex_format_end);
    registerBuiltin("vertex_create_buffer", builtin_vertex_create_buffer);

    // Font
    registerBuiltin("font_add_sprite_ext", builtin_font_add_sprite_ext);

    // JSON
    registerBuiltin("json_decode", builtin_json_decode);

    // GMS2 array creation
    registerBuiltin("@@NewGMLArray@@", builtin_newGMLArray);

    // Gamepad
    registerBuiltin("gamepad_get_device_count", builtin_gamepad_get_device_count);
    registerBuiltin("gamepad_is_connected", builtin_gamepad_is_connected);
    registerBuiltin("gamepad_button_check", builtin_gamepad_button_check);
    registerBuiltin("gamepad_button_check_pressed", builtin_gamepad_button_check_pressed);
    registerBuiltin("gamepad_button_check_released", builtin_gamepad_button_check_released);
    registerBuiltin("gamepad_axis_value", builtin_gamepad_axis_value);
    registerBuiltin("gamepad_get_description", builtin_gamepad_get_description);
    registerBuiltin("gamepad_button_value", builtin_gamepad_button_value);

    // INI
    registerBuiltin("ini_open", builtin_ini_open);
    registerBuiltin("ini_close", builtin_ini_close);
    registerBuiltin("ini_write_real", builtin_ini_write_real);
    registerBuiltin("ini_write_string", builtin_ini_write_string);
    registerBuiltin("ini_read_string", builtinIniReadString);
    registerBuiltin("ini_read_real", builtinIniReadReal);

    // File
    registerBuiltin("file_exists", builtin_file_exists);
    registerBuiltin("file_text_open_write", builtin_file_text_open_write);
    registerBuiltin("file_text_open_read", builtin_file_text_open_read);
    registerBuiltin("file_text_close", builtin_file_text_close);
    registerBuiltin("file_text_write_string", builtin_file_text_write_string);
    registerBuiltin("file_text_writeln", builtin_file_text_writeln);
    registerBuiltin("file_text_write_real", builtin_file_text_write_real);
    registerBuiltin("file_text_eof", builtin_file_text_eof);
    registerBuiltin("file_delete", builtin_file_delete);
    registerBuiltin("file_text_readln", builtin_file_text_readln);
    registerBuiltin("file_text_read_string", builtinFileTextReadString);
    registerBuiltin("file_text_read_real", builtinFileTextReadReal);
    registerBuiltin("file_copy", builtin_file_copy);

    // Keyboard
    registerBuiltin("keyboard_check", builtinKeyboardCheck);
    registerBuiltin("keyboard_check_pressed", builtinKeyboardCheckPressed);
    registerBuiltin("keyboard_check_released", builtinKeyboardCheckReleased);
    registerBuiltin("keyboard_check_direct", builtinKeyboardCheckDirect);
    registerBuiltin("keyboard_key_press", builtinKeyboardKeyPress);
    registerBuiltin("keyboard_key_release", builtinKeyboardKeyRelease);
    registerBuiltin("keyboard_clear", builtinKeyboardClear);

    // Joystick
    registerBuiltin("joystick_exists", builtin_joystick_exists);
    registerBuiltin("joystick_xpos", builtin_joystick_xpos);
    registerBuiltin("joystick_ypos", builtin_joystick_ypos);
    registerBuiltin("joystick_direction", builtin_joystick_direction);
    registerBuiltin("joystick_pov", builtin_joystick_pov);
    registerBuiltin("joystick_check_button", builtin_joystick_check_button);

    // Window
    registerBuiltin("window_get_fullscreen", builtin_window_get_fullscreen);
    registerBuiltin("window_set_fullscreen", builtin_window_set_fullscreen);
    registerBuiltin("window_set_caption", builtin_window_set_caption);
    registerBuiltin("window_set_size", builtin_window_set_size);
    registerBuiltin("window_center", builtin_window_center);
    registerBuiltin("window_get_width", builtinWindowGetWidth);
    registerBuiltin("window_get_height", builtinWindowGetHeight);

    // Game
    registerBuiltin("game_restart", builtin_game_restart);
    registerBuiltin("game_end", builtinGameEnd);
    registerBuiltin("game_save", builtin_game_save);
    registerBuiltin("game_load", builtin_game_load);

    // Instance
    registerBuiltin("instance_exists", builtinInstanceExists);
    registerBuiltin("instance_number", builtinInstanceNumber);
    registerBuiltin("instance_find", builtinInstanceFind);
    registerBuiltin("instance_destroy", builtinInstanceDestroy);
    registerBuiltin("instance_create", builtinInstanceCreate);
    registerBuiltin("action_kill_object", builtinActionKillObject);
    registerBuiltin("action_create_object", builtinActionCreateObject);
    registerBuiltin("action_set_relative", builtinActionSetRelative);
    registerBuiltin("action_move", builtinActionMove);
    registerBuiltin("action_move_to", builtinActionMoveTo);
    registerBuiltin("action_set_friction", builtinActionSetFriction);
    registerBuiltin("event_inherited", builtinEventInherited);
    registerBuiltin("event_user", builtinEventUser);
    registerBuiltin("event_perform", builtinEventPerform);

    // Buffer
    registerBuiltin("buffer_create", builtin_buffer_create);
    registerBuiltin("buffer_delete", builtin_buffer_delete);
    registerBuiltin("buffer_write", builtin_buffer_write);
    registerBuiltin("buffer_read", builtin_buffer_read);
    registerBuiltin("buffer_seek", builtin_buffer_seek);
    registerBuiltin("buffer_tell", builtin_buffer_tell);
    registerBuiltin("buffer_get_size", builtin_buffer_get_size);
    registerBuiltin("buffer_base64_encode", builtin_buffer_base64_encode);

    // PSN
    registerBuiltin("psn_init", builtin_psn_init);
    registerBuiltin("psn_default_user", builtin_psn_default_user);
    registerBuiltin("psn_get_leaderboard_score", builtin_psn_get_leaderboard_score);

    // Draw
    registerBuiltin("draw_sprite", builtin_drawSprite);
    registerBuiltin("draw_sprite_ext", builtin_drawSpriteExt);
    registerBuiltin("draw_sprite_part", builtin_drawSpritePart);
    registerBuiltin("draw_sprite_part_ext", builtinDrawSpritePartExt);
    registerBuiltin("draw_rectangle", builtin_drawRectangle);
    registerBuiltin("draw_set_color", builtin_drawSetColor);
    registerBuiltin("draw_set_alpha", builtin_drawSetAlpha);
    registerBuiltin("draw_set_font", builtin_drawSetFont);
    registerBuiltin("draw_set_halign", builtin_drawSetHalign);
    registerBuiltin("draw_set_valign", builtin_drawSetValign);
    registerBuiltin("draw_text", builtin_drawText);
    registerBuiltin("draw_text_transformed", builtin_drawTextTransformed);
    registerBuiltin("draw_text_ext", builtin_draw_text_ext);
    registerBuiltin("draw_text_ext_transformed", builtin_draw_text_ext_transformed);
    registerBuiltin("draw_surface", builtin_draw_surface);
    registerBuiltin("draw_surface_ext", builtin_draw_surface_ext);
    registerBuiltin("draw_background", builtin_drawBackground);
    registerBuiltin("draw_background_ext", builtin_drawBackgroundExt);
    registerBuiltin("draw_background_stretched", builtin_drawBackgroundStretched);
    registerBuiltin("draw_background_part_ext", builtin_drawBackgroundPartExt);
    registerBuiltin("background_get_width", builtinBackgroundGetWidth);
    registerBuiltin("background_get_height", builtinBackgroundGetHeight);
    registerBuiltin("draw_self", builtin_draw_self);
    registerBuiltin("draw_line", builtin_draw_line);
    registerBuiltin("draw_set_colour", builtin_draw_set_colour);
    registerBuiltin("draw_get_colour", builtin_draw_get_colour);
    registerBuiltin("draw_get_color", builtin_draw_get_color);
    registerBuiltin("draw_get_alpha", builtin_draw_get_alpha);

    // Surface
    registerBuiltin("surface_create", builtin_surface_create);
    registerBuiltin("surface_free", builtin_surface_free);
    registerBuiltin("surface_set_target", builtin_surface_set_target);
    registerBuiltin("surface_reset_target", builtin_surface_reset_target);
    registerBuiltin("surface_exists", builtin_surface_exists);
    registerBuiltin("surface_get_width", builtinSurfaceGetWidth);
    registerBuiltin("surface_get_height", builtinSurfaceGetHeight);

    // Sprite info
    registerBuiltin("sprite_get_width", builtin_sprite_get_width);
    registerBuiltin("sprite_get_height", builtin_sprite_get_height);
    registerBuiltin("sprite_get_number", builtin_sprite_get_number);
    registerBuiltin("sprite_get_xoffset", builtin_sprite_get_xoffset);
    registerBuiltin("sprite_get_yoffset", builtin_sprite_get_yoffset);

    // Text measurement
    registerBuiltin("string_width", builtin_stringWidth);
    registerBuiltin("string_height", builtin_stringHeight);
    registerBuiltin("string_width_ext", builtin_string_width_ext);
    registerBuiltin("string_height_ext", builtin_string_height_ext);

    // Color
    registerBuiltin("make_color_rgb", builtinMakeColor);
    registerBuiltin("make_colour_rgb", builtinMakeColour);
    registerBuiltin("merge_color", builtinMergeColor);
    registerBuiltin("merge_colour", builtinMergeColor);

    // Display
    registerBuiltin("display_get_width", builtinDisplayGetWidth);
    registerBuiltin("display_get_height", builtinDisplayGetHeight);

    // Collision
    registerBuiltin("place_meeting", builtinPlaceMeeting);
    registerBuiltin("collision_rectangle", builtinCollisionRectangle);
    registerBuiltin("collision_line", builtinCollisionLine);
    registerBuiltin("collision_point", builtinCollisionPoint);
    registerBuiltin("instance_position", builtinInstancePosition);

    // Tile layers
    registerBuiltin("tile_layer_hide", builtinTileLayerHide);
    registerBuiltin("tile_layer_show", builtinTileLayerShow);
    registerBuiltin("tile_layer_shift", builtinTileLayerShift);

    // Path
    registerBuiltin("path_start", builtinPathStart);
    registerBuiltin("path_end", builtinPathEnd);

    // Misc
    registerBuiltin("get_timer", builtin_get_timer);
    registerBuiltin("action_set_alarm", builtinActionSetAlarm);
}
