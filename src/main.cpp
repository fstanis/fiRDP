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

#include <termios.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sdl_prefs.hpp>
#include <string>

#ifdef __APPLE__
#include <ApplicationServices/ApplicationServices.h>

static bool check_accessibility(bool prompt) {
  CFStringRef key = kAXTrustedCheckOptionPrompt;
  CFBooleanRef value = kCFBooleanFalse;
  if (prompt) {
    value = kCFBooleanTrue;
  }
  CFDictionaryRef opts = CFDictionaryCreate(
      nullptr, reinterpret_cast<const void**>(&key), reinterpret_cast<const void**>(&value), 1, nullptr, nullptr);
  bool result = AXIsProcessTrustedWithOptions(opts);
  CFRelease(opts);
  return result;
}
#else
static bool check_accessibility([[maybe_unused]] bool prompt) {
  return true;
}
#endif

#include "host_keys.hpp"
#include "password_store.hpp"
#include "rdp_connection.hpp"
#include "rdp_file.hpp"

namespace {

constexpr const char* kDefaultConfig = R"({
  "SDL_KeyModMask": ["KMOD_NONE"],
  "host_keys": []
})";

struct Args {
  bool auto_connect = false;
  bool quiet = false;
  bool grab_keyboard = false;
  bool native_resolution = false;
  bool native_scale = false;

  bool prefer_h264 = false;
  bool low_latency = false;
  std::string rdp_path;
};

std::string read_password(const std::string& prompt) {
  std::cerr << prompt;

  struct termios old_term{};
  tcgetattr(STDIN_FILENO, &old_term);
  auto new_term = old_term;
  new_term.c_lflag &= ~ECHO;
  tcsetattr(STDIN_FILENO, TCSANOW, &new_term);

  std::string password;
  std::getline(std::cin, password);

  tcsetattr(STDIN_FILENO, TCSANOW, &old_term);
  std::cerr << '\n';
  return password;
}

void usage(const char* prog) {
  std::cerr << "Usage: " << prog << " [options] <file.rdp>\n"
            << "\nOptions:\n"
            << "  -c, --connect         Connect immediately (skip confirmation)\n"
            << "  -q, --quiet           Suppress connection info output\n"
            << "  -g, --grab-keyboard   Grab keyboard (requires Accessibility on macOS)\n"
            << "  -s, --native-scale    Override desktop scale factor with local display scale\n"
            << "      --native-resolution  Use display's native panel resolution (macOS only)\n"

            << "      --prefer-h264     Hint server to prefer H.264\n"
            << "      --low-latency     Send QoE feedback and suspend per-frame acks\n"
            << "  -h, --help            Show this help\n";
}

Args parse_args(int argc, char* argv[]) {
  Args args;
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "-c" || arg == "--connect") {
      args.auto_connect = true;
    } else if (arg == "-q" || arg == "--quiet") {
      args.quiet = true;
    } else if (arg == "-g" || arg == "--grab-keyboard") {
      args.grab_keyboard = true;
    } else if (arg == "-s" || arg == "--native-scale") {
      args.native_scale = true;
    } else if (arg == "--native-resolution") {
#ifdef __APPLE__
      args.native_resolution = true;
#else
      std::cerr << "Error: --native-resolution is only supported on macOS\n";
      std::exit(1);
#endif
    } else if (arg == "--prefer-h264") {
      args.prefer_h264 = true;
    } else if (arg == "--low-latency") {
      args.low_latency = true;
    } else if (arg == "-h" || arg == "--help") {
      usage(argv[0]);
      std::exit(0);
    } else if (arg[0] == '-') {
      std::cerr << "Unknown option: " << arg << '\n';
      usage(argv[0]);
      std::exit(1);
    } else {
      args.rdp_path = arg;
    }
  }
  if (args.rdp_path.empty()) {
    usage(argv[0]);
    std::exit(1);
  }
  return args;
}

void init_config() {
  auto config_path = std::filesystem::path(SdlPref::instance()->get_pref_file());

  if (!std::filesystem::exists(config_path)) {
    std::filesystem::create_directories(config_path.parent_path());
    std::ofstream(config_path) << kDefaultConfig;
    std::ignore = SdlPref::instance(config_path.string());
  }
}

std::unique_ptr<RdpFile> load_rdp_file(const std::string& path) {
  try {
    return RdpFile::parse(path);
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << '\n';
    std::exit(1);
  }
}

std::string resolve_password(const std::string& server, const std::string& username, bool quiet) {
  auto password = PasswordStore::lookup(server, username);
  if (!quiet) {
    if (password.empty()) {
      std::cerr << std::left << std::setw(24) << "Password" << "(not saved)" << '\n';
    } else {
      std::cerr << std::left << std::setw(24) << "Password" << "****" << '\n';
    }
  }
  if (!password.empty()) {
    return password;
  }
  password = read_password("Password: ");
  if (password.empty()) {
    std::cerr << "Error: password required\n";
    std::exit(1);
  }
  return password;
}

void wait_for_accessibility(bool grab_keyboard) {
  if (!grab_keyboard || check_accessibility(true)) {
    return;
  }
  std::cerr << "Waiting for Accessibility permission (grant it in the dialog or System Settings)...\n";
  while (!check_accessibility(false)) {
    usleep(500000);
  }
  std::cerr << "Accessibility permission granted.\n";
}

void confirm_connection(bool auto_connect) {
  if (auto_connect) {
    return;
  }
  std::cerr << "\nPress Enter to connect (or Ctrl+C to cancel)...";
  std::string dummy;
  std::getline(std::cin, dummy);
}

int run_session(RdpFile& rdp, const std::string& password, const SessionOptions& opts) {
  auto result = RdpSession::run(rdp.handle(), password, opts);
  if (result) {
    PasswordStore::store(rdp.server(), rdp.username(), password);
    return 0;
  }
  auto& [code, message] = result.error();
  std::cerr << "Error: " << message << '\n';
  if (code == SessionError::kLogonFailure) {
    PasswordStore::remove(rdp.server(), rdp.username());
  } else {
    PasswordStore::store(rdp.server(), rdp.username(), password);
  }
  return code == SessionError::kUserDisconnect ? 0 : 1;
}

void suppress_kerberos() {
  setenv("KRB5_CONFIG", "/dev/null", 0);
}

}  // namespace

int main(int argc, char* argv[]) {
  suppress_kerberos();
  init_config();
  auto args = parse_args(argc, argv);
  auto rdp = load_rdp_file(args.rdp_path);

  if (!args.quiet) {
    rdp->print(std::cerr);
  }

  auto password = resolve_password(rdp->server(), rdp->username(), args.quiet);
  wait_for_accessibility(args.grab_keyboard);
  confirm_connection(args.auto_connect);

  auto host_keys = parse_host_keys(SdlPref::instance()->get_array("host_keys"));
  return run_session(*rdp, password,
                     {.grab_keyboard = args.grab_keyboard,
                      .native_resolution = args.native_resolution,
                      .native_scale = args.native_scale,

                      .prefer_h264 = args.prefer_h264,
                      .low_latency = args.low_latency,
                      .host_keys = host_keys});
}
