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

// WAV loading borrowed from the 3DS frontend's libnx_sink: RIFF fmt/data chunks,
// 16-bit PCM mono or stereo, linearly resampled to kUiRate and widened to stereo.
struct UiClip {
	std::vector<s16> samples;  // interleaved stereo at kUiRate
};

u16 ReadU16(const u8 *data) {
	return (u16)(data[0] | ((u16)data[1] << 8));
}

u32 ReadU32(const u8 *data) {
	return (u32)data[0] | ((u32)data[1] << 8) | ((u32)data[2] << 16) | ((u32)data[3] << 24);
}

bool LoadUiWav(const char *path, UiClip *clip, int outputRate) {
	FILE *file = fopen(path, "rb");
	if (!file) {
		return false;
	}
	fseek(file, 0, SEEK_END);
	const long size = ftell(file);
	rewind(file);
	if (size < 44 || size > 4 * 1024 * 1024) {
		fclose(file);
		return false;
	}
	std::vector<u8> bytes((size_t)size);
	const bool readOk = fread(bytes.data(), 1, bytes.size(), file) == bytes.size();
	fclose(file);
	if (!readOk || memcmp(bytes.data(), "RIFF", 4) != 0 || memcmp(bytes.data() + 8, "WAVE", 4) != 0) {
		return false;
	}

	u16 format = 0, channels = 0, bits = 0;
	u32 sampleRate = 0;
	const u8 *pcm = nullptr;
	size_t pcmBytes = 0;
	for (size_t offset = 12; offset + 8 <= bytes.size();) {
		const u32 chunkSize = ReadU32(bytes.data() + offset + 4);
		if (offset + 8 + chunkSize > bytes.size()) {
			break;
		}
		if (memcmp(bytes.data() + offset, "fmt ", 4) == 0 && chunkSize >= 16) {
			format = ReadU16(bytes.data() + offset + 8);
			channels = ReadU16(bytes.data() + offset + 10);
			sampleRate = ReadU32(bytes.data() + offset + 12);
			bits = ReadU16(bytes.data() + offset + 22);
		} else if (memcmp(bytes.data() + offset, "data", 4) == 0) {
			pcm = bytes.data() + offset + 8;
			pcmBytes = chunkSize;
		}
		offset += 8 + chunkSize + (chunkSize & 1u);
	}
	if (format != 1 || (channels != 1 && channels != 2) || bits != 16 ||
		sampleRate == 0 || !pcm || pcmBytes < channels * sizeof(s16)) {
		return false;
	}

	const auto *input = reinterpret_cast<const s16 *>(pcm);
	const size_t inputFrames = pcmBytes / (channels * sizeof(s16));
	const size_t outputFrames = std::max<size_t>(1, inputFrames * (u32)outputRate / sampleRate);
	clip->samples.resize(outputFrames * 2);
	const double sourceStep = (double)sampleRate / (double)outputRate;
	for (size_t frame = 0; frame < outputFrames; ++frame) {
		const double source = frame * sourceStep;
		const size_t first = std::min((size_t)source, inputFrames - 1);
		const size_t second = std::min(first + 1, inputFrames - 1);
		const float fraction = (float)(source - first);
		for (u16 channel = 0; channel < 2; ++channel) {
			const size_t sourceChannel = channels == 1 ? 0 : channel;
			const float a = input[first * channels + sourceChannel];
			const float b = input[second * channels + sourceChannel];
			clip->samples[frame * 2 + channel] = (s16)((a + (b - a) * fraction) * 0.70f);
		}
	}
	return true;
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
	// The 3DS frontend's real UI sound set: SeNaviFocus (focus move),
	// SeBtnDecide (confirm), SeFooterDecideFinish (back/cancel).
	constexpr const char *kUiFiles[3] = {
		"romfs:/assets/sounds/SeNaviFocus.wav",
		"romfs:/assets/sounds/SeBtnDecide.wav",
		"romfs:/assets/sounds/SeFooterDecideFinish.wav",
	};
	constexpr int kUiRate = 48000;
	for (int s = 0; s < 3; ++s) {
		UiClip clip;
		if (!LoadUiWav(kUiFiles[s], &clip, kUiRate)) {
			LogMessage(log_, "GBAStation sfx UI sound unavailable %s", kUiFiles[s]);
			continue;
		}
		uiSamples_[s] = std::move(clip.samples);
	}
	uiSampleRate_ = kUiRate;
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
