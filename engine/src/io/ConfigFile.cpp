#include "nara/io/ConfigFile.h"

#include <cstdlib>
#include <fstream>

namespace nara {
namespace {

std::string Trim(const std::string& s) {
    size_t a = 0;
    size_t b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r'))
        ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r'))
        --b;
    return s.substr(a, b - a);
}

} // namespace

bool ConfigFile::Load(const std::string& path) {
    std::ifstream file(path);
    if (!file)
        return false;

    std::string line;
    while (std::getline(file, line)) {
        const std::string trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';')
            continue;
        const size_t eq = trimmed.find('=');
        if (eq == std::string::npos)
            continue;
        m_values[Trim(trimmed.substr(0, eq))] = Trim(trimmed.substr(eq + 1));
    }
    return true;
}

bool ConfigFile::Save(const std::string& path) const {
    std::ofstream file(path, std::ios::trunc);
    if (!file)
        return false;
    file << "# Nara config / save data\n";
    for (const auto& [key, value] : m_values)
        file << key << '=' << value << '\n';
    return static_cast<bool>(file);
}

bool ConfigFile::Has(const std::string& key) const {
    return m_values.find(key) != m_values.end();
}

std::string ConfigFile::GetString(const std::string& key, const std::string& fallback) const {
    const auto it = m_values.find(key);
    return it != m_values.end() ? it->second : fallback;
}

int ConfigFile::GetInt(const std::string& key, int fallback) const {
    const auto it = m_values.find(key);
    return it != m_values.end() ? std::atoi(it->second.c_str()) : fallback;
}

float ConfigFile::GetFloat(const std::string& key, float fallback) const {
    const auto it = m_values.find(key);
    return it != m_values.end() ? static_cast<float>(std::atof(it->second.c_str())) : fallback;
}

bool ConfigFile::GetBool(const std::string& key, bool fallback) const {
    const auto it = m_values.find(key);
    if (it == m_values.end())
        return fallback;
    const std::string& v = it->second;
    return v == "1" || v == "true" || v == "True" || v == "yes";
}

void ConfigFile::SetString(const std::string& key, const std::string& value) {
    m_values[key] = value;
}

void ConfigFile::SetInt(const std::string& key, int value) {
    m_values[key] = std::to_string(value);
}

void ConfigFile::SetFloat(const std::string& key, float value) {
    m_values[key] = std::to_string(value);
}

void ConfigFile::SetBool(const std::string& key, bool value) {
    m_values[key] = value ? "1" : "0";
}

} // namespace nara
