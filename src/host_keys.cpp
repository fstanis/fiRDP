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

#include "host_keys.hpp"

#include <SDL3/SDL_keyboard.h>

#include <algorithm>
#include <iostream>
#include <optional>
#include <ranges>
#include <string_view>

namespace {

struct ModName {
  const char* name;
  SDL_Keymod mod;
};

// clang-format off
constexpr ModName kModNames[] = {
    {"ctrl",  SDL_KMOD_CTRL},
    {"alt",   SDL_KMOD_ALT},
    {"shift", SDL_KMOD_SHIFT},
    {"super", SDL_KMOD_GUI},
    {"win",   SDL_KMOD_GUI},
    {"cmd",   SDL_KMOD_GUI},
    {"gui",   SDL_KMOD_GUI},
};
// clang-format on

std::string to_lower(std::string s) {
  std::ranges::transform(s, s.begin(), ::tolower);
  return s;
}

std::vector<std::string> split(std::string_view s, char delim) {
  return s | std::views::split(delim) | std::views::transform([](auto r) { return std::string(r.begin(), r.end()); }) |
         std::ranges::to<std::vector>();
}

std::optional<SDL_Keymod> parse_modifiers(const std::vector<std::string>& parts) {
  SDL_Keymod mods = SDL_KMOD_NONE;
  for (size_t i = 0; i + 1 < parts.size(); i++) {
    auto lower = to_lower(parts[i]);
    auto it = std::ranges::find_if(kModNames, [&](const ModName& m) { return lower == m.name; });
    if (it == std::ranges::end(kModNames)) {
      return std::nullopt;
    }
    mods = static_cast<SDL_Keymod>(mods | it->mod);
  }
  return mods;
}

}  // namespace

std::vector<HostKey> parse_host_keys(const std::vector<std::string>& specs) {
  std::vector<HostKey> result;
  for (const auto& spec : specs) {
    auto parts = split(spec, '+');
    if (parts.empty()) {
      std::cerr << "Warning: empty host key spec, skipping\n";
      continue;
    }

    auto mods = parse_modifiers(parts);
    if (!mods) {
      std::cerr << "Warning: unknown modifier in host key '" << spec << "', skipping\n";
      continue;
    }

    auto scancode = SDL_GetScancodeFromName(parts.back().c_str());
    if (scancode == SDL_SCANCODE_UNKNOWN) {
      std::cerr << "Warning: unknown key '" << parts.back() << "' in host key '" << spec << "', skipping\n";
      continue;
    }

    result.push_back({*mods, scancode});
  }
  return result;
}

bool is_host_key(std::span<const HostKey> keys, SDL_Keymod mods, SDL_Scancode scancode) {
  return std::ranges::any_of(keys,
                             [&](const HostKey& k) { return scancode == k.scancode && (mods & k.mods) == k.mods; });
}
