/**
 * mqtt_can/config_manager.hpp — 配置文件管理器
 *
 * 零依赖 key=value 格式解析，支持:
 *   - 注释 (# 开头)
 *   - 点号分层 (broker.address)
 *   - 类型安全取值 (get_string / get_int / get_bool)
 *   - 默认值回退
 */

#ifndef MQTT_CAN_CONFIG_MANAGER_HPP
#define MQTT_CAN_CONFIG_MANAGER_HPP

#include <string>
#include <unordered_map>
#include <optional>

namespace mqtt_can {

class ConfigManager {
public:
    /**
     * 从文件加载配置。
     * @return true=成功
     */
    bool load(const std::string& filepath);

    /** 获取字符串值 */
    std::string get_string(const std::string& key,
                           const std::string& default_val = "") const;

    /** 获取整数值 */
    int get_int(const std::string& key, int default_val = 0) const;

    /** 获取布尔值 (true/1/on/yes 视为 true) */
    bool get_bool(const std::string& key, bool default_val = false) const;

    /** 检查键是否存在 */
    bool has(const std::string& key) const;

    /** 打印所有配置项 (调试用) */
    void print() const;

private:
    std::unordered_map<std::string, std::string> kv_;

    static std::string trim(const std::string& s);
};

} // namespace mqtt_can

#endif // MQTT_CAN_CONFIG_MANAGER_HPP
