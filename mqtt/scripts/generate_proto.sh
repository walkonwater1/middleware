#!/usr/bin/env bash
# ============================================================
#  Linux: 编译 Proto 文件 → C (protobuf-c)
#
#  前置条件:
#    sudo apt install protobuf-c-compiler libprotobuf-c-dev
# ============================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

echo "[INFO] 项目目录: $PROJECT_DIR"
echo "[INFO] 编译 vehicle_can.proto -> C (protobuf-c) ..."

if command -v protoc-c &>/dev/null; then
    # 生成到 build 目录 (与 CMake 保持一致)
    mkdir -p "$PROJECT_DIR/build/proto"
    protoc-c \
        --c_out="$PROJECT_DIR/build" \
        --proto_path="$PROJECT_DIR" \
        "$PROJECT_DIR/proto/vehicle_can.proto"

    echo "[OK] 编译成功!"
    echo "  build/proto/vehicle_can.pb-c.h"
    echo "  build/proto/vehicle_can.pb-c.c"
else
    echo "[ERR] protoc-c 未安装! 请运行:"
    echo "  sudo apt install protobuf-c-compiler libprotobuf-c-dev"
    exit 1
fi
