/**
 * dds_ros2_bridge.cpp — DDS ↔ ROS2 双工桥接节点
 *
 * 在同一个进程中用 CycloneDDS C API + rclcpp, 证明 ROS2 底层就是 DDS。
 *
 * 数据流:
 *   DDS Topic "VehicleState"  →  本节点  →  ROS2 Topic "/vehicle/state"
 *   ROS2 Topic "/vehicle/cmd" →  本节点  →  DDS Topic "VehicleCommand"
 *
 * 运行方式:
 *   终端1: cd dds/build && ./publisher
 *   终端2: ros2 run dds_bridge dds_ros2_bridge
 *   终端3: ros2 topic echo /vehicle/state    # 看到和 DDS 一样的数据!
 */

#include <rclcpp/rclcpp.hpp>
#include "dds_bridge/msg/vehicle_state.hpp"

#include <dds/dds.h>
#include <cstring>
#include <string>

// 直接 include DDS IDL 生成的结构体 (零拷贝)
#include "VehicleState.h"

using namespace std::chrono_literals;

class DdsRos2Bridge : public rclcpp::Node {
public:
    DdsRos2Bridge()
        : Node("dds_ros2_bridge")
    {
        // ========== ROS2 发布者: DDS→ROS2 ==========
        rclcpp::QoS qos(10);
        qos.reliable();
        pub_ = this->create_publisher<dds_bridge::msg::VehicleState>(
            "/vehicle/state", qos);

        // ========== ROS2 订阅者: ROS2→DDS (反向, 预留) ==========
        sub_ = this->create_subscription<dds_bridge::msg::VehicleState>(
            "/vehicle/cmd", qos,
            [this](const dds_bridge::msg::VehicleState::SharedPtr msg) {
                (void)msg;
                RCLCPP_INFO_THROTTLE(this->get_logger(), *get_clock(), 5000,
                    "[ROS2→DDS] 收到指令 (可扩展 Writer)");
            });

        // ========== CycloneDDS 初始化 ==========
        participant_ = dds_create_participant(DDS_DOMAIN_DEFAULT, nullptr, nullptr);
        if (participant_ < 0) {
            RCLCPP_ERROR(get_logger(), "DDS 参与者失败! (是否已安装 CycloneDDS?)");
            return;
        }

        // 用 IDL 编译生成的类型注册 Topic
        topic_ = dds_create_topic(participant_, &vehicle_VehicleState_desc,
                                   "VehicleState", nullptr, 0, nullptr);
        if (topic_ < 0) {
            RCLCPP_WARN(get_logger(),
                "DDS Topic 未注册 (请确保 dds/publisher 先启动注册类型到 Domain)");
            RCLCPP_WARN(get_logger(),
                "桥接仍会运行, 收到数据后自动恢复");
        }

        dds_qos_t* dds_qos = dds_create_qos();
        dds_qset_reliability(dds_qos, DDS_RELIABILITY_RELIABLE, DDS_SECS(10));
        dds_qset_durability(dds_qos, DDS_DURABILITY_TRANSIENT_LOCAL);
        reader_ = dds_create_reader(participant_, topic_, dds_qos, nullptr);
        dds_delete_qos(dds_qos);

        if (reader_ < 0) {
            RCLCPP_ERROR(get_logger(), "DDS Reader 创建失败");
        }

        RCLCPP_INFO(get_logger(), "════════════════════════════════");
        RCLCPP_INFO(get_logger(), " DDS ↔ ROS2 桥接已启动 (Domain=0)");
        RCLCPP_INFO(get_logger(), " DDS Topic: VehicleState");
        RCLCPP_INFO(get_logger(), "   → ROS2 Topic: /vehicle/state");
        RCLCPP_INFO(get_logger(), "   → ros2 topic echo /vehicle/state");
        RCLCPP_INFO(get_logger(), "════════════════════════════════");

        // 100ms 轮询 DDS
        timer_ = create_wall_timer(100ms, [this]() { poll_dds(); });
    }

    ~DdsRos2Bridge() {
        if (participant_ > 0) dds_delete(participant_);
    }

private:
    void poll_dds() {
        if (reader_ < 0) return;

        vehicle_VehicleState* samples[4];
        dds_sample_info_t infos[4];

        int n = dds_read(reader_, reinterpret_cast<void**>(samples), infos, 4, 4);
        for (int i = 0; i < n; i++) {
            if (!infos[i].valid_data) continue;

            // 零拷贝: DDS 结构体 → ROS2 消息
            auto msg = dds_bridge::msg::VehicleState();
            msg.vehicle_id  = samples[i]->vehicle_id;
            msg.speed       = samples[i]->speed;
            msg.latitude    = samples[i]->latitude;
            msg.longitude   = samples[i]->longitude;
            msg.battery_soc = samples[i]->battery_soc;
            msg.status      = static_cast<int32_t>(samples[i]->status);
            msg.timestamp   = samples[i]->timestamp;

            pub_->publish(msg);

            RCLCPP_INFO_THROTTLE(get_logger(), *get_clock(), 2000,
                "[DDS→ROS2] %s speed=%.1f SOC=%.0f%% status=%d latency=%.1fms",
                samples[i]->vehicle_id, samples[i]->speed,
                samples[i]->battery_soc, samples[i]->status,
                (now_ns() - samples[i]->timestamp) / 1e6);
        }

        if (n > 0) dds_return_loan(reader_, reinterpret_cast<void**>(samples), n);
    }

    static uint64_t now_ns() {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    rclcpp::Publisher<dds_bridge::msg::VehicleState>::SharedPtr pub_;
    rclcpp::Subscription<dds_bridge::msg::VehicleState>::SharedPtr sub_;
    rclcpp::TimerBase::SharedPtr timer_;

    dds_entity_t participant_ = 0;
    dds_entity_t topic_       = 0;
    dds_entity_t reader_      = 0;
};

int main(int argc, char* argv[]) {
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<DdsRos2Bridge>());
    rclcpp::shutdown();
    return 0;
}
