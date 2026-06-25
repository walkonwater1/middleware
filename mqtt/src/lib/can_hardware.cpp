/**
 * can_hardware.cpp — SocketCAN 桥接实现
 */

#include "mqtt_can/can_hardware.hpp"
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>

#ifdef USE_SOCKETCAN
#include <linux/can.h>
#include <linux/can/raw.h>

namespace mqtt_can {

// ============================================================
// SocketCanReader
// ============================================================

SocketCanReader::SocketCanReader(const std::string& interface)
    : iface_(interface)
{}

SocketCanReader::~SocketCanReader()
{
    close();
}

bool SocketCanReader::open()
{
    fd_ = ::socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (fd_ < 0) {
        std::perror("[CAN] socket 创建失败");
        return false;
    }

    // 启用接收自己的帧 (用于回环测试)
    int recv_own_msgs = 1;
    ::setsockopt(fd_, SOL_CAN_RAW, CAN_RAW_RECV_OWN_MSGS,
                 &recv_own_msgs, sizeof(recv_own_msgs));

    // 绑定接口
    struct ifreq ifr{};
    std::strncpy(ifr.ifr_name, iface_.c_str(), IFNAMSIZ - 1);
    if (::ioctl(fd_, SIOCGIFINDEX, &ifr) < 0) {
        std::perror("[CAN] ioctl 获取接口索引失败");
        close();
        return false;
    }

    addr_.can_family  = AF_CAN;
    addr_.can_ifindex = ifr.ifr_ifindex;

    if (::bind(fd_, reinterpret_cast<struct sockaddr*>(&addr_),
               sizeof(addr_)) < 0) {
        std::perror("[CAN] bind 失败");
        close();
        return false;
    }

    std::printf("[CAN] 已打开 SocketCAN 接口: %s\n", iface_.c_str());
    return true;
}

void SocketCanReader::close()
{
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
        std::printf("[CAN] 已关闭接口: %s\n", iface_.c_str());
    }
}

bool SocketCanReader::read(CanFrame& frame, int timeout_ms)
{
    if (fd_ < 0) return false;

    // 设置超时
    struct timeval tv{};
    tv.tv_sec  = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(fd_, &readfds);

    int ret = ::select(fd_ + 1, &readfds, nullptr, nullptr, &tv);
    if (ret <= 0) return false;

    struct can_frame can_f{};
    ssize_t n = ::read(fd_, &can_f, sizeof(can_f));
    if (n < 0) return false;

    frame.can_id       = can_f.can_id & CAN_SFF_MASK;  // 11-bit ID
    frame.dlc          = can_f.len;
    frame.timestamp_ms = 0;  // SocketCAN 不提供时间戳, 如果需要可用 cmsg
    std::memcpy(frame.data.data(), can_f.data, std::min(size_t(can_f.len), frame.data.size()));

    return true;
}

} // namespace mqtt_can

#endif // USE_SOCKETCAN

// ============================================================
// CanDataSource 工厂
// ============================================================

namespace mqtt_can {

// ---- 模拟器适配器 ----
class SimulatorSource : public CanDataSource {
public:
    explicit SimulatorSource(bool cold_start) : sim_(cold_start) {}
    std::vector<CanFrame> fetch(int max_frames) override {
        return sim_.tick(max_frames);
    }
private:
    CanSimulator sim_;
};

#ifdef USE_SOCKETCAN
// ---- SocketCAN 适配器 ----
class SocketCanSource : public CanDataSource {
public:
    explicit SocketCanSource(const std::string& iface, bool /*cold_start*/)
        : reader_(iface) { reader_.open(); }
    std::vector<CanFrame> fetch(int max_frames) override {
        std::vector<CanFrame> frames;
        CanFrame f;
        while (frames.size() < static_cast<size_t>(max_frames) && reader_.read(f, 1))
            frames.push_back(f);
        return frames;
    }
private:
    SocketCanReader reader_;
};
#endif

std::unique_ptr<CanDataSource> CanDataSource::create(
    const std::string& socketcan_iface, bool cold_start)
{
#ifdef USE_SOCKETCAN
    std::printf("[CAN] 数据源: SocketCAN (%s)\n", socketcan_iface.c_str());
    return std::make_unique<SocketCanSource>(socketcan_iface, cold_start);
#else
    (void)socketcan_iface;
    std::printf("[CAN] 数据源: 模拟器\n");
    return std::make_unique<SimulatorSource>(cold_start);
#endif
}

} // namespace mqtt_can
