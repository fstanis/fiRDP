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

#include "password_store.hpp"
#include "rdp_connection.hpp"
#include "rdp_file.hpp"

namespace {

constexpr const char* kConfigDir = "fiRDP";
constexpr const char* kConfigFile = "config.json";
constexpr const char* kDefaultConfig = R"({
  "SDL_KeyModMask": ["KMOD_NONE"]
})";

struct Args {
  bool auto_connect = false;
  bool quiet = false;
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
            << "  -c, --connect    Connect immediately (skip confirmation)\n"
            << "  -q, --quiet      Suppress connection info output\n"
            << "  -h, --help       Show this help\n";
}

Args parse_args(int argc, char* argv[]) {
  Args args;
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "-c" || arg == "--connect")
      args.auto_connect = true;
    else if (arg == "-q" || arg == "--quiet")
      args.quiet = true;
    else if (arg == "-h" || arg == "--help") {
      usage(argv[0]);
      std::exit(0);
    } else if (arg[0] == '-') {
      std::cerr << "Unknown option: " << arg << '\n';
      usage(argv[0]);
      std::exit(1);
    } else
      args.rdp_path = arg;
  }
  if (args.rdp_path.empty()) {
    usage(argv[0]);
    std::exit(1);
  }
  return args;
}

void init_config() {
  auto config_dir = std::filesystem::path(getenv("HOME")) / ".config" / kConfigDir;
  auto config_path = config_dir / kConfigFile;

  if (!std::filesystem::exists(config_path)) {
    std::filesystem::create_directories(config_dir);
    std::ofstream(config_path) << kDefaultConfig;
  }

  std::ignore = SdlPref::instance(config_path.string());
}

}  // namespace

int main(int argc, char* argv[]) {
  init_config();
  auto args = parse_args(argc, argv);

  std::unique_ptr<RdpFile> rdp;
  try {
    rdp = RdpFile::parse(args.rdp_path);
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << '\n';
    return 1;
  }

  if (!args.quiet)
    rdp->print(std::cerr);

  auto server = rdp->server();
  auto username = rdp->username();

  auto password = PasswordStore::lookup(server, username);
  if (!args.quiet)
    std::cerr << std::left << std::setw(24) << "Password" << (password.empty() ? "(not saved)" : "****") << '\n';

  if (password.empty()) {
    password = read_password("Password: ");
    if (password.empty()) {
      std::cerr << "Error: password required\n";
      return 1;
    }
  }

  if (!args.auto_connect) {
    std::cerr << "\nPress Enter to connect (or Ctrl+C to cancel)...";
    std::string dummy;
    std::getline(std::cin, dummy);
  }

  PasswordStore::store(server, username, password);

  auto result = RdpSession::run(rdp->handle(), password);
  if (!result) {
    std::cerr << "Error: " << result.error() << '\n';
    return 1;
  }
  return 0;
}
