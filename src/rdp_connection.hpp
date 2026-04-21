// fiRDP: A lightweight RDP client
// Copyright (C) 2026 Filip Stanis
//
// Based on FreeRDP SDL client code:
// Copyright 2022-2025 Armin Novak <armin.novak@thincast.com>
// Licensed under the Apache License, Version 2.0
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

#include <freerdp/client/file.h>

#include <expected>
#include <string>
#include <vector>

#include "host_keys.hpp"

enum class SessionError {
  kLogonFailure,
  kUserDisconnect,
  kGeneral,
};

struct SessionFailure {
  SessionError code;
  std::string message;
};

struct SessionOptions {
  bool grab_keyboard = false;
  bool native_scale = false;
  bool prefer_h264 = false;
  bool low_latency = false;
  std::vector<HostKey> host_keys;
};

class RdpSession {
 public:
  static std::expected<void, SessionFailure> run(rdpFile* file,
                                                 const std::string& password,
                                                 const SessionOptions& opts = {});
};
