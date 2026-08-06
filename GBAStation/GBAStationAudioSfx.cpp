#include "GBAStation/GBAStationAudioSfx.h"

#include "ext/minimp3/minimp3_ex.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <vector>

namespace GBAStation {
namespace {

constexpr float kTrophyVolume = 0.85f;

AudioSfx g_audioSfx;

bool ReadFileBytes(const char *path, std::vector<uint8_t> *data) {
	data->clear();
	FILE *fp = fopen(path, "rb");
	if (!fp) {
		return false;
	}
	if (fseek(fp, 0, SEEK_END) != 0) {
		fclose(fp);
		return false;
	}
	const long size = ftell(fp);
	if (size <= 0) {
		fclose(fp);
		return false;
	}
	rewind(fp);
	data->resize((size_t)size);
	const size_t readSize = fread(data->data(), 1, data->size(), fp);
	fclose(fp);
	if (readSize != data->size()) {
		data->clear();
		return false;
	}
	return true;
}

inline s16 Clamp16(int sample) {
	if (sample < -32768) {
		return -32768;
	}
	if (sample > 32767) {
		return 32767;
	}
	return (s16)sample;
}

}  // namespace

AudioSfx &TrophySfx() {
	return g_audioSfx;
}

bool AudioSfx::Load(LogCallback log) {
	log_ = std::move(log);
	Shutdown();

	SynthesizeUiSounds();

	if (LoadMp3File("romfs:/assets/trophy.mp3")) {
		LogMessage(log_, "GBAStation sfx loaded romfs:/assets/trophy.mp3 rate=%d channels=%d samples=%zu",
			trophySampleRate_, trophyChannels_, trophySamples_.size());
		return true;
	}
	if (LoadMp3File("sdmc:/GBAStation/PSP/assets/trophy.mp3")) {
		LogMessage(log_, "GBAStation sfx loaded sdmc:/GBAStation/PSP/assets/trophy.mp3 rate=%d channels=%d samples=%zu",
			trophySampleRate_, trophyChannels_, trophySamples_.size());
		return true;
	}

	LogMessage(log_, "GBAStation sfx trophy.mp3 not found");
	return false;
}

void AudioSfx::Shutdown() {
	std::lock_guard<std::mutex> lock(mutex_);
	trophySamples_.clear();
	trophyChannels_ = 0;
	trophySampleRate_ = 0;
	trophyCursor_ = 0.0;
	trophyPlaying_ = false;
	uiSamples_[0].clear();
	uiSamples_[1].clear();
	uiSamples_[2].clear();
	uiSampleRate_ = 0;
	uiCursor_ = 0.0;
	uiPlaying_ = false;
	uiSound_ = 0;
}

void AudioSfx::SynthesizeUiSounds() {
	// Synthesized UI ticks in the style of the 3DS frontend: a short sine burst
	// with a fast decay.  Focus is a soft tick, confirm slightly brighter and
	// longer, cancel a lower dip.
	constexpr float kFreqs[3] = {1320.0f, 1760.0f, 880.0f};
	constexpr float kDurations[3] = {0.035f, 0.055f, 0.045f};
	constexpr float kVolumes[3] = {0.22f, 0.26f, 0.22f};
	const int rate = 48000;
	for (int s = 0; s < 3; ++s) {
		uiSamples_[s].clear();
		const int count = (int)(kDurations[s] * (float)rate);
		uiSamples_[s].reserve((size_t)count * 2);
		for (int i = 0; i < count; ++i) {
			const float t = (float)i / (float)rate;
			const float env = std::exp(-t * 55.0f);
			const float v = std::sin(6.2831853f * kFreqs[s] * t) * kVolumes[s] * env;
			const s16 sample = (s16)std::clamp((int)(v * 32767.0f), -32768, 32767);
			uiSamples_[s].push_back(sample);
			uiSamples_[s].push_back(sample);
		}
	}
	uiSampleRate_ = rate;
}

void AudioSfx::PlayUiSound(UiSound sound) {
	std::lock_guard<std::mutex> lock(mutex_);
	if (uiSampleRate_ <= 0 || uiSamples_[(int)sound].empty()) {
		return;
	}
	uiCursor_ = 0.0;
	uiPlaying_ = true;
	uiSound_ = (int)sound;
}

void AudioSfx::PlayTrophy() {
	std::lock_guard<std::mutex> lock(mutex_);
	if (trophySamples_.empty() || trophyChannels_ <= 0 || trophySampleRate_ <= 0) {
		return;
	}
	trophyCursor_ = 0.0;
	trophyPlaying_ = true;
}

void AudioSfx::Mix(s16 *output, int frames, int outputRate) {
	if (!output || frames <= 0 || outputRate <= 0) {
		return;
	}

	std::lock_guard<std::mutex> lock(mutex_);
	const bool trophyActive = trophyPlaying_ && !trophySamples_.empty() &&
		trophyChannels_ > 0 && trophySampleRate_ > 0;
	const bool uiActive = uiPlaying_ && uiSampleRate_ > 0 &&
		(int)uiSound_ >= 0 && (int)uiSound_ < 3 && !uiSamples_[uiSound_].empty();
	if (!trophyActive && !uiActive) {
		return;
	}

	const int totalTrophyFrames = trophyActive ? (int)trophySamples_.size() / trophyChannels_ : 0;
	const double trophyStep = trophyActive ? (double)trophySampleRate_ / (double)outputRate : 1.0;
	const int totalUiFrames = uiActive ? (int)uiSamples_[uiSound_].size() / 2 : 0;
	const double uiStep = uiActive ? (double)uiSampleRate_ / (double)outputRate : 1.0;
	for (int i = 0; i < frames; ++i) {
		int mixedLeft = output[i * 2];
		int mixedRight = output[i * 2 + 1];

		if (trophyActive) {
			const int sourceFrame = (int)trophyCursor_;
			if (sourceFrame >= totalTrophyFrames) {
				trophyPlaying_ = false;
				trophyCursor_ = 0.0;
			} else {
				const int sourceIndex = sourceFrame * trophyChannels_;
				const int left = trophySamples_[sourceIndex];
				const int right = trophyChannels_ > 1 ? trophySamples_[sourceIndex + 1] : left;
				mixedLeft += (int)((float)left * kTrophyVolume);
				mixedRight += (int)((float)right * kTrophyVolume);
				trophyCursor_ += trophyStep;
			}
		}

		if (uiActive) {
			const int sourceFrame = (int)uiCursor_;
			if (sourceFrame >= totalUiFrames) {
				uiPlaying_ = false;
				uiCursor_ = 0.0;
			} else {
				const int sourceIndex = sourceFrame * 2;
				mixedLeft += uiSamples_[uiSound_][sourceIndex];
				mixedRight += uiSamples_[uiSound_][sourceIndex + 1];
				uiCursor_ += uiStep;
			}
		}

		output[i * 2] = Clamp16(mixedLeft);
		output[i * 2 + 1] = Clamp16(mixedRight);
	}
}

bool AudioSfx::LoadMp3File(const char *path) {
	std::vector<uint8_t> data;
	if (!ReadFileBytes(path, &data)) {
		return false;
	}

	mp3dec_t decoder;
	mp3dec_init(&decoder);
	mp3dec_file_info_t info{};
	const int result = mp3dec_load_buf(&decoder, data.data(), data.size(), &info, nullptr, nullptr);
	if (result < 0 || info.samples == 0 || !info.buffer || info.channels <= 0 || info.hz <= 0) {
		if (info.buffer) {
			free(info.buffer);
		}
		LogMessage(log_, "GBAStation sfx failed to decode %s", path);
		return false;
	}

	{
		std::lock_guard<std::mutex> lock(mutex_);
		trophySamples_.assign(info.buffer, info.buffer + info.samples);
		trophyChannels_ = info.channels;
		trophySampleRate_ = info.hz;
		trophyCursor_ = 0.0;
		trophyPlaying_ = false;
	}
	free(info.buffer);
	return true;
}

}  // namespace GBAStation
