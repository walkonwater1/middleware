/**
 * mqtt_can/report_scheduler.hpp — 国标/企标数据上报调度器
 *
 * 支持两种上报策略:
 *   1. 周期上报: 按固定间隔上报 (如 GB/T 32960 要求 30s)
 *   2. 变化上报: 信号值变化超过阈值时立即上报
 *
 * 典型车载场景:
 *   周期信号 — 车速、里程、SOC (GB/T 32960 要求 ≤30s)
 *   变化信号 — 故障码、急加速/急减速、碰撞告警
 */

#ifndef MQTT_CAN_REPORT_SCHEDULER_HPP
#define MQTT_CAN_REPORT_SCHEDULER_HPP

#include "mqtt_can/can_parser.hpp"
#include <string>
#include <vector>
#include <functional>
#include <chrono>
#include <map>
#include <optional>

namespace mqtt_can {

// ============================================================
// 上报条目定义
// ============================================================

enum class ReportPolicy {
    Periodic,       // 周期上报
    OnChange,       // 变化上报
    PeriodicAndOnChange, // 周期 + 变化混合
};

struct ReportItem {
    std::string  signal_name;       // 信号名 (对应 DBC)
    std::string  gb_key;            // 国标/企标 JSON 字段名, 如 "vehicle_speed"
    std::string  unit;              // 单位, 如 "km/h"
    ReportPolicy policy;
    int          interval_sec  = 30;    // 周期上报间隔 (秒)
    float        change_threshold = 0;  // 变化阈值 (0 表示有变化即报)
    float        precision     = 1;     // 精度 (四舍五入到小数点)
};

// ============================================================
// 上报调度器
// ============================================================

class ReportScheduler {
public:
    /** 上报回调: (json_payload, topic) */
    using PublishFunc = std::function<void(const std::string& json, const std::string& topic)>;

    /**
     * @param items      上报条目列表
     * @param vehicle_id 车辆标识
     */
    ReportScheduler(std::vector<ReportItem> items, std::string vehicle_id);

    /**
     * 每帧调用: 从解析器获取最新信号值，按策略判断是否需要上报。
     * @param parser   信号解析器 (最新值缓存)
     * @param publish  发布回调
     * @return 本次上报的条目数
     */
    int tick(const CanParser& parser, const PublishFunc& publish);

    /** 立即强制上报所有信号 */
    void force_report_all(const CanParser& parser, const PublishFunc& publish);

    /** 切换整车状态 (充电/行驶/熄火), 影响上报策略 */
    void set_vehicle_status(const std::string& status);

private:
    struct ItemState {
        float    last_value    = 0;
        bool     has_last      = false;
        std::chrono::steady_clock::time_point last_periodic;
    };

    std::string build_json(const CanParser& parser,
                           const std::string& topic_hint) const;

    std::vector<ReportItem> items_;
    std::string vehicle_id_;
    std::map<std::string, ItemState> states_;
    std::string vehicle_status_ = "running";  // charging / running / parked
};

// ============================================================
// 国标 GB/T 32960 预定义信号列表
// ============================================================

/** 返回 GB/T 32960-2016 要求的周期上报信号 */
std::vector<ReportItem> gb32960_signals();

/** 返回常见企标扩展信号 (变化上报) */
std::vector<ReportItem> enterprise_signals();

} // namespace mqtt_can

#endif // MQTT_CAN_REPORT_SCHEDULER_HPP
