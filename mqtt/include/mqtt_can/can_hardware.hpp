/**
 * mqtt_can/can_hardware.hpp — SocketCAN 硬件接口
 *
 * 在真实 SocketCAN 设备和软件模拟器之间切换:
 *   - 编译时定义 USE_SOCKETCAN → 读取 vcan0 / can0
 *   - 未定义 → 使用 CanSimulator 生成模拟数据
 */

#ifndef MQTT_CAN_HARDWARE_HPP
#define MQTT_CAN_HARDWARE_HPP

#include "mqtt_can/can_simulator.hpp"
#include <string>
#include <vector>
#include <memory>

namespace mqtt_can {

// ============================================================
// SocketCAN Reader (Linux)
// ============================================================

#ifdef USE_SOCKETCAN

#include <linux/can.h>
#include <linux/can/raw.h>

class SocketCanReader {
public:
    /**
     * @param interface  CAN接口名, 如 "vcan0" / "can0"
     */
    explicit SocketCanReader(const std::string& interface);
    ~SocketCanReader();

    SocketCanReader(const SocketCanReader&) = delete;
    SocketCanReader& operator=(const SocketCanReader&) = delete;

    /** 打开接口 */
    bool open();

    /** 关闭接口 */
    void close();

    /** 读取一帧 (非阻塞, 返回 false 表示暂无数据) */
    bool read(CanFrame& frame, int timeout_ms = 10);

    /** 是否已打开 */
    bool is_open() const { return fd_ >= 0; }

private:
    std::string iface_;
    int         fd_ = -1;
    struct sockaddr_can addr_{};
};

#endif // USE_SOCKETCAN

// ============================================================
// 统一数据源接口 (模拟器 or 真实硬件)
// ============================================================

class CanDataSource {
public:
    virtual ~CanDataSource() = default;

    /** 获取一批 CAN 帧 */
    virtual std::vector<CanFrame> fetch(int max_frames = 8) = 0;

    /** 工厂: 根据编译选项创建 */
    static std::unique_ptr<CanDataSource> create(
        const std::string& socketcan_iface = "vcan0",
        bool cold_start = false
    );
};

} // namespace mqtt_can

#endif // MQTT_CAN_HARDWARE_HPP
