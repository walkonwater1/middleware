/**
 * config_manager.cpp — 配置文件管理器实现
 */

#include "mqtt_can/config_manager.hpp"
#include <fstream>
#include <sstream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>

namespace mqtt_can {

std::string ConfigManager::trim(const std::string& s)
{
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

bool ConfigManager::load(const std::string& filepath)
{
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::fprintf(stderr, "[CFG] 无法打开配置文件: %s\n", filepath.c_str());
        return false;
    }

    kv_.clear();
    std::string line;
    int line_no = 0;

    while (std::getline(file, line)) {
        line_no++;
        line = trim(line);

        // 跳过空行和注释
        if (line.empty() || line[0] == '#') continue;

        // 查找 '='
        auto eq_pos = line.find('=');
        if (eq_pos == std::string::npos) {
            std::fprintf(stderr, "[CFG] 第 %d 行格式错误: %s\n", line_no, line.c_str());
            continue;
        }

        std::string key   = trim(line.substr(0, eq_pos));
        std::string value = trim(line.substr(eq_pos + 1));

        if (key.empty()) continue;

        // 去掉 value 两边的引号 (如果有)
        if (value.size() >= 2) {
            char first = value.front();
            char last  = value.back();
            if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
                value = value.substr(1, value.size() - 2);
            }
        }

        kv_[key] = value;
    }

    std::printf("[CFG] 已加载配置: %s (%zu 项)\n", filepath.c_str(), kv_.size());
    return true;
}

std::string ConfigManager::get_string(const std::string& key,
                                      const std::string& default_val) const
{
    auto it = kv_.find(key);
    return (it != kv_.end()) ? it->second : default_val;
}

int ConfigManager::get_int(const std::string& key, int default_val) const
{
    auto it = kv_.find(key);
    if (it == kv_.end()) return default_val;
    return std::strtol(it->second.c_str(), nullptr, 10);
}

bool ConfigManager::get_bool(const std::string& key, bool default_val) const
{
    auto it = kv_.find(key);
    if (it == kv_.end()) return default_val;

    const auto& v = it->second;
    // 转为小写比较
    std::string lower;
    lower.reserve(v.size());
    for (char c : v) lower += static_cast<char>(std::tolower(c));

    if (lower == "true" || lower == "1" || lower == "yes" || lower == "on")
        return true;
    if (lower == "false" || lower == "0" || lower == "no" || lower == "off")
        return false;

    return default_val;
}

bool ConfigManager::has(const std::string& key) const
{
    return kv_.find(key) != kv_.end();
}

void ConfigManager::print() const
{
    if (kv_.empty()) {
        std::printf("[CFG] (空)\n");
        return;
    }
    std::printf("[CFG] 当前配置:\n");
    for (const auto& [k, v] : kv_) {
        std::printf("  %s = %s\n", k.c_str(), v.c_str());
    }
}

} // namespace mqtt_can
