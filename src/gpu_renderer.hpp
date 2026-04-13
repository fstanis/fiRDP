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

// GPU-accelerated renderer using SDL_Renderer + SDL_Texture.
// Replaces FreeRDP's CPU surface blitting to avoid Wayland blocking.
class GpuRenderer {
 public:
  ~GpuRenderer();

  // Initialize renderer for a window. Call after SDL_Init but before any
  // SDL_GetWindowSurface calls (which would lock the window into surface mode).
  bool init(SDL_Window* window);

  // Upload dirty regions from the GDI buffer and render the frame.
  void draw_frame(rdpGdi* gdi, const SDL_Rect* rects, int count);

  // Present the frame.
  void present();

 private:
  SDL_Renderer* renderer_ = nullptr;
  SDL_Texture* frame_tex_ = nullptr;
  int frame_w_ = 0;
  int frame_h_ = 0;
};
