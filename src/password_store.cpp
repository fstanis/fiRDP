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

#ifdef __APPLE__

#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#include <memory>

namespace {

struct CFDeleter {
  void operator()(CFTypeRef ref) const {
    if (ref)
      CFRelease(ref);
  }
};
using CFPtr = std::unique_ptr<void, CFDeleter>;

CFPtr make_cf(CFTypeRef ref) {
  return CFPtr(const_cast<void*>(ref));
}

CFPtr make_query(const std::string& server, const std::string& username) {
  auto dict =
      CFPtr(CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
  auto* d = static_cast<CFMutableDictionaryRef>(dict.get());

  auto account = make_cf(CFStringCreateWithCString(nullptr, (username + "@" + server).c_str(), kCFStringEncodingUTF8));
  auto service = make_cf(CFStringCreateWithCString(nullptr, "fiRDP", kCFStringEncodingUTF8));

  CFDictionarySetValue(d, kSecClass, kSecClassGenericPassword);
  CFDictionarySetValue(d, kSecAttrService, service.get());
  CFDictionarySetValue(d, kSecAttrAccount, account.get());
  return dict;
}

CFMutableDictionaryRef mut(const CFPtr& ptr) {
  return static_cast<CFMutableDictionaryRef>(ptr.get());
}

}  // namespace

std::string PasswordStore::lookup(const std::string& server, const std::string& username) {
  auto query = make_query(server, username);
  CFDictionarySetValue(mut(query), kSecReturnData, kCFBooleanTrue);
  CFDictionarySetValue(mut(query), kSecMatchLimit, kSecMatchLimitOne);

  CFTypeRef result = nullptr;
  if (SecItemCopyMatching(mut(query), &result) != errSecSuccess || !result)
    return {};

  auto data = make_cf(result);
  auto* cf_data = static_cast<CFDataRef>(result);
  return {reinterpret_cast<const char*>(CFDataGetBytePtr(cf_data)), static_cast<size_t>(CFDataGetLength(cf_data))};
}

void PasswordStore::store(const std::string& server, const std::string& username, const std::string& password) {
  remove(server, username);

  auto query = make_query(server, username);
  auto pw_data = make_cf(CFDataCreate(nullptr, reinterpret_cast<const UInt8*>(password.data()), password.size()));
  CFDictionarySetValue(mut(query), kSecValueData, pw_data.get());

  std::ignore = SecItemAdd(mut(query), nullptr);
}

void PasswordStore::remove(const std::string& server, const std::string& username) {
  auto query = make_query(server, username);
  std::ignore = SecItemDelete(mut(query));
}

#else

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

void PasswordStore::remove(const std::string& server, const std::string& username) {
  GError* err = nullptr;
  secret_password_clear_sync(&kSchema, nullptr, &err, "server", server.c_str(), "username", username.c_str(), nullptr);
  if (err)
    g_error_free(err);
}

#endif
