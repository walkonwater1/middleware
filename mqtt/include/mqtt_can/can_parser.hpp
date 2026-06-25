/**
 * mqtt_can/can_parser.hpp — CAN 帧解析器
 *
 * 将原始 CAN 帧按信号数据库解析为物理信号，
 * 聚合为统一的 VehicleSignals 快照。
 */

#ifndef MQTT_CAN_PARSER_HPP
#define MQTT_CAN_PARSER_HPP

#include "mqtt_can/can_simulator.hpp"
#include <string>
#include <map>
#include <optional>

namespace mqtt_can {

// ============================================================
// 解析器
// ============================================================

class CanParser {
public:
    explicit CanParser(std::string vehicle_id = "VIN_LX_TEST_001");

    /**
     * 喂入一个 CAN 帧，解析其中的信号并更新缓存。
     * @return 该帧中新解析出的信号数量
     */
    int feed(const CanFrame& frame);

    /** 批量喂入 */
    int feed(const std::vector<CanFrame>& frames);

    /**
     * 获取指定信号的最新值。
     * @return 信号值，若不存在则返回 std::nullopt
     */
    std::optional<float> get(const std::string& signal_name) const;

    /** 获取所有已知信号 */
    std::map<std::string, float> all_signals() const;

    /** 获取车辆 ID */
    const std::string& vehicle_id() const { return vehicle_id_; }

    /** 打印当前所有信号快照 */
    void print_snapshot() const;

    /** 清空信号缓存 */
    void clear();

private:
    std::string vehicle_id_;
    std::map<std::string, float> signals_;
};

} // namespace mqtt_can

#endif // MQTT_CAN_PARSER_HPP
