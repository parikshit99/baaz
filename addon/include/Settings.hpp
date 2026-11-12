#pragma once

#include <cstdint>
#include <string>

namespace Baaz {

struct SettingsConfig {
    uint32_t spacebarHoldMs = 350;
    uint32_t maxResults = 6;
    double fuzzThreshold = 60.0;
    bool enableChords = true;
};

// Valid ranges, enforced by Settings::Set and Settings::Load
constexpr uint32_t kHoldMsMin = 150;
constexpr uint32_t kHoldMsMax = 1000;
constexpr uint32_t kMaxResultsMin = 3;
constexpr uint32_t kMaxResultsMax = 15;

// Persisted in %USERPROFILE%\.baaz\config.json
class Settings {
public:
    static void Load();
    static const SettingsConfig& Get() { return s_config; }
    // Out-of-range values keep their current setting; saves to disk
    static void Set(const SettingsConfig& config);
    // %USERPROFILE%\.baaz, or .baaz in the current folder without a user profile
    static std::wstring FolderPath();

private:
    static void Save();
    static SettingsConfig s_config;
};

} // namespace Baaz
