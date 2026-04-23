// fiRDP: A lightweight RDP client
// Copyright (C) 2026 Filip Stanis
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.

#include "gpu_renderer.hpp"

#include <freerdp/gdi/gdi.h>

GpuRenderer::~GpuRenderer() {
  if (frame_tex_) {
    SDL_DestroyTexture(frame_tex_);
  }
  if (renderer_) {
    SDL_DestroyRenderer(renderer_);
  }
}

bool GpuRenderer::init(SDL_Window* window) {
  if (!window) {
    return false;
  }
  renderer_ = SDL_CreateRenderer(window, nullptr);
  return renderer_ != nullptr;
}

void GpuRenderer::ensure_texture(int width, int height) {
  if (frame_w_ == width && frame_h_ == height) {
    return;
  }
  if (frame_tex_) {
    SDL_DestroyTexture(frame_tex_);
  }
  frame_tex_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_BGRA32, SDL_TEXTUREACCESS_STREAMING, width, height);
  frame_w_ = width;
  frame_h_ = height;
  SDL_SetRenderLogicalPresentation(renderer_, width, height, SDL_LOGICAL_PRESENTATION_STRETCH);
}

void GpuRenderer::upload_regions(rdpGdi* gdi, const SDL_Rect* rects, int count) {
  if (count == 0) {
    SDL_UpdateTexture(frame_tex_, nullptr, gdi->primary_buffer, gdi->stride);
    return;
  }
  for (int i = 0; i < count; i++) {
    auto& r = rects[i];
    auto* src = gdi->primary_buffer + r.y * gdi->stride + r.x * 4;
    SDL_UpdateTexture(frame_tex_, &r, src, gdi->stride);
  }
}

void GpuRenderer::draw_frame(rdpGdi* gdi, const SDL_Rect* rects, int count) {
  if (!renderer_ || !gdi || !gdi->primary_buffer) {
    return;
  }
  ensure_texture(gdi->width, gdi->height);
  if (!frame_tex_) {
    return;
  }
  upload_regions(gdi, rects, count);
  SDL_RenderTexture(renderer_, frame_tex_, nullptr, nullptr);
}

void GpuRenderer::present() {
  if (renderer_) {
    SDL_RenderPresent(renderer_);
  }
}
