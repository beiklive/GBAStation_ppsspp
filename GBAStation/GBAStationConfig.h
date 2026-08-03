#pragma once

namespace GBAStation {

namespace Paths {
constexpr const char *Root = "sdmc:/GBAStation";
constexpr const char *Debug = "sdmc:/GBAStation/debug";
constexpr const char *Assets = "sdmc:/GBAStation/PSP/assets";
constexpr const char *Lang = "sdmc:/GBAStation/PSP/lang";
constexpr const char *System = "sdmc:/GBAStation/PSP";
constexpr const char *CoreConfigDir = "sdmc:/GBAStation/config/cores";
constexpr const char *PpssppCoreConfig = "sdmc:/GBAStation/config/cores/ppsspp.jsonc";
constexpr const char *PpssppDataRoot = "sdmc:/GBAStation/PSP";
constexpr const char *SavesRoot = "sdmc:/GBAStation/saves";
constexpr const char *PpssppSaveDataRoot = "sdmc:/GBAStation/saves/PSP";
constexpr const char *StatesRoot = "sdmc:/GBAStation/saves";
constexpr const char *PpssppSaveStates = "sdmc:/GBAStation/saves/PSP";
constexpr const char *BootLog = "sdmc:/GBAStation/debug/ppsspp_stub.log";
constexpr const char *LauncherNro = "sdmc:/switch/GBAStation.nro";
constexpr const char *LauncherNroFallback = "sdmc:/GBAStation/GBAStation.nro";
constexpr const char *DefaultTitleFont = "romfs:/fonts/font.ttf";
constexpr const char *DefaultDescriptionFont = "romfs:/fonts/description.ttf";
}  // namespace Paths

namespace Display {
constexpr int Width = 1280;
constexpr int Height = 720;
constexpr float FontSize = 32.0f;
}  // namespace Display

namespace Ppsspp {
constexpr int SaveStateSlotCount = 4;
}  // namespace Ppsspp

namespace Logging {
constexpr bool Enabled = true;
}  // namespace Logging

}  // namespace GBAStation
