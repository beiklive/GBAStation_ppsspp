#include "GBAStationOverlay.h"

#include "GBAStationConfig.h"
#include "GBAStationRetroAchievements.h"
#include "GBAStationTranslationManager.h"
#include "GBAStationUtils.h"
#include "Core/Config.h"
#include "Common/GPU/thin3d.h"
#include "Common/Math/lin/matrix4x4.h"
#include "Common/System/Display.h"
#include "ext/imgui/imgui.h"
#include "ext/imgui/imgui_impl_thin3d.h"
#define NANOSVG_IMPLEMENTATION
#define NANOSVGRAST_IMPLEMENTATION
#include "ext/nanosvg/src/nanosvg.h"
#include "ext/nanosvg/src/nanosvgrast.h"

#include <switch.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <vector>
#include <utility>

namespace GBAStation {
namespace {

constexpr float kOverlayAnimDuration = 0.4f;
constexpr float kQuickMenuWidth = 400.0f;

struct QuickMenuItem {
	const char *labelKey;
	enum class Action {
		Resume,
		SaveState,
		LoadState,
		Cheats,
		VideoSettings,
		CoreSettings,
		Reset,
		ExitGame,
	} action;
};

constexpr QuickMenuItem kQuickMenuItems[] = {
	{"emulator_resume", QuickMenuItem::Action::Resume},
	{"emulator_save_state", QuickMenuItem::Action::SaveState},
	{"emulator_load_state", QuickMenuItem::Action::LoadState},
	{"emulator_cheats", QuickMenuItem::Action::Cheats},
	{"emulator_video_settings", QuickMenuItem::Action::VideoSettings},
	{"emulator_core_settings", QuickMenuItem::Action::CoreSettings},
	{"emulator_reset", QuickMenuItem::Action::Reset},
	{"emulator_exit_game", QuickMenuItem::Action::ExitGame},
};

constexpr int kAnalogNavThreshold = 16000;
constexpr u64 kAnalogNavRepeatMs = 180;
constexpr int kCheatAnalogNavThreshold = 18000;
constexpr u64 kCheatVerticalNavInitialRepeatMs = 280;
constexpr u64 kCheatVerticalNavRepeatMs = 105;
constexpr u64 kCheatHorizontalNavInitialRepeatMs = 320;
constexpr u64 kCheatHorizontalNavRepeatMs = 180;
constexpr int kCheatPageStep = 10;

constexpr DisplaySize kAspectDisplaySizes[] = {
	DisplaySize::Stretch,
	DisplaySize::_4_3,
	DisplaySize::_16_9,
	DisplaySize::Original,
};

bool ReadFileBytes(const char *path, std::vector<unsigned char> *data) {
	data->clear();
	FILE *fp = fopen(path, "rb");
	if (!fp) {
		return false;
	}
	if (fseek(fp, 0, SEEK_END) != 0) {
		fclose(fp);
		return false;
	}
	const long fileSize = ftell(fp);
	if (fileSize <= 0) {
		fclose(fp);
		return false;
	}
	rewind(fp);

	data->resize((size_t)fileSize);
	const size_t readSize = fread(data->data(), 1, data->size(), fp);
	fclose(fp);
	if (readSize != data->size()) {
		data->clear();
		return false;
	}
	return true;
}

uint8_t *LoadImGuiFontData(const char *path, size_t *sizeOut) {
	*sizeOut = 0;
	FILE *fp = fopen(path, "rb");
	if (!fp) {
		return nullptr;
	}

	if (fseek(fp, 0, SEEK_END) != 0) {
		fclose(fp);
		return nullptr;
	}
	const long fileSize = ftell(fp);
	if (fileSize <= 0) {
		fclose(fp);
		return nullptr;
	}
	rewind(fp);

	uint8_t *data = (uint8_t *)ImGui::MemAlloc((size_t)fileSize);
	if (!data) {
		fclose(fp);
		return nullptr;
	}

	const size_t readSize = fread(data, 1, (size_t)fileSize, fp);
	fclose(fp);
	if (readSize != (size_t)fileSize) {
		ImGui::MemFree(data);
		return nullptr;
	}

	*sizeOut = (size_t)fileSize;
	return data;
}

uint8_t *LoadFirstImGuiFontData(const char *const *paths, size_t pathCount, size_t *sizeOut, const char **loadedPathOut) {
	for (size_t i = 0; i < pathCount; ++i) {
		uint8_t *data = LoadImGuiFontData(paths[i], sizeOut);
		if (data) {
			if (loadedPathOut) {
				*loadedPathOut = paths[i];
			}
			return data;
		}
	}

	if (loadedPathOut) {
		*loadedPathOut = nullptr;
	}
	*sizeOut = 0;
	return nullptr;
}

uint8_t *LoadSwitchChineseFontData(size_t *sizeOut, const char **sourceName) {
#ifdef __SWITCH__
	*sizeOut = 0;
	if (sourceName) *sourceName = nullptr;
	if (R_FAILED(plInitialize(PlServiceType_User)))
		return nullptr;

	const struct {
		PlSharedFontType type;
		const char *name;
	} fonts[] = {
		{PlSharedFontType_ChineseSimplified, "ChineseSimplified"},
		{PlSharedFontType_ExtChineseSimplified, "ExtChineseSimplified"},
		{PlSharedFontType_Standard, "Standard"},
	};

	PlFontData sharedFont{};
	const char *loadedName = nullptr;
	for (const auto &candidate : fonts) {
		if (R_SUCCEEDED(plGetSharedFontByType(&sharedFont, candidate.type)) &&
			sharedFont.address && sharedFont.size > 0) {
			loadedName = candidate.name;
			break;
		}
	}
	if (!loadedName) {
		plExit();
		return nullptr;
	}

	auto *data = static_cast<uint8_t *>(std::malloc(sharedFont.size));
	if (!data) {
		plExit();
		return nullptr;
	}
	std::memcpy(data, sharedFont.address, sharedFont.size);
	plExit();
	*sizeOut = sharedFont.size;
	if (sourceName) *sourceName = loadedName;
	return data;
#else
	(void)sizeOut;
	if (sourceName) *sourceName = nullptr;
	return nullptr;
#endif
}

float EaseOutCubic(float t) {
	t = std::clamp(t, 0.0f, 1.0f);
	return 1.0f - std::pow(1.0f - t, 3.0f);
}

void DrawSwitchButtonPrompt(ImDrawList *drawList, ImFont *font, float fontSize, ImVec2 center, float size, const char *symbol, float alpha) {
	const ImU32 fillColor = IM_COL32(220, 220, 220, (int)(255.0f * alpha));
	const ImU32 textColor = IM_COL32(40, 40, 40, (int)(255.0f * alpha));
	drawList->AddCircleFilled(center, size * 0.5f, fillColor, 12);

	const float symbolSize = fontSize * 0.75f;
	const ImVec2 textSize = font->CalcTextSizeA(symbolSize, 10000.0f, 0.0f, symbol);
	drawList->AddText(font, symbolSize, ImVec2(center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f), textColor, symbol);
}

DisplaySize CycleDisplaySize(DisplaySize current, const DisplaySize *sizes, int count, int direction) {
	int index = 0;
	for (int i = 0; i < count; ++i) {
		if (sizes[i] == current) {
			index = i;
			break;
		}
	}

	index = (index + direction) % count;
	if (index < 0) {
		index += count;
	}
	return sizes[index];
}

int GetAvailableIntegerDisplaySizes(DisplaySize *sizes, int sizeCount) {
	if (!sizes || sizeCount <= 0) {
		return 0;
	}

	int count = 0;
	const int maxScale = MaxPpssppIntegerScaleForCurrentDisplay();
	auto addSize = [&](DisplaySize size) {
		if (count < sizeCount) {
			sizes[count++] = size;
		}
	};

	if (maxScale >= 1) addSize(DisplaySize::_1x);
	if (maxScale >= 2) addSize(DisplaySize::_2x);
	if (maxScale >= 3) addSize(DisplaySize::_3x);
	if (maxScale >= 4) addSize(DisplaySize::_4x);
	addSize(DisplaySize::Auto);
	return count;
}

std::string TranslatedDisplayModeLabel(DisplayMode mode) {
	return mode == DisplayMode::Integer ? tr("emulator_integer") : tr("emulator_display");
}

std::string TranslatedDisplaySizeLabel(DisplaySize size) {
	switch (size) {
	case DisplaySize::Stretch: return tr("emulator_stretch");
	case DisplaySize::_4_3: return "4:3";
	case DisplaySize::_16_9: return "16:9";
	case DisplaySize::Original: return tr("emulator_original");
	case DisplaySize::_1x: return "1x";
	case DisplaySize::_2x: return "2x";
	case DisplaySize::_3x: return "3x";
	case DisplaySize::_4x: return "4x";
	case DisplaySize::Auto: return tr("emulator_auto");
	default: return DisplaySizeLabel(size);
	}
}

std::string TruncateToWidth(ImFont *font, float fontSize, const std::string &text, float maxWidth) {
	if (!font || text.empty() || font->CalcTextSizeA(fontSize, 10000.0f, 0.0f, text.c_str()).x <= maxWidth) {
		return text;
	}

	std::string result = text;
	while (result.size() > 4) {
		result.resize(result.size() - 2);
		const std::string candidate = result + "...";
		if (font->CalcTextSizeA(fontSize, 10000.0f, 0.0f, candidate.c_str()).x <= maxWidth) {
			return candidate;
		}
	}
	return "...";
}

u64 CurrentTimeMs() {
	const u64 tickFreq = armGetSystemTickFreq();
	const u64 ticksPerMs = tickFreq / 1000;
	return ticksPerMs > 0 ? armGetSystemTick() / ticksPerMs : 0;
}

bool HeldNavigationTriggered(int heldDir, int &activeDir, u64 &nextRepeatMs, u64 initialRepeatMs, u64 repeatMs, u64 nowMs) {
	if (heldDir == 0) {
		activeDir = 0;
		nextRepeatMs = 0;
		return false;
	}
	if (heldDir != activeDir) {
		activeDir = heldDir;
		nextRepeatMs = nowMs + initialRepeatMs;
		return true;
	}
	if (nowMs >= nextRepeatMs) {
		nextRepeatMs = nowMs + repeatMs;
		return true;
	}
	return false;
}

bool IsSelectableCheatRow(const std::vector<CheatMenuEntry> &cheats, int index) {
	return index >= 0 && index < (int)cheats.size() && cheats[index].toggleable;
}

int FirstSelectableCheatRow(const std::vector<CheatMenuEntry> &cheats) {
	for (int i = 0; i < (int)cheats.size(); ++i) {
		if (IsSelectableCheatRow(cheats, i)) {
			return i;
		}
	}
	return 0;
}

int FindNextSelectableCheatRow(const std::vector<CheatMenuEntry> &cheats, int selection, int direction) {
	const int count = (int)cheats.size();
	if (count <= 0 || direction == 0) {
		return 0;
	}

	int index = std::clamp(selection, 0, count - 1);
	for (int attempts = 0; attempts < count; ++attempts) {
		index = (index + direction + count) % count;
		if (IsSelectableCheatRow(cheats, index)) {
			return index;
		}
	}
	return std::clamp(selection, 0, count - 1);
}

void MoveCheatSelectionWrapped(int &selection, const std::vector<CheatMenuEntry> &cheats, int delta) {
	if (delta == 0) {
		return;
	}
	if (cheats.empty()) {
		selection = 0;
		return;
	}

	if (!IsSelectableCheatRow(cheats, selection)) {
		selection = FindNextSelectableCheatRow(cheats, selection, delta > 0 ? 1 : -1);
		if (IsSelectableCheatRow(cheats, selection)) {
			return;
		}
	}

	const int direction = delta > 0 ? 1 : -1;
	const int steps = std::abs(delta);
	for (int i = 0; i < steps; ++i) {
		const int next = FindNextSelectableCheatRow(cheats, selection, direction);
		if (next == selection) {
			break;
		}
		selection = next;
	}
}

}  // namespace

bool Overlay::Init(Draw::DrawContext *draw, const char *gamePath, LogCallback log) {
	if (log) {
		log_ = std::move(log);
	}
	if (!draw) {
		return false;
	}
	if (ready_) {
		return true;
	}
	TranslationManager::Instance().Init(log_);

	IMGUI_CHECKVERSION();
	context_ = ImGui::CreateContext();
	if (!context_) {
		return false;
	}

	ImGui::SetCurrentContext(context_);
	ImGuiIO &io = ImGui::GetIO();
	io.IniFilename = nullptr;
	io.LogFilename = nullptr;
	io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
	io.DisplaySize = ImVec2((float)std::max(1, g_display.pixel_xres), (float)std::max(1, g_display.pixel_yres));

	ImGui::StyleColorsDark();
	ImGuiStyle &style = ImGui::GetStyle();
	style.WindowRounding = 6.0f;
	style.ChildRounding = 4.0f;
	style.FrameRounding = 4.0f;
	style.GrabRounding = 3.0f;
	style.Colors[ImGuiCol_WindowBg] = ImVec4(0.055f, 0.075f, 0.090f, 0.97f);
	style.Colors[ImGuiCol_ChildBg] = ImVec4(0.070f, 0.105f, 0.120f, 1.00f);
	style.Colors[ImGuiCol_Header] = ImVec4(0.050f, 0.420f, 0.390f, 1.00f);
	style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.070f, 0.520f, 0.480f, 1.00f);
	style.Colors[ImGuiCol_Button] = ImVec4(0.050f, 0.420f, 0.390f, 1.00f);
	style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.070f, 0.520f, 0.480f, 1.00f);

	const char *const titleFontPaths[] = {
		"romfs:/fonts/font.ttf",
		"sdmc:/GBAStation/PSP/fonts/font.ttf",
		"sdmc:/GBAStation/PSP/assets/fonts/font.ttf",
		"sdmc:/GBAStation/PSP/assets/font.ttf",
	};
	const char *const descriptionFontPaths[] = {
		"romfs:/fonts/description.ttf",
		"sdmc:/GBAStation/PSP/fonts/description.ttf",
		"sdmc:/GBAStation/PSP/assets/fonts/description.ttf",
		"sdmc:/GBAStation/PSP/assets/description.ttf",
	};
	size_t titleFontSize = 0;
	size_t descriptionFontSize = 0;
	const char *loadedTitleFont = nullptr;
	const char *loadedDescriptionFont = nullptr;
	// Thin3D now builds the Switch shared-font atlas itself so all three shared
	// faces can be merged before its texture upload.  These ROMFS files remain
	// the fallback for a failed pl:u service and non-Switch builds.
	uint8_t *titleFont = LoadFirstImGuiFontData(titleFontPaths,
		sizeof(titleFontPaths) / sizeof(titleFontPaths[0]), &titleFontSize, &loadedTitleFont);
	uint8_t *descriptionFont = titleFont ? nullptr : LoadFirstImGuiFontData(
		descriptionFontPaths, sizeof(descriptionFontPaths) / sizeof(descriptionFontPaths[0]),
		&descriptionFontSize, &loadedDescriptionFont);
	LogMessage(log_, "GBAStation overlay font title=%s size=%u description=%s size=%u",
		loadedTitleFont ? loadedTitleFont : "<default>", (unsigned)titleFontSize,
		loadedDescriptionFont ? loadedDescriptionFont : "<default>", (unsigned)descriptionFontSize);

	if (!ImGui_ImplThin3d_Init(draw, titleFont, titleFontSize, descriptionFont, descriptionFontSize)) {
		ImGui::DestroyContext(context_);
		context_ = nullptr;
		return false;
	}

	const float loadedFontSize = io.Fonts->Fonts.Size > 0 ? io.Fonts->Fonts[0]->FontSize : 21.0f;
	if (loadedFontSize > 0.0f) {
		io.FontGlobalScale = Display::FontSize / loadedFontSize;
	}
	LogMessage(log_, "GBAStation overlay font scale=%.3f base=%.1f target=%.1f",
		io.FontGlobalScale, loadedFontSize, Display::FontSize);

	title_ = GameTitleFromPath(gamePath);
	displaySettings_ = LoadPpssppDisplaySettings(log_);
	ready_ = true;
	LogMessage(log_, "GBAStation overlay initialized title=%s", title_.c_str());
	return true;
}

Draw::Texture *Overlay::LoadRAIconTexture(Draw::DrawContext *draw) {
	if (raIconTexture_ || !draw) {
		return raIconTexture_;
	}

	const char *const raIconPaths[] = {
		"romfs:/assets/ra.svg",
		"sdmc:/GBAStation/PSP/assets/ra.svg",
	};
	for (const char *path : raIconPaths) {
		std::vector<unsigned char> svgData;
		if (!ReadFileBytes(path, &svgData)) {
			continue;
		}
		svgData.push_back('\0');
		NSVGimage *image = nsvgParse((char *)svgData.data(), "px", 96.0f);
		if (!image || image->width <= 0.0f || image->height <= 0.0f) {
			if (image) {
				nsvgDelete(image);
			}
			continue;
		}

		const float targetSize = 96.0f;
		const float svgMax = std::max(image->width, image->height);
		const float rasterScale = targetSize / svgMax;
		const int width = std::max(1, (int)std::ceil(image->width * rasterScale));
		const int height = std::max(1, (int)std::ceil(image->height * rasterScale));
		std::vector<unsigned char> pixels((size_t)width * (size_t)height * 4);
		NSVGrasterizer *rasterizer = nsvgCreateRasterizer();
		if (rasterizer) {
			nsvgRasterize(rasterizer, image, 0.0f, 0.0f, rasterScale, pixels.data(), width, height, width * 4);
			Draw::TextureDesc desc{};
			desc.type = Draw::TextureType::LINEAR2D;
			desc.format = Draw::DataFormat::R8G8B8A8_UNORM;
			desc.width = width;
			desc.height = height;
			desc.depth = 1;
			desc.mipLevels = 1;
			desc.generateMips = false;
			desc.tag = path;
			desc.initData.push_back(pixels.data());
			raIconTexture_ = draw->CreateTexture(desc);
			nsvgDeleteRasterizer(rasterizer);
		}
		nsvgDelete(image);
		if (raIconTexture_) {
			LogMessage(log_, "GBAStation overlay RA icon loaded path=%s", path);
			return raIconTexture_;
		}
	}

	LogMessage(log_, "GBAStation overlay RA icon not found");
	return nullptr;
}

void Overlay::ReleaseRAIconTexture() {
	if (raIconTexture_) {
		raIconTexture_->Release();
		raIconTexture_ = nullptr;
	}
}

void Overlay::Shutdown() {
	if (!ready_ && !context_) {
		return;
	}

	if (context_) {
		ImGui::SetCurrentContext(context_);
	}
	ReleaseRAIconTexture();
	if (ready_) {
		ImGui_ImplThin3d_Shutdown();
	}
	if (context_) {
		ImGui::DestroyContext(context_);
	}

	context_ = nullptr;
	ready_ = false;
	visible_ = false;
	comboDown_ = false;
	exitRequested_ = false;
	menu_ = Menu::Quick;
	selection_ = 0;
	tabSelection_ = 0;
	sidebarFocused_ = true;
	settingsSelection_ = 0;
	displaySettings_ = {};
	pendingCommand_ = {};
	slotInUse_.fill(false);
	cheatsEnabled_ = false;
	cheatsAvailable_ = false;
	cheatsLoading_ = false;
	cheatsLoadCommandSent_ = false;
	cheatsLoadingDelayFrames_ = 0;
	cheats_.clear();
	lastAnalogNavMs_ = 0;
	nextCheatVerticalNavMs_ = 0;
	nextCheatHorizontalNavMs_ = 0;
	cheatVerticalNavDir_ = 0;
	cheatHorizontalNavDir_ = 0;
}

void Overlay::SetVisible(bool visible) {
	if (visible_ == visible) {
		return;
	}

	visible_ = visible;
	menu_ = Menu::Quick;
	selection_ = 0;
	tabSelection_ = 0;
	sidebarFocused_ = true;
	settingsSelection_ = 0;
	coreSettingsPage_ = false;
	animTimer_ = 0.0f;
	lastAnalogNavMs_ = 0;
	nextCheatVerticalNavMs_ = 0;
	nextCheatHorizontalNavMs_ = 0;
	cheatVerticalNavDir_ = 0;
	cheatHorizontalNavDir_ = 0;
	if (visible_) {
		TranslationManager::Instance().Init(log_);
		displaySettings_ = LoadPpssppDisplaySettings(log_);
	} else {
		cheatsLoading_ = false;
		cheatsLoadCommandSent_ = false;
		cheatsLoadingDelayFrames_ = 0;
	}
	LogMessage(log_, "GBAStation overlay visible=%d", visible ? 1 : 0);
}

void Overlay::SetSaveStateInfo(int currentSlot, const std::array<bool, Ppsspp::SaveStateSlotCount> &slotInUse) {
	currentStateSlot_ = std::clamp(currentSlot, 0, Ppsspp::SaveStateSlotCount - 1);
	slotInUse_ = slotInUse;
}

void Overlay::SetCheatsEnabled(bool enabled) {
	cheatsEnabled_ = enabled;
	if (!enabled) {
		cheatsAvailable_ = false;
		cheatsLoading_ = false;
		cheatsLoadCommandSent_ = false;
		cheatsLoadingDelayFrames_ = 0;
		cheats_.clear();
		if (menu_ == Menu::Cheats) {
			menu_ = Menu::Quick;
			selection_ = 0;
			animTimer_ = kOverlayAnimDuration;
		}
	}
}

void Overlay::SetCheatInfo(bool enabled, bool available, const std::vector<CheatMenuEntry> &entries) {
	const bool wasLoading = cheatsLoading_;
	cheatsEnabled_ = enabled;
	cheatsAvailable_ = available;
	cheatsLoading_ = false;
	cheatsLoadCommandSent_ = false;
	cheatsLoadingDelayFrames_ = 0;
	cheats_ = entries;
	if (wasLoading && visible_ && menu_ == Menu::Quick) {
		menu_ = Menu::Cheats;
		selection_ = FirstSelectableCheatRow(cheats_);
		animTimer_ = kOverlayAnimDuration;
	} else if (menu_ == Menu::Cheats && !IsSelectableCheatRow(cheats_, selection_)) {
		selection_ = FirstSelectableCheatRow(cheats_);
	}
}

void Overlay::ReloadDisplaySettings() {
	displaySettings_ = LoadPpssppDisplaySettings(log_);
}

OverlayCommand Overlay::ConsumeCommand() {
	if (pendingCommand_.action == OverlayAction::None && cheatsLoading_ && !cheatsLoadCommandSent_) {
		if (cheatsLoadingDelayFrames_ > 0) {
			cheatsLoadingDelayFrames_--;
			return {};
		}
		cheatsLoadCommandSent_ = true;
		return { OverlayAction::LoadCheats, 0 };
	}
	OverlayCommand command = pendingCommand_;
	pendingCommand_ = {};
	return command;
}

int Overlay::QuickMenuStorageIndex(int visibleIndex) const {
	return std::clamp(visibleIndex, 0,
		static_cast<int>(sizeof(kQuickMenuItems) / sizeof(kQuickMenuItems[0])) - 1);
}

void Overlay::ActivateTab(int tab) {
	tabSelection_ = std::clamp(tab, 0, 7);
	selection_ = 0;
	settingsSelection_ = 0;
	sidebarFocused_ = true;

	switch (tabSelection_) {
	case 1:
		saveStateMode_ = OverlayAction::SaveState;
		menu_ = Menu::SaveStates;
		selection_ = currentStateSlot_;
		break;
	case 2:
		saveStateMode_ = OverlayAction::LoadState;
		menu_ = Menu::SaveStates;
		selection_ = currentStateSlot_;
		break;
	case 3:
		menu_ = Menu::Cheats;
		if (!cheatsLoading_) {
			cheatsLoading_ = true;
			cheatsLoadCommandSent_ = false;
			cheatsLoadingDelayFrames_ = 1;
		}
		break;
	case 4:
		menu_ = Menu::Settings;
		coreSettingsPage_ = false;
		break;
	case 5:
		menu_ = Menu::Settings;
		coreSettingsPage_ = true;
		break;
	default:
		menu_ = Menu::Quick;
		break;
	}
	animTimer_ = kOverlayAnimDuration;
}

int Overlay::ItemCount() const {
	if (menu_ == Menu::Quick) {
		return static_cast<int>(sizeof(kQuickMenuItems) / sizeof(kQuickMenuItems[0]));
	}
	if (menu_ == Menu::SaveStates) {
		return Ppsspp::SaveStateSlotCount;
	}
	if (menu_ == Menu::Cheats) {
		return std::max(1, (int)cheats_.size());
	}
	return coreSettingsPage_ ? 10 : 2;
}

void Overlay::ApplyDisplaySettings(bool save) {
	displaySettings_ = NormalizePpssppDisplaySettingsForCurrentMode(displaySettings_);
	ApplyPpssppDisplaySettings(displaySettings_);
	if (save) {
		SavePpssppDisplaySettings(displaySettings_, log_);
	}
}

void Overlay::CycleSetting(int direction) {
	if (direction == 0) {
		return;
	}

	if (coreSettingsPage_) {
		switch (settingsSelection_) {
		case 0: g_Config.iInternalResolution = std::clamp(g_Config.iInternalResolution + direction, 0, 5); break;
		case 1: g_Config.iFrameSkip = (g_Config.iFrameSkip + direction + 6) % 6; break;
		case 2: g_Config.bAutoFrameSkip = !g_Config.bAutoFrameSkip; break;
		case 3: g_Config.bFastMemory = !g_Config.bFastMemory; break;
		case 4: g_Config.bHardwareTransform = !g_Config.bHardwareTransform; break;
		case 5: g_Config.bSkipBufferEffects = !g_Config.bSkipBufferEffects; break;
		case 6: g_Config.bVSync = !g_Config.bVSync; break;
		case 7: g_Config.iTexFiltering = g_Config.iTexFiltering >= 4 ? 1 : g_Config.iTexFiltering + 1; break;
		case 8: g_Config.iAnisotropyLevel = (g_Config.iAnisotropyLevel + direction + 5) % 5; break;
		case 9: g_Config.bTexDeposterize = !g_Config.bTexDeposterize; break;
		default: break;
		}
		coreSettingsChanged_ = true;
		return;
	}
	displaySettings_ = NormalizePpssppDisplaySettingsForCurrentMode(displaySettings_);
	if (settingsSelection_ == 0) {
		displaySettings_.mode = displaySettings_.mode == DisplayMode::Integer ? DisplayMode::Display : DisplayMode::Integer;
		displaySettings_.size = displaySettings_.mode == DisplayMode::Integer ? DisplaySize::Auto : DisplaySize::_16_9;
	} else {
		if (displaySettings_.mode == DisplayMode::Integer) {
			DisplaySize integerSizes[5];
			const int integerSizeCount = GetAvailableIntegerDisplaySizes(integerSizes, (int)(sizeof(integerSizes) / sizeof(integerSizes[0])));
			displaySettings_.size = CycleDisplaySize(displaySettings_.size, integerSizes, integerSizeCount, direction);
		} else {
			displaySettings_.size = CycleDisplaySize(displaySettings_.size, kAspectDisplaySizes,
				(int)(sizeof(kAspectDisplaySizes) / sizeof(kAspectDisplaySizes[0])), direction);
		}
	}
	ApplyDisplaySettings(true);
}

void Overlay::ExecuteSelection() {
	const int itemCount = ItemCount();
	if (selection_ < 0 || selection_ >= itemCount) {
		return;
	}

	if (menu_ == Menu::Settings) {
		CycleSetting(1);
		return;
	}

	if (menu_ == Menu::SaveStates) {
		if (saveStateMode_ == OverlayAction::LoadState && !slotInUse_[selection_]) {
			LogMessage(log_, "GBAStation load state ignored empty slot=%d", selection_);
			return;
		}
		pendingCommand_ = { saveStateMode_, selection_ };
		SetVisible(false);
		return;
	}

	if (menu_ == Menu::Cheats) {
		if (cheats_.empty() || !cheats_[selection_].toggleable || cheats_[selection_].sourceIndex < 0) {
			return;
		}
		pendingCommand_ = { OverlayAction::ToggleCheat, cheats_[selection_].sourceIndex };
		return;
	}

	const QuickMenuItem &item = kQuickMenuItems[QuickMenuStorageIndex(tabSelection_)];
	if (item.action == QuickMenuItem::Action::SaveState || item.action == QuickMenuItem::Action::LoadState) {
		saveStateMode_ = item.action == QuickMenuItem::Action::SaveState ? OverlayAction::SaveState : OverlayAction::LoadState;
		menu_ = Menu::SaveStates;
		selection_ = currentStateSlot_;
		animTimer_ = kOverlayAnimDuration;
	} else if (item.action == QuickMenuItem::Action::Cheats) {
		if (!cheatsLoading_) {
			cheatsLoading_ = true;
			cheatsLoadCommandSent_ = false;
			cheatsLoadingDelayFrames_ = 1;
			pendingCommand_ = {};
		}
		menu_ = Menu::Cheats;
		selection_ = 0;
		animTimer_ = kOverlayAnimDuration;
	} else if (item.action == QuickMenuItem::Action::VideoSettings ||
		item.action == QuickMenuItem::Action::CoreSettings) {
		menu_ = Menu::Settings;
		coreSettingsPage_ = item.action == QuickMenuItem::Action::CoreSettings;
		selection_ = 0;
		settingsSelection_ = 0;
		animTimer_ = kOverlayAnimDuration;
	} else if (item.action == QuickMenuItem::Action::Resume) {
		SetVisible(false);
	} else if (item.action == QuickMenuItem::Action::Reset) {
		pendingCommand_ = { OverlayAction::Reset, 0 };
		SetVisible(false);
	} else {
		exitRequested_ = true;
	}
}

bool Overlay::HandleInput(u64 buttons, u64 pressed, int leftStickX, int leftStickY, int rightStickX, int rightStickY,
	bool menuTogglePressed) {
	const bool wasVisible = visible_;
	if (menuTogglePressed) {
		if (!visible_) {
			SetVisible(true);
		} else {
			sidebarFocused_ = true;
		}
	}

	if (visible_) {
		const int itemCount = ItemCount();
		bool navUp = (pressed & HidNpadButton_Up) != 0;
		bool navDown = (pressed & HidNpadButton_Down) != 0;
		bool navLeft = (pressed & HidNpadButton_Left) != 0;
		bool navRight = (pressed & HidNpadButton_Right) != 0;
		if (menu_ == Menu::Cheats) {
			navUp = false;
			navDown = false;
			navLeft = false;
			navRight = false;

			const bool analogUp = leftStickY > kCheatAnalogNavThreshold || rightStickY > kCheatAnalogNavThreshold;
			const bool analogDown = leftStickY < -kCheatAnalogNavThreshold || rightStickY < -kCheatAnalogNavThreshold;
			const bool analogLeft = leftStickX < -kCheatAnalogNavThreshold || rightStickX < -kCheatAnalogNavThreshold;
			const bool analogRight = leftStickX > kCheatAnalogNavThreshold || rightStickX > kCheatAnalogNavThreshold;
			const int verticalPressedDir = (pressed & HidNpadButton_Up) ? -1 : ((pressed & HidNpadButton_Down) ? 1 : 0);
			const int verticalHeldDir = ((buttons & HidNpadButton_Up) || analogUp) ? -1 : (((buttons & HidNpadButton_Down) || analogDown) ? 1 : 0);
			const int horizontalPressedDir = (pressed & HidNpadButton_Left) ? -1 : ((pressed & HidNpadButton_Right) ? 1 : 0);
			const int horizontalHeldDir = ((buttons & HidNpadButton_Left) || analogLeft) ? -1 : (((buttons & HidNpadButton_Right) || analogRight) ? 1 : 0);
			const u64 nowMs = CurrentTimeMs();

			if (verticalPressedDir != 0) {
				cheatVerticalNavDir_ = verticalPressedDir;
				nextCheatVerticalNavMs_ = nowMs + kCheatVerticalNavInitialRepeatMs;
				navUp = verticalPressedDir < 0;
				navDown = verticalPressedDir > 0;
			} else if (HeldNavigationTriggered(verticalHeldDir, cheatVerticalNavDir_, nextCheatVerticalNavMs_,
				kCheatVerticalNavInitialRepeatMs, kCheatVerticalNavRepeatMs, nowMs)) {
				navUp = verticalHeldDir < 0;
				navDown = verticalHeldDir > 0;
			}

			if (!navUp && !navDown) {
				if (horizontalPressedDir != 0) {
					cheatHorizontalNavDir_ = horizontalPressedDir;
					nextCheatHorizontalNavMs_ = nowMs + kCheatHorizontalNavInitialRepeatMs;
					navLeft = horizontalPressedDir < 0;
					navRight = horizontalPressedDir > 0;
				} else if (HeldNavigationTriggered(horizontalHeldDir, cheatHorizontalNavDir_, nextCheatHorizontalNavMs_,
					kCheatHorizontalNavInitialRepeatMs, kCheatHorizontalNavRepeatMs, nowMs)) {
					navLeft = horizontalHeldDir < 0;
					navRight = horizontalHeldDir > 0;
				}
			}

			lastAnalogNavMs_ = 0;
		} else {
			cheatVerticalNavDir_ = 0;
			cheatHorizontalNavDir_ = 0;
			nextCheatVerticalNavMs_ = 0;
			nextCheatHorizontalNavMs_ = 0;

			const bool analogUp = leftStickY > kAnalogNavThreshold || rightStickY > kAnalogNavThreshold;
			const bool analogDown = leftStickY < -kAnalogNavThreshold || rightStickY < -kAnalogNavThreshold;
			const bool analogLeft = leftStickX < -kAnalogNavThreshold || rightStickX < -kAnalogNavThreshold;
			const bool analogRight = leftStickX > kAnalogNavThreshold || rightStickX > kAnalogNavThreshold;
			if (analogUp || analogDown || analogLeft || analogRight) {
				const u64 nowMs = CurrentTimeMs();
				if (nowMs == 0 || nowMs - lastAnalogNavMs_ >= kAnalogNavRepeatMs) {
					navUp = navUp || analogUp;
					navDown = navDown || (!analogUp && analogDown);
					if (!analogUp && !analogDown) {
						navLeft = navLeft || analogLeft;
						navRight = navRight || (!analogLeft && analogRight);
					}
					lastAnalogNavMs_ = nowMs;
				}
			} else {
				lastAnalogNavMs_ = 0;
			}
		}

		if (sidebarFocused_) {
			if (navUp) ActivateTab((tabSelection_ + 7) % 8);
			if (navDown) ActivateTab((tabSelection_ + 1) % 8);
			if (navRight && (menu_ == Menu::SaveStates || menu_ == Menu::Cheats || menu_ == Menu::Settings)) sidebarFocused_ = false;
			if (pressed & HidNpadButton_A) {
				if (tabSelection_ == 0) SetVisible(false);
				else if (tabSelection_ == 6) { pendingCommand_ = { OverlayAction::Reset, 0 }; SetVisible(false); }
				else if (tabSelection_ == 7) exitRequested_ = true;
				else if (menu_ == Menu::SaveStates || menu_ == Menu::Cheats || menu_ == Menu::Settings) sidebarFocused_ = false;
			}
			if (pressed & HidNpadButton_B) SetVisible(false);
			return true;
		}

		if (menu_ == Menu::Cheats) {
			if (navUp) {
				MoveCheatSelectionWrapped(selection_, cheats_, -1);
			}
			if (navDown) {
				MoveCheatSelectionWrapped(selection_, cheats_, 1);
			}
			if (navLeft) {
				MoveCheatSelectionWrapped(selection_, cheats_, -kCheatPageStep);
			}
			if (navRight) {
				MoveCheatSelectionWrapped(selection_, cheats_, kCheatPageStep);
			}
		} else {
			if (navUp && itemCount > 0) {
				selection_ = (selection_ + itemCount - 1) % itemCount;
			}
			if (navDown && itemCount > 0) {
				selection_ = (selection_ + 1) % itemCount;
			}
		}
		if (menu_ == Menu::Settings) {
			settingsSelection_ = selection_;
			if (navLeft) {
				CycleSetting(-1);
			}
			if (navRight) {
				CycleSetting(1);
			}
		}
		if (pressed & HidNpadButton_A) {
			ExecuteSelection();
		}
		if (menu_ == Menu::Quick && (pressed & HidNpadButton_Minus) && !(buttons & HidNpadButton_Plus)) {
			pendingCommand_ = { OverlayAction::Reset, 0 };
			SetVisible(false);
			return true;
		}
		if (pressed & HidNpadButton_B) {
			if (menu_ != Menu::Quick) {
				sidebarFocused_ = true;
			} else {
				SetVisible(false);
			}
		}
	}

	return wasVisible || visible_ || menuTogglePressed;
}

void Overlay::DrawBackground(ImDrawList *drawList, ImVec2 displaySize, float ease) {
	const int baseAlpha = (int)(72.0f * ease);
	const int maxAlpha = (int)(110.0f * ease);
	if (baseAlpha <= 0) {
		return;
	}

	const float topH = displaySize.y * 0.20f;
	const float bottomH = displaySize.y * 0.20f;
	const float centerH = displaySize.y - topH - bottomH;
	const ImU32 colMax = IM_COL32(0, 0, 0, maxAlpha);
	const ImU32 colBase = IM_COL32(0, 0, 0, baseAlpha);

	drawList->AddRectFilledMultiColor(ImVec2(0.0f, 0.0f), ImVec2(displaySize.x, topH), colMax, colMax, colBase, colBase);
	drawList->AddRectFilled(ImVec2(0.0f, topH), ImVec2(displaySize.x, topH + centerH), colBase);
	drawList->AddRectFilledMultiColor(ImVec2(0.0f, displaySize.y - bottomH), displaySize, colBase, colBase, colMax, colMax);
}

void Overlay::DrawMenu(ImDrawList *drawList, ImVec2 displaySize, float scale, float ease) {
	// The whole overlay uses the same GBAStation 3DS shell.  Sub-pages do not
	// replace the shell: the active tab stays visible while its rows change.
	{
		const float width = displaySize.x;
		const float height = displaySize.y;
		const ImVec2 min(0.0f, 0.0f);
		const ImVec2 max(min.x + width, min.y + height);
		const float side = std::min(340.0f * scale, width * 0.34f);
		const float header = 72.0f * scale;
		const float tabHeight = (height - header - 24.0f * scale) / 8.0f;
		const char *tabs[] = {"返回游戏", "保存状态", "读取状态", "金手指", "画面设置", "功能设置", "重置游戏", "退出游戏"};
		const char *icons[] = {u8"\ue5c4", u8"\ue161", u8"\ue042", u8"\ue87d", u8"\ue3b6", u8"\ue8b8", u8"\ue5d5", u8"\ue8ac"};
		const char *descriptions[] = {"继续当前游戏。", "创建即时存档。", "读取即时存档。", "管理游戏金手指。", "调整画面比例和缩放。", "调整可即时生效的核心选项。", "重新启动当前游戏。", "返回 GBAStation。"};
		const int active = tabSelection_;
		const ImU32 bg = IM_COL32(9, 13, 23, (int)(158.0f * ease));
		const ImU32 panel = IM_COL32(19, 25, 40, (int)(178.0f * ease));
		const ImU32 line = IM_COL32(105, 126, 165, (int)(110.0f * ease));
		const ImU32 text = IM_COL32(243, 247, 255, (int)(255.0f * ease));
		const ImU32 muted = IM_COL32(178, 190, 213, (int)(240.0f * ease));
		ImFont *font = ImGui::GetFont();
		const float titleSize = ImGui::GetFontSize() * 1.08f;
		const float labelSize = ImGui::GetFontSize() * 0.84f;
		drawList->AddRectFilled(min, max, bg);
		drawList->AddRectFilled(ImVec2(min.x, min.y + header), ImVec2(min.x + side, max.y), panel);
		drawList->AddLine(ImVec2(min.x + side, min.y + header), ImVec2(min.x + side, max.y), line);
		drawList->AddLine(ImVec2(min.x, min.y + header), ImVec2(max.x, min.y + header), line);
		drawList->AddText(font, titleSize, ImVec2(min.x + 28.0f * scale, min.y + 17.0f * scale), text, "游戏菜单");
		for (int i = 0; i < 8; ++i) {
			const ImVec2 rowMin(min.x + 12.0f * scale, min.y + header + 10.0f * scale + i * tabHeight);
			const ImVec2 rowMax(min.x + side - 12.0f * scale, rowMin.y + tabHeight - 3.0f * scale);
			const bool selected = i == active;
			if (selected) {
				drawList->AddRectFilled(rowMin, rowMax, IM_COL32(56, 70, 105, (int)(105.0f * ease)));
				drawList->AddRect(rowMin, rowMax, IM_COL32(86, 169, 255, (int)(255.0f * ease)), 0.0f, 0, 2.0f * scale);
			}
			const ImVec2 size = font->CalcTextSizeA(labelSize, 10000.0f, 0.0f, tabs[i]);
			const float textY = rowMin.y + (rowMax.y - rowMin.y - size.y) * 0.5f;
			drawList->AddText(font, labelSize * 1.08f, ImVec2(rowMin.x + 18.0f * scale, textY), selected ? text : muted, icons[i]);
			drawList->AddText(font, labelSize, ImVec2(rowMin.x + 52.0f * scale, textY), selected ? text : muted, tabs[i]);
		}
		const float contentX = min.x + side + 34.0f * scale;
		const float contentRight = max.x - 34.0f * scale;
		const float firstRowY = min.y + header + 72.0f * scale;
		drawList->AddText(font, titleSize, ImVec2(contentX, min.y + 18.0f * scale), text, tabs[active]);
		auto row = [&](int i, bool selected, const std::string &label, const std::string &value) {
			const float rowHeight = 52.0f * scale;
			const ImVec2 rowMin(contentX, firstRowY + i * rowHeight), rowMax(contentRight, firstRowY + (i + 1) * rowHeight - 4.0f * scale);
			if (selected) {
				drawList->AddRectFilled(rowMin, rowMax, IM_COL32(48, 61, 92, (int)(150.0f * ease)));
				drawList->AddRect(rowMin, rowMax, IM_COL32(86, 169, 255, (int)(255.0f * ease)), 0.0f, 0, 2.0f * scale);
			}
			const float textY = rowMin.y + (rowMax.y - rowMin.y - font->CalcTextSizeA(labelSize, 10000.0f, 0.0f, label.c_str()).y) * 0.5f;
			drawList->AddText(font, labelSize, ImVec2(rowMin.x + 18.0f * scale, textY), selected ? text : muted, label.c_str());
			if (!value.empty()) { const ImVec2 size = font->CalcTextSizeA(labelSize, 10000.0f, 0.0f, value.c_str()); drawList->AddText(font, labelSize, ImVec2(rowMax.x - size.x - 18.0f * scale, textY), text, value.c_str()); }
		};
		if (menu_ == Menu::SaveStates) {
			const int firstSlot = std::clamp(selection_ - 5, 0, Ppsspp::SaveStateSlotCount - 6);
			for (int rowIndex = 0; rowIndex < 6; ++rowIndex) { const int slot = firstSlot + rowIndex; row(rowIndex, slot == selection_, "存档槽 " + std::to_string(slot + 1), slotInUse_[slot] ? "已有存档" : "空"); }
		} else if (menu_ == Menu::Cheats) {
			if (cheats_.empty()) drawList->AddText(font, labelSize, ImVec2(contentX, firstRowY), muted, "当前游戏没有可用金手指。");
			else for (int i = 0; i < std::min(7, (int)cheats_.size()); ++i) row(i, i == selection_, cheats_[i].name, cheats_[i].enabled ? "开启" : "关闭");
		} else if (menu_ == Menu::Settings) {
			if (coreSettingsPage_) {
				const char *labels[] = {
					"渲染分辨率", "跳帧", "自动跳帧", "快速内存", "硬件变换",
					"跳过缓冲区效果", "垂直同步", "纹理过滤", "各向异性过滤", "纹理去色带"};
				auto enabled = [](bool value) { return value ? std::string("开启") : std::string("关闭"); };
				auto settingValue = [&](int index) {
					switch (index) {
					case 0: return g_Config.iInternalResolution == 0 ? std::string("自动") : std::to_string(g_Config.iInternalResolution) + "x";
					case 1: return g_Config.iFrameSkip == 0 ? std::string("关闭") : std::to_string(g_Config.iFrameSkip) + " 帧";
					case 2: return enabled(g_Config.bAutoFrameSkip);
					case 3: return enabled(g_Config.bFastMemory);
					case 4: return enabled(g_Config.bHardwareTransform);
					case 5: return enabled(g_Config.bSkipBufferEffects);
					case 6: return enabled(g_Config.bVSync);
					case 7: {
						const char *filters[] = {"默认", "自动", "最近邻", "线性", "高质量"};
						return std::string(filters[std::clamp(g_Config.iTexFiltering, 0, 4)]);
					}
					case 8: return g_Config.iAnisotropyLevel == 0 ? std::string("关闭") : std::to_string(1 << g_Config.iAnisotropyLevel) + "x";
					default: return enabled(g_Config.bTexDeposterize);
					}
				};
				const int firstSetting = std::clamp(selection_ - 3, 0, 4);
				for (int rowIndex = 0; rowIndex < 6; ++rowIndex) {
					const int index = firstSetting + rowIndex;
					row(rowIndex, index == selection_, labels[index], settingValue(index));
				}
			} else {
				row(0, selection_ == 0, "显示模式", TranslatedDisplayModeLabel(displaySettings_.mode));
				row(1, selection_ == 1, "画面比例", TranslatedDisplaySizeLabel(displaySettings_.size));
			}
		} else {
			drawList->AddText(font, labelSize, ImVec2(contentX, firstRowY), muted, descriptions[active]);
			if (active == 1 || active == 2) {
				for (int slot = 0; slot < 6; ++slot) row(slot, false, "存档槽 " + std::to_string(slot + 1), slotInUse_[slot] ? "已有存档" : "空");
			} else if (active == 4 || active == 5) {
				row(0, false, "显示模式", TranslatedDisplayModeLabel(displaySettings_.mode));
				row(1, false, "画面比例", TranslatedDisplaySizeLabel(displaySettings_.size));
			}
		}
		return;
	}
}

void Overlay::DrawHelpers(ImDrawList *drawList, ImVec2 displaySize, float scale, float ease) {
	struct Helper {
		const char *button;
		std::string label;
	};
	std::vector<Helper> helpers;
	if (menu_ == Menu::Quick) {
		helpers.push_back({"-", tr("emulator_reset")});
	}
	if (menu_ == Menu::Settings) {
		helpers.push_back({"DPAD", tr("emulator_change")});
	}
	if (menu_ == Menu::Cheats && cheats_.size() > kCheatPageStep) {
		helpers.push_back({"<>", "+10"});
	}
	const bool canToggleCheat = menu_ == Menu::Cheats && !cheats_.empty() && selection_ >= 0 &&
		selection_ < (int)cheats_.size() && cheats_[selection_].toggleable;
	const std::string aLabel = menu_ == Menu::SaveStates
		? (saveStateMode_ == OverlayAction::SaveState ? tr("emulator_save_state") : tr("emulator_load_state"))
		: (canToggleCheat ? tr("emulator_toggle") : tr("emulator_select"));
	helpers.push_back({"B", tr("emulator_back")});
	helpers.push_back({"A", aLabel});

	ImFont *font = ImGui::GetFont();
	const float barHeight = 48.0f * scale;
	const float marginBottom = 24.0f * scale;
	const float padding = 16.0f * scale;
	const float buttonSize = 22.0f * scale;
	const float itemSpacing = 12.0f * scale;
	const float fontSize = ImGui::GetFontSize() * 0.78f;

	float totalWidth = padding * 2.0f;
	for (size_t i = 0; i < helpers.size(); ++i) {
		totalWidth += buttonSize + (8.0f * scale) + font->CalcTextSizeA(fontSize, 10000.0f, 0.0f, helpers[i].label.c_str()).x;
		if (i + 1 < helpers.size()) {
			totalWidth += itemSpacing;
		}
	}

	const float currentOffset = (kQuickMenuWidth * scale) * (1.0f - ease);
	const float barX = displaySize.x - totalWidth - 20.0f * scale + currentOffset;
	const float barY = displaySize.y - marginBottom - barHeight;
	const float centerY = barY + barHeight * 0.5f;
	float cursorX = barX + padding;
	const ImU32 textColor = IM_COL32(200, 200, 200, (int)(255.0f * ease));

	for (size_t i = 0; i < helpers.size(); ++i) {
		const Helper &helper = helpers[i];
		DrawSwitchButtonPrompt(drawList, font, fontSize, ImVec2(cursorX + buttonSize * 0.5f, centerY), buttonSize, helper.button, ease);
		cursorX += buttonSize + (8.0f * scale);
		const ImVec2 textSize = font->CalcTextSizeA(fontSize, 10000.0f, 0.0f, helper.label.c_str());
		drawList->AddText(font, fontSize, ImVec2(cursorX, centerY - textSize.y * 0.5f), textColor, helper.label.c_str());
		cursorX += textSize.x + itemSpacing;
	}
}

void Overlay::DrawRAAlerts(Draw::DrawContext *draw, ImDrawList *drawList, ImVec2 displaySize, float scale, float deltaTime) {
	auto &notifications = RetroAchievements().Notifications();
	if (notifications.empty()) {
		return;
	}

	for (RANotification &notification : notifications) {
		notification.timer += deltaTime;
	}
	notifications.erase(std::remove_if(notifications.begin(), notifications.end(), [](const RANotification &notification) {
		return notification.timer >= notification.duration;
	}), notifications.end());
	if (notifications.empty()) {
		return;
	}

	const RAAlertPosition position = RetroAchievements().AlertPosition();
	const bool isTop = position == RAAlertPosition::TopLeft || position == RAAlertPosition::TopRight;
	const bool isRight = position == RAAlertPosition::TopRight || position == RAAlertPosition::BottomRight;
	const float margin = 16.0f * scale;
	const float spacing = 8.0f * scale;
	const float alertWidth = std::min(420.0f * scale, displaySize.x - margin * 2.0f);
	const float alertHeight = 100.0f * scale;
	const float padding = 12.0f * scale;
	const float cornerRadius = 14.0f * scale;
	const float badgeSize = 76.0f * scale;
	const float badgeRadius = 4.0f * scale;
	const float badgeMargin = 12.0f * scale;
	const float titleSize = ImGui::GetFontSize() * 0.85f;
	const float descriptionSize = ImGui::GetFontSize() * 0.65f;
	ImFont *font = ImGui::GetFont();
	ImFont *descriptionFont = font;
	if (ImGui::GetIO().Fonts->Fonts.Size > 1) {
		descriptionFont = ImGui::GetIO().Fonts->Fonts[1];
	}

	size_t visibleIndex = 0;
	for (size_t i = 0; i < notifications.size(); ++i) {
		RANotification &notification = notifications[i];
		if (notification.timer < 0.0f) {
			continue;
		}

		float slideProgress = 1.0f;
		if (notification.timer < notification.slideIn) {
			slideProgress = EaseOutCubic(notification.timer / notification.slideIn);
		} else if (notification.timer > notification.duration - notification.slideOut) {
			slideProgress = EaseOutCubic((notification.duration - notification.timer) / notification.slideOut);
		}
		slideProgress = std::clamp(slideProgress, 0.0f, 1.0f);
		const int alpha = (int)(230.0f * slideProgress);
		if (alpha <= 0) {
			continue;
		}

		const float stack = (alertHeight + spacing) * (float)visibleIndex;
		const float anchorX = isRight ? displaySize.x - alertWidth - margin : margin;
		const float anchorY = isTop ? margin + stack : displaySize.y - margin - alertHeight - stack;
		const float slideOffsetY = isTop
			? -(alertHeight + margin + stack) * (1.0f - slideProgress)
			: (alertHeight + margin + stack) * (1.0f - slideProgress);
		const ImVec2 min(anchorX, anchorY + slideOffsetY);
		const ImVec2 max(anchorX + alertWidth, min.y + alertHeight);

		drawList->AddRectFilled(min, max, IM_COL32(35, 35, 40, alpha), cornerRadius);
		drawList->AddRect(min, max, IM_COL32(70, 70, 80, (int)(180.0f * slideProgress)), cornerRadius, 0, 1.5f * scale);

		Draw::Texture *badgeTexture = nullptr;
		const bool isRAIcon = notification.badgeName == "ra_icon";
		if (isRAIcon) {
			badgeTexture = LoadRAIconTexture(draw);
		} else {
			badgeTexture = RetroAchievements().GetBadgeTexture(draw, notification.badgeName);
		}

		float textX = min.x + padding;
		const ImVec2 badgeMin(min.x + badgeMargin, min.y + (alertHeight - badgeSize) * 0.5f);
		const ImVec2 badgeMax(badgeMin.x + badgeSize, badgeMin.y + badgeSize);
		if (badgeTexture) {
			float drawBadgeSize = badgeSize;
			float drawBadgeX = badgeMin.x;
			float drawBadgeY = badgeMin.y;
			if (isRAIcon) {
				drawBadgeSize = badgeSize * 0.70f;
				drawBadgeX += (badgeSize - drawBadgeSize) * 0.5f;
				drawBadgeY += (badgeSize - drawBadgeSize) * 0.5f;
			}
			const ImTextureID textureId = ImGui_ImplThin3d_AddTextureTemp(badgeTexture);
			drawList->AddImageRounded(textureId, ImVec2(drawBadgeX, drawBadgeY),
				ImVec2(drawBadgeX + drawBadgeSize, drawBadgeY + drawBadgeSize),
				ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), IM_COL32(255, 255, 255, alpha), badgeRadius);
			textX = badgeMin.x + badgeSize + badgeMargin;
		} else {
			drawList->AddRectFilled(badgeMin, badgeMax, IM_COL32(45, 45, 52, alpha), badgeRadius);
			const char *ra = "RA";
			const ImVec2 raSize = font->CalcTextSizeA(titleSize, 10000.0f, 0.0f, ra);
			drawList->AddText(font, titleSize, ImVec2((badgeMin.x + badgeMax.x - raSize.x) * 0.5f,
				(badgeMin.y + badgeMax.y - raSize.y) * 0.5f), IM_COL32(230, 230, 238, alpha), ra);
			textX = badgeMin.x + badgeSize + badgeMargin;
		}

		const float maxTextWidth = max.x - textX - padding;
		std::string title = TruncateToWidth(font, titleSize, notification.title, maxTextWidth);
		std::string description = notification.description;
		const float maxDescriptionHeight = descriptionSize * 2.5f;
		if (descriptionFont->CalcTextSizeA(descriptionSize, 10000.0f, maxTextWidth, description.c_str()).y > maxDescriptionHeight) {
			description += "...";
			while (description.size() > 4 &&
				descriptionFont->CalcTextSizeA(descriptionSize, 10000.0f, maxTextWidth, description.c_str()).y > maxDescriptionHeight) {
				description.erase(description.size() - 4, 1);
			}
		}

		const ImVec2 titleTextSize = font->CalcTextSizeA(titleSize, 10000.0f, 0.0f, title.c_str());
		const ImVec2 descriptionTextSize = descriptionFont->CalcTextSizeA(descriptionSize, 10000.0f, maxTextWidth, description.c_str());
		const float textSpacing = 4.0f * scale;
		const float totalTextHeight = titleTextSize.y + textSpacing + descriptionTextSize.y;
		const float titleY = min.y + (alertHeight - totalTextHeight) * 0.5f;
		const float descriptionY = titleY + titleTextSize.y + textSpacing;
		const ImU32 titleColor = IM_COL32(255, 255, 255, alpha);
		const ImU32 descriptionColor = IM_COL32(185, 185, 195, alpha);

		drawList->AddText(font, titleSize, ImVec2(textX + 1.0f, titleY + 1.0f),
			IM_COL32(0, 0, 0, (int)(80.0f * slideProgress)), title.c_str());
		drawList->AddText(font, titleSize, ImVec2(textX, titleY), titleColor, title.c_str());
		drawList->AddText(descriptionFont, descriptionSize, ImVec2(textX, descriptionY), descriptionColor,
			description.c_str(), nullptr, maxTextWidth);
		visibleIndex++;
	}
}

void Overlay::DrawUI(float width, float height, float deltaTime) {
	animTimer_ = std::min(animTimer_ + deltaTime, kOverlayAnimDuration);
	const float ease = EaseOutCubic(animTimer_ / kOverlayAnimDuration);
	ImDrawList *drawList = ImGui::GetForegroundDrawList();
	const ImVec2 displaySize(width, height);
	const float scale = std::max(1.0f, height / 720.0f);

	DrawBackground(drawList, displaySize, ease);
	DrawMenu(drawList, displaySize, scale, ease);
	DrawHelpers(drawList, displaySize, scale, ease);
}

void Overlay::Render(Draw::DrawContext *draw) {
	if (!ready_ || !context_ || !draw) {
		return;
	}

	const bool hasRAAlerts = !RetroAchievements().Notifications().empty();
	if (!visible_ && !hasRAAlerts) {
		return;
	}

	ImGui::SetCurrentContext(context_);
	const float width = (float)std::max(1, g_display.pixel_xres);
	const float height = (float)std::max(1, g_display.pixel_yres);
	ImGuiIO &io = ImGui::GetIO();
	io.DisplaySize = ImVec2(width, height);
	io.DeltaTime = 1.0f / std::max(1.0f, g_display.display_hz);

	const float orthoW = g_display.dp_xres > 0 ? (float)g_display.dp_xres : width;
	const float orthoH = g_display.dp_yres > 0 ? (float)g_display.dp_yres : height;
	const float scale = std::max(1.0f, height / 720.0f);
	const float loadedFontSize = io.Fonts->Fonts.Size > 0 ? io.Fonts->Fonts[0]->FontSize : 21.0f;
	if (loadedFontSize > 0.0f) {
		io.FontGlobalScale = (Display::FontSize * scale) / loadedFontSize;
	}

	ImGui_ImplThin3d_NewFrame(draw, ComputeOrthoMatrix(orthoW, orthoH, draw->GetDeviceCaps().coordConvention));
	ImGui::NewFrame();
	if (visible_) {
		DrawUI(width, height, io.DeltaTime);
	}
	DrawRAAlerts(draw, ImGui::GetForegroundDrawList(), ImVec2(width, height), scale, io.DeltaTime);
	ImGui::Render();

	const Draw::RenderPassInfo overlayPass{
		Draw::RPAction::KEEP,
		Draw::RPAction::KEEP,
		Draw::RPAction::KEEP,
		0x00000000,
		1.0f,
		0,
		"GBAStationOverlay",
	};
	draw->BindFramebufferAsRenderTarget(nullptr, overlayPass, "GBAStationOverlay");
	ImGui_ImplThin3d_RenderDrawData(ImGui::GetDrawData(), draw);
}

}  // namespace GBAStation
