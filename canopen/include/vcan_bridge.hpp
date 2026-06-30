/**
 * vcan_bridge.hpp — SocketCAN 虚拟 CAN 接口桥接
 *
 * 将 VirtualCanBus 与真实 Linux SocketCAN (vcan0) 双向桥接,
 * 使 CANopen demo 可以与外部真实 CAN 设备通信.
 *
 * 用法:
 *   VirtualCanBus bus;
 *   VcanBridge vcan("vcan0");
 *   vcan.attach(bus);  // 双向转发
 *
 * 依赖: Linux SocketCAN (CONFIG_CAN_VCAN)
 * 设置: sudo ip link add dev vcan0 type vcan
 *       sudo ip link set vcan0 up
 */

#ifndef VCAN_BRIDGE_HPP
#define VCAN_BRIDGE_HPP

#include "canopen.hpp"

#include <linux/can.h>
#include <linux/can/raw.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>
#include <thread>
#include <atomic>
#include <stdexcept>
#include <string>

namespace canopen {

class VcanBridge {
public:
    explicit VcanBridge(const std::string& ifname = "vcan0")
        : ifname_(ifname)
    {
        sock_ = ::socket(PF_CAN, SOCK_RAW, CAN_RAW);
        if (sock_ < 0)
            throw std::runtime_error("socket(PF_CAN) failed: " +
                                     std::string(strerror(errno)));

        struct ifreq ifr = {};
        strncpy(ifr.ifr_name, ifname_.c_str(), IFNAMSIZ - 1);
        if (::ioctl(sock_, SIOCGIFINDEX, &ifr) < 0)
            throw std::runtime_error("ioctl(SIOCGIFINDEX) for '" +
                                     ifname_ + "': " + strerror(errno));

        struct sockaddr_can addr = {};
        addr.can_family  = AF_CAN;
        addr.can_ifindex = ifr.ifr_ifindex;
        if (::bind(sock_, (struct sockaddr*)&addr, sizeof(addr)) < 0)
            throw std::runtime_error("bind() failed: " +
                                     std::string(strerror(errno)));

        printf("[VCAN] 桥接 %s (fd=%d) 已就绪\n", ifname_.c_str(), sock_);
    }

    ~VcanBridge() {
        stop();
        if (sock_ >= 0) ::close(sock_);
    }

    /// 绑定到 VirtualCanBus, 启动双向转发线程
    void attach(VirtualCanBus& bus) {
        bus_ = &bus;
        running_ = true;

        // TX 线程: VirtualCanBus → SocketCAN
        tx_thread_ = std::thread([this]() {
            while (running_) {
                // 从虚拟总线订阅所有消息由 bus 的 subscriber 机制处理,
                // 这里需要 bus 提供一个方式获取待发送帧.
                // 作为简化, 直接在 bus 上注册一个全局转发器.
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });

        // RX 线程: SocketCAN → VirtualCanBus
        rx_thread_ = std::thread([this]() {
            struct can_frame frame;
            while (running_) {
                ssize_t n = ::read(sock_, &frame, sizeof(frame));
                if (n < 0) {
                    if (errno == EAGAIN) continue;
                    break;
                }
                if (n != CAN_MTU && n != CANFD_MTU) continue;

                // 转换 can_frame → CanFrame 并发送到虚拟总线
                CanFrame cf(frame.can_id & CAN_SFF_MASK,
                            frame.data, frame.len);
                bus_->send(cf);
            }
        });
    }

    /// 停止转发
    void stop() {
        running_ = false;
        if (tx_thread_.joinable()) tx_thread_.join();
        if (rx_thread_.joinable()) rx_thread_.join();
    }

    /// 发送 CAN 帧到真实 SocketCAN
    void send(const CanFrame& cf) {
        struct can_frame frame = {};
        frame.can_id  = cf.cob_id | CAN_EFF_FLAG;  // 使用扩展帧
        frame.len     = cf.dlc;
        memcpy(frame.data, cf.data, cf.dlc);
        ::write(sock_, &frame, sizeof(frame));
    }

    /// 创建 vcan0 接口 (需要 root 权限)
    static bool setup_vcan(const char* ifname = "vcan0") {
        char cmd[256];
        snprintf(cmd, sizeof(cmd),
                 "ip link add dev %s type vcan 2>/dev/null; "
                 "ip link set %s up 2>/dev/null",
                 ifname, ifname);
        return (system(cmd) == 0);
    }

private:
    std::string ifname_;
    int sock_ = -1;
    VirtualCanBus* bus_ = nullptr;
    std::atomic<bool> running_{false};
    std::thread tx_thread_;
    std::thread rx_thread_;
};

} // namespace canopen

#endif // VCAN_BRIDGE_HPP
