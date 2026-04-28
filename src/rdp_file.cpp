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

#include "rdp_file.hpp"

#include <charconv>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <stdexcept>

std::unique_ptr<RdpFile> RdpFile::parse(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Cannot open " + path);
  }

  std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

  auto* rdp = freerdp_client_rdp_file_new();
  if (!rdp) {
    throw std::runtime_error("Failed to create RDP file object");
  }

  if (!freerdp_client_parse_rdp_file_buffer_ex(
          rdp, reinterpret_cast<const BYTE*>(content.data()), content.size(), nullptr)) {
    freerdp_client_rdp_file_free(rdp);
    throw std::runtime_error("Failed to parse " + path);
  }

  auto result = std::unique_ptr<RdpFile>(new RdpFile(rdp));
  result->validate();
  return result;
}

static std::string url_decode(const std::string& input) {
  std::string result;
  result.reserve(input.size());
  for (size_t i = 0; i < input.size(); i++) {
    if (input[i] == '%' && i + 2 < input.size()) {
      int value = 0;
      auto [ptr, ec] = std::from_chars(&input[i + 1], &input[i + 3], value, 16);
      if (ec == std::errc{}) {
        result += static_cast<char>(value);
        i += 2;
        continue;
      }
    }
    result += input[i];
  }
  return result;
}

std::unique_ptr<RdpFile> RdpFile::from_url(const std::string& url) {
  auto scheme_end = url.find("://");
  if (scheme_end == std::string::npos) {
    throw std::runtime_error("Invalid RDP URL: missing scheme");
  }
  auto params = url.substr(scheme_end + 3);

  std::string content;
  std::istringstream stream(params);
  std::string pair;
  while (std::getline(stream, pair, '&')) {
    auto eq = pair.find('=');
    if (eq == std::string::npos) {
      throw std::runtime_error("Invalid RDP URL parameter: " + pair);
    }
    auto key = url_decode(pair.substr(0, eq));
    auto value = url_decode(pair.substr(eq + 1));
    content += key + ":" + value + "\r\n";
  }

  auto* rdp = freerdp_client_rdp_file_new();
  if (!rdp) {
    throw std::runtime_error("Failed to create RDP file object");
  }

  if (!freerdp_client_parse_rdp_file_buffer_ex(
          rdp, reinterpret_cast<const BYTE*>(content.data()), content.size(), nullptr)) {
    freerdp_client_rdp_file_free(rdp);
    throw std::runtime_error("Failed to parse RDP URL parameters");
  }

  auto result = std::unique_ptr<RdpFile>(new RdpFile(rdp));
  result->validate();
  return result;
}

void RdpFile::apply_overrides(const std::vector<std::string>& overrides) {
  for (const auto& line : overrides) {
    auto last_colon = line.rfind(':');
    if (last_colon == std::string::npos) {
      throw std::runtime_error("Invalid RDP override (expected key:type:value): " + line);
    }
    auto second_last = line.rfind(':', last_colon - 1);
    if (second_last == std::string::npos) {
      throw std::runtime_error("Invalid RDP override (expected key:type:value): " + line);
    }

    auto key = line.substr(0, second_last);
    auto type = line[second_last + 1];
    auto value = line.substr(last_colon + 1);

    if (type == 'i') {
      freerdp_client_rdp_file_set_integer_option(file_, key.c_str(), std::stoi(value));
    } else if (type == 's') {
      freerdp_client_rdp_file_set_string_option(file_, key.c_str(), value.c_str());
    } else {
      throw std::runtime_error("Invalid RDP override type '" + std::string(1, type) +
                               "' (expected 'i' or 's'): " + line);
    }
  }
}

void RdpFile::validate() const {
  if (server().empty()) {
    throw std::runtime_error("Missing required field: full address");
  }
  if (username().empty()) {
    throw std::runtime_error("Missing required field: username");
  }
  if (has_password()) {
    throw std::runtime_error(
        "Embedded passwords in .rdp files are not supported (insecure). "
        "Remove the password field and use the system keyring instead.");
  }
}

RdpFile::RdpFile(rdpFile* file) : file_(file) {}

RdpFile::~RdpFile() {
  if (file_) {
    freerdp_client_rdp_file_free(file_);
  }
}

std::string RdpFile::server() const {
  return get_string("full address");
}

std::string RdpFile::username() const {
  return get_string("username");
}

std::string RdpFile::domain() const {
  return get_string("domain");
}

bool RdpFile::has_password() const {
  return !get_string("password 51").empty();
}

int RdpFile::get_int(const char* name) const {
  return freerdp_client_rdp_file_get_integer_option(file_, name);
}

std::string RdpFile::get_string(const char* name) const {
  const char* v = freerdp_client_rdp_file_get_string_option(file_, name);
  if (v) {
    return v;
  }
  return "";
}

void RdpFile::print(std::ostream& out) const {
  constexpr int w = 26;

  auto str = [&](const char* label, const char* key) {
    auto v = get_string(key);
    if (!v.empty()) {
      out << std::left << std::setw(w) << label << v << '\n';
    }
  };
  auto num = [&](const char* label, const char* key) {
    auto v = get_int(key);
    if (v > 0) {
      out << std::left << std::setw(w) << label << v << '\n';
    }
  };
  auto flag = [&](const char* label, const char* key) {
    auto v = get_int(key);
    if (v) {
      out << std::left << std::setw(w) << label << "yes" << '\n';
    } else {
      out << std::left << std::setw(w) << label << "no" << '\n';
    }
  };

  str("Server", "full address");
  str("Username", "username");
  str("Domain", "domain");
  str("Gateway", "gatewayhostname");

  num("Desktop Width", "desktopwidth");
  num("Desktop Height", "desktopheight");
  num("Color Depth", "session bpp");
  num("Scale Factor", "desktopscalefactor");

  auto mode = get_int("screen mode id");
  if (mode == 2) {
    out << std::left << std::setw(w) << "Screen Mode" << "fullscreen" << '\n';
  } else {
    out << std::left << std::setw(w) << "Screen Mode" << "windowed" << '\n';
  }

  flag("Multi Monitor", "use multimon");
  flag("Smart Sizing", "smartsizing");

  num("Connection Type", "connection type");
  flag("Compression", "compression");
  flag("Bandwidth Auto-Detect", "bandwidthautodetect");
  flag("Network Auto-Detect", "networkautodetect");
  flag("Bitmap Cache", "bitmapcachepersistenable");

  flag("Font Smoothing", "allow font smoothing");
  flag("Desktop Composition", "allow desktop composition");
  flag("Wallpaper", "disable wallpaper");
  flag("Menu Animations", "disable menu anims");
  flag("Full Window Drag", "disable full window drag");
  flag("Themes", "disable themes");

  flag("Clipboard", "redirectclipboard");
  flag("Printers", "redirectprinters");
  flag("Smart Cards", "redirectsmartcards");
  flag("COM Ports", "redirectcomports");
  flag("POS Devices", "redirectposdevices");

  num("Audio Mode", "audiomode");
  flag("Audio Capture", "audiocapturemode");
  flag("Video Playback", "videoplaybackmode");

  flag("Auto Reconnect", "autoreconnection enabled");
  num("Auth Level", "authentication level");
  flag("Keyboard Hook", "keyboardhook");
}
