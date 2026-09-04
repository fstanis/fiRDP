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

#include "server_diagnostics.hpp"

#include <winpr/collections.h>
#include <winpr/wtsapi.h>

#include <freerdp/autodetect.h>
#include <freerdp/channels/channels.h>
#include <freerdp/channels/rdpgfx.h>
#include <freerdp/client/rdpgfx.h>
#include <freerdp/event.h>
#include <freerdp/log.h>
#include <freerdp/settings.h>

#include <cstring>
#include <string>

#include "rdp_connection.hpp"
#include "sdl_context.hpp"

#define TAG CLIENT_TAG("fiRDP")

constexpr auto kFirstPrintDelay = std::chrono::seconds(2);
constexpr auto kNetworkUpdateInterval = std::chrono::seconds(10);

enum NegotiatedProtocol : UINT32 {
  kProtocolRdp = 0x00000000,
  kProtocolTls = 0x00000001,
  kProtocolNla = 0x00000002,
  kProtocolRdstls = 0x00000004,
  kProtocolNlaEx = 0x00000008,
  kProtocolAad = 0x00000010,
};

struct H264Support {
  bool available = false;
  bool avc444 = false;
  const char* detail = "";
};

extern "C" void autodetect_register_server_callbacks(rdpAutoDetect* autodetect);
extern "C" void autodetect_on_connect_time_auto_detect_begin(rdpAutoDetect* autodetect);

static ServerDiagnostics* s_diag = nullptr;
static pcRdpgfxCapsConfirm s_original_caps_confirm = nullptr;

static const char* yesno(bool value) {
  if (value) {
    return "yes";
  }
  return "no";
}

static const char* security_name(UINT32 selected) {
  switch (selected) {
    case kProtocolRdp:
      return "native RDP encryption";
    case kProtocolTls:
      return "TLS";
    case kProtocolNla:
      return "NLA (CredSSP)";
    case kProtocolRdstls:
      return "RD-TLS";
    case kProtocolNlaEx:
      return "NLA extended (CredSSP)";
    case kProtocolAad:
      return "AAD authentication";
    default:
      return "unknown";
  }
}

static H264Support h264_support(const ServerDiagnostics& diag) {
  if (!diag.gfxChannelConnected) {
    return {false, false, "no graphics pipeline"};
  }
  const auto version = diag.gfxCapsVersion.load();
  const auto flags = diag.gfxCapsFlags.load();
  if (version == 0) {
    return {false, false, "caps not confirmed"};
  }
  if (version < RDPGFX_CAPVERSION_10) {
    if (flags & RDPGFX_CAPS_FLAG_AVC420_ENABLED) {
      return {true, false, "AVC420 only"};
    }
    return {false, false, "not advertised in caps"};
  }
  if (flags & RDPGFX_CAPS_FLAG_AVC_DISABLED) {
    return {false, false, "disabled by server"};
  }
  return {true, true, "AVC444/AVC420"};
}

static UINT diag_caps_confirm(RdpgfxClientContext* context, const RDPGFX_CAPS_CONFIRM_PDU* pdu) {
  auto* diag = s_diag;
  if (diag && pdu && pdu->capsSet) {
    diag->gfxCapsVersion = pdu->capsSet->version;
    diag->gfxCapsFlags = pdu->capsSet->flags;
  }
  if (s_original_caps_confirm) {
    return s_original_caps_confirm(context, pdu);
  }
  return CHANNEL_RC_OK;
}

static void diag_channel_connected(void*, const ChannelConnectedEventArgs* e) {
  auto* diag = s_diag;
  if (!diag || !e) {
    return;
  }
  if (strcmp(e->name, RDPGFX_DVC_CHANNEL_NAME) != 0) {
    return;
  }
  auto* gfx = static_cast<RdpgfxClientContext*>(e->pInterface);
  if (!gfx || gfx->CapsConfirm == diag_caps_confirm) {
    return;
  }
  diag->gfxChannelConnected = true;
  s_original_caps_confirm = gfx->CapsConfirm;
  gfx->CapsConfirm = diag_caps_confirm;
}

static BOOL diag_rtt_response(rdpAutoDetect*, RDP_TRANSPORT_TYPE, UINT16) {
  auto* diag = s_diag;
  if (diag) {
    WLog_Print(diag->log_, WLOG_DEBUG, "RTT measure response received");
  }
  return TRUE;
}

static BOOL diag_netchar_sync(rdpAutoDetect*, RDP_TRANSPORT_TYPE, UINT16, UINT32 bandwidth, UINT32 rtt) {
  auto* diag = s_diag;
  if (diag) {
    diag->bandwidthKbps = bandwidth;
    diag->rttMs = rtt;
  }
  return TRUE;
}

static BOOL diag_netchar_result(rdpAutoDetect*, RDP_TRANSPORT_TYPE, UINT16,
                                const rdpNetworkCharacteristicsResult* result) {
  auto* diag = s_diag;
  if (!diag || !result) {
    return TRUE;
  }
  const bool hasBandwidth = result->type == RDP_NETCHAR_RESULT_TYPE_BW_AVG_RTT ||
                            result->type == RDP_NETCHAR_RESULT_TYPE_BASE_RTT_BW_AVG_RTT;
  if (hasBandwidth) {
    diag->bandwidthKbps = result->bandwidth;
  }
  if (result->averageRTT > 0) {
    diag->rttMs = result->averageRTT;
  }
  return TRUE;
}

static BOOL diag_bandwidth_results(rdpAutoDetect*, RDP_TRANSPORT_TYPE, UINT16, UINT16, UINT32 timeDelta,
                                   UINT32 byteCount) {
  auto* diag = s_diag;
  if (diag && timeDelta > 0) {
    diag->bandwidthKbps = byteCount * 8 / timeDelta;
  }
  return TRUE;
}

void ServerDiagnostics::install(rdpContext* context) {
  s_diag = this;
  log_ = WLog_Get(TAG);
  context_ = context;
  PubSub_SubscribeChannelConnected(context->pubSub, diag_channel_connected);
  auto* autodetect = autodetect_get(context);
  if (!autodetect) {
    return;
  }
  autodetect_register_server_callbacks(autodetect);
  autodetect->RTTMeasureResponse = diag_rtt_response;
  autodetect->NetworkCharacteristicsResult = diag_netchar_result;
  autodetect->NetworkCharacteristicsSync = diag_netchar_sync;
  autodetect->BandwidthMeasureResults = diag_bandwidth_results;
}

void ServerDiagnostics::arm() {
  armed_ = true;
  armedAt_ = std::chrono::steady_clock::now();
  send_rtt_probe();
}

void ServerDiagnostics::send_rtt_probe() {
  if (!context_) {
    return;
  }
  if (!freerdp_settings_get_bool(context_->settings, FreeRDP_NetworkAutoDetect)) {
    return;
  }
  auto* autodetect = autodetect_get(context_);
  if (!autodetect || !autodetect->OnConnectTimeAutoDetectBegin) {
    return;
  }
  autodetect_on_connect_time_auto_detect_begin(autodetect);
}

static void print_security(wLog* log, const rdpSettings* settings) {
  const auto selected = freerdp_settings_get_uint32(settings, FreeRDP_SelectedProtocol);
  WLog_Print(log, WLOG_INFO, "security: %s (FreeRDP_SelectedProtocol=0x%08" PRIx32 ")", security_name(selected),
             selected);
  if (selected == kProtocolRdp) {
    WLog_Print(log, WLOG_WARN, "server negotiated native RDP encryption, no TLS or NLA protection");
  }
}

static void print_legacy_codecs(wLog* log, const rdpSettings* settings) {
  const bool rfx = freerdp_settings_get_bool(settings, FreeRDP_RemoteFxCodec);
  const bool nsc = freerdp_settings_get_bool(settings, FreeRDP_NSCodec);
  WLog_Print(log, WLOG_INFO, "legacy codecs: RemoteFX=%s NSCodec=%s", yesno(rfx), yesno(nsc));
}

static void print_graphics(wLog* log, const rdpSettings* settings, const ServerDiagnostics& diag) {
  const bool pipeline = freerdp_settings_get_bool(settings, FreeRDP_SupportGraphicsPipeline);
  if (!pipeline) {
    WLog_Print(log, WLOG_INFO, "graphics pipeline: server does not support RDPEGFX (XDDM driver or policy)");
    print_legacy_codecs(log, settings);
  } else if (!diag.gfxChannelConnected) {
    WLog_Print(log, WLOG_INFO, "graphics pipeline: negotiated but channel not established");
    print_legacy_codecs(log, settings);
  } else if (diag.gfxCapsVersion == 0) {
    WLog_Print(log, WLOG_INFO, "graphics pipeline: active, caps not confirmed yet");
  } else {
    WLog_Print(log, WLOG_INFO, "graphics pipeline: active (caps version=0x%08" PRIx32 " flags=0x%08" PRIx32 ")",
               diag.gfxCapsVersion.load(), diag.gfxCapsFlags.load());
  }

  const auto h264 = h264_support(diag);
  WLog_Print(log, WLOG_INFO, "h264: %s", h264.detail);
}

static NetworkValues read_network_values(rdpContext* context, const ServerDiagnostics& diag) {
  NetworkValues values;
  values.averageRtt = diag.rttMs.load();
  values.bandwidthKbps = diag.bandwidthKbps.load();
  const auto* autodetect = autodetect_get(context);
  if (autodetect) {
    values.baseRtt = autodetect->netCharBaseRTT;
    if (autodetect->netCharAverageRTT > 0) {
      values.averageRtt = autodetect->netCharAverageRTT;
    }
  }
  return values;
}

static void append_network_value(std::string& line, const char* label, UINT32 value, const char* suffix) {
  if (value == 0) {
    return;
  }
  line += " ";
  line += label;
  line += " ";
  line += std::to_string(value);
  line += suffix;
}

static void print_network_values(wLog* log, const NetworkValues& values) {
  std::string line = "network autodetect:";
  append_network_value(line, "base RTT", values.baseRtt, " ms,");
  append_network_value(line, "average RTT", values.averageRtt, " ms,");
  if (values.bandwidthKbps > 0) {
    line += " bandwidth " + std::to_string(values.bandwidthKbps) + " kbps (server estimate)";
  } else if (values.averageRtt > 0 || values.baseRtt > 0) {
    line += " bandwidth not reported by server";
  }
  WLog_Print(log, WLOG_INFO, "%s", line.c_str());
}

static void print_network(wLog* log, rdpContext* context, const ServerDiagnostics& diag) {
  const bool enabled = freerdp_settings_get_bool(context->settings, FreeRDP_NetworkAutoDetect);
  if (!enabled) {
    WLog_Print(log, WLOG_INFO, "network autodetect: not negotiated with server");
    return;
  }
  const auto values = read_network_values(context, diag);
  if (values.averageRtt == 0 && values.bandwidthKbps == 0) {
    WLog_Print(log, WLOG_INFO, "network autodetect: enabled, no measurements received");
    return;
  }
  print_network_values(log, values);
}

static void print_low_latency_warning(wLog* log, const ServerDiagnostics& diag) {
  if (!diag.gfxChannelConnected) {
    WLog_Print(log, WLOG_WARN, "--low-latency: no effect without the graphics pipeline");
    return;
  }
  const auto version = diag.gfxCapsVersion.load();
  if (version == 0) {
    return;
  }
  if (version < RDPGFX_CAPVERSION_10) {
    WLog_Print(log, WLOG_WARN,
               "--low-latency: QoE frame feedback has no effect, server confirmed pre-10.0 graphics caps "
               "(0x%08" PRIx32 "), frame-ack suspension still applies",
               version);
  }
}

static void print_warnings(wLog* log, const SessionOptions& opts, const ServerDiagnostics& diag) {
  if (opts.prefer_h264) {
    const auto h264 = h264_support(diag);
    if (!h264.available) {
      WLog_Print(log, WLOG_WARN, "--prefer-h264: server does not provide H.264 (%s)", h264.detail);
    } else if (!h264.avc444) {
      WLog_Print(log, WLOG_WARN, "--prefer-h264: server does not allow AVC444, falling back to AVC420");
    }
  }
  if (opts.low_latency) {
    print_low_latency_warning(log, diag);
  }
}

static void print_diagnostics(SdlContext* sdl, const SessionOptions& opts, const ServerDiagnostics& diag) {
  auto* log = sdl->getWLog();
  auto* context = sdl->context();
  const auto* settings = context->settings;
  WLog_Print(log, WLOG_INFO, "--- server diagnostics ---");
  print_security(log, settings);
  print_graphics(log, settings, diag);
  print_network(log, context, diag);
  print_warnings(log, opts, diag);
}

void ServerDiagnostics::poll(SdlContext* sdl, const SessionOptions& opts) {
  if (!armed_) {
    return;
  }
  const auto now = std::chrono::steady_clock::now();
  if (!printed_) {
    if (now - armedAt_ < kFirstPrintDelay) {
      return;
    }
    printed_ = true;
    print_diagnostics(sdl, opts, *this);
    lastNetworkPrint_ = now;
    lastReportedNetwork_ = read_network_values(sdl->context(), *this);
    return;
  }
  if (now - lastNetworkPrint_ < kNetworkUpdateInterval) {
    return;
  }
  lastNetworkPrint_ = now;
  const auto values = read_network_values(sdl->context(), *this);
  if (values.averageRtt == 0 && values.bandwidthKbps == 0) {
    return;
  }
  if (values == lastReportedNetwork_) {
    return;
  }
  lastReportedNetwork_ = values;
  print_network_values(sdl->getWLog(), values);
}
