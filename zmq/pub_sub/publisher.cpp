/**
 * ZeroMQ PUB/SUB 模式 — 发布者 (C++)
 *
 * 模拟机器人传感器数据广播: 关节角度、IMU、电池电压
 * 发布主题: robot/joints, robot/imu, robot/battery
 */

#include <zmq.hpp>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>
#include <cmath>
#include <cstdlib>
#include <thread>
#include <chrono>
#include <vector>
#include <random>

/* ============================================================
 * 配置
 * ============================================================ */
constexpr const char *ENDPOINT = "tcp://*:5556";

/* ============================================================
 * 数据生成器
 * ============================================================ */
class SensorSimulator
{
public:
    SensorSimulator()
        : gen_(rd_()), dist_small_(-1.0, 1.0),
          dist_speed_noise_(-1.0, 1.0)
    {
    }

    std::string generate_joint_angles()
    {
        auto now = std::chrono::system_clock::now().time_since_epoch();
        double t = std::chrono::duration<double>(now).count();

        std::ostringstream oss;
        oss << "{";
        for (int i = 1; i <= 6; ++i) {
            double angle = 30.0 * std::sin(t + i) + dist_small_(gen_);
            oss << "\"joint_" << i << "\":" << std::fixed << std::setprecision(2) << angle;
            if (i < 6) oss << ",";
        }
        oss << "}";
        return oss.str();
    }

    std::string generate_imu()
    {
        std::ostringstream oss;
        oss << "{\"accel\":{"
            << "\"x\":" << std::setprecision(4) << dist_small_(gen_) * 0.1 << ","
            << "\"y\":" << std::setprecision(4) << dist_small_(gen_) * 0.1 << ","
            << "\"z\":" << std::setprecision(4) << (9.8 + dist_small_(gen_) * 0.05)
            << "},"
            << "\"gyro\":{"
            << "\"x\":" << std::setprecision(4) << dist_small_(gen_) * 0.01 << ","
            << "\"y\":" << std::setprecision(4) << dist_small_(gen_) * 0.01 << ","
            << "\"z\":" << std::setprecision(4) << dist_small_(gen_) * 0.01
            << "}}";
        return oss.str();
    }

    std::string generate_battery()
    {
        std::ostringstream oss;
        oss << "{\"voltage\":" << std::fixed << std::setprecision(2)
            << (24.0 + dist_small_(gen_) * 0.5)
            << ",\"current\":" << (0.5 + std::abs(dist_small_(gen_)) * 5.0)
            << ",\"soc\":" << (85.0 + dist_small_(gen_) * 2.0)
            << ",\"temp\":" << (35.0 + dist_small_(gen_) * 3.0)
            << "}";
        return oss.str();
    }

private:
    std::random_device rd_;
    std::mt19937 gen_;
    std::uniform_real_distribution<double> dist_small_;
    std::uniform_real_distribution<double> dist_speed_noise_;
};

/* ============================================================
 * 发布者
 * ============================================================ */
int main()
{
    zmq::context_t context(1);
    zmq::socket_t  socket(context, ZMQ_PUB);  /* PUB = 发布端 */

    socket.bind(ENDPOINT);
    std::cout << "[PUB] 机器人数据发布者已启动, 绑定 " << ENDPOINT << std::endl;
    std::cout << "发布主题: robot/joints, robot/imu, robot/battery" << std::endl;

    /* 给订阅者建立连接的时间 (PUB/SUB "慢加入" 问题) */
    std::this_thread::sleep_for(std::chrono::seconds(1));

    SensorSimulator sim;

    try {
        while (true) {
            /* 发布关节角度 */
            std::string joints = sim.generate_joint_angles();
            socket.send(zmq::buffer("robot/joints"), zmq::send_flags::sndmore);
            socket.send(zmq::buffer(joints), zmq::send_flags::none);
            std::cout << "[PUB] robot/joints: " << joints << std::endl;

            /* 发布IMU */
            std::string imu = sim.generate_imu();
            socket.send(zmq::buffer("robot/imu"), zmq::send_flags::sndmore);
            socket.send(zmq::buffer(imu), zmq::send_flags::none);
            std::cout << "[PUB] robot/imu: " << imu << std::endl;

            /* 发布电池 */
            std::string battery = sim.generate_battery();
            socket.send(zmq::buffer("robot/battery"), zmq::send_flags::sndmore);
            socket.send(zmq::buffer(battery), zmq::send_flags::none);
            std::cout << "[PUB] robot/battery: " << battery << std::endl;

            std::cout << "---" << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    catch (const zmq::error_t &e) {
        std::cerr << "[ERR] ZMQ 异常: " << e.what() << std::endl;
    }

    socket.close();
    context.shutdown();
    return 0;
}
