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

#include <algorithm>
#include <freerdp/gdi/gdi.h>

namespace {

SDL_Rect bounding_rect(const SDL_Rect* rects, int count) {
  SDL_Rect bb = rects[0];
  int x2 = bb.x + bb.w;
  int y2 = bb.y + bb.h;
  for (int i = 1; i < count; i++) {
    const auto& r = rects[i];
    bb.x = std::min(bb.x, r.x);
    bb.y = std::min(bb.y, r.y);
    x2 = std::max(x2, r.x + r.w);
    y2 = std::max(y2, r.y + r.h);
  }
  bb.w = x2 - bb.x;
  bb.h = y2 - bb.y;
  return bb;
}

}  // namespace

GpuRenderer::~GpuRenderer() {
  if (frame_tex_) {
    SDL_DestroyTexture(frame_tex_);
  }
}

void GpuRenderer::init(SDL_Renderer* renderer) {
  renderer_ = renderer;
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
    return;
  }
  SDL_Rect bb = bounding_rect(rects, count);
  auto* src = gdi->primary_buffer + bb.y * gdi->stride + bb.x * 4;
  SDL_UpdateTexture(frame_tex_, &bb, src, gdi->stride);
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
