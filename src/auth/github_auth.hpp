#pragma once

#include <string>
#include <functional>

namespace acecode {

struct DeviceCodeResponse {
    std::string device_code;
    std::string user_code;
    std::string verification_uri;
    int interval = 5; // polling interval in seconds
    int expires_in = 900;
};

struct CopilotToken {
    std::string token;
    int64_t expires_at = 0; // unix timestamp
};

// Step 1: Request a device code for GitHub OAuth Device Flow
DeviceCodeResponse request_device_code();

// Step 2: Poll for access token. Returns the GitHub OAuth token (gho_...) on success.
// Calls status_callback periodically with a status string for UI updates.
// Returns empty string on failure/expiry.
std::string poll_for_access_token(
    const std::string& device_code,
    int interval,
    int expires_in,
    std::function<void(const std::string&)> status_callback = nullptr
);

// Step 3: Exchange GitHub OAuth token for Copilot session token
CopilotToken exchange_copilot_token(const std::string& github_token);

// Persistence: save/load GitHub OAuth token from ~/.acecode/github_token
void save_github_token(const std::string& token);
std::string load_github_token();

} // namespace acecode
