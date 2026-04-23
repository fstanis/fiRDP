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

#include "display_info.hpp"

#include <SDL3/SDL_video.h>
#include <tuple>

#ifdef __APPLE__
#include <CoreGraphics/CoreGraphics.h>

static constexpr uint32_t kNativeModeFlag = 0x02000000;

static NativeDisplay find_native_mode(CGDirectDisplayID display) {
  auto modes = CGDisplayCopyAllDisplayModes(display, nullptr);
  if (!modes) {
    return {};
  }
  NativeDisplay result{};
  for (CFIndex i = 0; i < CFArrayGetCount(modes); i++) {
    auto mode = static_cast<CGDisplayModeRef>(
        const_cast<void*>(CFArrayGetValueAtIndex(modes, i)));
    if (CGDisplayModeGetIOFlags(mode) & kNativeModeFlag) {
      result.pixel_w = static_cast<uint32_t>(CGDisplayModeGetPixelWidth(mode));
      result.pixel_h = static_cast<uint32_t>(CGDisplayModeGetPixelHeight(mode));
      break;
    }
  }
  CFRelease(modes);
  return result;
}

NativeDisplay DisplayInfo::native_display() {
  auto display = CGMainDisplayID();
  auto current = CGDisplayCopyDisplayMode(display);
  if (!current) {
    return {};
  }
  auto logical_w = static_cast<uint32_t>(CGDisplayModeGetWidth(current));
  auto logical_h = static_cast<uint32_t>(CGDisplayModeGetHeight(current));
  CGDisplayModeRelease(current);

  auto native = find_native_mode(display);
  if (native.pixel_w == 0) {
    return {};
  }
  native.logical_w = logical_w;
  native.logical_h = logical_h;
  return native;
}

static uint32_t native_scale_percent() {
  auto d = DisplayInfo::native_display();
  if (d.logical_w == 0) {
    return 0;
  }
  return static_cast<uint32_t>(
      static_cast<float>(d.pixel_w) / static_cast<float>(d.logical_w) * 100.0f);
}

#else

NativeDisplay DisplayInfo::native_display() {
  return {};
}

#endif

static uint32_t sdl_scale_percent() {
  const auto* mode = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
  if (!mode || mode->pixel_density <= 0.0f) {
    return 0;
  }
  return static_cast<uint32_t>(mode->pixel_density * 100.0f);
}

uint32_t DisplayInfo::scale_percent(bool native_resolution) {
#ifdef __APPLE__
  if (native_resolution) {
    auto pct = native_scale_percent();
    if (pct != 0) {
      return pct;
    }
  }
#else
  std::ignore = native_resolution;
#endif
  return sdl_scale_percent();
}
