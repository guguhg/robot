#!/usr/bin/env python3
"""
ROS 图像 → SRS 低延迟推流（WHIP 优先，可回退 RTMP）
改进：① 不再为刷新 token 重启 ffmpeg（SRS 仅 on_publish 时校验一次）；
      ② WHIP/UDP 替代 RTMP/TCP；③ 低延迟参数 + 帧节奏控制 + 分辨率适配。
"""
import rclpy, threading, subprocess, time
from datetime import datetime
from rclpy.node import Node
from sensor_msgs.msg import Image
from cv_bridge import CvBridge
import cv2, requests

# ---------- 配置 ----------
API_BASE  = "http://192.168.1.139:5122"
USERNAME  = "admin"
PASSWORD  = "admin123"
STREAM_NAME = "demo-01"
MODE      = "rtmp"            # "whip"(推荐，需 ffmpeg 带 webrtc 复用器) 或 "rtmp"
SRS_HOST  = "192.168.1.139"
TARGET_W, TARGET_H, FPS = 320, 200, 15
BITRATE   = "600k"
# ---------------------------

class RosStream(Node):
    def __init__(self):
        super().__init__('ros_stream')
        self.bridge = CvBridge()
        self.create_subscription(Image, '/aurora/rgb/image_raw', self.on_image, 10)
        self.ffmpeg = None
        self.token = self.url = None
        self.expires_at = 0.0
        self.lock = threading.Lock()
        self.need_restart = threading.Event()
        self.running = True
        self._last_ts = 0.0

        if not self.refresh_token():
            raise SystemExit('❌ 首次获取 token 失败')
        self.start_ffmpeg()
        threading.Thread(target=self.token_loop,   daemon=True).start()
        threading.Thread(target=self.restart_loop, daemon=True).start()
        self.get_logger().info(f'✅ 推流已启动 mode={MODE}')

    # ---- token/URL 获取（不重启 ffmpeg）----
    def refresh_token(self):
        try:
            jwt = requests.post(f"{API_BASE}/api/auth/login",
                                json={"username": USERNAME, "password": PASSWORD}, timeout=5)
            jwt.raise_for_status()
            r = requests.get(f"{API_BASE}/api/devices/{STREAM_NAME}/publish-url",
                             headers={"Authorization": f"Bearer {jwt.json()['token']}"}, timeout=5)
            r.raise_for_status()
            d = r.json()
            with self.lock:
                self.token = d['token']
                if MODE == "whip":
                    base = d.get('whipUrl') or \
                           f"http://{SRS_HOST}:1985/rtc/v1/whip/?app=live&stream={STREAM_NAME}"
                    self.url = base + ('&' if '?' in base else '?') + f'token={self.token}'
                else:
                    self.url = f"rtmp://{SRS_HOST}:1935/live/{STREAM_NAME}?token={self.token}"
                try:
                    self.expires_at = datetime.fromisoformat(d['expiresAt'].replace('Z','+00:00')).timestamp()
                except Exception:
                    self.expires_at = time.time() + 110
            return True
        except Exception as e:
            self.get_logger().error(f'获取 token 失败: {e}'); return False

    def token_loop(self):
        """过期前 60s 静默刷新（仅更新缓存，不重启）"""
        while self.running:
            time.sleep(5)
            if time.time() > self.expires_at - 60:
                self.refresh_token()

    # ---- ffmpeg ----
    def _cmd(self):
        with self.lock: url = self.url
        fmt = 'webrtc' if MODE == "whip" else 'flv'
        return ['ffmpeg','-y','-fflags','nobuffer','-flags','low_delay','-flush_packets','1',
                '-f','rawvideo','-vcodec','rawvideo','-pix_fmt','bgr24',
                '-s',f'{TARGET_W}x{TARGET_H}','-r',str(FPS),'-i','pipe:0',
                '-c:v','libx264','-preset','ultrafast','-tune','zerolatency',
                '-profile:v','baseline','-g',str(FPS),'-bf','0',
                '-b:v',BITRATE,'-pix_fmt','yuv420p','-an','-f',fmt,url]

    def start_ffmpeg(self):
        self.ffmpeg = subprocess.Popen(self._cmd(), stdin=subprocess.PIPE, bufsize=0)
        self.get_logger().info('🚀 ffmpeg 启动')

    def restart_loop(self):
        """管道断开时按退避重启（用最新 token/url）"""
        backoff = 2
        while self.running:
            if not self.need_restart.wait(timeout=2): continue
            self.need_restart.clear()
            with self.lock: p = self.ffmpeg
            if p:
                try: p.terminate(); p.wait(timeout=3)
                except Exception: pass
            self.start_ffmpeg()
            time.sleep(backoff); backoff = min(backoff*2, 30)

    # ---- 帧回调 ----
    def on_image(self, msg):
        now = time.time()
        if now - self._last_ts < 1.0/FPS - 0.002:   # 限到 FPS，多余的丢，防缓冲堆积
            return
        self._last_ts = now
        p = self.ffmpeg
        if p is None or p.poll() is not None:
            self.need_restart.set(); return
        try:
            img = self.bridge.imgmsg_to_cv2(msg, 'bgr8')
            if img.shape[1] != TARGET_W or img.shape[0] != TARGET_H:   # 分辨率适配，防花屏
                img = cv2.resize(img, (TARGET_W, TARGET_H))
            p.stdin.write(img.tobytes()); p.stdin.flush()               # 立即 flush
        except (BrokenPipeError, OSError):
            self.need_restart.set()
        except Exception as e:
            self.get_logger().warn(f'帧错误: {e}')

    def destroy_node(self):
        self.running = False; self.need_restart.set()
        if self.ffmpeg:
            try: self.ffmpeg.terminate()
            except Exception: pass
        super().destroy_node()

def main():
    rclpy.init(); node = RosStream()
    try: rclpy.spin(node)
    except KeyboardInterrupt: pass
    finally: node.destroy_node(); rclpy.shutdown()

if __name__ == '__main__': main()