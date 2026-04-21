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
#include <sstream>

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
  std::transform(s.begin(), s.end(), s.begin(), ::tolower);
  return s;
}

std::vector<std::string> split(const std::string& s, char delim) {
  std::vector<std::string> parts;
  std::istringstream ss(s);
  std::string token;
  while (std::getline(ss, token, delim)) {
    parts.push_back(token);
  }
  return parts;
}

std::optional<SDL_Keymod> parse_modifiers(const std::vector<std::string>& parts) {
  SDL_Keymod mods = SDL_KMOD_NONE;
  for (size_t i = 0; i + 1 < parts.size(); i++) {
    auto lower = to_lower(parts[i]);
    bool found = false;
    for (const auto& [name, mod] : kModNames) {
      if (lower == name) {
        mods = static_cast<SDL_Keymod>(mods | mod);
        found = true;
        break;
      }
    }
    if (!found) {
      return std::nullopt;
    }
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

bool is_host_key(const std::vector<HostKey>& keys, SDL_Keymod mods, SDL_Scancode scancode) {
  for (const auto& [key_mods, key_scancode] : keys) {
    if (scancode == key_scancode && (mods & key_mods) == key_mods) {
      return true;
    }
  }
  return false;
}
