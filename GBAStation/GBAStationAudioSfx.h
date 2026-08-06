#pragma once

#include "Common/CommonTypes.h"
#include "GBAStation/GBAStationCoreConfig.h"

#include <mutex>
#include <vector>

namespace GBAStation {

class AudioSfx {
public:
	enum class UiSound {
		Focus,
		Confirm,
		Cancel,
	};

	bool Load(LogCallback log = {});
	void Shutdown();
	void PlayTrophy();
	void PlayUiSound(UiSound sound);
	void Mix(s16 *output, int frames, int outputRate);

private:
	bool LoadMp3File(const char *path);
	void SynthesizeUiSounds();

	LogCallback log_;
	std::mutex mutex_;
	std::vector<s16> trophySamples_;
	int trophyChannels_ = 0;
	int trophySampleRate_ = 0;
	double trophyCursor_ = 0.0;
	bool trophyPlaying_ = false;

	std::vector<s16> uiSamples_[3];
	int uiSampleRate_ = 0;
	double uiCursor_ = 0.0;
	bool uiPlaying_ = false;
	int uiSound_ = 0;
};

AudioSfx &TrophySfx();

}  // namespace GBAStation
