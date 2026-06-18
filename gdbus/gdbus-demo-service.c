/*
 * GDBus 服务端 Demo — 车辆信息服务
 *
 * 总线名:  com.example.VehicleService
 * 对象路径: /com/example/Vehicle
 * 接口名:  com.example.Vehicle
 *
 * 提供:
 *   - 方法: GetVehicleInfo() -> (s, d, d)
 *   - 信号: SpeedChanged(d new_speed)
 *   - 属性: Speed (可读, d)
 */

#include <gio/gio.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* ============================================================
 * XML 自省数据 — 定义接口
 * ============================================================ */
static const gchar introspection_xml[] =
    "<node>"
    "  <interface name='com.example.Vehicle'>"
    "    <!-- 方法: 获取车辆信息 -->"
    "    <method name='GetVehicleInfo'>"
    "      <arg type='s' name='vehicle_id' direction='out'/>"
    "      <arg type='d' name='speed'     direction='out'/>"
    "      <arg type='d' name='odometer'  direction='out'/>"
    "    </method>"
    "    <!-- 信号: 速度变化 -->"
    "    <signal name='SpeedChanged'>"
    "      <arg type='d' name='new_speed'/>"
    "    </signal>"
    "    <!-- 属性: 当前速度 -->"
    "    <property name='Speed' type='d' access='read'/>"
    "  </interface>"
    "</node>";

/* ============================================================
 * 车辆状态
 * ============================================================ */
static GDBusConnection *connection = NULL;
static guint speed_timer_id = 0;
static gdouble current_speed = 0.0;
static gdouble odometer = 12345.6;

/* ============================================================
 * 方法调用处理
 * ============================================================ */
static void
handle_method_call(GDBusConnection       *conn,
                   const gchar           *sender,
                   const gchar           *object_path,
                   const gchar           *interface_name,
                   const gchar           *method_name,
                   GVariant              *parameters,
                   GDBusMethodInvocation *invocation,
                   gpointer               user_data)
{
    (void)conn;
    (void)sender;
    (void)object_path;
    (void)interface_name;
    (void)parameters;
    (void)user_data;

    if (g_strcmp0(method_name, "GetVehicleInfo") == 0) {
        g_print("[SERVICE] 收到 GetVehicleInfo 请求 (来自 %s)\n", sender);

        GVariant *result = g_variant_new("(sdd)",
                                         "VIN-ABC-123456789",
                                         current_speed,
                                         odometer);

        g_dbus_method_invocation_return_value(invocation, result);
        g_print("[SERVICE] 返回: id=VIN-ABC-123456789, speed=%.1f, odo=%.1f\n",
                current_speed, odometer);
    } else {
        g_print("[SERVICE] 未知方法: %s\n", method_name);
        g_dbus_method_invocation_return_error(invocation,
                                              G_DBUS_ERROR,
                                              G_DBUS_ERROR_UNKNOWN_METHOD,
                                              "Unknown method: %s", method_name);
    }
}

/* ============================================================
 * 属性读取处理
 * ============================================================ */
static GVariant *
handle_get_property(GDBusConnection *conn,
                    const gchar     *sender,
                    const gchar     *object_path,
                    const gchar     *interface_name,
                    const gchar     *property_name,
                    GError         **error,
                    gpointer         user_data)
{
    (void)conn;
    (void)sender;
    (void)object_path;
    (void)interface_name;
    (void)user_data;
    (void)error;

    if (g_strcmp0(property_name, "Speed") == 0) {
        g_print("[SERVICE] 属性读取: Speed = %.1f\n", current_speed);
        return g_variant_new_double(current_speed);
    }

    g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY,
                "Unknown property: %s", property_name);
    return NULL;
}

/* ============================================================
 * 接口虚表
 * ============================================================ */
static const GDBusInterfaceVTable interface_vtable = {
    .method_call  = handle_method_call,
    .get_property = handle_get_property,
    .set_property = NULL,
};

/* ============================================================
 * 定时器: 模拟速度变化, 发出 SpeedChanged 信号
 * ============================================================ */
static gboolean
on_speed_timer(gpointer user_data)
{
    (void)user_data;

    /* 模拟速度在小范围内波动 */
    gdouble delta = (g_random_double() - 0.5) * 10.0;  // -5 ~ +5
    current_speed += delta;
    if (current_speed < 0.0)  current_speed = 0.0;
    if (current_speed > 120.0) current_speed = 120.0;

    odometer += current_speed / 3600.0 * 2.0;  // 2秒内的里程增量

    g_print("[SERVICE] 发出信号: SpeedChanged(%.1f km/h)\n", current_speed);

    GError *error = NULL;
    g_dbus_connection_emit_signal(connection,
                                  NULL,                        /* destination (广播) */
                                  "/com/example/Vehicle",      /* object_path */
                                  "com.example.Vehicle",       /* interface_name */
                                  "SpeedChanged",              /* signal_name */
                                  g_variant_new("(d)", current_speed),
                                  &error);

    if (error) {
        g_print("[SERVICE] 信号发送失败: %s\n", error->message);
        g_error_free(error);
    }

    /* 同时发出属性变更通知 */
    GVariantBuilder *invalidated_builder = g_variant_builder_new(G_VARIANT_TYPE("as"));
    GVariantBuilder *changed_builder = g_variant_builder_new(G_VARIANT_TYPE_ARRAY(
        G_VARIANT_TYPE("{sv}")));

    g_variant_builder_add(changed_builder, "{sv}",
                          "Speed", g_variant_new_double(current_speed));

    g_dbus_connection_emit_signal(connection,
                                  NULL,
                                  "/com/example/Vehicle",
                                  "org.freedesktop.DBus.Properties",
                                  "PropertiesChanged",
                                  g_variant_new("(sa{sv}as)",
                                                "com.example.Vehicle",
                                                changed_builder,
                                                invalidated_builder),
                                  &error);

    g_variant_builder_unset(invalidated_builder);
    g_variant_builder_unset(changed_builder);

    return G_SOURCE_CONTINUE;  /* 继续定时器 */
}

/* ============================================================
 * 总线回调
 * ============================================================ */
static void
on_bus_acquired(GDBusConnection *conn,
                const gchar     *name,
                gpointer         user_data)
{
    (void)name;
    (void)user_data;

    g_print("[SERVICE] 已获取 D-Bus 连接\n");
    connection = conn;

    /* 注册对象 */
    guint registration_id = g_dbus_connection_register_object(
        conn,
        "/com/example/Vehicle",
        g_dbus_node_info_new_for_xml(introspection_xml, NULL)->interfaces[0],
        &interface_vtable,
        NULL,    /* user_data */
        NULL,    /* user_data_free_func */
        NULL);   /* GError */

    g_print("[SERVICE] 已注册对象 /com/example/Vehicle (reg_id=%u)\n", registration_id);

    /* 启动速度模拟定时器 (每2秒) */
    speed_timer_id = g_timeout_add_seconds(2, on_speed_timer, NULL);
    g_print("[SERVICE] 已启动速度模拟 (2s 间隔)\n");
}

static void
on_name_acquired(GDBusConnection *conn,
                 const gchar     *name,
                 gpointer         user_data)
{
    (void)conn;
    (void)user_data;
    g_print("[SERVICE] 已获取总线名: %s\n", name);
}

static void
on_name_lost(GDBusConnection *conn,
             const gchar     *name,
             gpointer         user_data)
{
    (void)conn;
    (void)user_data;
    g_print("[SERVICE] 总线名丢失: %s\n", name);
    if (speed_timer_id > 0) {
        g_source_remove(speed_timer_id);
    }
    exit(1);
}

/* ============================================================
 * Main
 * ============================================================ */
int
main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    g_print("=== GDBus 车辆信息服务 ===\n");
    g_print("总线名:  com.example.VehicleService\n");
    g_print("对象路径: /com/example/Vehicle\n\n");

    /* 获取总线名 */
    guint owner_id = g_bus_own_name(G_BUS_TYPE_SESSION,
                                    "com.example.VehicleService",
                                    G_BUS_NAME_OWNER_FLAGS_NONE,
                                    on_bus_acquired,
                                    on_name_acquired,
                                    on_name_lost,
                                    NULL,
                                    NULL);

    g_print("服务已启动, 按 Ctrl+C 退出\n\n");

    /* 运行主循环 */
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);

    /* 清理 */
    g_bus_unown_name(owner_id);
    g_main_loop_unref(loop);

    return 0;
}
