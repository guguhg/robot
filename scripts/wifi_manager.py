#!/usr/bin/env python3
"""
Jetson Wi-Fi 配网系统
功能：开机等待10秒 → 检测Wi-Fi连接 → 未连接则开启AP → 网页配网 → 连接成功后自动保存
"""

import os
import subprocess
import time
import re
from flask import Flask, request, render_template_string

app = Flask(__name__)

# ========== 配置 ==========
HOTSPOT_SSID = "Jetson_Setup"
HOTSPOT_PASSWORD = "12345678"


def get_wifi_interface():
    """自动获取无线网卡名称"""
    try:
        result = subprocess.run(
            ['nmcli', '-t', '-f', 'DEVICE,TYPE', 'dev', 'status'],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=5
        )
        for line in result.stdout.decode().split('\n'):
            if ':wifi' in line:
                iface = line.split(':')[0]
                if iface:
                    return iface
        for iface in os.listdir('/sys/class/net/'):
            if iface.startswith('wl'):
                return iface
        return 'wlan0'
    except Exception as e:
        print("[ERROR] 获取网卡名失败:", e)
        return 'wlan0'


def get_hotspot_ip():
    """获取热点网关 IP"""
    try:
        result = subprocess.run(
            ['ip', 'addr', 'show', HOTSPOT_INTERFACE],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=5
        )
        output = result.stdout.decode()
        match = re.search(r'inet (\d+\.\d+\.\d+\.\d+)/\d+', output)
        if match:
            return match.group(1)
    except Exception:
        pass
    return "10.42.0.1"


def is_wifi_connected():
    """检查是否已连接 Wi-Fi"""
    try:
        result = subprocess.run(
            ['nmcli', '-t', '-f', 'TYPE,STATE', 'device', 'status'],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=5
        )
        for line in result.stdout.decode().split('\n'):
            if 'wifi:connected' in line:
                return True
        return False
    except Exception as e:
        print("[ERROR] 检查 Wi-Fi 状态失败:", e)
        return False


def start_hotspot():
    """开启 AP 热点"""
    print(f"[INFO] 开启热点 {HOTSPOT_SSID} ...")
    try:
        subprocess.run(['sudo', 'nmcli', 'con', 'down', 'Hotspot'],
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        subprocess.run(['sudo', 'nmcli', 'con', 'delete', 'Hotspot'],
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)

        result = subprocess.run(
            ['sudo', 'nmcli', 'dev', 'wifi', 'hotspot',
             'ifname', HOTSPOT_INTERFACE,
             'ssid', HOTSPOT_SSID,
             'password', HOTSPOT_PASSWORD],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30
        )
        if result.returncode == 0:
            print(f"[INFO] 热点已启动: {HOTSPOT_SSID} / {HOTSPOT_PASSWORD}")
            return True
        else:
            print("[ERROR] 热点启动失败:", result.stderr.decode())
            return False
    except Exception as e:
        print("[ERROR] 热点启动异常:", e)
        return False


def stop_hotspot():
    """彻底关闭 AP 热点"""
    try:
        print("[INFO] 正在彻底关闭 AP 模式...")
        subprocess.run(['sudo', 'nmcli', 'con', 'down', 'Hotspot'],
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        subprocess.run(['sudo', 'nmcli', 'con', 'delete', 'Hotspot'],
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        subprocess.run(['sudo', 'nmcli', 'dev', 'disconnect', HOTSPOT_INTERFACE],
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        time.sleep(3)
        print("[INFO] AP 模式已关闭")
        return True
    except Exception as e:
        print("[ERROR] 关闭热点失败:", e)
        return False


def connect_wifi(ssid, password):
    """连接指定的 Wi-Fi（先彻底关闭 AP）"""
    print(f"[INFO] 正在连接 {ssid} ...")
    try:
        # 1. 彻底关闭 AP 模式
        print("[INFO] 关闭 AP 模式...")
        subprocess.run(['sudo', 'nmcli', 'con', 'down', 'Hotspot'],
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        subprocess.run(['sudo', 'nmcli', 'con', 'delete', 'Hotspot'],
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        subprocess.run(['sudo', 'nmcli', 'dev', 'disconnect', HOTSPOT_INTERFACE],
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        
        # 2. 等待网卡完全释放
        print("[INFO] 等待网卡释放...")
        time.sleep(5)
        
        # 3. 扫描附近 Wi-Fi（确认网卡已切换到客户端模式）
        print("[INFO] 扫描 Wi-Fi...")
        subprocess.run(['sudo', 'nmcli', 'dev', 'wifi', 'list'],
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=10)

        # 4. 连接目标 Wi-Fi
        print(f"[INFO] 连接 {ssid} ...")
        result = subprocess.run(
            ['sudo', 'nmcli', 'dev', 'wifi', 'connect', ssid, 'password', password],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=20
        )
        
        if result.returncode == 0:
            print(f"[INFO] 连接成功: {ssid}")
            subprocess.run(['sudo', 'nmcli', 'con', 'mod', ssid, 'connection.autoconnect', 'yes'],
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            return True
        else:
            error_msg = result.stderr.decode()
            print("[ERROR] 连接失败:", error_msg)
            return False
    except Exception as e:
        print("[ERROR] 连接异常:", e)
        return False


def scan_wifi():
    """扫描附近 Wi-Fi"""
    try:
        result = subprocess.run(
            ['sudo', 'nmcli', '-t', '-f', 'SSID', 'dev', 'wifi', 'list'],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=10
        )
        networks = []
        for line in result.stdout.decode().split('\n'):
            line = line.strip()
            if line and not line.startswith('--') and line != HOTSPOT_SSID:
                networks.append(line)
        return sorted(set(networks))
    except Exception as e:
        print("[ERROR] 扫描 Wi-Fi 失败:", e)
        return []


# ========== Flask Web 界面 ==========
HTML = '''
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Jetson Wi-Fi 配置</title>
    <style>
        body { font-family: Arial; max-width: 500px; margin: 50px auto; padding: 20px; }
        input, button { width: 100%; padding: 10px; margin: 8px 0; font-size: 16px; box-sizing: border-box; }
        button { background: #4CAF50; color: white; border: none; cursor: pointer; }
        .msg { padding: 10px; margin: 10px 0; border-radius: 4px; }
        .success { background: #d4edda; color: #155724; }
        .error { background: #f8d7da; color: #721c24; }
    </style>
</head>
<body>
    <h2>📶 Jetson Wi-Fi 配置</h2>
    <form method="POST">
        <label>Wi-Fi 名称 (SSID):</label>
        <input type="text" name="ssid" placeholder="请输入 Wi-Fi 名称" required>
        <label>密码:</label>
        <input type="password" name="password" placeholder="请输入 Wi-Fi 密码" required>
        <button type="submit">连接</button>
    </form>
    {% if msg %}
    <div class="msg {{ 'success' if success else 'error' }}">{{ msg }}</div>
    {% endif %}
</body>
</html>
'''


@app.route('/', methods=['GET', 'POST'])
def index():
    msg = ''
    success = False

    if request.method == 'POST':
        ssid = request.form.get('ssid', '').strip()
        password = request.form.get('password', '').strip()

        if not ssid:
            msg = '请输入 Wi-Fi 名称'
        else:
            stop_hotspot()
            time.sleep(2)
            
            if connect_wifi(ssid, password):
                msg = f'✅ 成功连接到 {ssid}！'
                success = True
                print(f"[INFO] 配网完成，已连接 {ssid}")
            else:
                msg = f'❌ 连接失败，请检查 SSID 或密码后重试。'
                time.sleep(2)
                start_hotspot()
                networks = scan_wifi()
                return render_template_string(HTML, networks=networks, msg=msg, success=success)

    networks = scan_wifi()
    return render_template_string(HTML, networks=networks, msg=msg, success=success)


def start_web_server():
    app.run(host='0.0.0.0', port=8080, debug=False)


# ========== 主逻辑 ==========
def main():
    global HOTSPOT_INTERFACE, HOTSPOT_IP

    print("=" * 50)
    print("Jetson Wi-Fi 配网系统启动")
    print("=" * 50)

    # 1. 等待 10 秒（修改这里）
    print("[INFO] 等待 10 秒...")
    time.sleep(10)

    # 2. 获取网卡信息
    HOTSPOT_INTERFACE = get_wifi_interface()
    HOTSPOT_IP = get_hotspot_ip()
    print(f"[INFO] 无线网卡: {HOTSPOT_INTERFACE}")
    print(f"[INFO] 热点网关: {HOTSPOT_IP}")

    # 3. 检测是否已连接 Wi-Fi
    if is_wifi_connected():
        print("[INFO] 已连接 Wi-Fi，跳过配网")
        result = subprocess.run(['nmcli', '-t', '-f', 'ACTIVE,SSID', 'con', 'show', '--active'],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        for line in result.stdout.decode().split('\n'):
            if line.startswith('yes:'):
                ssid = line.split(':', 1)[1]
                print(f"[INFO] 当前 Wi-Fi: {ssid}")
                break
        return

    # 4. 未连接，开启 AP
    print("[INFO] 未连接 Wi-Fi，启动 AP 模式...")
    if not start_hotspot():
        print("[ERROR] AP 启动失败，退出")
        return

    print(f"[INFO] 请用手机连接热点 {HOTSPOT_SSID}（密码 {HOTSPOT_PASSWORD}）")
    print(f"[INFO] 打开浏览器访问 http://{HOTSPOT_IP}:8080")
    print("=" * 50)

    # 5. 启动 Web 服务
    start_web_server()


if __name__ == '__main__':
    main()