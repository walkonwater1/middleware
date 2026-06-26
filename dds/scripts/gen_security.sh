#!/bin/bash
# ============================================================
# gen_security.sh — 生成 DDS Security 证书 + 配置文件
#
# 生成内容:
#   security/
#   ├── ca.crt / ca.key          CA 根证书
#   ├── pub.crt / pub.key        发布者证书
#   ├── sub.crt / sub.key        订阅者证书
#   ├── governance.xml           治理规则 (保护等级)
#   ├── permissions_pub.xml      发布者权限
#   ├── permissions_sub.xml      订阅者权限
#   └── config.xml               CycloneDDS 安全配置入口
# ============================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
DDS_DIR="$(dirname "$SCRIPT_DIR")"
SEC_DIR="$DDS_DIR/security"

echo "╔══════════════════════════════════════════╗"
echo "║  🔑 生成 DDS Security 证书和配置         ║"
echo "╚══════════════════════════════════════════╝"
echo ""

mkdir -p "$SEC_DIR"

# ---- 1. 生成 CA 根证书 ----
if [ ! -f "$SEC_DIR/ca.key" ]; then
    echo "[1/5] 生成 CA 根证书..."
    openssl req -new -x509 -days 365 -nodes \
        -subj "/C=CN/O=DDS-Lab/CN=DDS-Security-CA" \
        -keyout "$SEC_DIR/ca.key" \
        -out "$SEC_DIR/ca.crt" 2>/dev/null
    echo "  ✅ ca.crt / ca.key"
else
    echo "[1/5] CA 根证书已存在, 跳过"
fi

# ---- 2. 生成发布者证书 ----
if [ ! -f "$SEC_DIR/pub.key" ]; then
    echo "[2/5] 生成发布者证书..."
    openssl req -new -nodes \
        -subj "/C=CN/O=DDS-Lab/CN=Publisher" \
        -keyout "$SEC_DIR/pub.key" \
        -out "$SEC_DIR/pub.csr" 2>/dev/null
    openssl x509 -req -days 365 -CA "$SEC_DIR/ca.crt" -CAkey "$SEC_DIR/ca.key" \
        -CAcreateserial -in "$SEC_DIR/pub.csr" \
        -out "$SEC_DIR/pub.crt" 2>/dev/null
    rm -f "$SEC_DIR/pub.csr"
    echo "  ✅ pub.crt / pub.key"
else
    echo "[2/5] 发布者证书已存在, 跳过"
fi

# ---- 3. 生成订阅者证书 ----
if [ ! -f "$SEC_DIR/sub.key" ]; then
    echo "[3/5] 生成订阅者证书..."
    openssl req -new -nodes \
        -subj "/C=CN/O=DDS-Lab/CN=Subscriber" \
        -keyout "$SEC_DIR/sub.key" \
        -out "$SEC_DIR/sub.csr" 2>/dev/null
    openssl x509 -req -days 365 -CA "$SEC_DIR/ca.crt" -CAkey "$SEC_DIR/ca.key" \
        -CAcreateserial -in "$SEC_DIR/sub.csr" \
        -out "$SEC_DIR/sub.crt" 2>/dev/null
    rm -f "$SEC_DIR/sub.csr"
    echo "  ✅ sub.crt / sub.key"
else
    echo "[3/5] 订阅者证书已存在, 跳过"
fi

# ---- 4. 治理规则 governance.xml ----
echo "[4/5] 生成 governance.xml..."
cat > "$SEC_DIR/governance.xml" << 'GOVXML'
<?xml version="1.0" encoding="UTF-8"?>
<dds xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
     xsi:noNamespaceSchemaLocation="http://www.omg.org/spec/DDS-Security/2017/09/omg_shared_ca_governance.xsd">
  <domain_access_rules>
    <domain_rule>
      <domains>
        <id>0</id>
      </domains>
      <allow_unauthenticated_participants>false</allow_unauthenticated_participants>
      <enable_join_access_control>true</enable_join_access_control>
      <discovery_protection_kind>ENCRYPT</discovery_protection_kind>
      <liveliness_protection_kind>ENCRYPT</liveliness_protection_kind>
      <rtps_protection_kind>ENCRYPT</rtps_protection_kind>
      <topic_access_rules>
        <topic_rule>
          <topic_expression>*</topic_expression>
          <enable_discovery_protection>true</enable_discovery_protection>
          <enable_liveliness_protection>true</enable_liveliness_protection>
          <enable_read_access_control>true</enable_read_access_control>
          <enable_write_access_control>true</enable_write_access_control>
          <metadata_protection_kind>ENCRYPT</metadata_protection_kind>
          <data_protection_kind>ENCRYPT</data_protection_kind>
        </topic_rule>
      </topic_access_rules>
    </domain_rule>
  </domain_access_rules>
</dds>
GOVXML
echo "  ✅ governance.xml"

# ---- 5. 权限文件 ----
echo "[5/5] 生成权限文件..."

# 发布者权限
cat > "$SEC_DIR/permissions_pub.xml" << 'PERMPUBXML'
<?xml version="1.0" encoding="UTF-8"?>
<dds xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
     xsi:noNamespaceSchemaLocation="http://www.omg.org/spec/DDS-Security/2017/09/omg_shared_ca_permissions.xsd">
  <permissions>
    <grant name="Publisher">
      <subject_name>CN=Publisher,O=DDS-Lab,C=CN</subject_name>
      <validity>
        <not_before>2024-01-01T00:00:00</not_before>
        <not_after>2030-12-31T23:59:59</not_after>
      </validity>
      <allow_rule>
        <domains><id>0</id></domains>
        <publish>
          <topics><topic>*</topic></topics>
        </publish>
      </allow_rule>
    </grant>
  </permissions>
</dds>
PERMPUBXML

# 订阅者权限
cat > "$SEC_DIR/permissions_sub.xml" << 'PERMSUBXML'
<?xml version="1.0" encoding="UTF-8"?>
<dds xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
     xsi:noNamespaceSchemaLocation="http://www.omg.org/spec/DDS-Security/2017/09/omg_shared_ca_permissions.xsd">
  <permissions>
    <grant name="Subscriber">
      <subject_name>CN=Subscriber,O=DDS-Lab,C=CN</subject_name>
      <validity>
        <not_before>2024-01-01T00:00:00</not_before>
        <not_after>2030-12-31T23:59:59</not_after>
      </validity>
      <allow_rule>
        <domains><id>0</id></domains>
        <subscribe>
          <topics><topic>*</topic></topics>
        </subscribe>
      </allow_rule>
    </grant>
  </permissions>
</dds>
PERMSUBXML
echo "  ✅ permissions_pub.xml / permissions_sub.xml"

# ---- 6. 主配置文件 ----
cat > "$SEC_DIR/config.xml" << 'CONFXML'
<?xml version="1.0" encoding="UTF-8"?>
<CycloneDDS xmlns="https://cdds.io/config">
  <Domain id="0">
    <Security>
      <Governance>file://$SEC_DIR/governance.xml</Governance>
      <Permissions>file://$SEC_DIR/permissions_pub.xml</Permissions>
      <Authentication>
        <IdentityCertificate>file://$SEC_DIR/pub.crt</IdentityCertificate>
        <PrivateKey>file://$SEC_DIR/pub.key</PrivateKey>
        <IdentityCA>file://$SEC_DIR/ca.crt</IdentityCA>
      </Authentication>
    </Security>
  </Domain>
</CycloneDDS>
CONFXML

# 替换 $SEC_DIR 为实际路径
sed -i "s|\$SEC_DIR|$SEC_DIR|g" "$SEC_DIR/config.xml"

echo "  ✅ config.xml"
echo ""

echo "╔══════════════════════════════════════════╗"
echo "║  ✅ 安全配置生成完毕!                    ║"
echo "╠══════════════════════════════════════════╣"
echo "║  文件位置: dds/security/                ║"
echo "║                                         ║"
echo "║  运行安全 Demo:                         ║"
echo "║    cd dds/build                         ║"
echo "║    # 安全 pub                           ║"
echo "║    CYCLONEDDS_URI=file://../security/config.xml \\"
echo "║      ./security_lab pub                 ║"
echo "║    # 安全 sub (另一个终端)              ║"
echo "║    CYCLONEDDS_URI=file://../security/config.xml \\"
echo "║      ./security_lab sub                 ║"
echo "║    # 尝试无安全入侵 (应被拒绝!)         ║"
echo "║    ./security_lab pub_open              ║"
echo "╚══════════════════════════════════════════╝"
