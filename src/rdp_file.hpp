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

#include <freerdp/client/file.h>

#include <memory>
#include <ostream>
#include <string>

class RdpFile {
 public:
  static std::unique_ptr<RdpFile> parse(const std::string& path);

  ~RdpFile();
  RdpFile(const RdpFile&) = delete;
  RdpFile& operator=(const RdpFile&) = delete;

  void validate() const;
  void print(std::ostream& out) const;

  std::string server() const;
  std::string username() const;
  std::string domain() const;
  bool has_password() const;
  int get_int(const char* name) const;

  rdpFile* handle() const { return file_; }

 private:
  explicit RdpFile(rdpFile* file);
  std::string get_string(const char* name) const;

  rdpFile* file_;
};
