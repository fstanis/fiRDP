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

#include <freerdp/freerdp.h>
#include <winpr/wlog.h>

#include <atomic>
#include <chrono>

struct SessionOptions;
class SdlContext;

struct NetworkValues {
  UINT32 baseRtt = 0;
  UINT32 averageRtt = 0;
  UINT32 bandwidthKbps = 0;
  bool operator==(const NetworkValues&) const = default;
};

class ServerDiagnostics {
 public:
  void install(rdpContext* context);
  void arm();
  void poll(SdlContext* sdl, const SessionOptions& opts);

  std::atomic<bool> gfxChannelConnected{false};
  std::atomic<UINT32> gfxCapsVersion{0};
  std::atomic<UINT32> gfxCapsFlags{0};
  std::atomic<UINT32> bandwidthKbps{0};
  std::atomic<UINT32> rttMs{0};
  wLog* log_ = nullptr;

 private:
  void send_rtt_probe();

  rdpContext* context_ = nullptr;
  bool armed_ = false;
  bool printed_ = false;
  std::chrono::steady_clock::time_point armedAt_{};
  std::chrono::steady_clock::time_point lastNetworkPrint_{};
  NetworkValues lastReportedNetwork_{};
};
