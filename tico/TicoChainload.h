#pragma once

#include "tico/TicoCoreConfig.h"

namespace Tico {

void SetLauncherReturnPath(const char *path);
void SetExternalSessionToken(const char *token);
bool ChainloadLauncher(LogCallback log = {});

}  // namespace Tico
