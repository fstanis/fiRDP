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

#include <freerdp/freerdp.h>
#include <freerdp/log.h>
#include <freerdp/settings.h>

#include <sstream>

class SDLConnectionDialog {
 public:
  ~SDLConnectionDialog() = default;
};

#include "dialogs/sdl_connection_dialog_hider.hpp"
#include "dialogs/sdl_connection_dialog_wrapper.hpp"

SdlConnectionDialogWrapper::SdlConnectionDialogWrapper(wLog* log) : _log(log) {}

SdlConnectionDialogWrapper::~SdlConnectionDialogWrapper() = default;

void SdlConnectionDialogWrapper::create([[maybe_unused]] rdpContext* context) {}

void SdlConnectionDialogWrapper::destroy() {
  _connection_dialog.reset();
}

bool SdlConnectionDialogWrapper::isRunning() const {
  return false;
}

bool SdlConnectionDialogWrapper::isVisible() const {
  return false;
}

bool SdlConnectionDialogWrapper::handleEvent([[maybe_unused]] const SDL_Event& event) {
  return false;
}

void SdlConnectionDialogWrapper::setTitle(const char* fmt, ...) {
  va_list ap = {};
  va_start(ap, fmt);
  va_end(ap);
}

void SdlConnectionDialogWrapper::setTitle([[maybe_unused]] const std::string& title) {}

void SdlConnectionDialogWrapper::showInfo(const char* fmt, ...) {
  va_list ap = {};
  va_start(ap, fmt);
  char buf[1024];
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  WLog_Print(_log, WLOG_INFO, "%s", buf);
}

void SdlConnectionDialogWrapper::showInfo(const std::string& info) {
  WLog_Print(_log, WLOG_INFO, "%s", info.c_str());
}

void SdlConnectionDialogWrapper::showWarn(const char* fmt, ...) {
  va_list ap = {};
  va_start(ap, fmt);
  char buf[1024];
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  WLog_Print(_log, WLOG_WARN, "%s", buf);
}

void SdlConnectionDialogWrapper::showWarn(const std::string& info) {
  WLog_Print(_log, WLOG_WARN, "%s", info.c_str());
}

void SdlConnectionDialogWrapper::showError(const char* fmt, ...) {
  va_list ap = {};
  va_start(ap, fmt);
  char buf[1024];
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  WLog_Print(_log, WLOG_ERROR, "%s", buf);
}

void SdlConnectionDialogWrapper::showError(const std::string& error) {
  WLog_Print(_log, WLOG_ERROR, "%s", error.c_str());
}

void SdlConnectionDialogWrapper::show([[maybe_unused]] SdlConnectionDialogWrapper::MsgType type,
                                      const std::string& msg) {
  DWORD level = WLOG_INFO;
  switch (type) {
    case MSG_WARN:
      level = WLOG_WARN;
      break;
    case MSG_ERROR:
      level = WLOG_ERROR;
      break;
    default:
      break;
  }
  WLog_Print(_log, level, "%s", msg.c_str());
}

void SdlConnectionDialogWrapper::show([[maybe_unused]] bool visible) {}

void SdlConnectionDialogWrapper::handleShow() {}

SdlConnectionDialogWrapper::EventArg::EventArg(bool visible) : _visible(visible), _mask(8) {}

SdlConnectionDialogWrapper::EventArg::EventArg(const std::string& title) : _title(title), _mask(1) {}

SdlConnectionDialogWrapper::EventArg::EventArg(MsgType type, const std::string& msg, bool visible)
    : _message(msg), _type(type), _visible(visible), _mask(14) {}

bool SdlConnectionDialogWrapper::EventArg::hasTitle() const {
  return _mask & 0x01;
}

const std::string& SdlConnectionDialogWrapper::EventArg::title() const {
  return _title;
}

bool SdlConnectionDialogWrapper::EventArg::hasMessage() const {
  return _mask & 0x02;
}

const std::string& SdlConnectionDialogWrapper::EventArg::message() const {
  return _message;
}

bool SdlConnectionDialogWrapper::EventArg::hasType() const {
  return _mask & 0x04;
}

SdlConnectionDialogWrapper::MsgType SdlConnectionDialogWrapper::EventArg::type() const {
  return _type;
}

bool SdlConnectionDialogWrapper::EventArg::hasVisibility() const {
  return _mask & 0x08;
}

bool SdlConnectionDialogWrapper::EventArg::visible() const {
  return _visible;
}

std::string SdlConnectionDialogWrapper::EventArg::str() const {
  std::stringstream ss;
  ss << "{ title:" << _title << ", message:" << _message << ", type:" << _type << ", visible:" << _visible
     << ", mask:" << _mask << "}";
  return ss.str();
}

void SdlConnectionDialogWrapper::push([[maybe_unused]] EventArg&& arg) {}

SDLConnectionDialogHider::SDLConnectionDialogHider([[maybe_unused]] SdlContext* sdl) {}

SDLConnectionDialogHider::~SDLConnectionDialogHider() = default;
