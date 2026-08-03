#include "ppsspp_config.h"

#if PPSSPP_PLATFORM(SWITCH)

#include "GBAStation/PpssppRuntime.h"
#include "GBAStation/GBAStationConfig.h"
#include "GBAStation/GBAStationLogger.h"
#include "GBAStation/GBAStationMain.h"

#include <sys/stat.h>

int main(int argc, char **argv) {
	GBAStation::FileLogger logger;
	if constexpr (GBAStation::Logging::Enabled) {
		mkdir(GBAStation::Paths::Root, 0777);
		mkdir(GBAStation::Paths::Debug, 0777);
		logger.Open(GBAStation::Paths::BootLog);
		logger.Log("ppsspp entry argc=%d build=20260802-switchvk-directhost-v8", argc);
	}
	GBAStation::PpssppRuntime runtime(logger.Callback());
	GBAStation::Main app(runtime, logger.Callback());
	return app.Run(argc, argv);
}

#endif
