
#include "Common/GPU/OpenGL/GLFeatures.h"
#include "Common/Log.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/System.h"

#include "libretro/LibretroGLContext.h"

#ifndef USE_GLAD
// rglgen is used for dynamic GL function loading in standard libretro
#include <glsym/rglgen.h>
extern const struct rglgen_sym_map rglgen_symbol_map_ppsspp;
#endif

bool LibretroGLContext::Init() {
  if (!LibretroHWRenderContext::Init(true))
    return false;

  g_Config.iGPUBackend = (int)GPUBackend::OPENGL;
  return true;
}

void LibretroGLContext::CreateDrawContext() {
  if (!glewInitDone) {
#ifdef USE_GLAD
    // GLAD is already initialized by the main app, just check extensions
    CheckGLExtensions();
#else
    rglgen_resolve_symbols_custom(&eglGetProcAddress,
                                  &rglgen_symbol_map_ppsspp);
    CheckGLExtensions();
#endif
    glewInitDone = true;
  }
  draw_ = Draw::T3DCreateGLContext(false);
  renderManager_ = (GLRenderManager *)draw_->GetNativeObject(
      Draw::NativeObject::RENDER_MANAGER);
  renderManager_->SetInflightFrames(g_Config.iInflightFrames);
  SetGPUBackend(GPUBackend::OPENGL);
  draw_->CreatePresets();
}

void LibretroGLContext::DestroyDrawContext() {
  LibretroHWRenderContext::DestroyDrawContext();
  renderManager_ = nullptr;
}
