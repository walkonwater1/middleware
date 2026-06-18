/*
 * GDBus 客户端 Demo — 调用车辆信息服务
 *
 * - 调用 GetVehicleInfo 方法
 * - 监听 SpeedChanged 信号
 * - 读取 Speed 属性
 */

#include <gio/gio.h>
#include <stdio.h>
#include <stdlib.h>

static GMainLoop *loop = NULL;

/* ============================================================
 * SpeedChanged 信号回调
 * ============================================================ */
static void
on_speed_changed(GDBusConnection *conn,
                 const gchar     *sender_name,
                 const gchar     *object_path,
                 const gchar     *interface_name,
                 const gchar     *signal_name,
                 GVariant        *parameters,
                 gpointer         user_data)
{
    (void)conn;
    (void)sender_name;
    (void)object_path;
    (void)interface_name;
    (void)signal_name;
    (void)user_data;

    gdouble new_speed = 0.0;
    g_variant_get(parameters, "(d)", &new_speed);
    g_print("[CLIENT] <<< 收到信号 SpeedChanged: %.1f km/h\n", new_speed);
}

/* ============================================================
 * GetVehicleInfo 方法调用回调
 * ============================================================ */
static void
on_get_vehicle_info_done(GObject      *source_object,
                         GAsyncResult *res,
                         gpointer      user_data)
{
    (void)user_data;

    GError *error = NULL;
    GVariant *result = g_dbus_connection_call_finish(
        G_DBUS_CONNECTION(source_object), res, &error);

    if (error) {
        g_print("[CLIENT] GetVehicleInfo 调用失败: %s\n", error->message);
        g_error_free(error);
        return;
    }

    gchar *vehicle_id;
    gdouble speed, odometer;

    g_variant_get(result, "(sdd)", &vehicle_id, &speed, &odometer);

    g_print("[CLIENT] GetVehicleInfo 结果:\n");
    g_print("  Vehicle ID: %s\n", vehicle_id);
    g_print("  Speed:      %.1f km/h\n", speed);
    g_print("  Odometer:   %.1f km\n", odometer);

    g_free(vehicle_id);
    g_variant_unref(result);
}

/* ============================================================
 * 读取属性回调
 * ============================================================ */
static void
on_property_read_done(GObject      *source_object,
                      GAsyncResult *res,
                      gpointer      user_data)
{
    (void)user_data;

    GError *error = NULL;
    GVariant *result = g_dbus_connection_call_finish(
        G_DBUS_CONNECTION(source_object), res, &error);

    if (error) {
        g_print("[CLIENT] 属性读取失败: %s\n", error->message);
        g_error_free(error);
        return;
    }

    GVariant *prop_value = NULL;
    g_variant_get(result, "(v)", &prop_value);

    if (g_variant_is_of_type(prop_value, G_VARIANT_TYPE_DOUBLE)) {
        gdouble speed = g_variant_get_double(prop_value);
        g_print("[CLIENT] 属性 Speed = %.1f km/h\n", speed);
    }

    g_variant_unref(prop_value);
    g_variant_unref(result);
}

/* ============================================================
 * 连接就绪后执行调用
 * ============================================================ */
static void
on_connection_ready(GObject      *source_object,
                    GAsyncResult *res,
                    gpointer      user_data)
{
    (void)user_data;

    GError *error = NULL;
    GDBusConnection *conn = g_dbus_connection_new_for_address_finish(res, &error);

    if (error) {
        g_print("[CLIENT] D-Bus 连接失败: %s\n", error->message);
        g_error_free(error);
        g_main_loop_quit(loop);
        return;
    }

    g_print("[CLIENT] D-Bus 连接成功\n");

    /* 1. 订阅 SpeedChanged 信号 */
    g_dbus_connection_signal_subscribe(conn,
                                       "com.example.VehicleService",  /* sender */
                                       "com.example.Vehicle",         /* interface */
                                       "SpeedChanged",                /* signal */
                                       "/com/example/Vehicle",        /* object_path */
                                       NULL,                          /* arg0 */
                                       G_DBUS_SIGNAL_FLAGS_NONE,
                                       on_speed_changed,
                                       NULL,
                                       NULL);

    g_print("[CLIENT] 已订阅 SpeedChanged 信号\n");

    /* 2. 调用 GetVehicleInfo 方法 */
    g_print("[CLIENT] 调用 GetVehicleInfo...\n");
    g_dbus_connection_call(conn,
                           "com.example.VehicleService",  /* bus_name */
                           "/com/example/Vehicle",        /* object_path */
                           "com.example.Vehicle",         /* interface_name */
                           "GetVehicleInfo",              /* method_name */
                           NULL,                          /* parameters */
                           G_VARIANT_TYPE("(sdd)"),       /* reply_type */
                           G_DBUS_CALL_FLAGS_NONE,
                           -1,                            /* timeout (default) */
                           NULL,                          /* cancellable */
                           on_get_vehicle_info_done,
                           NULL);

    /* 3. 读取 Speed 属性 (通过 Properties 接口) */
    g_dbus_connection_call(conn,
                           "com.example.VehicleService",
                           "/com/example/Vehicle",
                           "org.freedesktop.DBus.Properties",
                           "Get",
                           g_variant_new("(ss)", "com.example.Vehicle", "Speed"),
                           G_VARIANT_TYPE("(v)"),
                           G_DBUS_CALL_FLAGS_NONE,
                           -1,
                           NULL,
                           on_property_read_done,
                           NULL);

    /* 4. 每5秒再次查询属性, 演示持续交互 */
    g_timeout_add_seconds(5, (GSourceFunc)on_property_read_done, NULL);
}

/* ============================================================
 * Main
 * ============================================================ */
int
main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    g_print("=== GDBus 车辆信息客户端 ===\n\n");

    loop = g_main_loop_new(NULL, FALSE);

    /* 连接 Session Bus */
    g_dbus_connection_new_for_address(
        g_getenv("DBUS_SESSION_BUS_ADDRESS"),
        G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
        G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,
        NULL,    /* GDBusAuthObserver */
        NULL,    /* cancellable */
        on_connection_ready,
        NULL);

    g_print("正在连接 D-Bus session bus...\n");
    g_print("按 Ctrl+C 退出\n\n");

    g_main_loop_run(loop);

    g_main_loop_unref(loop);
    return 0;
}
