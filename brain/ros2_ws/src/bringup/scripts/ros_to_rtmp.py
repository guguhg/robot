#!/usr/bin/env python3
"""
ROS 图像推流 → RTMP (带 Token 动态刷新)
"""
import rclpy
from rclpy.node import Node
from sensor_msgs.msg import Image
from cv_bridge import CvBridge
import cv2
import subprocess
import time
import threading
import requests
import json
import sys
import signal

# ---------- 配置（请修改）----------
API_BASE = "http://124.222.135.234:5122"
USERNAME = "admin"
PASSWORD = "admin123"
STREAM_NAME = "demo-01"
RTMP_BASE = f"rtmp://124.222.135.234:1935/live/{STREAM_NAME}"
TOKEN_REFRESH_INTERVAL = 100  # 秒（小于120秒过期时间）
# ------------------------------------

class RosToRtmp(Node):
    def __init__(self):
        super().__init__('ros_to_rtmp')
        self.bridge = CvBridge()
        self.sub = self.create_subscription(Image, '/aurora/rgb/image_raw', self.callback, 10)
        self.ffmpeg = None
        self.token = None
        self.token_lock = threading.Lock()
        self.running = True
        
        # 启动时获取 Token
        self.refresh_token()
        # 启动后台刷新线程
        self.thread = threading.Thread(target=self.token_refresh_loop, daemon=True)
        self.thread.start()
        # 启动 FFmpeg 推流
        self.start_ffmpeg()
        
        self.get_logger().info('✅ RTMP 推流节点已启动，Token 自动刷新')
    
    def refresh_token(self):
        """登录并获取推流 Token"""
        try:
            # ① 登录获取 JWT
            login_resp = requests.post(
                f"{API_BASE}/api/auth/login",
                json={"username": USERNAME, "password": PASSWORD},
                timeout=5
            )
            login_resp.raise_for_status()
            jwt = login_resp.json()['token']
            self.get_logger().info('✅ 登录成功，已获取 JWT')
            
            # ② 用 JWT 换取推流 Token
            token_resp = requests.get(
                f"{API_BASE}/api/devices/{STREAM_NAME}/publish-url",
                headers={"Authorization": f"Bearer {jwt}"},
                timeout=5
            )
            token_resp.raise_for_status()
            new_token = token_resp.json()['token']
            
            with self.token_lock:
                self.token = new_token
            self.get_logger().info('✅ 推流 Token 已更新')
            return True
        except Exception as e:
            self.get_logger().error(f'❌ 获取 Token 失败: {e}')
            return False
    
    def token_refresh_loop(self):
        """定时刷新 Token"""
        while self.running:
            time.sleep(TOKEN_REFRESH_INTERVAL)
            if self.refresh_token():
                # Token 更新后需要重启 FFmpeg（新 Token 生效）
                self.restart_ffmpeg()
    
    def start_ffmpeg(self):
        """启动 FFmpeg 推流进程"""
        with self.token_lock:
            token = self.token
        if not token:
            self.get_logger().error('❌ 没有有效 Token，无法推流')
            return
        
        rtmp_url = f"{RTMP_BASE}?token={token}"
        cmd = [
            'ffmpeg', '-y', '-f', 'rawvideo', '-vcodec', 'rawvideo',
            '-pix_fmt', 'bgr24', '-s', '320x200', '-r', '15',
            '-i', 'pipe:0', '-c:v', 'libx264', '-preset', 'ultrafast',
            '-tune', 'zerolatency', '-g', '15', '-f', 'flv',
            rtmp_url
        ]
        self.ffmpeg = subprocess.Popen(cmd, stdin=subprocess.PIPE)
        self.get_logger().info(f'🚀 FFmpeg 已启动，推流地址: {rtmp_url[:60]}...')
    
    def restart_ffmpeg(self):
        """重启 FFmpeg（新 Token）"""
        if self.ffmpeg:
            self.ffmpeg.terminate()
            self.ffmpeg.wait(timeout=3)
            self.get_logger().info('🔄 旧 FFmpeg 已终止，启动新进程...')
        self.start_ffmpeg()
    
    def callback(self, msg):
        """ROS 图像回调，将数据写入 FFmpeg 管道"""
        if not self.ffmpeg or self.ffmpeg.poll() is not None:
            return  # 进程已死，等待重启
        try:
            cv_img = self.bridge.imgmsg_to_cv2(msg, 'bgr8')
            self.ffmpeg.stdin.write(cv_img.tobytes())
        except (BrokenPipeError, OSError):
            self.get_logger().warn('⚠️ FFmpeg 管道已断开，等待重启...')
        except Exception as e:
            self.get_logger().warn(f'推流错误: {e}')
    
    def destroy_node(self):
        self.running = False
        if self.ffmpeg:
            self.ffmpeg.terminate()
        super().destroy_node()

def main():
    rclpy.init()
    node = RosToRtmp()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()

if __name__ == '__main__':
    main()