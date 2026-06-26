# ROS2 自定义消息 — 与 DDS IDL (dds/idl/HelloWorld.idl) 结构一致
# 这样 ROS2 节点和原生 DDS 节点可以直接通信, 无需序列化转换

string vehicle_id
float64 speed
float64 latitude
float64 longitude
float64 battery_soc
int32 status          # 0=STOPPED, 1=RUNNING, 2=CHARGING, 3=ERROR
int64 timestamp       # 微秒
