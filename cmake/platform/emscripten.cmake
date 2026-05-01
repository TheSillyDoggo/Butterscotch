# Butterscotch VM/interpreter profiler
option(ENABLE_VM_PROFILER "Enable Butterscotch VM/interpreter profiler" ON)
if(ENABLE_VM_PROFILER)
    add_compile_definitions(ENABLE_VM_PROFILER)
endif()

# Butterscotch VM/interpreter tracing
option(ENABLE_VM_TRACING "Enable Butterscotch VM/interpreter tracing checks" ON)
if(ENABLE_VM_TRACING)
    add_compile_definitions(ENABLE_VM_TRACING)
endif()

# Spatial grid logs
option(ENABLE_SPATIAL_GRID_LOGS "Enable Spatial Grid logs" ON)
if(ENABLE_SPATIAL_GRID_LOGS)
    add_compile_definitions(ENABLE_SPATIAL_GRID_LOGS)
endif()

# VM unknown/stubbed function logs
option(ENABLE_VM_STUB_LOGS "Enable VM unknown/stubbed function logs" ON)
if(ENABLE_VM_STUB_LOGS)
    add_compile_definitions(ENABLE_VM_STUB_LOGS)
endif()

# stb_image
target_include_directories(butterscotch PUBLIC vendor/stb/image)

# stb_vorbis
target_include_directories(butterscotch PUBLIC vendor/stb/vorbis)

target_compile_options(butterscotch PRIVATE
    -sUSE_SDL=2
    -sUSE_SDL_MIXER=2
)

target_link_options(butterscotch PRIVATE
    -sUSE_SDL=2
    -sUSE_SDL_MIXER=2
    -sALLOW_MEMORY_GROWTH

    # build folder
    "--preload-file=${CMAKE_BINARY_DIR}/game@/game"
    "--preload-file=${CMAKE_BINARY_DIR}/game@/lang/lang"
    "--preload-file=${CMAKE_BINARY_DIR}/game@/mus/mus"
)

target_link_libraries(butterscotch m pthread bz2)

target_sources(butterscotch
    PRIVATE 
    src/sdl2/sdl2_renderer.c
    src/sdl2/sdl2_gamepad.c
    src/sdl2/sdl2_file_system.c
    src/sdl2/sdl2_audio_system.c
)

set(CMAKE_EXECUTABLE_SUFFIX ".html")