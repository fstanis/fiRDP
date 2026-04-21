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

#include <SDL3/SDL.h>
#include <freerdp/freerdp.h>
#include <freerdp/log.h>
#include <freerdp/settings.h>
#include <freerdp/utils/smartcardlogon.h>

#include <cstring>

#include "dialogs/sdl_dialogs.hpp"

#define TAG CLIENT_TAG("fi.dialogs")

BOOL sdl_authenticate_ex(freerdp* instance, char** username, char** password, char** domain, rdp_auth_reason reason) {
  WINPR_ASSERT(instance);
  WINPR_ASSERT(username);
  WINPR_ASSERT(password);
  WINPR_ASSERT(domain);

  switch (reason) {
    case AUTH_TLS:
    case AUTH_RDP:
    case AUTH_SMARTCARD_PIN:
      if ((*username) && (*password)) {
        return TRUE;
      }
      break;
    default:
      break;
  }

  if (*username && *password) {
    return TRUE;
  }

  WLog_WARN(TAG, "Authentication requested but no credentials available (reason=%d)", reason);
  return FALSE;
}

BOOL sdl_choose_smartcard([[maybe_unused]] freerdp* instance,
                          [[maybe_unused]] SmartcardCertInfo** cert_list,
                          DWORD count,
                          DWORD* choice,
                          [[maybe_unused]] BOOL gateway) {
  WINPR_ASSERT(choice);
  if (count == 0) {
    return FALSE;
  }
  *choice = 0;
  return TRUE;
}

SSIZE_T sdl_retry_dialog(freerdp* instance, const char* what, size_t current, [[maybe_unused]] void* userarg) {
  WINPR_ASSERT(instance);
  WINPR_ASSERT(instance->context);
  WINPR_ASSERT(what);

  auto settings = instance->context->settings;
  const BOOL enabled = freerdp_settings_get_bool(settings, FreeRDP_AutoReconnectionEnabled);
  const size_t delay = freerdp_settings_get_uint32(settings, FreeRDP_TcpConnectTimeout);

  if ((strcmp(what, "arm-transport") != 0) && (strcmp(what, "connection") != 0)) {
    WLog_ERR(TAG, "Unknown retry module %s, aborting", what);
    return -1;
  }

  if (!enabled) {
    WLog_ERR(TAG, "Automatic reconnection disabled, terminating");
    return -1;
  }

  const size_t max = freerdp_settings_get_uint32(settings, FreeRDP_AutoReconnectMaxRetries);
  if (current >= max) {
    WLog_ERR(TAG, "[%s] retries exceeded (%" PRIuz "/%" PRIuz ")", what, current, max);
    return -1;
  }

  WLog_INFO(TAG, "[%s] retry %" PRIuz "/%" PRIuz ", delaying %" PRIuz "ms", what, current + 1, max, delay);
  return WINPR_ASSERTING_INT_CAST(ssize_t, delay);
}

DWORD sdl_verify_certificate_ex([[maybe_unused]] freerdp* instance,
                                [[maybe_unused]] const char* host,
                                [[maybe_unused]] UINT16 port,
                                [[maybe_unused]] const char* common_name,
                                [[maybe_unused]] const char* subject,
                                [[maybe_unused]] const char* issuer,
                                [[maybe_unused]] const char* fingerprint,
                                [[maybe_unused]] DWORD flags) {
  WLog_INFO(TAG, "Auto-accepting certificate for %s:%" PRIu16, host, port);
  return 1;
}

DWORD sdl_verify_changed_certificate_ex([[maybe_unused]] freerdp* instance,
                                        [[maybe_unused]] const char* host,
                                        [[maybe_unused]] UINT16 port,
                                        [[maybe_unused]] const char* common_name,
                                        [[maybe_unused]] const char* subject,
                                        [[maybe_unused]] const char* issuer,
                                        [[maybe_unused]] const char* new_fingerprint,
                                        [[maybe_unused]] const char* old_subject,
                                        [[maybe_unused]] const char* old_issuer,
                                        [[maybe_unused]] const char* old_fingerprint,
                                        [[maybe_unused]] DWORD flags) {
  WLog_WARN(TAG, "Certificate changed for %s:%" PRIu16 " — auto-accepting", host, port);
  return 1;
}

int sdl_logon_error_info(freerdp* instance, UINT32 data, UINT32 type) {
  if (!instance || !instance->context) {
    return -1;
  }

  if (type == LOGON_MSG_SESSION_CONTINUE) {
    return 0;
  }

  const char* str_data = freerdp_get_logon_error_info_data(data);
  const char* str_type = freerdp_get_logon_error_info_type(type);
  WLog_ERR(TAG, "Logon Error Info: %s [%s]", str_data, str_type);
  return -1;
}

BOOL sdl_present_gateway_message([[maybe_unused]] freerdp* instance,
                                 [[maybe_unused]] UINT32 type,
                                 [[maybe_unused]] BOOL isDisplayMandatory,
                                 [[maybe_unused]] BOOL isConsentMandatory,
                                 [[maybe_unused]] size_t length,
                                 [[maybe_unused]] const WCHAR* message) {
  return TRUE;
}

BOOL sdl_cert_dialog_show([[maybe_unused]] const char* title, [[maybe_unused]] const char* message) {
  return sdl_push_user_event(SDL_EVENT_USER_CERT_RESULT, 1);
}

BOOL sdl_message_dialog_show([[maybe_unused]] const char* title,
                             [[maybe_unused]] const char* message,
                             [[maybe_unused]] Sint32 flags) {
  return sdl_push_user_event(SDL_EVENT_USER_SHOW_RESULT, 1);
}

BOOL sdl_auth_dialog_show(const SDL_UserAuthArg* args) {
  char* user = nullptr;
  if (args->user) {
    user = _strdup(args->user);
  }
  char* domain = nullptr;
  if (args->domain) {
    domain = _strdup(args->domain);
  }
  char* pwd = nullptr;
  if (args->password) {
    pwd = _strdup(args->password);
  }
  return sdl_push_user_event(SDL_EVENT_USER_AUTH_RESULT, user, domain, pwd, 1);
}

BOOL sdl_scard_dialog_show([[maybe_unused]] const char* title,
                           [[maybe_unused]] Sint32 count,
                           [[maybe_unused]] const char** list) {
  return sdl_push_user_event(SDL_EVENT_USER_SCARD_RESULT, 0);
}

void sdl_dialogs_init() {}

void sdl_dialogs_uninit() {}
