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

#include "password_store.hpp"

#include <libsecret/secret.h>

namespace {

const SecretSchema kSchema = {
    "com.fiRDP.password",
    SECRET_SCHEMA_NONE,
    {{"server", SECRET_SCHEMA_ATTRIBUTE_STRING},
     {"username", SECRET_SCHEMA_ATTRIBUTE_STRING},
     {nullptr, SecretSchemaAttributeType(0)}},
    0,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
};

}  // namespace

std::string PasswordStore::lookup(const std::string& server, const std::string& username) {
  GError* err = nullptr;
  gchar* pw = secret_password_lookup_sync(
      &kSchema, nullptr, &err, "server", server.c_str(), "username", username.c_str(), nullptr);
  if (err) {
    g_error_free(err);
    return {};
  }
  if (!pw)
    return {};

  std::string result(pw);
  secret_password_free(pw);
  return result;
}

void PasswordStore::store(const std::string& server, const std::string& username, const std::string& password) {
  GError* err = nullptr;
  auto label = "fiRDP: " + username + "@" + server;
  secret_password_store_sync(&kSchema,
                             SECRET_COLLECTION_DEFAULT,
                             label.c_str(),
                             password.c_str(),
                             nullptr,
                             &err,
                             "server",
                             server.c_str(),
                             "username",
                             username.c_str(),
                             nullptr);
  if (err)
    g_error_free(err);
}
