#pragma once

#include <string>

#include "Common/GraphicsContext.h"
#include "Core/CoreParameter.h"

bool InitializeGBAStationGraphicsHost(std::string *error_message, GraphicsContext **ctx, GPUCore core);
void ShutdownGBAStationGraphicsHost();

class GBAStationGraphicsHost {
public:
	bool Init(std::string *error_message, GraphicsContext **ctx, GPUCore core);
	void Shutdown();

private:
	GraphicsContext *gfx_ = nullptr;
};
