#pragma once

// GLCommonCompat.h - Compatibility defines for GLAD (GL 4.3 Core)
// These constants are extensions or legacy GL that GLAD's core profile doesn't
// define, but PPSSPP code uses. They are the same values as defined in GLEW.

#ifdef USE_GLAD

// Legacy constants (pre-core profile)
#ifndef GL_LUMINANCE
#define GL_LUMINANCE 0x1909
#endif

#ifndef GL_LUMINANCE_ALPHA
#define GL_LUMINANCE_ALPHA 0x190A
#endif

#ifndef GL_MAX_CLIP_PLANES
#define GL_MAX_CLIP_PLANES 0x0D32 // Same as GL_MAX_CLIP_DISTANCES
#endif

// GL_R is not a valid constant - should be GL_RED (0x1903)
#ifndef GL_R
#define GL_R 0x1903 // Map to GL_RED
#endif

// S3TC/DXT texture compression extension constants
#ifndef GL_COMPRESSED_RGB_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGB_S3TC_DXT1_EXT 0x83F0
#endif

#ifndef GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#endif

#ifndef GL_COMPRESSED_RGBA_S3TC_DXT3_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT 0x83F2
#endif

#ifndef GL_COMPRESSED_RGBA_S3TC_DXT5_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#endif

// Anisotropic filtering extension constants
#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#endif

#ifndef GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif

// BGRA extension (common on desktop, may be needed)
#ifndef GL_BGRA_EXT
#define GL_BGRA_EXT 0x80E1
#endif

// Legacy EXT framebuffer constants - map to core equivalents
#ifndef GL_RENDERBUFFER_EXT
#define GL_RENDERBUFFER_EXT GL_RENDERBUFFER
#endif

#ifndef GL_FRAMEBUFFER_EXT
#define GL_FRAMEBUFFER_EXT GL_FRAMEBUFFER
#endif

#ifndef GL_COLOR_ATTACHMENT0_EXT
#define GL_COLOR_ATTACHMENT0_EXT GL_COLOR_ATTACHMENT0
#endif

#ifndef GL_DEPTH_ATTACHMENT_EXT
#define GL_DEPTH_ATTACHMENT_EXT GL_DEPTH_ATTACHMENT
#endif

#ifndef GL_STENCIL_ATTACHMENT_EXT
#define GL_STENCIL_ATTACHMENT_EXT GL_STENCIL_ATTACHMENT
#endif

#ifndef GL_FRAMEBUFFER_COMPLETE_EXT
#define GL_FRAMEBUFFER_COMPLETE_EXT GL_FRAMEBUFFER_COMPLETE
#endif

#ifndef GL_FRAMEBUFFER_UNSUPPORTED_EXT
#define GL_FRAMEBUFFER_UNSUPPORTED_EXT GL_FRAMEBUFFER_UNSUPPORTED
#endif

#ifndef GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT_EXT
#define GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT_EXT                               \
  GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT
#endif

#ifndef GL_DEPTH_STENCIL_EXT
#define GL_DEPTH_STENCIL_EXT GL_DEPTH_STENCIL
#endif

// Legacy EXT framebuffer function mappings to core functions
#define glGenFramebuffersEXT glGenFramebuffers
#define glDeleteFramebuffersEXT glDeleteFramebuffers
#define glBindFramebufferEXT glBindFramebuffer
#define glCheckFramebufferStatusEXT glCheckFramebufferStatus
#define glFramebufferTexture2DEXT glFramebufferTexture2D
#define glFramebufferRenderbufferEXT glFramebufferRenderbuffer
#define glGenRenderbuffersEXT glGenRenderbuffers
#define glDeleteRenderbuffersEXT glDeleteRenderbuffers
#define glBindRenderbufferEXT glBindRenderbuffer
#define glRenderbufferStorageEXT glRenderbufferStorage

// NV_copy_image extension - map to core glCopyImageSubData
#define glCopyImageSubDataNV glCopyImageSubData

// GL 4.5 DSA function - glGetTextureSubImage (not in GL 4.3!)
// This function doesn't exist in GL 4.3 Core, and doesn't have a direct
// equivalent. We define a stub that should never be called (code checks
// gl_extensions.VersionGEThan(4, 5) first)
static inline void glGetTextureSubImage(GLuint texture, GLint level,
                                        GLint xoffset, GLint yoffset,
                                        GLint zoffset, GLsizei width,
                                        GLsizei height, GLsizei depth,
                                        GLenum format, GLenum type,
                                        GLsizei bufSize, void *pixels) {
  // Stub for GL 4.5 function - should never actually be called on GL 4.3
  (void)texture;
  (void)level;
  (void)xoffset;
  (void)yoffset;
  (void)zoffset;
  (void)width;
  (void)height;
  (void)depth;
  (void)format;
  (void)type;
  (void)bufSize;
  (void)pixels;
}

// GL 4.4 function - glBufferStorage (not in GL 4.3!)
// Code checks for ARB_buffer_storage extension at runtime, but we need the
// symbol to compile.
#ifndef GL_MAP_PERSISTENT_BIT
#define GL_MAP_PERSISTENT_BIT 0x0040
#endif
#ifndef GL_MAP_COHERENT_BIT
#define GL_MAP_COHERENT_BIT 0x0080
#endif
static inline void glBufferStorage(GLenum target, GLsizeiptr size,
                                   const void *data, GLbitfield flags) {
  // Stub for GL 4.4 function - code checks for extension support at runtime
  (void)target;
  (void)size;
  (void)data;
  (void)flags;
}

#endif // USE_GLAD
