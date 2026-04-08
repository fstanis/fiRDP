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

#include <SDL3/SDL.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_video.h>
#include <freerdp/channels/channels.h>
#include <freerdp/client/channels.h>
#include <freerdp/client/cliprdr.h>
#include <freerdp/client/file.h>
#include <freerdp/constants.h>
#include <freerdp/freerdp.h>
#include <freerdp/gdi/gdi.h>
#include <freerdp/log.h>
#include <freerdp/streamdump.h>
#include <freerdp/utils/signal.h>
#include <winpr/assert.h>
#include <winpr/crt.h>
#include <winpr/synch.h>

#include <memory>
#include <string>
#include <vector>

#include "dialogs/sdl_connection_dialog_hider.hpp"
#include "dialogs/sdl_dialogs.hpp"
#include "sdl_context.hpp"
#include "sdl_utils.hpp"

#define TAG CLIENT_TAG("fiRDP")

// SDL hints applied before SDL_Init
static constexpr struct {
  const char* key;
  const char* value;
} kPreInitHints[] = {
    {SDL_HINT_VIDEO_DRIVER, "wayland"},
    {SDL_HINT_VIDEO_DOUBLE_BUFFER, "1"},
};

// SDL hints applied after SDL_Init
static constexpr struct {
  const char* key;
  const char* value;
} kPostInitHints[] = {
    {SDL_HINT_RENDER_GPU_LOW_POWER, "0"},
    {SDL_HINT_RENDER_VSYNC, "0"},
    {SDL_HINT_FRAMEBUFFER_ACCELERATION, "1"},
    {SDL_HINT_VIDEO_SYNC_WINDOW_OPERATIONS, "0"},
    {SDL_HINT_ALLOW_ALT_TAB_WHILE_GRABBED, "0"},
    {SDL_HINT_PEN_MOUSE_EVENTS, "0"},
    {SDL_HINT_TOUCH_MOUSE_EVENTS, "0"},
    {SDL_HINT_PEN_TOUCH_EVENTS, "1"},
    {SDL_HINT_TRACKPAD_IS_TOUCH_ONLY, "1"},
    {SDL_HINT_MOUSE_DPI_SCALE_CURSORS, "1"},
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

// FreeRDP client entry point callbacks

static BOOL sdl_client_global_init() {
  return freerdp_handle_signals() == 0;
}

static void sdl_client_global_uninit() {}

static BOOL sdl_client_new(freerdp* instance, rdpContext* context) {
  if (!instance || !context)
    return FALSE;
  auto* ctx = reinterpret_cast<sdl_rdp_context*>(context);
  ctx->sdl = new SdlContext(context);
  return ctx->sdl != nullptr;
}

static void sdl_client_free([[maybe_unused]] freerdp* instance, rdpContext* context) {
  if (!context)
    return;
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
  if (ctx)
    freerdp_client_context_free(&ctx->common.context);
}

// SDL log → WLog bridge

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
  if (WLog_IsLevelActive(log, level))
    WLog_PrintTextMessage(log, level, __LINE__, __FILE__, __func__, "[SDL:%d] %s", category, message);
}

// Event loop helpers

static void handle_key_event(SdlContext* sdl, const SDL_Event& ev) {
  auto mods = SDL_GetModState();
  WLog_Print(sdl->getWLog(), WLOG_DEBUG, "KEY_DOWN scancode=0x%x mods=0x%x", ev.key.scancode, mods);

  if (ev.key.scancode != SDL_SCANCODE_PAUSE)
    return;

  if ((mods & SDL_KMOD_CTRL) && (mods & SDL_KMOD_ALT)) {
    WLog_Print(sdl->getWLog(), WLOG_INFO, "Ctrl+Alt+Break: disconnecting");
    std::ignore = freerdp_abort_connect_context(sdl->context());
    return;
  }
  if (mods & SDL_KMOD_ALT) {
    WLog_Print(sdl->getWLog(), WLOG_INFO, "Alt+Break: toggling fullscreen");
    std::ignore = sdl->toggleFullscreen();
  }
}

static void handle_user_event(SdlContext* sdl, const SDL_Event& ev) {
  switch (ev.type) {
    case SDL_EVENT_USER_CERT_DIALOG: {
      SDLConnectionDialogHider hider(sdl);
      if (!sdl_cert_dialog_show(static_cast<const char*>(ev.user.data1), static_cast<const char*>(ev.user.data2)))
        throw ConnectionError{-1, "sdl_cert_dialog_show"};
    } break;
    case SDL_EVENT_USER_SHOW_DIALOG: {
      SDLConnectionDialogHider hider(sdl);
      if (!sdl_message_dialog_show(
              static_cast<const char*>(ev.user.data1), static_cast<const char*>(ev.user.data2), ev.user.code))
        throw ConnectionError{-1, "sdl_message_dialog_show"};
    } break;
    case SDL_EVENT_USER_SCARD_DIALOG: {
      SDLConnectionDialogHider hider(sdl);
      if (!sdl_scard_dialog_show(
              static_cast<const char*>(ev.user.data1), ev.user.code, static_cast<const char**>(ev.user.data2)))
        throw ConnectionError{-1, "sdl_scard_dialog_show"};
    } break;
    case SDL_EVENT_USER_AUTH_DIALOG: {
      SDLConnectionDialogHider hider(sdl);
      if (!sdl_auth_dialog_show(reinterpret_cast<const SDL_UserAuthArg*>(ev.padding)))
        throw ConnectionError{-1, "sdl_auth_dialog_show"};
    } break;
    case SDL_EVENT_USER_UPDATE: {
      std::vector<SDL_Rect> rects;
      do {
        rects = sdl->pop();
        if (!sdl->drawToWindows(rects))
          throw ConnectionError{-1, "drawToWindows"};
      } while (!rects.empty());
    } break;
    case SDL_EVENT_USER_CREATE_WINDOWS:
      if (!static_cast<SdlContext*>(ev.user.data1)->createWindows())
        throw ConnectionError{-1, "createWindows"};
      break;
    case SDL_EVENT_USER_WINDOW_RESIZEABLE:
      if (auto* w = static_cast<SdlWindow*>(ev.user.data1))
        w->resizeable(ev.user.code != 0);
      break;
    case SDL_EVENT_USER_WINDOW_FULLSCREEN:
      if (auto* w = static_cast<SdlWindow*>(ev.user.data1))
        w->fullscreen(ev.user.code != 0, ev.user.data2 != nullptr);
      break;
    case SDL_EVENT_USER_WINDOW_MINIMIZE:
      if (!sdl->minimizeAllWindows())
        throw ConnectionError{-1, "minimizeAllWindows"};
      break;
    case SDL_EVENT_USER_POINTER_NULL:
      if (!sdl->setCursor(SdlContext::CURSOR_NULL))
        throw ConnectionError{-1, "setCursor(NULL)"};
      break;
    case SDL_EVENT_USER_POINTER_DEFAULT:
      if (!sdl->setCursor(SdlContext::CURSOR_DEFAULT))
        throw ConnectionError{-1, "setCursor(DEFAULT)"};
      break;
    case SDL_EVENT_USER_POINTER_POSITION: {
      auto x = static_cast<float>(static_cast<INT32>(reinterpret_cast<uintptr_t>(ev.user.data1)));
      auto y = static_cast<float>(static_cast<INT32>(reinterpret_cast<uintptr_t>(ev.user.data2)));
      if (!sdl->moveMouseTo({x, y}))
        throw ConnectionError{-1, "moveMouseTo"};
    } break;
    case SDL_EVENT_USER_POINTER_SET:
      if (!sdl->setCursor(static_cast<rdpPointer*>(ev.user.data1)))
        throw ConnectionError{-1, "setCursor(IMAGE)"};
      break;
    default:
      break;
  }
}

static int event_loop(SdlContext* sdl) {
  try {
    while (!sdl->shallAbort()) {
      SDL_Event ev = {};
      while (!sdl->shallAbort() && SDL_WaitEventTimeout(nullptr, 1000)) {
        if (SDL_PeepEvents(&ev, 1, SDL_GETEVENT, SDL_EVENT_FIRST, SDL_EVENT_USER_RETRY_DIALOG) < 0) {
          if (sdl_log_error(-1, sdl->getWLog(), "SDL_PeepEvents"))
            continue;
        }

        if (sdl->shallAbort(true))
          continue;

        if (sdl->getDialog().handleEvent(ev))
          continue;

        switch (ev.type) {
          case SDL_EVENT_QUIT:
            std::ignore = freerdp_abort_connect_context(sdl->context());
            break;
          case SDL_EVENT_KEY_DOWN:
            handle_key_event(sdl, ev);
            if (!sdl->handleEvent(ev))
              throw ConnectionError{-1, "handleEvent"};
            break;
          case SDL_EVENT_USER_CERT_DIALOG:
          case SDL_EVENT_USER_SHOW_DIALOG:
          case SDL_EVENT_USER_SCARD_DIALOG:
          case SDL_EVENT_USER_AUTH_DIALOG:
          case SDL_EVENT_USER_UPDATE:
          case SDL_EVENT_USER_CREATE_WINDOWS:
          case SDL_EVENT_USER_WINDOW_RESIZEABLE:
          case SDL_EVENT_USER_WINDOW_FULLSCREEN:
          case SDL_EVENT_USER_WINDOW_MINIMIZE:
          case SDL_EVENT_USER_POINTER_NULL:
          case SDL_EVENT_USER_POINTER_DEFAULT:
          case SDL_EVENT_USER_POINTER_POSITION:
          case SDL_EVENT_USER_POINTER_SET:
            handle_user_event(sdl, ev);
            break;
          default:
            if (!sdl->handleEvent(ev))
              throw ConnectionError{-1, "handleEvent"};
            break;
        }
      }
    }
    return 0;
  } catch (ConnectionError& err) {
    WLog_Print(sdl->getWLog(), WLOG_ERROR, "%s", err.what());
    return err.rc();
  }
}

// Apply SDL hints from a table
static void apply_hints(auto& table) {
  for (const auto& [key, value] : table)
    SDL_SetHint(key, value);
}

using Result = std::expected<void, std::string>;

static Result init_freerdp(rdpFile* file,
                           const std::string& password,
                           std::unique_ptr<sdl_rdp_context, void (*)(sdl_rdp_context*)>& owner) {
  RDP_CLIENT_ENTRY_POINTS ep = {};
  register_entry_points(&ep);

  owner = {reinterpret_cast<sdl_rdp_context*>(freerdp_client_context_new(&ep)), context_free};
  if (!owner)
    return std::unexpected("Failed to create FreeRDP context");

  auto* settings = owner->sdl->context()->settings;

  if (!freerdp_client_populate_settings_from_rdp_file(file, settings))
    return std::unexpected("Failed to apply RDP file settings");

  if (!password.empty())
    freerdp_settings_set_string(settings, FreeRDP_Password, password.c_str());

  freerdp_settings_set_bool(settings, FreeRDP_AutoAcceptCertificate, TRUE);
  return {};
}

static Result init_sdl(SdlContext* sdl) {
  apply_hints(kPreInitHints);

  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
    return std::unexpected(std::string("SDL_Init failed: ") + SDL_GetError());

  apply_hints(kPostInitHints);

  SDL_SetLogOutputFunction(sdl_log_bridge, sdl);
  SDL_SetLogPriorities(wlog_to_sdl(WLog_GetLogLevel(sdl->getWLog())));

  WLog_Print(sdl->getWLog(), WLOG_INFO, "fiRDP using backend '%s'", SDL_GetCurrentVideoDriver());
  sdl->setMetadata();
  return {};
}

std::expected<void, std::string> RdpSession::run(rdpFile* file, const std::string& password) {
  std::unique_ptr<sdl_rdp_context, void (*)(sdl_rdp_context*)> owner(nullptr, context_free);

  if (auto r = init_freerdp(file, password, owner); !r)
    return r;

  auto* sdl = owner->sdl;

  if (auto r = init_sdl(sdl); !r)
    return r;

  auto cleanup = [&]() {
    sdl->cleanup();
    freerdp_del_signal_cleanup_handler(sdl->context(), sdl_term_handler);
    SDL_Quit();
  };

  if (!sdl->detectDisplays()) {
    cleanup();
    return std::unexpected("Failed to detect displays");
  }

  auto* context = sdl->context();

  if (!stream_dump_register_handlers(context, CONNECTION_STATE_MCS_CREATE_REQUEST, FALSE)) {
    cleanup();
    return std::unexpected("Failed to register stream handlers");
  }

  if (freerdp_client_start(context) != 0) {
    cleanup();
    return std::unexpected("Failed to start RDP connection");
  }

  int rc = event_loop(sdl);

  if (freerdp_client_stop(context) != 0)
    rc = -1;
  if (sdl->exitCode() != 0)
    rc = sdl->exitCode();

  cleanup();

  if (rc != 0)
    return std::unexpected("RDP session ended with error");
  return {};
}
