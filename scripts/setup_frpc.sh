#!/bin/bash
# ============================================================
# frpc 部署脚本 (ARM64)
# 功能：在机器人端部署 frp 客户端，打通内网穿透隧道
# 用途：将容器内的 9090 端口 (rosbridge) 暴露到公网
# 服务器：124.222.135.234:7000
# ============================================================

set -e  # 遇到错误立即退出

# ---------- 颜色输出 ----------
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

print_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# ---------- 检查是否以 root 运行 ----------
if [ "$EUID" -eq 0 ]; then
    print_error "请不要以 root 用户运行此脚本（不要用 sudo）"
    exit 1
fi

print_info "开始部署 frpc (ARM64) ..."

# ---------- 1. 下载并解压 ----------
print_info "1. 下载 frp v0.58.0 (ARM64) ..."
cd ~
if [ -f "frp_0.58.0_linux_arm64.tar.gz" ]; then
    print_warn "安装包已存在，跳过下载"
else
    wget https://github.com/fatedier/frp/releases/download/v0.58.0/frp_0.58.0_linux_arm64.tar.gz
fi

print_info "2. 解压 ..."
tar -xzf frp_0.58.0_linux_arm64.tar.gz
cd frp_0.58.0_linux_arm64

# ---------- 2. 安装二进制 ----------
print_info "3. 安装 frpc 到 /usr/local/bin/ ..."
sudo cp frpc /usr/local/bin/
sudo chmod +x /usr/local/bin/frpc

# 验证安装
if command -v frpc &> /dev/null; then
    print_info "frpc 安装成功: $(which frpc)"
else
    print_error "frpc 安装失败"
    exit 1
fi

# ---------- 3. 创建配置 ----------
print_info "4. 创建配置文件 /etc/frp/frpc.toml ..."
sudo mkdir -p /etc/frp

sudo tee /etc/frp/frpc.toml > /dev/null <<'EOF'
serverAddr = "124.222.135.234"
serverPort = 7000
auth.token = "frp-token-change-me-2026-abc123def456"

[[proxies]]
name = "rosbridge"
type = "tcp"
localIP = "127.0.0.1"
localPort = 9090
remotePort = 9090
EOF

print_info "配置文件已写入："
cat /etc/frp/frpc.toml

# ---------- 4. 创建 systemd 服务 ----------
print_info "5. 创建 systemd 服务 /etc/systemd/system/frpc.service ..."
sudo tee /etc/systemd/system/frpc.service > /dev/null <<'EOF'
[Unit]
Description=frp client
After=network.target
[Service]
Type=simple
ExecStart=/usr/local/bin/frpc -c /etc/frp/frpc.toml
Restart=always
RestartSec=5
[Install]
WantedBy=multi-user.target
EOF

# ---------- 5. 启动服务 ----------
print_info "6. 重载 systemd 并启动 frpc ..."
sudo systemctl daemon-reload
sudo systemctl enable --now frpc
sudo systemctl restart frpc

# ---------- 6. 检查状态 ----------
print_info "7. 检查 frpc 服务状态 ..."
sleep 1
sudo systemctl status frpc --no-pager

print_info "=========================================="
print_info "✅ frpc 部署完成！"
print_info "=========================================="
print_info "查看实时日志：sudo journalctl -u frpc -f"
print_info "服务状态：sudo systemctl status frpc"
print_info "停止服务：sudo systemctl stop frpc"
print_info ""
print_info "端口映射："
print_info "  本地 127.0.0.1:9090 → 公网 124.222.135.234:9090"
print_info "=========================================="
