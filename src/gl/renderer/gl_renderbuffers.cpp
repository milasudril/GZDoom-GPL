/*
** gl_renderbuffers.cpp
** Render buffers used during rendering
**
**---------------------------------------------------------------------------
** Copyright 2016 Magnus Norddahl
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
**
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. The name of the author may not be used to endorse or promote products
**    derived from this software without specific prior written permission.
** 4. When not used as part of GZDoom or a GZDoom derivative, this code will be
**    covered by the terms of the GNU Lesser General Public License as published
**    by the Free Software Foundation; either version 2.1 of the License, or (at
**    your option) any later version.
** 5. Full disclosure of the entire project's source code, except for third
**    party libraries is mandatory. (NOTE: This clause is non-negotiable!)
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
** IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
** IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
** NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
** THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**---------------------------------------------------------------------------
**
*/

#include "gl/system/gl_system.h"
#include "files.h"
#include "m_swap.h"
#include "v_video.h"
#include "gl/gl_functions.h"
#include "vectors.h"
#include "gl/system/gl_interface.h"
#include "gl/system/gl_framebuffer.h"
#include "gl/system/gl_cvars.h"
#include "gl/renderer/gl_renderer.h"
#include "gl/renderer/gl_renderbuffers.h"
#include "w_wad.h"
#include "i_system.h"
#include "doomerrors.h"

CVAR(Int, gl_multisample, 1, CVAR_ARCHIVE|CVAR_GLOBALCONFIG);
CVAR(Bool, gl_renderbuffers, true, CVAR_ARCHIVE|CVAR_GLOBALCONFIG);

//==========================================================================
//
// Initialize render buffers and textures used in rendering passes
//
//==========================================================================

FGLRenderBuffers::FGLRenderBuffers()
{
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, (GLint*)&mOutputFB);
}

//==========================================================================
//
// Free render buffer resources
//
//==========================================================================

FGLRenderBuffers::~FGLRenderBuffers()
{
	ClearScene();
	ClearHud();
	ClearBloom();
}

void FGLRenderBuffers::ClearScene()
{
	DeleteFrameBuffer(mSceneFB);
	DeleteFrameBuffer(mSceneTextureFB);
	DeleteRenderBuffer(mSceneMultisample);
	DeleteRenderBuffer(mSceneDepthStencil);
	DeleteRenderBuffer(mSceneDepth);
	DeleteRenderBuffer(mSceneStencil);
	DeleteTexture(mSceneTexture);
}

void FGLRenderBuffers::ClearHud()
{
	DeleteFrameBuffer(mHudFB);
	DeleteTexture(mHudTexture);
}

void FGLRenderBuffers::ClearBloom()
{
	for (int i = 0; i < NumBloomLevels; i++)
	{
		auto &level = BloomLevels[i];
		DeleteFrameBuffer(level.HFramebuffer);
		DeleteFrameBuffer(level.VFramebuffer);
		DeleteTexture(level.HTexture);
		DeleteTexture(level.VTexture);
		level = FGLBloomTextureLevel();
	}
}

void FGLRenderBuffers::DeleteTexture(GLuint &handle)
{
	if (handle != 0)
		glDeleteTextures(1, &handle);
	handle = 0;
}

void FGLRenderBuffers::DeleteRenderBuffer(GLuint &handle)
{
	if (handle != 0)
		glDeleteRenderbuffers(1, &handle);
	handle = 0;
}

void FGLRenderBuffers::DeleteFrameBuffer(GLuint &handle)
{
	if (handle != 0)
		glDeleteFramebuffers(1, &handle);
	handle = 0;
}

//==========================================================================
//
// Makes sure all render buffers have sizes suitable for rending at the
// specified resolution
//
//==========================================================================

void FGLRenderBuffers::Setup(int width, int height)
{
	int samples = GetCvarSamples();

	if (width == mWidth && height == mHeight && mSamples != samples)
	{
		CreateScene(mWidth, mHeight, samples);
		mSamples = samples;
	}
	else if (width > mWidth || height > mHeight)
	{
		CreateScene(width, height, samples);
		CreateHud(width, height);
		CreateBloom(width, height);
		mWidth = width;
		mHeight = height;
		mSamples = samples;
	}

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glBindRenderbuffer(GL_RENDERBUFFER, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

//==========================================================================
//
// Creates the scene buffers
//
//==========================================================================

void FGLRenderBuffers::CreateScene(int width, int height, int samples)
{
	ClearScene();

	mSceneTexture = Create2DTexture(GetHdrFormat(), width, height);
	mSceneTextureFB = CreateFrameBuffer(mSceneTexture);

	if (samples > 1)
		mSceneMultisample = CreateRenderBuffer(GetHdrFormat(), samples, width, height);

	if ((gl.flags & RFL_NO_DEPTHSTENCIL) != 0)
	{
		mSceneDepth = CreateRenderBuffer(GL_DEPTH_COMPONENT24, samples, width, height);
		mSceneStencil = CreateRenderBuffer(GL_STENCIL_INDEX8, samples, width, height);
		mSceneFB = CreateFrameBuffer(samples > 1 ? mSceneMultisample : mSceneTexture, mSceneDepth, mSceneStencil, samples > 1);
	}
	else
	{
		mSceneDepthStencil = CreateRenderBuffer(GL_DEPTH24_STENCIL8, samples, width, height);
		mSceneFB = CreateFrameBuffer(samples > 1 ? mSceneMultisample : mSceneTexture, mSceneDepthStencil, samples > 1);
	}
}

//==========================================================================
//
// Creates the post-tonemapping-step buffers
//
//==========================================================================

void FGLRenderBuffers::CreateHud(int width, int height)
{
	ClearHud();
	mHudTexture = Create2DTexture(GetHdrFormat(), width, height);
	mHudFB = CreateFrameBuffer(mHudTexture);
}

//==========================================================================
//
// Creates bloom pass working buffers
//
//==========================================================================

void FGLRenderBuffers::CreateBloom(int width, int height)
{
	ClearBloom();

	int bloomWidth = MAX(width / 2, 1);
	int bloomHeight = MAX(height / 2, 1);
	for (int i = 0; i < NumBloomLevels; i++)
	{
		auto &level = BloomLevels[i];
		level.Width = MAX(bloomWidth / 2, 1);
		level.Height = MAX(bloomHeight / 2, 1);

		level.VTexture = Create2DTexture(GetHdrFormat(), level.Width, level.Height);
		level.HTexture = Create2DTexture(GetHdrFormat(), level.Width, level.Height);
		level.VFramebuffer = CreateFrameBuffer(level.VTexture);
		level.HFramebuffer = CreateFrameBuffer(level.HTexture);

		bloomWidth = level.Width;
		bloomHeight = level.Height;
	}
}

//==========================================================================
//
// Fallback support for older OpenGL where RGBA16F might not be available
//
//==========================================================================

GLuint FGLRenderBuffers::GetHdrFormat()
{
	return ((gl.flags & RFL_NO_RGBA16F) != 0) ? GL_RGBA8 : GL_RGBA16F;
}

//==========================================================================
//
// Converts the CVAR multisample value into a valid level for OpenGL
//
//==========================================================================

int FGLRenderBuffers::GetCvarSamples()
{
	int maxSamples = 0;
	glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);

	int samples = clamp((int)gl_multisample, 0, maxSamples);

	int count;
	for (count = 0; samples > 0; count++)
		samples >>= 1;
	return count;
}

//==========================================================================
//
// Creates a 2D texture defaulting to linear filtering and clamp to edge
//
//==========================================================================

GLuint FGLRenderBuffers::Create2DTexture(GLuint format, int width, int height)
{
	GLuint type = (format == GL_RGBA16F) ? GL_FLOAT : GL_UNSIGNED_BYTE;
	GLuint handle = 0;
	glGenTextures(1, &handle);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, handle);
	glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, GL_RGBA, type, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	return handle;
}

//==========================================================================
//
// Creates a render buffer
//
//==========================================================================

GLuint FGLRenderBuffers::CreateRenderBuffer(GLuint format, int width, int height)
{
	GLuint handle = 0;
	glGenRenderbuffers(1, &handle);
	glBindRenderbuffer(GL_RENDERBUFFER, handle);
	glRenderbufferStorage(GL_RENDERBUFFER, format, width, height);
	return handle;
}

GLuint FGLRenderBuffers::CreateRenderBuffer(GLuint format, int samples, int width, int height)
{
	if (samples <= 1)
		return CreateRenderBuffer(format, width, height);

	GLuint handle = 0;
	glGenRenderbuffers(1, &handle);
	glBindRenderbuffer(GL_RENDERBUFFER, handle);
	glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, format, width, height);
	return handle;
}

//==========================================================================
//
// Creates a frame buffer
//
//==========================================================================

GLuint FGLRenderBuffers::CreateFrameBuffer(GLuint colorbuffer)
{
	GLuint handle = 0;
	glGenFramebuffers(1, &handle);
	glBindFramebuffer(GL_FRAMEBUFFER, handle);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorbuffer, 0);
	CheckFrameBufferCompleteness();
	return handle;
}

GLuint FGLRenderBuffers::CreateFrameBuffer(GLuint colorbuffer, GLuint depthstencil, bool colorIsARenderBuffer)
{
	GLuint handle = 0;
	glGenFramebuffers(1, &handle);
	glBindFramebuffer(GL_FRAMEBUFFER, handle);
	if (colorIsARenderBuffer)
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, colorbuffer);
	else
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorbuffer, 0);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depthstencil);
	CheckFrameBufferCompleteness();
	return handle;
}

GLuint FGLRenderBuffers::CreateFrameBuffer(GLuint colorbuffer, GLuint depth, GLuint stencil, bool colorIsARenderBuffer)
{
	GLuint handle = 0;
	glGenFramebuffers(1, &handle);
	glBindFramebuffer(GL_FRAMEBUFFER, handle);
	if (colorIsARenderBuffer)
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, colorbuffer);
	else
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorbuffer, 0);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, stencil);
	CheckFrameBufferCompleteness();
	return handle;
}

//==========================================================================
//
// Verifies that the frame buffer setup is valid
//
//==========================================================================

void FGLRenderBuffers::CheckFrameBufferCompleteness()
{
	GLenum result = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if (result == GL_FRAMEBUFFER_COMPLETE)
		return;

	FString error = "glCheckFramebufferStatus failed: ";
	switch (result)
	{
	default: error.AppendFormat("error code %d", (int)result); break;
	case GL_FRAMEBUFFER_UNDEFINED: error << "GL_FRAMEBUFFER_UNDEFINED"; break;
	case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT: error << "GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT"; break;
	case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT: error << "GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT"; break;
	case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER: error << "GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER"; break;
	case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER: error << "GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER"; break;
	case GL_FRAMEBUFFER_UNSUPPORTED: error << "GL_FRAMEBUFFER_UNSUPPORTED"; break;
	case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE: error << "GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE"; break;
	case GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS: error << "GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS"; break;
	}
	I_FatalError(error);
}

//==========================================================================
//
// Resolves the multisample frame buffer by copying it to the scene texture
//
//==========================================================================

void FGLRenderBuffers::BlitSceneToTexture()
{
	if (mSamples <= 1)
		return;

	glBindFramebuffer(GL_READ_FRAMEBUFFER, mSceneFB);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, mSceneTextureFB);
	glBlitFramebuffer(0, 0, mWidth, mHeight, 0, 0, mWidth, mHeight, GL_COLOR_BUFFER_BIT, GL_NEAREST);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
}

//==========================================================================
//
// Makes the scene frame buffer active (multisample, depth, stecil, etc.)
//
//==========================================================================

void FGLRenderBuffers::BindSceneFB()
{
	glBindFramebuffer(GL_FRAMEBUFFER, mSceneFB);
}

//==========================================================================
//
// Makes the scene texture frame buffer active (final 2D texture only) 
//
//==========================================================================

void FGLRenderBuffers::BindSceneTextureFB()
{
	glBindFramebuffer(GL_FRAMEBUFFER, mSceneTextureFB);
}

//==========================================================================
//
// Makes the 2D/HUD frame buffer active 
//
//==========================================================================

void FGLRenderBuffers::BindHudFB()
{
	if (gl_tonemap != 0)
		glBindFramebuffer(GL_FRAMEBUFFER, mHudFB);
	else
		glBindFramebuffer(GL_FRAMEBUFFER, mSceneTextureFB);
}

//==========================================================================
//
// Makes the screen frame buffer active
//
//==========================================================================

void FGLRenderBuffers::BindOutputFB()
{
	glBindFramebuffer(GL_FRAMEBUFFER, mOutputFB);
}

//==========================================================================
//
// Binds the scene frame buffer texture to the specified texture unit
//
//==========================================================================

void FGLRenderBuffers::BindSceneTexture(int index)
{
	glActiveTexture(GL_TEXTURE0 + index);
	glBindTexture(GL_TEXTURE_2D, mSceneTexture);
}

//==========================================================================
//
// Binds the 2D/HUD frame buffer texture to the specified texture unit
//
//==========================================================================

void FGLRenderBuffers::BindHudTexture(int index)
{
	glActiveTexture(GL_TEXTURE0 + index);
	if (gl_tonemap != 0)
		glBindTexture(GL_TEXTURE_2D, mHudTexture);
	else
		glBindTexture(GL_TEXTURE_2D, mSceneTexture);
}

//==========================================================================
//
// Returns true if render buffers are supported and should be used
//
//==========================================================================

bool FGLRenderBuffers::IsEnabled()
{
	return gl_renderbuffers && gl.glslversion != 0;
}
