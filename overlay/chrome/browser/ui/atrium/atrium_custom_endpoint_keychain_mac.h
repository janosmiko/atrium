// Copyright 2026 The Atrium Authors

#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_CUSTOM_ENDPOINT_KEYCHAIN_MAC_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_CUSTOM_ENDPOINT_KEYCHAIN_MAC_H_

#include <optional>
#include <string>
#include <string_view>

namespace atrium {

// the custom endpoint's API key, kept out of settings.json by
// living here instead. All three calls block on Keychain Services, so run
// them off the UI thread.

// Whether a key is currently stored, without reading its value - all the
// Settings > AI row needs to show a masked "key saved" state.
bool HasCustomEndpointApiKey();

// Stores `api_key`, replacing any key already saved. An empty `api_key`
// deletes the stored item instead, so this one call also covers "no key
// configured". Returns whether the keychain accepted the change.
bool SetCustomEndpointApiKey(std::string_view api_key);

// The plaintext key, for the one caller that actually needs to send it:
// the outbound request. nullopt when nothing is stored or the
// item could not be read - never an empty string standing in for "no key".
std::optional<std::string> GetCustomEndpointApiKey();

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_CUSTOM_ENDPOINT_KEYCHAIN_MAC_H_
