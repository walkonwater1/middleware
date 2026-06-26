# VehicleState.msg — ROS2 自定义消息
# 对应 DDS: dds/idl/VehicleState.idl
#
# DDS IDL → ROS2 .msg:
#   string<32> vehicle_id → string vehicle_id
#   double speed          → float64 speed
#   int64_t timestamp     → int64 timestamp

string  vehicle_id
float64 speed
float64 latitude
float64 longitude
float64 battery_soc
int64   timestamp
