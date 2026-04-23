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

#include "sdl_config.hpp"

namespace {

struct CFDeleter {
  void operator()(CFTypeRef ref) const {
    if (ref) {
      CFRelease(ref);
    }
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
  auto svc = make_cf(CFStringCreateWithCString(nullptr, SDL_CLIENT_UUID, kCFStringEncodingUTF8));

  CFDictionarySetValue(d, kSecClass, kSecClassGenericPassword);
  CFDictionarySetValue(d, kSecAttrService, svc.get());
  CFDictionarySetValue(d, kSecAttrAccount, account.get());
  return dict;
}

CFMutableDictionaryRef mut(const CFPtr& ptr) {
  return static_cast<CFMutableDictionaryRef>(ptr.get());
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
CFPtr make_permissive_access() {
  SecAccessRef access = nullptr;
  if (SecAccessCreate(CFSTR("fiRDP"), nullptr, &access) != errSecSuccess || !access) {
    return {};
  }

  CFArrayRef acls = nullptr;
  if (SecAccessCopyACLList(access, &acls) == errSecSuccess && acls) {
    for (CFIndex i = 0; i < CFArrayGetCount(acls); i++) {
      auto acl = reinterpret_cast<SecACLRef>(const_cast<void*>(CFArrayGetValueAtIndex(acls, i)));
      CFArrayRef apps = nullptr;
      CFStringRef desc = nullptr;
      SecKeychainPromptSelector prompt = 0;
      if (SecACLCopyContents(acl, &apps, &desc, &prompt) == errSecSuccess) {
        std::ignore = SecACLSetContents(acl, nullptr, desc, prompt);
        if (apps) CFRelease(apps);
        if (desc) CFRelease(desc);
      }
    }
    CFRelease(acls);
  }

  return make_cf(access);
}
#pragma clang diagnostic pop

}  // namespace

std::string PasswordStore::lookup(const std::string& server, const std::string& username) {
  auto query = make_query(server, username);
  CFDictionarySetValue(mut(query), kSecReturnData, kCFBooleanTrue);
  CFDictionarySetValue(mut(query), kSecMatchLimit, kSecMatchLimitOne);

  CFTypeRef result = nullptr;
  if (SecItemCopyMatching(mut(query), &result) != errSecSuccess || !result) {
    return {};
  }

  auto data = make_cf(result);
  auto* cf_data = static_cast<CFDataRef>(result);
  return {reinterpret_cast<const char*>(CFDataGetBytePtr(cf_data)), static_cast<size_t>(CFDataGetLength(cf_data))};
}

void PasswordStore::store(const std::string& server, const std::string& username, const std::string& password) {
  auto query = make_query(server, username);

  std::ignore = SecItemDelete(mut(query));

  auto pw_data = make_cf(CFDataCreate(nullptr, reinterpret_cast<const UInt8*>(password.data()), password.size()));
  CFDictionarySetValue(mut(query), kSecValueData, pw_data.get());

  auto access = make_permissive_access();
  if (access) {
    CFDictionarySetValue(mut(query), kSecAttrAccess, access.get());
  }

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

SecretCollection* get_default_collection(SecretService* service) {
  GError* err = nullptr;
  SecretCollection* collection =
      secret_collection_for_alias_sync(service, SECRET_COLLECTION_DEFAULT, SECRET_COLLECTION_NONE, nullptr, &err);
  if (err) {
    g_error_free(err);
  }
  return collection;
}

void unlock_collection(SecretService* service, SecretCollection* collection) {
  if (!secret_collection_get_locked(collection)) {
    return;
  }
  GList* objects = g_list_prepend(nullptr, collection);
  GError* err = nullptr;
  secret_service_unlock_sync(service, objects, nullptr, nullptr, &err);
  g_list_free(objects);
  if (err) {
    g_error_free(err);
  }
}

void ensure_default_collection_unlocked() {
  GError* err = nullptr;
  SecretService* service = secret_service_get_sync(SECRET_SERVICE_LOAD_COLLECTIONS, nullptr, &err);
  if (err) {
    g_error_free(err);
  }
  if (!service) {
    return;
  }
  SecretCollection* collection = get_default_collection(service);
  if (collection) {
    unlock_collection(service, collection);
    g_object_unref(collection);
  }
  g_object_unref(service);
}

}  // namespace

std::string PasswordStore::lookup(const std::string& server, const std::string& username) {
  ensure_default_collection_unlocked();
  GError* err = nullptr;
  gchar* pw = secret_password_lookup_sync(
      &kSchema, nullptr, &err, "server", server.c_str(), "username", username.c_str(), nullptr);
  if (err) {
    g_error_free(err);
    return {};
  }
  if (!pw) {
    return {};
  }

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
  if (err) {
    g_error_free(err);
  }
}

void PasswordStore::remove(const std::string& server, const std::string& username) {
  GError* err = nullptr;
  secret_password_clear_sync(&kSchema, nullptr, &err, "server", server.c_str(), "username", username.c_str(), nullptr);
  if (err) {
    g_error_free(err);
  }
}

#endif
