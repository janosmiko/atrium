// Copyright 2026 The Atrium Authors

#include "chrome/browser/ui/atrium/atrium_custom_endpoint_keychain_mac.h"

#include <Security/Security.h>

#include "base/apple/foundation_util.h"
#include "base/apple/scoped_cftyperef.h"

namespace atrium {

namespace {

// kSecAttrAccount identifies the field this build's own settings row edits,
// in case another Atrium keychain item is ever added under the same service.
CFStringRef const kService = CFSTR("atrium.ai.customEndpointApiKey");
CFStringRef const kAccount = CFSTR("customEndpointApiKey");

// The query every call starts from - one generic-password item, named by
// service and account, with no data attached yet.
base::apple::ScopedCFTypeRef<CFMutableDictionaryRef> BaseQuery() {
  base::apple::ScopedCFTypeRef<CFMutableDictionaryRef> query(
      CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
                                &kCFTypeDictionaryKeyCallBacks,
                                &kCFTypeDictionaryValueCallBacks));
  CFDictionarySetValue(query.get(), kSecClass, kSecClassGenericPassword);
  CFDictionarySetValue(query.get(), kSecAttrService, kService);
  CFDictionarySetValue(query.get(), kSecAttrAccount, kAccount);
  return query;
}

}  // namespace

bool HasCustomEndpointApiKey() {
  base::apple::ScopedCFTypeRef<CFMutableDictionaryRef> query = BaseQuery();
  // kSecReturnData stays unset - this is an existence check, and the whole
  // point is to answer it without ever bringing the plaintext into memory.
  return SecItemCopyMatching(query.get(), /*result=*/nullptr) == errSecSuccess;
}

bool SetCustomEndpointApiKey(std::string_view api_key) {
  base::apple::ScopedCFTypeRef<CFMutableDictionaryRef> query = BaseQuery();

  if (api_key.empty()) {
    OSStatus status = SecItemDelete(query.get());
    // errSecItemNotFound means the end state (no key stored) already holds.
    return status == errSecSuccess || status == errSecItemNotFound;
  }

  base::apple::ScopedCFTypeRef<CFDataRef> data(CFDataCreate(
      kCFAllocatorDefault, reinterpret_cast<const UInt8*>(api_key.data()),
      static_cast<CFIndex>(api_key.size())));

  base::apple::ScopedCFTypeRef<CFMutableDictionaryRef> attributes_to_set(
      CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
                                &kCFTypeDictionaryKeyCallBacks,
                                &kCFTypeDictionaryValueCallBacks));
  CFDictionarySetValue(attributes_to_set.get(), kSecValueData, data.get());
  // Never synced to iCloud Keychain - a value this sensitive stays on this
  // machine.
  CFDictionarySetValue(attributes_to_set.get(), kSecAttrSynchronizable,
                       kCFBooleanFalse);

  OSStatus update_status =
      SecItemUpdate(query.get(), attributes_to_set.get());
  if (update_status == errSecSuccess) {
    return true;
  }
  if (update_status != errSecItemNotFound) {
    return false;
  }

  // No existing item to update - add one instead. SecItemAdd takes one
  // dictionary rather than a query/attributes pair, so the add dictionary is
  // the query merged with the new attributes.
  base::apple::ScopedCFTypeRef<CFMutableDictionaryRef> add_query(
      CFDictionaryCreateMutableCopy(kCFAllocatorDefault, 0, query.get()));
  CFDictionarySetValue(add_query.get(), kSecValueData, data.get());
  CFDictionarySetValue(add_query.get(), kSecAttrSynchronizable,
                       kCFBooleanFalse);
  // Readable once the user has unlocked the Mac at least once since boot -
  // consistent with an app that itself only runs while logged in.
  CFDictionarySetValue(add_query.get(), kSecAttrAccessible,
                       kSecAttrAccessibleAfterFirstUnlock);

  return SecItemAdd(add_query.get(), /*result=*/nullptr) == errSecSuccess;
}

std::optional<std::string> GetCustomEndpointApiKey() {
  base::apple::ScopedCFTypeRef<CFMutableDictionaryRef> query = BaseQuery();
  CFDictionarySetValue(query.get(), kSecReturnData, kCFBooleanTrue);
  CFTypeRef result = nullptr;
  if (SecItemCopyMatching(query.get(), &result) != errSecSuccess) {
    return std::nullopt;
  }
  base::apple::ScopedCFTypeRef<CFDataRef> data(
      base::apple::CFCast<CFDataRef>(result));
  if (!data) {
    return std::nullopt;
  }
  return std::string(reinterpret_cast<const char*>(CFDataGetBytePtr(data.get())),
                     static_cast<size_t>(CFDataGetLength(data.get())));
}

}  // namespace atrium
