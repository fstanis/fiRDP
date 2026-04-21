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

#pragma once

#include <SDL3/SDL.h>
#include <freerdp/freerdp.h>

class GpuRenderer {
 public:
  ~GpuRenderer();

  bool init(SDL_Window* window);
  void draw_frame(rdpGdi* gdi, const SDL_Rect* rects, int count);
  void present();

 private:
  void ensure_texture(int width, int height);
  void upload_regions(rdpGdi* gdi, const SDL_Rect* rects, int count);

  SDL_Renderer* renderer_ = nullptr;
  SDL_Texture* frame_tex_ = nullptr;
  int frame_w_ = 0;
  int frame_h_ = 0;
};
