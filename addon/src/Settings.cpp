#include "Settings.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace Baaz {

SettingsConfig Settings::s_config;

std::wstring Settings::FolderPath() {
    wchar_t* userProfile = nullptr;
    size_t len = 0;
    std::filesystem::path dir = L".";
    if (_wdupenv_s(&userProfile, &len, L"USERPROFILE") == 0 && userProfile != nullptr) {
        dir = userProfile;
        free(userProfile);
    }
    return (dir / L".baaz").wstring();
}

static std::filesystem::path ConfigPath() {
    return std::filesystem::path(Settings::FolderPath()) / L"config.json";
}

template <typename T>
static void Assign(T value, T min, T max, T& out) {
    if (value >= min && value <= max) out = value;
}

// The file is the flat object Save() writes, one "key": value per line
static void ReadNumber(const std::string& line, const char* key, double min, double max, double& out) {
    size_t keyPos = line.find(std::string("\"") + key + "\"");
    if (keyPos == std::string::npos) return;
    size_t colon = line.find(':', keyPos);
    if (colon == std::string::npos) return;
    char* end = nullptr;
    double value = std::strtod(line.c_str() + colon + 1, &end);
    if (end != line.c_str() + colon + 1) Assign(value, min, max, out);
}

void Settings::Load() {
    std::ifstream file(ConfigPath());
    if (!file.is_open()) return;

    double holdMs = s_config.spacebarHoldMs;
    double maxResults = s_config.maxResults;
    std::string line;
    while (std::getline(file, line)) {
        ReadNumber(line, "spacebar_hold_ms", kHoldMsMin, kHoldMsMax, holdMs);
        ReadNumber(line, "max_results", kMaxResultsMin, kMaxResultsMax, maxResults);
        ReadNumber(line, "fuzz_threshold", 0.0, 100.0, s_config.fuzzThreshold);
        if (line.find("\"enable_chords\"") != std::string::npos) {
            s_config.enableChords = (line.find("false") == std::string::npos);
        }
    }
    s_config.spacebarHoldMs = static_cast<uint32_t>(holdMs);
    s_config.maxResults = static_cast<uint32_t>(maxResults);
}

void Settings::Set(const SettingsConfig& config) {
    Assign(config.spacebarHoldMs, kHoldMsMin, kHoldMsMax, s_config.spacebarHoldMs);
    Assign(config.maxResults, kMaxResultsMin, kMaxResultsMax, s_config.maxResults);
    Assign(config.fuzzThreshold, 0.0, 100.0, s_config.fuzzThreshold);
    s_config.enableChords = config.enableChords;
    Save();
}

void Settings::Save() {
    std::filesystem::path path = ConfigPath();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream file(path, std::ios::trunc);
    if (!file.is_open()) return;

    file << "{\n";
    file << "  \"spacebar_hold_ms\": " << s_config.spacebarHoldMs << ",\n";
    file << "  \"max_results\": " << s_config.maxResults << ",\n";
    file << "  \"fuzz_threshold\": " << s_config.fuzzThreshold << ",\n";
    file << "  \"enable_chords\": " << (s_config.enableChords ? "true" : "false") << "\n";
    file << "}\n";
}

} // namespace Baaz
