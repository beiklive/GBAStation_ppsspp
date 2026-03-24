cmake_minimum_required(VERSION 3.16)
project(ppsspp_tico LANGUAGES C CXX)

# ============================================================================
# PPSSPP Tico NRO — Standalone NRO with Tico overlay
# Links against the Makefile-built ppsspp_libretro_libnx.a
# ============================================================================

# Required: path to the pre-built core .a and PPSSPP source root
if(NOT PPSSPP_CORE_LIB)
    message(FATAL_ERROR "PPSSPP_CORE_LIB must be set to the path of ppsspp_libretro_libnx.a")
endif()
if(NOT PPSSPP_SOURCE_DIR)
    message(FATAL_ERROR "PPSSPP_SOURCE_DIR must be set to the PPSSPP source root")
endif()

# Local deps (glad, imgui, nlohmann, libretro-common)
set(LOCAL_DEP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../dep")

# ---- GLAD (OpenGL loader) ----
add_library(glad STATIC
    ${LOCAL_DEP_DIR}/glad/src/glad.c
)
target_include_directories(glad PUBLIC
    ${LOCAL_DEP_DIR}/glad/include
)

# ---- rcheevos (RetroAchievements) ----
set(RCHEEVOS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../rcheevos")
add_library(rcheevos STATIC
    ${RCHEEVOS_DIR}/src/rcheevos/alloc.c
    ${RCHEEVOS_DIR}/src/rcheevos/condition.c
    ${RCHEEVOS_DIR}/src/rcheevos/condset.c
    ${RCHEEVOS_DIR}/src/rcheevos/consoleinfo.c
    ${RCHEEVOS_DIR}/src/rcheevos/format.c
    ${RCHEEVOS_DIR}/src/rcheevos/lboard.c
    ${RCHEEVOS_DIR}/src/rcheevos/memref.c
    ${RCHEEVOS_DIR}/src/rcheevos/operand.c
    ${RCHEEVOS_DIR}/src/rcheevos/rc_validate.c
    ${RCHEEVOS_DIR}/src/rcheevos/richpresence.c
    ${RCHEEVOS_DIR}/src/rcheevos/trigger.c
    ${RCHEEVOS_DIR}/src/rcheevos/value.c
    ${RCHEEVOS_DIR}/src/rcheevos/runtime.c
    ${RCHEEVOS_DIR}/src/rcheevos/runtime_progress.c
    ${RCHEEVOS_DIR}/src/rc_client.c
    ${RCHEEVOS_DIR}/src/rc_client_raintegration.c
    ${RCHEEVOS_DIR}/src/rc_compat.c
    ${RCHEEVOS_DIR}/src/rc_util.c
    ${RCHEEVOS_DIR}/src/rapi/rc_api_common.c
    ${RCHEEVOS_DIR}/src/rapi/rc_api_editor.c
    ${RCHEEVOS_DIR}/src/rapi/rc_api_info.c
    ${RCHEEVOS_DIR}/src/rapi/rc_api_runtime.c
    ${RCHEEVOS_DIR}/src/rapi/rc_api_user.c
    ${RCHEEVOS_DIR}/src/rhash/cdreader.c
    ${RCHEEVOS_DIR}/src/rhash/hash.c
    ${RCHEEVOS_DIR}/src/rhash/hash_disc.c
    ${RCHEEVOS_DIR}/src/rhash/hash_rom.c
    ${RCHEEVOS_DIR}/src/rhash/md5.c
    ${RCHEEVOS_DIR}/src/rhash/hash_zip.c
    ${RCHEEVOS_DIR}/src/rhash/hash_encrypted.c
    ${RCHEEVOS_DIR}/src/rhash/aes.c
)
target_include_directories(rcheevos PUBLIC
    ${RCHEEVOS_DIR}/include
)
target_compile_definitions(rcheevos PUBLIC RC_DISABLE_LUA RC_CLIENT_SUPPORTS_HASH)

# ---- STB Image ----
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/stb_image.c "#define STBI_NO_THREAD_LOCALS\n#define STB_IMAGE_IMPLEMENTATION\n#include \"deps/stb/stb_image.h\"\n")
add_library(tico_stb STATIC ${CMAKE_CURRENT_BINARY_DIR}/stb_image.c)
target_include_directories(tico_stb PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# ---- Pre-built PPSSPP libretro static library ----
add_library(ppsspp_libretro_static STATIC IMPORTED)
set_target_properties(ppsspp_libretro_static PROPERTIES
    IMPORTED_LOCATION "${PPSSPP_CORE_LIB}"
)

# ---- Main executable ----
add_executable(ppsspp_tico TicoMain.cpp)
target_link_options(ppsspp_tico PRIVATE "-Wl,--allow-multiple-definition")

target_compile_definitions(ppsspp_tico PRIVATE
    LIBRETRO __LIBRETRO__ HAVE_LIBNX HAVE_OPENGL USE_TICO USE_SDL USE_SDL_AUDIO
    IMGUI_IMPL_OPENGL_LOADER_CUSTOM HAVE_EGL
    IMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS IMGUI_DEFINE_MATH_OPERATORS
    __SWITCH__ RC_CLIENT_SUPPORTS_HASH
)

target_sources(ppsspp_tico PRIVATE
    TicoCore.cpp
    TicoOverlay.cpp
    TicoStubs.cpp
    TicoTranslationManager.cpp
    ${LOCAL_DEP_DIR}/imgui/imgui.cpp
    ${LOCAL_DEP_DIR}/imgui/imgui_draw.cpp
    ${LOCAL_DEP_DIR}/imgui/imgui_tables.cpp
    ${LOCAL_DEP_DIR}/imgui/imgui_widgets.cpp
    ${LOCAL_DEP_DIR}/imgui/backends/imgui_impl_sdl2.cpp
    ${LOCAL_DEP_DIR}/imgui/backends/imgui_impl_opengl3.cpp
)

target_include_directories(ppsspp_tico PRIVATE
    .
    "${PPSSPP_SOURCE_DIR}/libretro"
    "${PPSSPP_SOURCE_DIR}/libretro/libretro-common/include"
    "${LOCAL_DEP_DIR}/nlohmann"
    "${LOCAL_DEP_DIR}/imgui"
    "${LOCAL_DEP_DIR}/imgui/backends"
    "${LOCAL_DEP_DIR}/glad/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../rcheevos/include"
)

find_package(SDL2 REQUIRED)
target_include_directories(ppsspp_tico PRIVATE ${SDL2_INCLUDE_DIRS})

# Mesa EGL libraries
set(MESA_DIR "/mesa-new")
set(MESA_LIBRARIES
    ${MESA_DIR}/src/egl/libEGL.a
    ${MESA_DIR}/src/mapi/es2api/libGLESv2.a
    ${MESA_DIR}/src/mapi/shared-glapi/libglapi.a
    ${MESA_DIR}/src/gallium/drivers/nouveau/libnouveau.a
    ${MESA_DIR}/src/nouveau/codegen/libnouveau_codegen.a
    ${MESA_DIR}/src/gallium/winsys/nouveau/switch/libnouveauwinsys.a
    ${MESA_DIR}/libdrm_nouveau/lib/libdrm_nouveau.a
)

# FFmpeg libraries (built for Switch)
set(FFMPEG_LIB_DIR "${PPSSPP_SOURCE_DIR}/ffmpeg/switch_build/lib")

target_link_libraries(ppsspp_tico PRIVATE
    rcheevos
    ppsspp_libretro_static
    ${FFMPEG_LIB_DIR}/libavformat.a
    ${FFMPEG_LIB_DIR}/libavcodec.a
    ${FFMPEG_LIB_DIR}/libswresample.a
    ${FFMPEG_LIB_DIR}/libswscale.a
    ${FFMPEG_LIB_DIR}/libavutil.a
    glad tico_stb
    -lcurl -lmbedtls -lmbedx509 -lmbedcrypto
    -lSDL2_mixer -lmpg123 -lmodplug -lopusfile -lopus -lvorbisidec -logg
    SDL2
    ${MESA_LIBRARIES}
    -lzstd -lz
)

# ---- ROMFS (fonts & lang & assets) ----
set(ROMFS_DIR "${CMAKE_CURRENT_BINARY_DIR}/romfs")
file(MAKE_DIRECTORY ${ROMFS_DIR})
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/fonts" DESTINATION ${ROMFS_DIR})
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/lang" DESTINATION ${ROMFS_DIR})
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/assets" DESTINATION ${ROMFS_DIR})

# ---- NRO generation ----
nx_generate_nacp(ppsspp_tico.nacp
    NAME "PPSSPP"
    AUTHOR "libretro"
    VERSION "1.0.0"
)
nx_create_nro(ppsspp_tico NACP ppsspp_tico.nacp ROMFS ${ROMFS_DIR})
