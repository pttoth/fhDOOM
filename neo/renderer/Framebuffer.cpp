/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 2016 Johannes Ohlemacher (http://github.com/eXistence/fhDOOM)

This file is part of the Doom 3 GPL Source Code (?Doom 3 Source Code?).

Doom 3 Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

#include "Framebuffer.h"
#include "Image.h"
#include "tr_local.h"

fhFramebuffer* fhFramebuffer::currentDrawBuffer = nullptr;
fhFramebuffer* fhFramebuffer::shadowmapFramebuffer = nullptr;
fhFramebuffer* fhFramebuffer::defaultFramebuffer = nullptr;
fhFramebuffer* fhFramebuffer::currentDepthFramebuffer = nullptr;
fhFramebuffer* fhFramebuffer::currentRenderFramebuffer = nullptr;
fhFramebuffer* fhFramebuffer::bloomFramebuffer = nullptr;
fhFramebuffer* fhFramebuffer::bloomTmpFramebuffer = nullptr;
fhFramebuffer* fhFramebuffer::renderFramebuffer = nullptr;

fhFramebuffer::fhFramebuffer( int w, int h, idImage* color, idImage* depth ) {
	width = w;
	height = h;
	name = (!color && !depth) ? 0 : -1;
	colorAttachment = color;
	depthAttachment = depth;
	samples = 1;
	colorFormat = color ? color->pixelFormat : pixelFormat_t::RGBA;
	depthFormat = depth ? depth->pixelFormat : pixelFormat_t::DEPTH_24_STENCIL_8;
}

static const char* GetFrameBufferStatusMessage( int status ) {
	switch (status) {
	case GL_FRAMEBUFFER_UNDEFINED:
		return "GL_FRAMEBUFFER_UNDEFINED";
	case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
		return "GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT";
		break;
	case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
		return "GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT";
	case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER:
		return "GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER";
	case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER:
		return "GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER";
	case GL_FRAMEBUFFER_UNSUPPORTED:
		return "GL_FRAMEBUFFER_UNSUPPORTED";
	case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE:
		return "GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE";
	case GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS:
		return "GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS";
	case GL_FRAMEBUFFER_COMPLETE:
		return nullptr;
	default:
		return "FRAMEBUFFER_STATUS_UNKNOWN";
	}
}

void fhFramebuffer::Bind() {
	if (currentDrawBuffer == this) {
		return;
	}

	if (name == -1) {
		Allocate();
	}

	glBindFramebuffer( GL_DRAW_FRAMEBUFFER, name );
	currentDrawBuffer = this;

	SetDrawBuffer();
}

bool fhFramebuffer::IsDefault() const {
	return (name == 0);
}

void fhFramebuffer::Resize(int width, int height, int samples, pixelFormat_t colorFormat, pixelFormat_t depthFormat) {
	if (!colorAttachment && !depthAttachment) {
		return;
	}

	if (this->width == width
		&& this->height == height
		&& this->samples == samples
		&& this->colorFormat == colorFormat
		&& this->depthFormat == depthFormat) {
		return;
	}


	if (currentDrawBuffer == this) {
		defaultFramebuffer->Bind();
	}

	Purge();

	this->width = width;
	this->height = height;
	this->samples = samples;
	this->colorFormat = colorFormat;
	this->depthFormat = depthFormat;
}

int fhFramebuffer::GetWidth() const {
	if (!colorAttachment && !depthAttachment) {
		return glConfig.windowWidth;
	}
	return width;
}

int fhFramebuffer::GetHeight() const {
	if (!colorAttachment && !depthAttachment) {
		return glConfig.windowHeight;
	}
	return height;
}

int fhFramebuffer::GetSamples() const {
	if (!colorAttachment && !depthAttachment) {
		return 1;
	}
	return samples;
}

void fhFramebuffer::Purge() {
	if (name != 0 && name != -1) {
		if (currentDrawBuffer == this) {
			defaultFramebuffer->Bind();
		}

		glDeleteFramebuffers( 1, &name );
		name = -1;
	}
}

void fhFramebuffer::SetDrawBuffer() {
	if (!colorAttachment && !depthAttachment) {
		glDrawBuffer( GL_BACK );
	}
	else if (!colorAttachment) {
		glDrawBuffer( GL_NONE );
	}
	else {
		glDrawBuffer( GL_COLOR_ATTACHMENT0 );
	}
}

void fhFramebuffer::Allocate() {
	if (!colorAttachment && !depthAttachment) {
		return;
	}

	glGenFramebuffers( 1, &name );
	glBindFramebuffer( GL_DRAW_FRAMEBUFFER, name );
	currentDrawBuffer = this;

	auto target = samples > 1 ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D;

	if (colorAttachment) {
		colorAttachment->AllocateMultiSampleStorage( colorFormat, width, height, samples );
		colorAttachment->filter = TF_LINEAR;
		colorAttachment->repeat = TR_CLAMP;
		colorAttachment->SetImageFilterAndRepeat();
		glFramebufferTexture2D( GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, target, colorAttachment->texnum, 0 );
	}

	if (depthAttachment) {
		GLenum attachmentType;

		if (depthAttachment->pixelFormat == pixelFormat_t::DEPTH_24_STENCIL_8) {
			attachmentType = GL_DEPTH_STENCIL_ATTACHMENT;
		}
		else {
			attachmentType = GL_DEPTH_ATTACHMENT;
		}

		depthAttachment->AllocateMultiSampleStorage( depthFormat, width, height, samples );
		depthAttachment->filter = TF_LINEAR;
		depthAttachment->repeat = TR_CLAMP;
		depthAttachment->SetImageFilterAndRepeat();
		glFramebufferTexture2D( GL_DRAW_FRAMEBUFFER, attachmentType, target, depthAttachment->texnum, 0 );
	}

	auto status = GetFrameBufferStatusMessage( glCheckFramebufferStatus( GL_DRAW_FRAMEBUFFER ) );
	if (status) {
		common->Warning( "failed to generate framebuffer: %s", status );
		name = 0;
	}
}

void fhFramebuffer::Init() {
	shadowmapFramebuffer = new fhFramebuffer( 1024 * 4, 1024 * 4, nullptr, globalImages->shadowmapImage );
	defaultFramebuffer = new fhFramebuffer( 0, 0, nullptr, nullptr );
	currentDepthFramebuffer = new fhFramebuffer( 1024, 1024, nullptr, globalImages->currentDepthImage );
	currentRenderFramebuffer = new fhFramebuffer( 1024, 1024, globalImages->currentRenderImage, nullptr );
	renderFramebuffer = new fhFramebuffer(1024, 1024, globalImages->renderColorImage, globalImages->renderDepthImage);
	bloomFramebuffer = new fhFramebuffer(128, 128, globalImages->bloomImage, nullptr);
	bloomTmpFramebuffer = new fhFramebuffer(128, 128, globalImages->bloomImageTmp, nullptr);
	currentDrawBuffer = defaultFramebuffer;
}

void fhFramebuffer::PurgeAll() {
	defaultFramebuffer->Bind();

	shadowmapFramebuffer->Purge();
	defaultFramebuffer->Purge();
	currentDepthFramebuffer->Purge();
	currentRenderFramebuffer->Purge();
	bloomFramebuffer->Purge();
	renderFramebuffer->Purge();
	bloomTmpFramebuffer->Purge();
}

void fhFramebuffer::BlitColor( fhFramebuffer* source, fhFramebuffer* dest ) {
	Blit( source, 0, 0, source->GetWidth(), source->GetHeight(),
		dest, 0, 0, dest->GetWidth(), dest->GetHeight(),
		GL_COLOR_BUFFER_BIT, TF_LINEAR );
}

void fhFramebuffer::BlitColor( fhFramebuffer* source, uint32 sourceX, uint32 sourceY, uint32 sourceWidth, uint32 sourceHeight, fhFramebuffer* dest ) {
	Blit( source, sourceX, sourceY, sourceWidth, sourceHeight,
		dest, 0, 0, dest->GetWidth(), dest->GetHeight(),
		GL_COLOR_BUFFER_BIT, TF_LINEAR );
}

void fhFramebuffer::BlitDepth( fhFramebuffer* source, fhFramebuffer* dest ) {
	Blit( source, 0, 0, source->GetWidth(), source->GetHeight(),
		dest, 0, 0, dest->GetWidth(), dest->GetHeight(),
		GL_DEPTH_BUFFER_BIT, TF_NEAREST );
}

void fhFramebuffer::Blit(
	fhFramebuffer* source, uint32 sourceX, uint32 sourceY, uint32 sourceWidth, uint32 sourceHeight,
	fhFramebuffer* dest, uint32 destX, uint32 destY, uint32 destWidth, uint32 destHeight,
	GLint bufferMask, textureFilter_t filter ) {

	if (source == dest) {
		return;
	}

	fhFramebuffer* currentDrawBuffer = fhFramebuffer::GetCurrentDrawBuffer();

	if (dest->name == -1) {
		dest->Allocate();
	}

	dest->Bind();
	glBindFramebuffer( GL_READ_FRAMEBUFFER, source->name );

	glBlitFramebufferEXT( sourceX, sourceY, sourceWidth, sourceHeight,
		destX, destY, destWidth, destHeight,
		bufferMask,
		filter == TF_LINEAR ? GL_LINEAR : GL_NEAREST );

	glBindFramebuffer( GL_READ_FRAMEBUFFER, 0 );

	currentDrawBuffer->Bind();
}
