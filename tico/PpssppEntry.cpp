#include "ppsspp_config.h"

#if PPSSPP_PLATFORM(SWITCH)

#include "tico/PpssppRuntime.h"
#include "tico/TicoConfig.h"
#include "tico/TicoLogger.h"
#include "tico/TicoMain.h"

#include <sys/stat.h>

int main(int argc, char **argv) {
	Tico::FileLogger logger;
	if constexpr (Tico::Logging::Enabled) {
		mkdir(Tico::Paths::Root, 0777);
		mkdir(Tico::Paths::Debug, 0777);
		logger.Open(Tico::Paths::BootLog);
		logger.Log("ppsspp entry argc=%d build=20260802-switchvk-directhost-v8", argc);
	}
	Tico::PpssppRuntime runtime(logger.Callback());
	Tico::Main app(runtime, logger.Callback());
	return app.Run(argc, argv);
}

#endif
