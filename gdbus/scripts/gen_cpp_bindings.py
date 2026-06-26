#!/usr/bin/env python3
"""
gen_cpp_bindings.py — 从 D-Bus 接口 XML 自动生成 C++17 包装类

用法:
    python3 scripts/gen_cpp_bindings.py interfaces/com.example.Robot.xml \\
        --namespace Demo --c-header robot-dbus.h \\
        --output-header generated/robot_bindings.hpp \\
        --output-impl generated/robot_bindings.cpp

当 XML 中增删改查方法/信号/属性时, 重新 cmake 即可自动更新 C++ 包装,
无需手动修改任何 C++ 代码。

类名约定:
  C typedef:  DemoRobotSkeleton / DemoRobotProxy  (来自 gdbus-codegen)
  C++ class:  DemoRobotServer   / DemoRobotClient  (避免与 C typedef 冲突)
"""

import argparse
import re
import sys
from xml.etree import ElementTree


# D-Bus type → C type (for codegen API)
DBUS_TO_C = {
    's': 'const gchar*',
    'd': 'gdouble',
    'i': 'gint32',
    'u': 'guint32',
    'b': 'gboolean',
    't': 'guint64',
    'x': 'gint64',
    'y': 'guchar',
    'o': 'const gchar*',
    'g': 'const gchar*',
}

# D-Bus type → C++ type
DBUS_TO_CXX = {
    's': 'std::string',
    'd': 'double',
    'i': 'int32_t',
    'u': 'uint32_t',
    'b': 'bool',
    't': 'uint64_t',
    'x': 'int64_t',
    'y': 'uint8_t',
    'o': 'std::string',
    'g': 'std::string',
}

# D-Bus type → GVariant format char
DBUS_TO_FORMAT = {
    's': 's',
    'd': 'd',
    'i': 'i',
    'u': 'u',
    'b': 'b',
    't': 't',
    'x': 'x',
    'y': 'y',
    'o': 'o',
    'g': 'g',
}

# D-Bus type → g_variant_new_* function name
DBUS_TO_VARIANT_NEW = {
    's': 'g_variant_new_string',
    'd': 'g_variant_new_double',
    'i': 'g_variant_new_int32',
    'u': 'g_variant_new_uint32',
    'b': 'g_variant_new_boolean',
    't': 'g_variant_new_uint64',
    'x': 'g_variant_new_int64',
    'y': 'g_variant_new_byte',
    'o': 'g_variant_new_object_path',
    'g': 'g_variant_new_signature',
}


def camel_to_kebab(name: str) -> str:
    """GetVehicleInfo → get-vehicle-info"""
    result = re.sub(r'([A-Z])', r'-\1', name).lower().lstrip('-')
    return result


def camel_to_snake(name: str) -> str:
    """GetVehicleInfo → get_vehicle_info"""
    result = re.sub(r'([A-Z])', r'_\1', name).lower().lstrip('_')
    return result


def cxx_type(dbus_type: str) -> str:
    """Map D-Bus type string to C++ type"""
    if dbus_type in DBUS_TO_CXX:
        return DBUS_TO_CXX[dbus_type]
    return f"/* TODO: {dbus_type} */"


def c_type(dbus_type: str) -> str:
    if dbus_type in DBUS_TO_C:
        return DBUS_TO_C[dbus_type]
    return f"/* TODO: {dbus_type} */"


def parse_args(xml_node) -> list:
    """Parse <arg> elements, return list of {name, type, direction}"""
    args = []
    for arg in xml_node.findall('arg'):
        args.append({
            'name': arg.get('name', ''),
            'type': arg.get('type', ''),
            'direction': arg.get('direction', 'in'),
        })
    return args


def method_handler_signal(namespace_lower: str, type_lower: str, method_name: str) -> str:
    """The gdbus-codegen signal name: handle-get-vehicle-info"""
    return f"handle-{camel_to_kebab(method_name)}"


def method_complete_func(namespace_lower: str, type_lower: str, method_name: str) -> str:
    """demo_vehicle_complete_get_vehicle_info"""
    return f"{namespace_lower}_{type_lower}_complete_{camel_to_snake(method_name)}"


def proxy_call_func(namespace_lower: str, type_lower: str, method_name: str) -> str:
    """demo_vehicle_call_get_vehicle_info"""
    return f"{namespace_lower}_{type_lower}_call_{camel_to_snake(method_name)}"


def proxy_finish_func(namespace_lower: str, type_lower: str, method_name: str) -> str:
    """demo_vehicle_call_get_vehicle_info_finish"""
    return f"{namespace_lower}_{type_lower}_call_{camel_to_snake(method_name)}_finish"


def emit_func(namespace_lower: str, type_lower: str, signal_name: str) -> str:
    """demo_vehicle_emit_speed_changed"""
    return f"{namespace_lower}_{type_lower}_emit_{camel_to_snake(signal_name)}"


def getter_func(namespace_lower: str, type_lower: str, prop_name: str) -> str:
    """demo_vehicle_get_speed"""
    return f"{namespace_lower}_{type_lower}_get_{camel_to_snake(prop_name)}"


def setter_func(namespace_lower: str, type_lower: str, prop_name: str) -> str:
    """demo_vehicle_set_speed"""
    return f"{namespace_lower}_{type_lower}_set_{camel_to_snake(prop_name)}"


def generate_header(xml_path: str, namespace: str, intf_prefix: str,
                    c_header: str) -> str:
    """Generate the complete C++ header from XML"""
    tree = ElementTree.parse(xml_path)
    root = tree.getroot()

    intf = root.find('interface')
    if intf is None:
        raise ValueError("No <interface> found in XML")
    intf_name = intf.get('name', '')
    short_name = intf_name.replace(intf_prefix, '')

    ns_lower = namespace.lower()
    type_name = f"{namespace}{short_name}"              # DemoVehicle
    type_lower = camel_to_snake(short_name)              # vehicle

    methods = []
    signals = []
    properties = []

    for child in intf:
        tag = child.tag
        if tag == 'method':
            in_args = [a for a in parse_args(child) if a['direction'] == 'in']
            out_args = [a for a in parse_args(child) if a['direction'] == 'out']
            methods.append({
                'name': child.get('name', ''),
                'in_args': in_args,
                'out_args': out_args,
            })
        elif tag == 'signal':
            args = parse_args(child)
            signals.append({
                'name': child.get('name', ''),
                'args': args,
            })
        elif tag == 'property':
            properties.append({
                'name': child.get('name', ''),
                'type': child.get('type', ''),
                'access': child.get('access', 'read'),
            })

    lines = []
    def L(s=''): lines.append(s)
    IND = '    '

    L(f'// AUTO-GENERATED by gen_cpp_bindings.py from {xml_path}')
    L(f'// DO NOT EDIT — modify the XML and rebuild')
    L('')
    L('#pragma once')
    L('')
    L(f'#include "{c_header}"  // gdbus-codegen output')
    L('')
    L('#include <cstdint>')
    L('#include <functional>')
    L('#include <memory>')
    L('#include <string>')
    L('')

    # ---- Forward declare C structs used as opaque pointers ----
    L(f'// C types from gdbus-codegen (used as opaque pointers)')
    L(f'// {type_name}        — GObject D-Bus interface')
    L(f'// {type_name}Proxy   — GDBusProxy subclass (C struct)')
    L(f'// {type_name}Skeleton — GDBusInterfaceSkeleton subclass (C struct)')
    L('')

    # ================================================================
    # Server class (wraps DemoVehicleSkeleton C struct)
    # ================================================================
    server_class = f'{type_name}Server'
    L('// ==========================================================================')
    L(f'// {server_class} — service-side (auto-generated)')
    L(f'// Wraps {type_name}* from gdbus-codegen skeleton')
    L('// ==========================================================================')
    L(f'class {server_class} {{')
    L('public:')

    # ---- Result structs nested inside Server ----
    for m in methods:
        if not m['out_args']:
            continue
        struct_name = f"{m['name']}Result"
        L(f'{IND}// Result struct for {m["name"]}()')
        L(f'{IND}struct {struct_name} {{')
        for a in m['out_args']:
            L(f'{IND}{IND}{cxx_type(a["type"])} {a["name"]};')
        L(f'{IND}}};')
        L('')

    L(f'{IND}{server_class}();')
    L(f'{IND}~{server_class}();')
    L('')
    L(f'{IND}{server_class}(const {server_class}&) = delete;')
    L(f'{IND}{server_class}& operator=(const {server_class}&) = delete;')
    L('')
    L(f'{IND}/// Export skeleton on D-Bus connection')
    L(f'{IND}bool export_on_bus(GDBusConnection* conn, const std::string& object_path);')
    L('')

    # Method handlers
    if methods:
        L(f'{IND}// --- Method handlers ---')
    for m in methods:
        handler_name = f"{m['name']}Handler"
        if m['out_args']:
            result_type = f"{m['name']}Result"
        else:
            result_type = 'void'

        in_params = ', '.join(f'{cxx_type(a["type"])} {a["name"]}'
                              for a in m['in_args'])
        all_args = [(a['type'], a['name']) for a in m['in_args'] + m['out_args']]
        doc = ', '.join(f'{t} {n}' for t, n in all_args)
        if m['out_args']:
            out_types = ', '.join(f'{t} {n}' for t, n in [(a['type'], a['name']) for a in m['out_args']])
            doc = f'{doc} → {out_types}'

        L(f'{IND}/// {m["name"]}({doc})')
        in_params_no_default = in_params if in_params else ''
        L(f'{IND}using {handler_name} = std::function<{result_type}({in_params_no_default})>;')
        L(f'{IND}void set_{camel_to_snake(m["name"])}_handler({handler_name} cb);')
        L('')

    # Signal emitters
    if signals:
        L(f'{IND}// --- Signal emitters ---')
    for s in signals:
        params = ', '.join(f'{cxx_type(a["type"])} {a["name"]}' for a in s['args'])
        doc = ', '.join(f'{a["type"]} {a["name"]}' for a in s['args'])
        L(f'{IND}/// {s["name"]}({doc})')
        L(f'{IND}void emit_{camel_to_snake(s["name"])}({params});')
        L('')

    # Properties (skeleton: getter for read, setter for ALL — service updates internally)
    if properties:
        L(f'{IND}// --- Properties ---')
    for p in properties:
        pt = cxx_type(p['type'])
        L(f'{IND}/// {p["name"]}: {p["type"]} ({p["access"]})')
        if 'read' in p['access']:
            L(f'{IND}{pt} get_{camel_to_snake(p["name"])}() const;')
        # Always generate setter for server-side (even read-only props need internal update)
        L(f'{IND}void set_{camel_to_snake(p["name"])}({pt} val);')
        L('')

    L('private:')
    L(f'{IND}{type_name}* skel_ = nullptr;')
    for m in methods:
        L(f'{IND}gulong hnd_{camel_to_snake(m["name"])}_ = 0;')
    L('};')
    L('')

    # ================================================================
    # Client class (wraps DemoVehicleProxy C struct, stores as DemoVehicle*)
    # ================================================================
    client_class = f'{type_name}Client'
    L('// ==========================================================================')
    L(f'// {client_class} — client-side (auto-generated)')
    L(f'// Wraps {type_name}* from gdbus-codegen proxy (stored as interface pointer)')
    L('// ==========================================================================')
    L(f'class {client_class} {{')
    L('public:')
    if methods:
        for m in methods:
            if m['out_args']:
                L(f'{IND}using {m["name"]}Result = {server_class}::{m["name"]}Result;')
        L('')
    L(f'{IND}/// Create proxy synchronously')
    L(f'{IND}static std::unique_ptr<{client_class}> create_sync(')
    L(f'{IND}    GDBusConnection* conn,')
    L(f'{IND}    const std::string& bus_name = "com.example.VehicleService",')
    L(f'{IND}    const std::string& object_path = "/com/example/Vehicle");')
    L('')
    L(f'{IND}~{client_class}();')
    L('')
    L(f'{IND}{client_class}(const {client_class}&) = delete;')
    L(f'{IND}{client_class}& operator=(const {client_class}&) = delete;')
    L('')

    # Async method calls
    if methods:
        L(f'{IND}// --- Async method calls ---')
    for m in methods:
        sn = camel_to_snake(m['name'])
        if m['out_args']:
            result_type = f"{m['name']}Result"
            done_cb = f'std::function<void(const {result_type}&)>'
        else:
            done_cb = 'std::function<void()>'
        L(f'{IND}/// {m["name"]}() async')
        L(f'{IND}void {sn}_async(')
        for a in m['in_args']:
            L(f'{IND}    {cxx_type(a["type"])} {a["name"]},')
        L(f'{IND}    {done_cb} on_done,')
        L(f'{IND}    std::function<void(const std::string&)> on_error = nullptr);')
        L('')

    # Signal subscriptions
    if signals:
        L(f'{IND}// --- Signal subscriptions ---')
    for s in signals:
        params = ', '.join(f'{cxx_type(a["type"])} {a["name"]}' for a in s['args'])
        L(f'{IND}/// Subscribe to {s["name"]} signal')
        L(f'{IND}void on_{camel_to_snake(s["name"])}(std::function<void({params})> cb);')
        L('')

    # Property access (sync — uses cached values from GDBusProxy)
    if properties:
        L(f'{IND}// --- Property access (sync via D-Bus property cache) ---')
    for p in properties:
        pt = cxx_type(p['type'])
        sn = camel_to_snake(p['name'])
        if 'read' in p['access']:
            L(f'{IND}/// Get {p["name"]} ({p["type"]}) from proxy cache')
            L(f'{IND}{pt} get_{sn}() const;')
            L('')
        if 'write' in p['access']:
            L(f'{IND}/// Set {p["name"]} ({p["type"]}) via D-Bus')
            L(f'{IND}void set_{sn}_async(')
            L(f'{IND}    {pt} val,')
            L(f'{IND}    std::function<void()> on_done = nullptr,')
            L(f'{IND}    std::function<void(const std::string&)> on_error = nullptr);')
            L('')

    L('private:')
    L(f'{IND}explicit {client_class}({type_name}* proxy);')
    L(f'{IND}{type_name}* proxy_ = nullptr;  // {type_name}* from proxy_new_sync')
    for s in signals:
        L(f'{IND}gulong sig_{camel_to_snake(s["name"])}_ = 0;')
    L('};')

    return '\n'.join(lines)


def generate_impl(xml_path: str, namespace: str, intf_prefix: str,
                  c_header: str, hpp_basename: str) -> str:
    """Generate the .cpp implementation file"""
    tree = ElementTree.parse(xml_path)
    root = tree.getroot()
    intf = root.find('interface')
    intf_name = intf.get('name', '')
    short_name = intf_name.replace(intf_prefix, '')

    ns_lower = namespace.lower()
    type_name = f"{namespace}{short_name}"
    type_lower = camel_to_snake(short_name)
    type_upper = f"{namespace.upper()}_{short_name.upper()}"

    server_class = f'{type_name}Server'
    client_class = f'{type_name}Client'

    methods = []
    signals = []
    properties = []

    for child in intf:
        tag = child.tag
        if tag == 'method':
            in_args = [a for a in parse_args(child) if a['direction'] == 'in']
            out_args = [a for a in parse_args(child) if a['direction'] == 'out']
            methods.append({
                'name': child.get('name', ''),
                'in_args': in_args,
                'out_args': out_args,
            })
        elif tag == 'signal':
            signals.append({
                'name': child.get('name', ''),
                'args': parse_args(child),
            })
        elif tag == 'property':
            properties.append({
                'name': child.get('name', ''),
                'type': child.get('type', ''),
                'access': child.get('access', 'read'),
            })

    lines = []
    def L(s=''): lines.append(s)
    IND = '    '

    # Derive include filename from output
    hpp_name = c_header.replace('-dbus.h', '_bindings.hpp')
    # Actually, since we define the hpp name separately, let's just use a
    # relative path that works. The .cpp is generated alongside the .hpp,
    # so the include is just the basename.
    L(f'// AUTO-GENERATED by gen_cpp_bindings.py from {xml_path}')
    L(f'// DO NOT EDIT')
    L('')
    L(f'#include "{hpp_basename}"')
    L('')
    L('#include <cstdio>')
    L('')

    # ================================================================
    # Server implementation
    # ================================================================
    L(f'// ==========================================================================')
    L(f'// {server_class}')
    L(f'// ==========================================================================')
    L('')

    # Constructor
    L(f'{server_class}::{server_class}() {{')
    L(f'{IND}skel_ = {ns_lower}_{type_lower}_skeleton_new();')
    L('')

    for m in methods:
        sn = camel_to_snake(m['name'])
        handler_cb_name = f'cb_{sn}'
        kebab = camel_to_kebab(m['name'])
        handler_signal = f'handle-{kebab}'
        complete_func = method_complete_func(ns_lower, type_lower, m['name'])

        L(f'{IND}// {m["name"]} handler')
        L(f'{IND}struct {handler_cb_name}_t {{')
        L(f'{IND}{IND}{m["name"]}Handler fn;')
        # Build trampoline signature with input args
        tramp_params = f'{type_name}* obj, GDBusMethodInvocation* inv'
        for a in m['in_args']:
            tramp_params += f', {c_type(a["type"])} arg_{a["name"]}'
        tramp_params += ', gpointer d'
        L(f'{IND}{IND}static gboolean tramp({tramp_params}) {{')
        L(f'{IND}{IND}{IND}auto* self = static_cast<{handler_cb_name}_t*>(d);')
        L(f'{IND}{IND}{IND}if (!self->fn) {{')
        L(f'{IND}{IND}{IND}{IND}g_dbus_method_invocation_return_error(inv, G_DBUS_ERROR, G_DBUS_ERROR_FAILED,')
        L(f'{IND}{IND}{IND}{IND}{IND}"Handler not set for {m["name"]}");')
        L(f'{IND}{IND}{IND}{IND}return TRUE;')
        L(f'{IND}{IND}{IND}}}')
        L('')

        # Call handler with parsed input args
        if m['in_args']:
            call_args = []
            for a in m['in_args']:
                if a['type'] == 's':
                    call_args.append(f'arg_{a["name"]} ? arg_{a["name"]} : ""')
                else:
                    call_args.append(f'arg_{a["name"]}')
            L(f'{IND}{IND}{IND}auto result = self->fn({", ".join(call_args)});')
        else:
            L(f'{IND}{IND}{IND}auto result = self->fn();')

        L('')
        # Complete method
        if m['out_args']:
            getter_params = []
            for a in m['out_args']:
                if a['type'] == 's':
                    getter_params.append(f'result.{a["name"]}.c_str()')
                elif a['type'] == 'b':
                    getter_params.append(f'static_cast<gboolean>(result.{a["name"]})')
                else:
                    getter_params.append(f'result.{a["name"]}')
            L(f'{IND}{IND}{IND}{complete_func}(obj, inv, {", ".join(getter_params)});')
        else:
            L(f'{IND}{IND}{IND}{complete_func}(obj, inv);')
        L(f'{IND}{IND}{IND}return TRUE;')
        L(f'{IND}{IND}}}')
        L(f'{IND}{IND}static void destroy(gpointer d, GClosure*) {{ delete static_cast<{handler_cb_name}_t*>(d); }}')
        L(f'{IND}}};')
        L(f'{IND}auto* {handler_cb_name} = new {handler_cb_name}_t{{nullptr}};')
        L(f'{IND}hnd_{sn}_ = g_signal_connect_data(skel_, "{handler_signal}",')
        L(f'{IND}{IND}G_CALLBACK({handler_cb_name}_t::tramp), {handler_cb_name},')
        L(f'{IND}{IND}{handler_cb_name}_t::destroy, GConnectFlags(0));')
        L('')
    L('}')
    L('')

    # Destructor
    L(f'{server_class}::~{server_class}() {{')
    L(f'{IND}if (skel_) g_object_unref(skel_);')
    L(f'}}')
    L('')

    # export_on_bus
    L(f'bool {server_class}::export_on_bus(GDBusConnection* conn, const std::string& path) {{')
    L(f'{IND}GError* err = nullptr;')
    L(f'{IND}g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON(skel_), conn,')
    L(f'{IND}{IND}{IND}{IND}{IND}{IND}{IND}{IND}{IND}{IND}{IND}{IND}{IND}path.c_str(), &err);')
    L(f'{IND}if (err) {{ g_print("[SERVER] export error: %s\\n", err->message); g_error_free(err); return false; }}')
    L(f'{IND}return true;')
    L(f'}}')
    L('')

    # Method handler setters
    for m in methods:
        sn = camel_to_snake(m['name'])
        handler_cb_name = f'cb_{sn}'
        kebab = camel_to_kebab(m['name'])
        handler_signal = f'handle-{kebab}'
        complete_func = method_complete_func(ns_lower, type_lower, m['name'])

        L(f'void {server_class}::set_{sn}_handler({m["name"]}Handler cb) {{')
        L(f'{IND}// Disconnect old handler, reconnect with new callback')
        L(f'{IND}if (hnd_{sn}_) {{')
        L(f'{IND}{IND}g_signal_handler_disconnect(skel_, hnd_{sn}_);')
        L(f'{IND}{IND}hnd_{sn}_ = 0;')
        L(f'{IND}}}')
        L(f'{IND}struct {handler_cb_name}_t {{')
        L(f'{IND}{IND}{m["name"]}Handler fn;')
        # Build trampoline signature with input args
        tramp_params2 = f'{type_name}* obj, GDBusMethodInvocation* inv'
        for a in m['in_args']:
            tramp_params2 += f', {c_type(a["type"])} arg_{a["name"]}'
        tramp_params2 += ', gpointer d'
        L(f'{IND}{IND}static gboolean tramp({tramp_params2}) {{')
        L(f'{IND}{IND}{IND}auto* self = static_cast<{handler_cb_name}_t*>(d);')
        L(f'{IND}{IND}{IND}if (!self->fn) {{')
        L(f'{IND}{IND}{IND}{IND}g_dbus_method_invocation_return_error(inv, G_DBUS_ERROR, G_DBUS_ERROR_FAILED,')
        L(f'{IND}{IND}{IND}{IND}{IND}"Handler not set");')
        L(f'{IND}{IND}{IND}{IND}return TRUE;')
        L(f'{IND}{IND}{IND}}}')
        if m['in_args']:
            call_args2 = []
            for a in m['in_args']:
                if a['type'] == 's':
                    call_args2.append(f'arg_{a["name"]} ? arg_{a["name"]} : ""')
                else:
                    call_args2.append(f'arg_{a["name"]}')
            L(f'{IND}{IND}{IND}auto result = self->fn({", ".join(call_args2)});')
        else:
            L(f'{IND}{IND}{IND}auto result = self->fn();')
        L('')
        if m['out_args']:
            getter_params = []
            for a in m['out_args']:
                if a['type'] == 's':
                    getter_params.append(f'result.{a["name"]}.c_str()')
                elif a['type'] == 'b':
                    getter_params.append(f'static_cast<gboolean>(result.{a["name"]})')
                else:
                    getter_params.append(f'result.{a["name"]}')
            L(f'{IND}{IND}{IND}{complete_func}(obj, inv, {", ".join(getter_params)});')
        else:
            L(f'{IND}{IND}{IND}{complete_func}(obj, inv);')
        L(f'{IND}{IND}{IND}return TRUE;')
        L(f'{IND}{IND}}}')
        L(f'{IND}{IND}static void destroy(gpointer d, GClosure*) {{ delete static_cast<{handler_cb_name}_t*>(d); }}')
        L(f'{IND}}};')
        L(f'{IND}auto* data = new {handler_cb_name}_t{{std::move(cb)}};')
        L(f'{IND}hnd_{sn}_ = g_signal_connect_data(skel_, "{handler_signal}",')
        L(f'{IND}{IND}G_CALLBACK({handler_cb_name}_t::tramp), data,')
        L(f'{IND}{IND}{handler_cb_name}_t::destroy, GConnectFlags(0));')
        L(f'}}')
        L('')

    # Signal emitters
    for s in signals:
        sn = camel_to_snake(s['name'])
        emit_fn = emit_func(ns_lower, type_lower, s['name'])
        params = ', '.join(f'{cxx_type(a["type"])} {a["name"]}' for a in s['args'])
        call_params_list = []
        for a in s['args']:
            if a['type'] == 's':
                call_params_list.append(f'{a["name"]}.c_str()')
            else:
                call_params_list.append(a['name'])
        call_params = ', '.join(call_params_list)
        L(f'void {server_class}::emit_{sn}({params}) {{')
        L(f'{IND}{emit_fn}(skel_, {call_params});')
        L(f'}}')
        L('')

    # Property getters/setters (server — use C functions)
    for p in properties:
        sn = camel_to_snake(p['name'])
        pt = cxx_type(p['type'])
        get_fn = getter_func(ns_lower, type_lower, p['name'])
        set_fn = setter_func(ns_lower, type_lower, p['name'])

        if 'read' in p['access']:
            L(f'{pt} {server_class}::get_{sn}() const {{')
            L(f'{IND}return {get_fn}(skel_);')
            L(f'}}')
            L('')

        # Always generate setter — service needs to update property internally
        L(f'void {server_class}::set_{sn}({pt} val) {{')
        if pt == 'std::string':
            L(f'{IND}{set_fn}(skel_, val.c_str());')
        else:
            L(f'{IND}{set_fn}(skel_, val);')
        L(f'}}')
        L('')

    # ================================================================
    # Client implementation
    # ================================================================
    L(f'// ==========================================================================')
    L(f'// {client_class}')
    L(f'// ==========================================================================')
    L('')

    # create_sync
    L(f'std::unique_ptr<{client_class}> {client_class}::create_sync(')
    L(f'{IND}GDBusConnection* conn,')
    L(f'{IND}const std::string& bus_name,')
    L(f'{IND}const std::string& path) {{')
    L(f'{IND}GError* err = nullptr;')
    L(f'{IND}auto* p = {ns_lower}_{type_lower}_proxy_new_sync(')
    L(f'{IND}{IND}conn, G_DBUS_PROXY_FLAGS_NONE, bus_name.c_str(), path.c_str(), nullptr, &err);')
    L(f'{IND}if (err) {{ g_print("[CLIENT] proxy create error: %s\\n", err->message); g_error_free(err); return nullptr; }}')
    L(f'{IND}// p is {type_name}* (interface type), store directly')
    L(f'{IND}return std::unique_ptr<{client_class}>(new {client_class}(p));')
    L(f'}}')
    L('')

    # Private constructor (takes DemoVehicle* from proxy_new_sync)
    L(f'{client_class}::{client_class}({type_name}* p) : proxy_(p) {{}}')
    L('')

    # Destructor
    L(f'{client_class}::~{client_class}() {{')
    L(f'{IND}if (proxy_) g_object_unref(proxy_);')
    L(f'}}')
    L('')

    # Method calls
    for m in methods:
        sn = camel_to_snake(m['name'])
        call_fn = proxy_call_func(ns_lower, type_lower, m['name'])
        finish_fn = proxy_finish_func(ns_lower, type_lower, m['name'])
        type_proxy_upper = type_upper  # DEMO_VEHICLE for casts

        if m['out_args']:
            result_type = f'{m["name"]}Result'
        else:
            result_type = 'void'

        L(f'void {client_class}::{sn}_async(')
        for a in m['in_args']:
            L(f'{IND}{cxx_type(a["type"])} {a["name"]},')
        if m['out_args']:
            L(f'{IND}std::function<void(const {result_type}&)> on_done,')
        else:
            L(f'{IND}std::function<void()> on_done,')
        L(f'{IND}std::function<void(const std::string&)> on_error) {{')
        L('')
        L(f'{IND}struct Ctx {{')
        L(f'{IND}{IND}{type_name}* proxy;  // for finish_fn (needs {type_name}*, not GObject*)')
        # Store input args for passing to proxy call
        for a in m['in_args']:
            L(f'{IND}{IND}{cxx_type(a["type"])} _{a["name"]};')
        if m['out_args']:
            L(f'{IND}{IND}std::function<void(const {result_type}&)> done;')
        else:
            L(f'{IND}{IND}std::function<void()> done;')
        L(f'{IND}{IND}std::function<void(const std::string&)> error;')
        L(f'{IND}{IND}static void callback(GObject* /*src*/, GAsyncResult* res, gpointer d) {{')
        L(f'{IND}{IND}{IND}auto* ctx = static_cast<Ctx*>(d);')
        L(f'{IND}{IND}{IND}GError* err = nullptr;')
        for a in m['out_args']:
            ct = c_type(a['type'])
            if a['type'] == 's':
                L(f'{IND}{IND}{IND}gchar* _{a["name"]} = nullptr;')
            elif a['type'] == 'd':
                L(f'{IND}{IND}{IND}gdouble _{a["name"]} = 0.0;')
            elif a['type'] == 'i':
                L(f'{IND}{IND}{IND}gint32 _{a["name"]} = 0;')
            elif a['type'] == 'b':
                L(f'{IND}{IND}{IND}gboolean _{a["name"]} = FALSE;')
            else:
                L(f'{IND}{IND}{IND}{ct} _{a["name"]} = {{}};')
        L('')
        # Build finish call
        finish_params = []
        for a in m['out_args']:
            finish_params.append(f'&_{a["name"]}')
        finish_params.extend(['res', '&err'])

        L(f'{IND}{IND}{IND}gboolean ok = {finish_fn}(ctx->proxy,')
        L(f'{IND}{IND}{IND}{IND}{", ".join(finish_params)});')
        L('')
        L(f'{IND}{IND}{IND}if (err) {{')
        L(f'{IND}{IND}{IND}{IND}if (ctx->error) ctx->error(err->message);')
        L(f'{IND}{IND}{IND}{IND}g_error_free(err);')
        L(f'{IND}{IND}{IND}}} else if (ok && ctx->done) {{')
        if m['out_args']:
            result_fields = []
            for a in m['out_args']:
                if a['type'] == 's':
                    result_fields.append(f'std::string(_{a["name"]} ? _{a["name"]} : "")')
                elif a['type'] == 'b':
                    result_fields.append(f'static_cast<bool>(_{a["name"]})')
                else:
                    result_fields.append(f'_{a["name"]}')
            L(f'{IND}{IND}{IND}{IND}ctx->done({result_type}{{{", ".join(result_fields)}}});')
        else:
            L(f'{IND}{IND}{IND}{IND}ctx->done();')
        L(f'{IND}{IND}{IND}}}')
        # Free string args
        for a in m['out_args']:
            if a['type'] == 's':
                L(f'{IND}{IND}{IND}g_free(_{a["name"]});')
        L(f'{IND}{IND}{IND}delete ctx;')
        L(f'{IND}{IND}}}')
        L(f'{IND}}};')
        # Build Ctx initialization
        ctx_init_fields = ['proxy_']
        for a in m['in_args']:
            ctx_init_fields.append(f'std::move({a["name"]})')
        ctx_init_fields.extend(['std::move(on_done)', 'std::move(on_error)'])
        L(f'{IND}auto* ctx = new Ctx{{{", ".join(ctx_init_fields)}}};')
        # Build proxy call args: proxy_, in_args..., nullptr, callback, ctx
        call_args_list = ['proxy_']
        for a in m['in_args']:
            if a['type'] == 's':
                call_args_list.append(f'ctx->_{a["name"]}.c_str()')
            else:
                call_args_list.append(f'ctx->_{a["name"]}')
        call_args_list.extend(['nullptr', 'Ctx::callback', 'ctx'])
        L(f'{IND}{call_fn}({", ".join(call_args_list)});')
        L(f'}}')
        L('')

    # Signal subscriptions
    for s in signals:
        sn = camel_to_snake(s['name'])
        sk = camel_to_kebab(s['name'])

        cb_params = ', '.join(f'{cxx_type(a["type"])} {a["name"]}' for a in s['args'])
        L(f'void {client_class}::on_{sn}(std::function<void({cb_params})> cb) {{')
        L(f'{IND}struct Ctx {{')
        L(f'{IND}{IND}std::function<void({cb_params})> fn;')

        # gdbus-codegen callback signature: (DemoVehicle*, gdouble arg_new_speed, gpointer)
        cb_args = []
        for a in s['args']:
            cb_args.append(f'{c_type(a["type"])} arg_{a["name"]}')
        cb_args.append('gpointer d')
        cb_sig = ', '.join(cb_args)

        L(f'{IND}{IND}static void callback({type_name}* /*obj*/, {cb_sig}) {{')
        L(f'{IND}{IND}{IND}auto* ctx = static_cast<Ctx*>(d);')
        # Convert C args to C++ args
        conv_args = []
        for a in s['args']:
            if a['type'] == 's':
                conv_args.append(f'std::string(arg_{a["name"]} ? arg_{a["name"]} : "")')
            else:
                conv_args.append(f'arg_{a["name"]}')
        L(f'{IND}{IND}{IND}if (ctx->fn) ctx->fn({", ".join(conv_args)});')
        L(f'{IND}{IND}}}')
        L(f'{IND}{IND}static void destroy(gpointer d, GClosure*) {{ delete static_cast<Ctx*>(d); }}')
        L(f'{IND}}};')
        L(f'{IND}if (sig_{sn}_) g_signal_handler_disconnect(proxy_, sig_{sn}_);')
        L(f'{IND}auto* ctx = new Ctx{{std::move(cb)}};')
        L(f'{IND}sig_{sn}_ = g_signal_connect_data(proxy_, "{sk}", G_CALLBACK(Ctx::callback),')
        L(f'{IND}{IND}ctx, Ctx::destroy, GConnectFlags(0));')
        L(f'}}')
        L('')

    # Property access (client — sync via cached property)
    for p in properties:
        sn = camel_to_snake(p['name'])
        pt = cxx_type(p['type'])
        gfn = getter_func(ns_lower, type_lower, p['name'])
        sfn = setter_func(ns_lower, type_lower, p['name'])

        if 'read' in p['access']:
            L(f'{pt} {client_class}::get_{sn}() const {{')
            L(f'{IND}// Property is cached by GDBusProxy — sync call is instantaneous')
            L(f'{IND}return {gfn}(proxy_);')
            L(f'}}')
            L('')

        if 'write' in p['access']:
            variant_new_fn = DBUS_TO_VARIANT_NEW.get(p['type'], 'g_variant_new_string')
            L(f'void {client_class}::set_{sn}_async(')
            L(f'{IND}{pt} val,')
            L(f'{IND}std::function<void()> on_done,')
            L(f'{IND}std::function<void(const std::string&)> on_error) {{')
            L(f'{IND}// Use org.freedesktop.DBus.Properties.Set via g_dbus_proxy_call')
            L(f'{IND}// Build: g_variant_new("(ssv)", iface, prop, g_variant_new_X(val))')
            L(f'{IND}GVariant* v = {variant_new_fn}(val);')
            L(f'{IND}GVariant* args = g_variant_new("(ssv)",')
            L(f'{IND}{IND}"{intf_name}", "{p["name"]}", v);')
            L(f'{IND}struct Ctx {{')
            L(f'{IND}{IND}{type_name}* proxy;')
            L(f'{IND}{IND}std::function<void()> done;')
            L(f'{IND}{IND}std::function<void(const std::string&)> error;')
            L(f'{IND}{IND}static void callback(GObject* /*src*/, GAsyncResult* res, gpointer d) {{')
            L(f'{IND}{IND}{IND}auto* ctx = static_cast<Ctx*>(d);')
            L(f'{IND}{IND}{IND}GError* err = nullptr;')
            L(f'{IND}{IND}{IND}g_dbus_proxy_call_finish(G_DBUS_PROXY(ctx->proxy), res, &err);')
            L(f'{IND}{IND}{IND}if (err) {{')
            L(f'{IND}{IND}{IND}{IND}if (ctx->error) ctx->error(err->message);')
            L(f'{IND}{IND}{IND}{IND}g_error_free(err);')
            L(f'{IND}{IND}{IND}}} else if (ctx->done) {{')
            L(f'{IND}{IND}{IND}{IND}ctx->done();')
            L(f'{IND}{IND}{IND}}}')
            L(f'{IND}{IND}{IND}delete ctx;')
            L(f'{IND}{IND}}}')
            L(f'{IND}}};')
            L(f'{IND}auto* ctx = new Ctx{{proxy_, std::move(on_done), std::move(on_error)}};')
            L(f'{IND}g_dbus_proxy_call(G_DBUS_PROXY(proxy_),')
            L(f'{IND}{IND}"org.freedesktop.DBus.Properties.Set",')
            L(f'{IND}{IND}args,')
            L(f'{IND}{IND}G_DBUS_CALL_FLAGS_NONE, -1, nullptr,')
            L(f'{IND}{IND}Ctx::callback, ctx);')
            L(f'}}')
            L('')

    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser(
        description='Generate C++17 bindings from D-Bus XML')
    parser.add_argument('xml', help='Path to interface XML file')
    parser.add_argument('--namespace', default='Demo',
                        help='C namespace prefix (default: Demo)')
    parser.add_argument('--interface-prefix', default='com.example.',
                        help='Interface prefix to strip (default: com.example.)')
    parser.add_argument('--c-header', default='vehicle-dbus.h',
                        help='Filename of the gdbus-codegen generated C header')
    parser.add_argument('--output-header', required=True,
                        help='Output C++ header file')
    parser.add_argument('--output-impl', required=True,
                        help='Output C++ implementation file')
    args = parser.parse_args()

    import os
    hpp_basename = os.path.basename(args.output_header)
    header = generate_header(args.xml, args.namespace, args.interface_prefix,
                             args.c_header)
    impl = generate_impl(args.xml, args.namespace, args.interface_prefix,
                         args.c_header, hpp_basename)

    with open(args.output_header, 'w') as f:
        f.write(header)
    with open(args.output_impl, 'w') as f:
        f.write(impl)

    print(f"Generated: {args.output_header}")
    print(f"Generated: {args.output_impl}")


if __name__ == '__main__':
    main()
