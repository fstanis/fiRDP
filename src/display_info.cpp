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

static SDL_DisplayID resolve_sdl_id(SDL_DisplayID display) {
  if (display != 0) {
    return display;
  }
  return SDL_GetPrimaryDisplay();
}

#ifdef __APPLE__

static constexpr uint32_t kNativeModeFlag = 0x02000000;
static constexpr uint32_t kMaxDisplays = 32;

static CGDirectDisplayID cg_display_for_bounds(const SDL_Rect& bounds) {
  CGDirectDisplayID ids[kMaxDisplays];
  uint32_t count = 0;
  if (CGGetActiveDisplayList(kMaxDisplays, ids, &count) != kCGErrorSuccess) {
    return kCGNullDirectDisplay;
  }
  for (uint32_t i = 0; i < count; i++) {
    auto rect = CGDisplayBounds(ids[i]);
    if (static_cast<int>(rect.origin.x) == bounds.x && static_cast<int>(rect.origin.y) == bounds.y &&
        static_cast<int>(rect.size.width) == bounds.w && static_cast<int>(rect.size.height) == bounds.h) {
      return ids[i];
    }
  }
  return kCGNullDirectDisplay;
}

static CGDirectDisplayID cg_display_for_sdl(SDL_DisplayID display) {
  SDL_Rect bounds{};
  if (display != 0 && SDL_GetDisplayBounds(display, &bounds)) {
    auto id = cg_display_for_bounds(bounds);
    if (id != kCGNullDirectDisplay) {
      return id;
    }
  }
  return CGMainDisplayID();
}

static NativeDisplay find_native_mode(CGDirectDisplayID display) {
  auto modes = CGDisplayCopyAllDisplayModes(display, nullptr);
  if (!modes) {
    return {};
  }
  NativeDisplay result{};
  for (CFIndex i = 0; i < CFArrayGetCount(modes); i++) {
    auto mode = static_cast<CGDisplayModeRef>(const_cast<void*>(CFArrayGetValueAtIndex(modes, i)));
    if (CGDisplayModeGetIOFlags(mode) & kNativeModeFlag) {
      result.pixel_w = static_cast<uint32_t>(CGDisplayModeGetPixelWidth(mode));
      result.pixel_h = static_cast<uint32_t>(CGDisplayModeGetPixelHeight(mode));
      break;
    }
  }
  CFRelease(modes);
  return result;
}

DisplayInfo::DisplayInfo(SDL_DisplayID display)
    : sdl_id_(resolve_sdl_id(display)), cg_id_(cg_display_for_sdl(sdl_id_)) {}

NativeDisplay DisplayInfo::native_display() const {
  auto current = CGDisplayCopyDisplayMode(cg_id_);
  if (!current) {
    return {};
  }
  auto logical_w = static_cast<uint32_t>(CGDisplayModeGetWidth(current));
  auto logical_h = static_cast<uint32_t>(CGDisplayModeGetHeight(current));
  CGDisplayModeRelease(current);

  auto native = find_native_mode(cg_id_);
  if (native.pixel_w == 0) {
    return {};
  }
  native.logical_w = logical_w;
  native.logical_h = logical_h;
  return native;
}

static uint32_t native_scale_percent(const NativeDisplay& d) {
  if (d.logical_w == 0) {
    return 0;
  }
  return static_cast<uint32_t>(static_cast<float>(d.pixel_w) / static_cast<float>(d.logical_w) * 100.0f);
}

#else

DisplayInfo::DisplayInfo(SDL_DisplayID display) : sdl_id_(resolve_sdl_id(display)) {}

NativeDisplay DisplayInfo::native_display() const {
  return {};
}

#endif

uint32_t DisplayInfo::scale_percent(bool native_resolution) const {
#ifdef __APPLE__
  if (native_resolution) {
    auto pct = native_scale_percent(native_display());
    if (pct != 0) {
      return pct;
    }
  }
#else
  (void)native_resolution;
#endif
  const auto* mode = SDL_GetDesktopDisplayMode(sdl_id_);
  if (!mode || mode->pixel_density <= 0.0f) {
    return 0;
  }
  return static_cast<uint32_t>(mode->pixel_density * 100.0f);
}
