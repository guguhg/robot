#!/bin/bash
# ============================================================
# frpc 卸载脚本 (ARM64)
# ============================================================

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

print_info() { echo -e "${GREEN}[INFO]${NC} $1"; }
print_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
print_error() { echo -e "${RED}[ERROR]${NC} $1"; }
print_step() { echo -e "${BLUE}[STEP]${NC} $1"; }

if [ "$EUID" -eq 0 ]; then
    print_error "请不要以 root 用户运行此脚本（不要用 sudo）"
    exit 1
fi

echo ""
echo "=========================================="
print_warn "即将卸载 frpc (frp 客户端)"
echo "=========================================="
echo "将执行以下清理操作："
echo "  1. 停止并禁用 frpc systemd 服务"
echo "  2. 删除 /etc/systemd/system/frpc.service"
echo "  3. 删除 /usr/local/bin/frpc 可执行文件"
echo "  4. 删除 /etc/frp 配置目录"
echo "  5. 删除 ~/frp_0.58.0_linux_arm64 解压目录"
echo "  6. 删除 ~/frp_0.58.0_linux_arm64.tar.gz 安装包"
echo "  7. 重载 systemd 配置"
echo "=========================================="
echo ""

read -p "确认要卸载 frpc 吗？(输入 y 确认，任意键取消) " -r
if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    print_info "操作已取消"
    exit 0
fi

echo ""

print_step "1. 检查 frpc 服务状态 ..."
if systemctl list-units --full --all | grep -q "frpc.service"; then
    STATUS=$(systemctl is-active frpc 2>/dev/null || echo "inactive")
    if [ "$STATUS" = "active" ]; then
        print_info "frpc 服务当前运行中，将停止并禁用..."
        sudo systemctl stop frpc
        sudo systemctl disable frpc
        print_info "服务已停止并禁用"
    else
        print_warn "frpc 服务当前未运行，直接清理..."
        sudo systemctl disable frpc 2>/dev/null || true
    fi
else
    print_warn "frpc 服务文件不存在，跳过服务相关操作"
fi

print_step "2. 删除 systemd 服务文件 ..."
if [ -f /etc/systemd/system/frpc.service ]; then
    sudo rm -f /etc/systemd/system/frpc.service
    print_info "已删除 /etc/systemd/system/frpc.service"
else
    print_warn "服务文件不存在，跳过"
fi

print_step "3. 删除 frpc 可执行文件 ..."
if [ -f /usr/local/bin/frpc ]; then
    sudo rm -f /usr/local/bin/frpc
    print_info "已删除 /usr/local/bin/frpc"
else
    print_warn "frpc 二进制文件不存在，跳过"
fi

print_step "4. 删除配置目录 ..."
if [ -d /etc/frp ]; then
    sudo rm -rf /etc/frp
    print_info "已删除 /etc/frp"
else
    print_warn "配置目录 /etc/frp 不存在，跳过"
fi

print_step "5. 删除解压目录 ..."
if [ -d ~/frp_0.58.0_linux_arm64 ]; then
    rm -rf ~/frp_0.58.0_linux_arm64
    print_info "已删除 ~/frp_0.58.0_linux_arm64"
else
    print_warn "解压目录不存在，跳过"
fi

print_step "6. 删除安装包 ..."
if [ -f ~/frp_0.58.0_linux_arm64.tar.gz ]; then
    rm -f ~/frp_0.58.0_linux_arm64.tar.gz
    print_info "已删除 ~/frp_0.58.0_linux_arm64.tar.gz"
else
    print_warn "安装包不存在，跳过"
fi

print_step "7. 重载 systemd 配置 ..."
sudo systemctl daemon-reload
print_info "systemd 已重载"

echo ""
print_info "=========================================="
print_info "✅ frpc 已彻底卸载！"
print_info "=========================================="
echo ""
print_info "验证命令："
echo "  which frpc"
echo "  ls -la /etc/frp"
echo "  systemctl status frpc"
echo "=========================================="
