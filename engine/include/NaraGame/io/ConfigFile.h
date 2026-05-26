#pragma once

#include <map>
#include <string>

namespace NaraGame {

// Simple key=value text store for settings and save data. Lines are "key=value";
// blank lines and lines starting with '#' or ';' are ignored. Keys are written
// back in sorted order so saved files are stable/diff-friendly.
class ConfigFile {
public:
    // Returns false if the file is missing (the object is still usable / empty).
    bool Load(const std::string& path);
    bool Save(const std::string& path) const;

    bool Has(const std::string& key) const;

    std::string GetString(const std::string& key, const std::string& fallback = "") const;
    int GetInt(const std::string& key, int fallback = 0) const;
    float GetFloat(const std::string& key, float fallback = 0.0f) const;
    bool GetBool(const std::string& key, bool fallback = false) const;

    void SetString(const std::string& key, const std::string& value);
    void SetInt(const std::string& key, int value);
    void SetFloat(const std::string& key, float value);
    void SetBool(const std::string& key, bool value);

private:
    std::map<std::string, std::string> m_values;
};

} // namespace NaraGame
