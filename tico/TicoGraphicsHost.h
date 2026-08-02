#pragma once

#include <string>

#include "Common/GraphicsContext.h"
#include "Core/CoreParameter.h"

bool InitializeTicoGraphicsHost(std::string *error_message, GraphicsContext **ctx, GPUCore core);
void ShutdownTicoGraphicsHost();

class TicoGraphicsHost {
public:
	bool Init(std::string *error_message, GraphicsContext **ctx, GPUCore core);
	void Shutdown();

private:
	GraphicsContext *gfx_ = nullptr;
};
