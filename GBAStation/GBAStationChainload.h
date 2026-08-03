#pragma once

#include "GBAStation/GBAStationCoreConfig.h"

namespace GBAStation {

void SetLauncherReturnPath(const char *path);
void SetExternalSessionToken(const char *token);
bool ChainloadLauncher(LogCallback log = {});

}  // namespace GBAStation
