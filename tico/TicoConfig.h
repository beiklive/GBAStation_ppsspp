/// @file TicoConfig.h
/// @brief Minimal hardcoded configuration for tico overlay (PPSSPP)
#pragma once

#include <string>

namespace TicoConfig {
    // Hardcoded test ROM for easy testing
    constexpr const char* TEST_ROM = "sdmc:/tico/roms/psp/game.iso";
    
    // Asset paths
    constexpr const char* FONT_PATH = "romfs:/fonts/font.ttf";
    constexpr const char* SYSTEM_PATH = "sdmc:/tico/system/psp/";
    constexpr const char* SAVES_PATH = "sdmc:/tico/saves/psp/";
    constexpr const char* STATES_PATH = "sdmc:/tico/states/psp/";
    constexpr const char* CONFIG_PATH = "sdmc:/tico/config/cores/ppsspp.jsonc";
    
    // Window settings
    constexpr int WINDOW_WIDTH = 1280;
    constexpr int WINDOW_HEIGHT = 720;
    constexpr float FONT_SIZE = 32.0f;

    // Audio backend configuration
    // true = Use SDL_QueueAudio (Push model)
    // false = Use Mix_HookMusic + RingBuffer (Callback model)
    constexpr bool USE_SDLQUEUEAUDIO = false;
}

// UI Actions for HelpersBar
enum UIActions {
    ACTION_CONFIRM,
    ACTION_BACK,
    ACTION_DETAILS,
    ACTION_MENU,
    ACTION_EDIT,
    ACTION_DELETE
};
