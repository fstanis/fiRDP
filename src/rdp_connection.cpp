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

#include "rdp_connection.hpp"

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#endif

#include <SDL3/SDL.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_video.h>
#include <freerdp/channels/channels.h>
#include <freerdp/client/channels.h>
#include <freerdp/client/cliprdr.h>
#include <freerdp/client/file.h>
#include <freerdp/constants.h>
#include <freerdp/error.h>
#include <freerdp/freerdp.h>
#include <freerdp/gdi/gdi.h>
#include <freerdp/locale/keyboard.h>
#include <winpr/timezone.h>
#include <freerdp/log.h>
#include <freerdp/streamdump.h>
#include <freerdp/utils/signal.h>
#include <winpr/assert.h>
#include <winpr/crt.h>
#include <winpr/synch.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "display_info.hpp"
#include "gpu_renderer.hpp"
#include "sdl_config.hpp"
#include "sdl_context.hpp"
#include "sdl_utils.hpp"
#include "server_diagnostics.hpp"

#define TAG CLIENT_TAG("fiRDP")

#ifdef __APPLE__
static void disable_macos_press_and_hold() {
  CFPreferencesSetAppValue(CFSTR("ApplePressAndHoldEnabled"), kCFBooleanFalse, kCFPreferencesCurrentApplication);
}

static bool is_autorepeat(CGEventRef event) {
  return CGEventGetIntegerValueField(event, kCGKeyboardEventAutorepeat) != 0;
}

static CGEventRef event_tap_callback(CGEventTapProxy, CGEventType type, CGEventRef event, void*) {
  if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput) {
    return event;
  }
  if (type == kCGEventKeyDown && is_autorepeat(event)) {
    return event;
  }
  CGEventPost(kCGAnnotatedSessionEventTap, event);
  return nullptr;
}

struct EventTapGuard {
  CFMachPortRef tap = nullptr;
  CFRunLoopSourceRef source = nullptr;

  explicit EventTapGuard(bool enabled) {
    if (!enabled) {
      return;
    }
    tap = CGEventTapCreate(kCGSessionEventTap,
                           kCGHeadInsertEventTap,
                           kCGEventTapOptionDefault,
                           CGEventMaskBit(kCGEventKeyDown) | CGEventMaskBit(kCGEventKeyUp),
                           event_tap_callback,
                           nullptr);
    if (!tap) {
      return;
    }
    source = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, tap, 0);
    CFRunLoopAddSource(CFRunLoopGetCurrent(), source, kCFRunLoopCommonModes);
  }

  ~EventTapGuard() {
    if (source) {
      CFRunLoopRemoveSource(CFRunLoopGetCurrent(), source, kCFRunLoopCommonModes);
      CFRelease(source);
    }
    if (tap) {
      CFRelease(tap);
    }
  }

  EventTapGuard(const EventTapGuard&) = delete;
  EventTapGuard& operator=(const EventTapGuard&) = delete;
};

#endif

static constexpr struct {
  const char* key;
  const char* value;
} kPreInitHints[] = {
#ifdef __APPLE__
    {SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES, "0"},
    {SDL_HINT_MAC_OPTION_AS_ALT, "both"},
    {SDL_HINT_MAC_PRESS_AND_HOLD, "0"},
#endif
    {SDL_HINT_APP_ID, SDL_CLIENT_UUID},
};

static constexpr struct {
  const char* key;
  const char* value;
} kPostInitHints[] = {
    {SDL_HINT_VIDEO_SYNC_WINDOW_OPERATIONS, "0"},
    {SDL_HINT_ALLOW_ALT_TAB_WHILE_GRABBED, "0"},
    {SDL_HINT_PEN_MOUSE_EVENTS, "0"},
    {SDL_HINT_TOUCH_MOUSE_EVENTS, "0"},
#ifdef SDL_HINT_MOUSE_DPI_SCALE_CURSORS
    {SDL_HINT_MOUSE_DPI_SCALE_CURSORS, "1"},
#endif
    {SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1"},
#ifdef __APPLE__
    {SDL_HINT_RENDER_VSYNC, "1"},
    {SDL_HINT_RENDER_DRIVER, "metal"},
#else
    {SDL_HINT_RENDER_VSYNC, "0"},
    {SDL_HINT_RENDER_DRIVER, "vulkan,opengl"},
#endif
};

class ConnectionError : public std::exception {
 public:
  ConnectionError(int rc, std::string msg) : rc_(rc), msg_(std::move(msg)) {}
  [[nodiscard]] int rc() const { return rc_; }
  [[nodiscard]] const char* what() const noexcept override { return msg_.c_str(); }

 private:
  int rc_;
  std::string msg_;
};

static void sdl_term_handler([[maybe_unused]] int signum,
                             [[maybe_unused]] const char* signame,
                             [[maybe_unused]] void* context) {
  std::ignore = sdl_push_quit();
}

static BOOL sdl_client_global_init() {
  return freerdp_handle_signals() == 0;
}

static void sdl_client_global_uninit() {}

static BOOL sdl_client_new(freerdp* instance, rdpContext* context) {
  if (!instance || !context) {
    return FALSE;
  }
  auto* ctx = reinterpret_cast<sdl_rdp_context*>(context);
  ctx->sdl = new SdlContext(context);
  return ctx->sdl != nullptr;
}

static void sdl_client_free([[maybe_unused]] freerdp* instance, rdpContext* context) {
  if (!context) {
    return;
  }
  delete reinterpret_cast<sdl_rdp_context*>(context)->sdl;
}

static int sdl_client_start(rdpContext* context) {
  return get_context(context)->start();
}

static int sdl_client_stop(rdpContext* context) {
  return get_context(context)->join();
}

static void register_entry_points(RDP_CLIENT_ENTRY_POINTS* ep) {
  WINPR_ASSERT(ep);
  ZeroMemory(ep, sizeof(RDP_CLIENT_ENTRY_POINTS));
  ep->Version = RDP_CLIENT_INTERFACE_VERSION;
  ep->Size = sizeof(RDP_CLIENT_ENTRY_POINTS_V1);
  ep->GlobalInit = sdl_client_global_init;
  ep->GlobalUninit = sdl_client_global_uninit;
  ep->ContextSize = sizeof(sdl_rdp_context);
  ep->ClientNew = sdl_client_new;
  ep->ClientFree = sdl_client_free;
  ep->ClientStart = sdl_client_start;
  ep->ClientStop = sdl_client_stop;
}

static void context_free(sdl_rdp_context* ctx) {
  if (ctx) {
    freerdp_client_context_free(&ctx->common.context);
  }
}

static SDL_LogPriority wlog_to_sdl(DWORD level) {
  switch (level) {
    case WLOG_TRACE:
      return SDL_LOG_PRIORITY_VERBOSE;
    case WLOG_DEBUG:
      return SDL_LOG_PRIORITY_DEBUG;
    case WLOG_INFO:
      return SDL_LOG_PRIORITY_INFO;
    case WLOG_WARN:
      return SDL_LOG_PRIORITY_WARN;
    case WLOG_ERROR:
      return SDL_LOG_PRIORITY_ERROR;
    case WLOG_FATAL:
      return SDL_LOG_PRIORITY_CRITICAL;
    default:
      return SDL_LOG_PRIORITY_VERBOSE;
  }
}

static DWORD sdl_to_wlog(SDL_LogPriority priority) {
  switch (priority) {
    case SDL_LOG_PRIORITY_VERBOSE:
      return WLOG_TRACE;
    case SDL_LOG_PRIORITY_DEBUG:
      return WLOG_DEBUG;
    case SDL_LOG_PRIORITY_INFO:
      return WLOG_INFO;
    case SDL_LOG_PRIORITY_WARN:
      return WLOG_WARN;
    case SDL_LOG_PRIORITY_ERROR:
      return WLOG_ERROR;
    case SDL_LOG_PRIORITY_CRITICAL:
      return WLOG_FATAL;
    default:
      return WLOG_OFF;
  }
}

static void SDLCALL sdl_log_bridge(void* userdata, int category, SDL_LogPriority priority, const char* message) {
  auto* sdl = static_cast<SdlContext*>(userdata);
  const DWORD level = sdl_to_wlog(priority);
  auto* log = sdl->getWLog();
  if (WLog_IsLevelActive(log, level)) {
    WLog_PrintTextMessage(log, level, __LINE__, __FILE__, __func__, "[SDL:%d] %s", category, message);
  }
}

static bool is_disconnect_shortcut(const SDL_Event& ev, SDL_Keymod mods) {
  return (mods & SDL_KMOD_SHIFT) && ev.key.scancode == SDL_SCANCODE_F12;
}

static bool is_fullscreen_shortcut(const SDL_Event& ev, SDL_Keymod mods) {
  return (mods & SDL_KMOD_SHIFT) && ev.key.scancode == SDL_SCANCODE_F11;
}

static void handle_key_event(SdlContext* sdl, const SDL_Event& ev) {
  auto mods = SDL_GetModState();
  WLog_Print(sdl->getWLog(), WLOG_DEBUG, "KEY_DOWN scancode=0x%x mods=0x%x", ev.key.scancode, mods);

  if (is_disconnect_shortcut(ev, mods)) {
    WLog_Print(sdl->getWLog(), WLOG_INFO, "Disconnect shortcut pressed");
    if (!freerdp_abort_connect_context(sdl->context())) {
      WLog_Print(sdl->getWLog(), WLOG_WARN, "Failed to abort connection");
    }
    return;
  }
  if (is_fullscreen_shortcut(ev, mods)) {
    WLog_Print(sdl->getWLog(), WLOG_INFO, "Fullscreen shortcut pressed");
    if (!sdl->toggleFullscreen()) {
      WLog_Print(sdl->getWLog(), WLOG_WARN, "Failed to toggle fullscreen");
    }
  }
}

static SDL_FPoint decode_pointer_position(const SDL_Event& ev) {
  auto x = static_cast<float>(static_cast<INT32>(reinterpret_cast<uintptr_t>(ev.user.data1)));
  auto y = static_cast<float>(static_cast<INT32>(reinterpret_cast<uintptr_t>(ev.user.data2)));
  return {x, y};
}

static void drain_and_render(SdlContext* sdl, GpuRenderer& gpu) {
  std::vector<SDL_Rect> all_rects;
  for (auto rects = sdl->pop(); !rects.empty(); rects = sdl->pop()) {
    all_rects.insert(all_rects.end(), rects.begin(), rects.end());
  }
  if (all_rects.empty()) {
    return;
  }

  auto* gdi = sdl->context()->gdi;
  if (!gdi) {
    return;
  }
  gpu.draw_frame(gdi, all_rects);
  gpu.present();

  SDL_Event drained;
  while (SDL_PeepEvents(&drained, 1, SDL_GETEVENT, SDL_EVENT_USER_UPDATE, SDL_EVENT_USER_UPDATE) > 0) {
  }
}

static int event_loop(SdlContext* sdl, const SessionOptions& opts, ServerDiagnostics& diagnostics) {
  const auto& host_keys = opts.host_keys;
  GpuRenderer gpu;
#ifdef __APPLE__
  std::optional<EventTapGuard> event_tap;
#endif

  try {
    while (!sdl->shallAbort()) {
      diagnostics.poll(sdl, opts);
      SDL_Event ev = {};
      if (!SDL_WaitEventTimeout(&ev, 1000)) {
        continue;
      }

      if (sdl->shallAbort(true)) {
        continue;
      }

      if (sdl->getDialog().handleEvent(ev)) {
        continue;
      }

      switch (ev.type) {
        case SDL_EVENT_QUIT:
          std::ignore = freerdp_abort_connect_context(sdl->context());
          break;
        case SDL_EVENT_KEY_DOWN:
          if (opts.no_key_repeat && ev.key.repeat) {
            break;
          }
          handle_key_event(sdl, ev);
          if (!host_keys.empty() && is_host_key(host_keys, SDL_GetModState(), ev.key.scancode)) {
            break;
          }
          if (!sdl->handleEvent(ev)) {
            WLog_Print(sdl->getWLog(), WLOG_WARN, "handleEvent failed for event 0x%x", ev.type);
          }
          break;
        case SDL_EVENT_KEY_UP:
          if (!host_keys.empty() && is_host_key(host_keys, SDL_GetModState(), ev.key.scancode)) {
            break;
          }
          if (!sdl->handleEvent(ev)) {
            WLog_Print(sdl->getWLog(), WLOG_WARN, "handleEvent failed for event 0x%x", ev.type);
          }
          break;
        case SDL_EVENT_USER_POINTER_NULL:
          if (!sdl->setCursor(SdlContext::CURSOR_NULL)) {
            throw ConnectionError{-1, "setCursor(NULL)"};
          }
          break;
        case SDL_EVENT_USER_POINTER_DEFAULT:
          if (!sdl->setCursor(SdlContext::CURSOR_DEFAULT)) {
            throw ConnectionError{-1, "setCursor(DEFAULT)"};
          }
          break;
        case SDL_EVENT_USER_POINTER_SET:
          if (!sdl->setCursor(static_cast<rdpPointer*>(ev.user.data1))) {
            throw ConnectionError{-1, "setCursor(IMAGE)"};
          }
          break;
        case SDL_EVENT_USER_POINTER_POSITION: {
          auto pos = decode_pointer_position(ev);
          if (!sdl->moveMouseTo(pos)) {
            throw ConnectionError{-1, "moveMouseTo"};
          }
          break;
        }
        case SDL_EVENT_USER_CREATE_WINDOWS: {
          if (!static_cast<SdlContext*>(ev.user.data1)->createWindows()) {
            throw ConnectionError{-1, "createWindows"};
          }
          auto* first = sdl->getFirstWindow();
          if (first) {
            gpu.init(first->renderer());
          }
          diagnostics.arm();
          break;
        }
        case SDL_EVENT_USER_UPDATE:
          drain_and_render(sdl, gpu);
          break;
        case SDL_EVENT_USER_WINDOW_RESIZEABLE:
          if (auto* w = static_cast<SdlWindow*>(ev.user.data1)) {
            w->resizeable(ev.user.code != 0);
          }
          break;
        case SDL_EVENT_USER_WINDOW_FULLSCREEN:
          if (auto* w = static_cast<SdlWindow*>(ev.user.data1)) {
            w->fullscreen(ev.user.code != 0, ev.user.data2 != nullptr);
          }
          break;
        case SDL_EVENT_MOUSE_WHEEL:
          ev.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
          if (!sdl->handleEvent(ev)) {
            WLog_Print(sdl->getWLog(), WLOG_WARN, "handleEvent failed for event 0x%x", ev.type);
          }
          break;
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
          break;
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
#ifdef __APPLE__
          if (opts.grab_keyboard && !event_tap) {
            event_tap.emplace(true);
            if (!event_tap->tap) {
              WLog_Print(sdl->getWLog(), WLOG_WARN, "Failed to create CGEventTap");
            }
          }
#endif
          if (freerdp_settings_get_bool(sdl->context()->settings, FreeRDP_GrabKeyboard)) {
            if (auto* w = sdl->getWindowForId(ev.window.windowID)) {
              if (!w->grabKeyboard(true)) {
                WLog_Print(sdl->getWLog(), WLOG_WARN, "Failed to grab keyboard on focus");
              }
            }
          }
          if (!sdl->handleEvent(ev)) {
            WLog_Print(sdl->getWLog(), WLOG_WARN, "handleEvent failed for event 0x%x", ev.type);
          }
          break;
        default:
          if (!sdl->handleEvent(ev)) {
            WLog_Print(sdl->getWLog(), WLOG_WARN, "handleEvent failed for event 0x%x", ev.type);
          }
          break;
      }
    }
    return 0;
  } catch (ConnectionError& err) {
    WLog_Print(sdl->getWLog(), WLOG_ERROR, "%s", err.what());
    return err.rc();
  }
}

static void apply_hints(auto& table) {
  for (const auto& [key, value] : table) {
    SDL_SetHint(key, value);
  }
}

using Result = std::expected<void, SessionFailure>;

static auto fail(SessionError code, std::string msg) {
  return std::unexpected(SessionFailure{code, std::move(msg)});
}

static auto fail(std::string msg) {
  return fail(SessionError::kGeneral, std::move(msg));
}

static void force_nla(rdpSettings* settings) {
  freerdp_settings_set_bool(settings, FreeRDP_AadSecurity, FALSE);
  freerdp_settings_set_bool(settings, FreeRDP_UseRdpSecurityLayer, FALSE);
  freerdp_settings_set_bool(settings, FreeRDP_RdpSecurity, FALSE);
  freerdp_settings_set_bool(settings, FreeRDP_NlaSecurity, TRUE);
  freerdp_settings_set_bool(settings, FreeRDP_TlsSecurity, FALSE);
}

static Result init_freerdp(rdpFile* file,
                           const std::string& password,
                           const SessionOptions& opts,
                           std::unique_ptr<sdl_rdp_context, void (*)(sdl_rdp_context*)>& owner) {
  RDP_CLIENT_ENTRY_POINTS ep = {};
  register_entry_points(&ep);

  owner = {reinterpret_cast<sdl_rdp_context*>(freerdp_client_context_new(&ep)), context_free};
  if (!owner) {
    return fail("Failed to create FreeRDP context");
  }

  auto* settings = owner->sdl->context()->settings;

  if (!freerdp_client_populate_settings_from_rdp_file(file, settings)) {
    return fail("Failed to apply RDP file settings");
  }

  if (!password.empty()) {
    freerdp_settings_set_string(settings, FreeRDP_Password, password.c_str());
    freerdp_settings_set_bool(settings, FreeRDP_AutoLogonEnabled, TRUE);
  }

  freerdp_settings_set_bool(settings, FreeRDP_AutoAcceptCertificate, TRUE);
  freerdp_settings_set_bool(settings, FreeRDP_AutoReconnectionEnabled, TRUE);
  freerdp_settings_set_uint32(settings, FreeRDP_AutoReconnectMaxRetries, 3);
  freerdp_settings_set_bool(settings, FreeRDP_TcpKeepAlive, TRUE);
  freerdp_settings_set_uint32(settings, FreeRDP_TcpKeepAliveDelay, 2);
  freerdp_settings_set_uint32(settings, FreeRDP_TcpKeepAliveInterval, 1);
  freerdp_settings_set_uint32(settings, FreeRDP_TcpKeepAliveRetries, 3);
  freerdp_settings_set_uint32(settings, FreeRDP_TcpAckTimeout, 3000);
  freerdp_settings_set_uint32(settings, FreeRDP_TcpConnectTimeout, 3000);

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  DWORD layout = freerdp_keyboard_init(0);
#pragma clang diagnostic pop
  if (layout != 0) {
    freerdp_settings_set_uint32(settings, FreeRDP_KeyboardLayout, layout);
  }

  if (opts.grab_keyboard) {
    freerdp_settings_set_bool(settings, FreeRDP_GrabKeyboard, TRUE);
  } else {
    freerdp_settings_set_bool(settings, FreeRDP_GrabKeyboard, FALSE);
  }

  if (opts.takeover) {
    freerdp_settings_set_bool(settings, FreeRDP_ConsoleSession, TRUE);
  }

  if (opts.auto_login) {
    force_nla(settings);
  }

  freerdp_settings_set_bool(settings, FreeRDP_SupportGraphicsPipeline, TRUE);

  if (opts.prefer_h264) {
    freerdp_settings_set_bool(settings, FreeRDP_GfxThinClient, TRUE);
    freerdp_settings_set_bool(settings, FreeRDP_GfxH264, TRUE);
    freerdp_settings_set_bool(settings, FreeRDP_GfxAVC444, TRUE);
  }

  if (opts.low_latency) {
    freerdp_settings_set_bool(settings, FreeRDP_GfxSendQoeAck, TRUE);
    freerdp_settings_set_bool(settings, FreeRDP_GfxSuspendFrameAck, TRUE);
  }

  return {};
}

static Result init_sdl(SdlContext* sdl, const SessionOptions& opts) {
  apply_hints(kPreInitHints);
#ifdef __APPLE__
  disable_macos_press_and_hold();
#else
  if (!opts.no_wayland) {
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "wayland");
  }
#endif

  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
    return fail(std::string("SDL_Init failed: ") + SDL_GetError());
  }

  apply_hints(kPostInitHints);

  SDL_SetLogOutputFunction(sdl_log_bridge, sdl);
  SDL_SetLogPriorities(wlog_to_sdl(WLog_GetLogLevel(sdl->getWLog())));

  WLog_Print(sdl->getWLog(), WLOG_INFO, "fiRDP using backend '%s'", SDL_GetCurrentVideoDriver());
  sdl->setMetadata();
  return {};
}

static SessionError classify_exit(rdpContext* context, int exit_code) {
  UINT32 error = freerdp_get_last_error(context);
  if (exit_code == ERRCONNECT_LOGON_FAILURE || error == FREERDP_ERROR_CONNECT_LOGON_FAILURE) {
    return SessionError::kLogonFailure;
  }
  if (error == FREERDP_ERROR_CONNECT_CANCELLED) {
    return SessionError::kUserDisconnect;
  }
  return SessionError::kGeneral;
}

struct ResolutionOverride {
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t scale = 0;
};

static ResolutionOverride s_resolution_override{};
static BOOL (*s_original_preconnect)(freerdp*) = nullptr;

static void write_monitor_resolution(rdpSettings* settings, const ResolutionOverride& o) {
  freerdp_settings_set_uint32(settings, FreeRDP_DesktopWidth, o.width);
  freerdp_settings_set_uint32(settings, FreeRDP_DesktopHeight, o.height);
  if (o.scale != 0) {
    freerdp_settings_set_uint32(settings, FreeRDP_DesktopScaleFactor, o.scale);
  }
  if (freerdp_settings_get_uint32(settings, FreeRDP_MonitorCount) == 0) {
    return;
  }
  auto* monitor = static_cast<rdpMonitor*>(
      freerdp_settings_get_pointer_array_writable(settings, FreeRDP_MonitorDefArray, 0));
  if (monitor == nullptr) {
    return;
  }
  monitor->width = static_cast<INT32>(o.width);
  monitor->height = static_cast<INT32>(o.height);
  if (o.scale != 0) {
    monitor->attributes.desktopScaleFactor = o.scale;
  }
}

static BOOL wrapped_preconnect(freerdp* instance) {
  if (!s_original_preconnect(instance)) {
    return FALSE;
  }
  if (s_resolution_override.width == 0) {
    return TRUE;
  }
  write_monitor_resolution(instance->context->settings, s_resolution_override);
  WLog_Print(get_context(instance->context)->getWLog(),
             WLOG_INFO,
             "Resolution override: %ux%u @ %u%%",
             s_resolution_override.width,
             s_resolution_override.height,
             s_resolution_override.scale);
  return TRUE;
}

static void install_preconnect_override(SdlContext* sdl, const ResolutionOverride& o) {
  s_resolution_override = o;
  auto* instance = sdl->context()->instance;
  if (instance->PreConnect == wrapped_preconnect) {
    return;
  }
  s_original_preconnect = instance->PreConnect;
  instance->PreConnect = wrapped_preconnect;
}

#ifdef __APPLE__

static void apply_native_resolution(SdlContext* sdl, const DisplayInfo& info) {
  auto native = info.native_display();
  if (native.pixel_w == 0) {
    return;
  }
  install_preconnect_override(sdl, {native.pixel_w, native.pixel_h, 0});
}

#endif

static std::expected<std::optional<SDL_DisplayID>, std::string> resolve_display_id(std::optional<int> index) {
  if (!index.has_value()) {
    return std::optional<SDL_DisplayID>{};
  }
  if (*index < 0) {
    return std::unexpected(std::string("display index must be non-negative"));
  }
  int count = 0;
  auto* ids = SDL_GetDisplays(&count);
  if (!ids) {
    return std::unexpected(std::string("SDL_GetDisplays failed: ") + SDL_GetError());
  }
  std::optional<SDL_DisplayID> result;
  if (*index < count) {
    result = ids[*index];
  }
  SDL_free(ids);
  if (!result.has_value()) {
    return std::unexpected("display index " + std::to_string(*index) + " out of range (have " + std::to_string(count) +
                           " displays)");
  }
  return result;
}

static void apply_monitor_id(SdlContext* sdl, SDL_DisplayID id) {
  auto value = static_cast<UINT32>(id);
  freerdp_settings_set_pointer_len(sdl->context()->settings, FreeRDP_MonitorIds, &value, 1);
}

static void apply_native_scale(SdlContext* sdl, bool native_resolution, const DisplayInfo& info) {
  auto pct = info.scale_percent(native_resolution);
  if (pct == 0) {
    return;
  }
  freerdp_settings_set_uint32(sdl->context()->settings, FreeRDP_DesktopScaleFactor, pct);
  WLog_Print(sdl->getWLog(), WLOG_INFO, "Desktop scale factor: %u%%", pct);
}

static uint32_t round_even(double value) {
  auto rounded = static_cast<uint32_t>(value + 0.5);
  return rounded & ~1u;
}

static uint32_t width_for_aspect(uint32_t height, const NativeDisplay& display) {
  double aspect = static_cast<double>(display.pixel_w) / static_cast<double>(display.pixel_h);
  return round_even(static_cast<double>(height) * aspect);
}

static void apply_resolution(SdlContext* sdl, const DisplayInfo& info, uint32_t target_height) {
  auto display = info.backing_size();
  if (display.pixel_w == 0 || display.pixel_h == 0) {
    WLog_Print(sdl->getWLog(), WLOG_WARN, "--resolution ignored: display size unavailable");
    return;
  }
  uint32_t height = round_even(target_height);
  uint32_t width = width_for_aspect(height, display);
  freerdp_settings_set_bool(sdl->context()->settings, FreeRDP_SmartSizing, TRUE);
  install_preconnect_override(sdl, {width, height, 0});
  WLog_Print(sdl->getWLog(), WLOG_INFO, "Resolution %ux%u (matched to display aspect, scaled to fill)", width,
             height);
}

std::expected<void, SessionFailure> RdpSession::run(rdpFile* file,
                                                    const std::string& password,
                                                    const SessionOptions& opts) {
  std::unique_ptr<sdl_rdp_context, void (*)(sdl_rdp_context*)> owner(nullptr, context_free);

  if (auto r = init_freerdp(file, password, opts, owner); !r) {
    return r;
  }

  auto* sdl = owner->sdl;

  if (auto r = init_sdl(sdl, opts); !r) {
    return r;
  }

  auto cleanup = [&]() {
    std::ignore = sdl->setGrabMouse(false);
    std::ignore = sdl->setGrabKeyboard(false);
    sdl->cleanup();
    freerdp_del_signal_cleanup_handler(sdl->context(), sdl_term_handler);
    SDL_Quit();
  };

  if (!sdl->detectDisplays()) {
    cleanup();
    return fail("Failed to detect displays");
  }

  auto display_id = resolve_display_id(opts.display);
  if (!display_id) {
    cleanup();
    return fail(display_id.error());
  }
  if (display_id->has_value()) {
    apply_monitor_id(sdl, **display_id);
  }
  auto info = DisplayInfo(display_id->value_or(0));

  if (opts.resolution_height > 0) {
    apply_resolution(sdl, info, opts.resolution_height);
  } else {
#ifdef __APPLE__
    if (opts.native_resolution && freerdp_settings_get_bool(sdl->context()->settings, FreeRDP_Fullscreen)) {
      apply_native_resolution(sdl, info);
    }
#endif
    if (opts.native_scale) {
      apply_native_scale(sdl, opts.native_resolution, info);
    }
  }

  auto* context = sdl->context();

  if (!stream_dump_register_handlers(context, CONNECTION_STATE_MCS_CREATE_REQUEST, FALSE)) {
    cleanup();
    return fail("Failed to register stream handlers");
  }

  ServerDiagnostics diagnostics;
  diagnostics.install(context);

  if (freerdp_client_start(context) != 0) {
    cleanup();
    return fail("Failed to start RDP connection");
  }

  int rc = event_loop(sdl, opts, diagnostics);

  if (freerdp_client_stop(context) != 0) {
    rc = -1;
  }

  int exit_code = sdl->exitCode();
  cleanup();

  if (rc == 0 && exit_code == 0) {
    return {};
  }

  auto code = classify_exit(context, exit_code);
  if (code == SessionError::kUserDisconnect) {
    return {};
  }
  return fail(code, "RDP session ended with error (exit code " + std::to_string(exit_code) + ")");
}
