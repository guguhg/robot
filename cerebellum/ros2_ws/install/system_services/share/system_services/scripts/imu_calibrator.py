#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
IMU 零漂校准工具

用法:
    ros2 run system_services imu_calibrator

功能:
    采集静止数据，计算陀螺仪零漂
    打印结果，手动填入 config.yaml
"""

import sys
import time
import argparse
import numpy as np

try:
    import rclpy
    from rclpy.node import Node
    from sensor_msgs.msg import Imu
except ImportError:
    print("❌ rclpy not available")
    sys.exit(1)


def parse_args():
    parser = argparse.ArgumentParser(
        description='IMU 零漂校准工具 - 打印结果，手动填入 config.yaml'
    )
    parser.add_argument('-t', '--topic', type=str, default="/imu/data_raw")
    parser.add_argument('-s', '--samples', type=int, default=200)
    return parser.parse_args()


class IMUCalibrator(Node):
    def __init__(self, topic, samples=200):
        super().__init__("imu_calibrator")
        self.samples = samples
        self.latest_gyro = None
        self.data_received = False
        self.sub = self.create_subscription(Imu, topic, self.callback, 1)

        self.get_logger().info("="*60)
        self.get_logger().info("🔧 IMU 零漂校准工具")
        self.get_logger().info("="*60)
        self.get_logger().info(f"📡 订阅话题: {topic}")
        self.get_logger().info(f"📊 采样数: {samples}")
        self.get_logger().info("")
        self.get_logger().info("⏳ 等待 IMU 数据...")

    def callback(self, msg):
        self.latest_gyro = (msg.angular_velocity.x, msg.angular_velocity.y, msg.angular_velocity.z)
        self.data_received = True

    def wait_for_data(self, timeout=5.0):
        start = time.time()
        while not self.data_received and (time.time() - start) < timeout:
            rclpy.spin_once(self, timeout_sec=0.1)
        return self.data_received

    def collect_bias(self):
        self.get_logger().info("")
        self.get_logger().info("="*60)
        self.get_logger().info("📊 采集陀螺仪零漂")
        self.get_logger().info("="*60)
        self.get_logger().info("请将小车水平静止放置，按 Enter 开始采集...")
        input()

        self.get_logger().info(f"   采集 {self.samples} 个样本...")

        samples = []
        for i in range(self.samples):
            rclpy.spin_once(self, timeout_sec=0.01)
            if self.latest_gyro is not None:
                samples.append(self.latest_gyro)
            time.sleep(0.01)

        if len(samples) < self.samples * 0.5:
            self.get_logger().error(f"❌ 采样不足")
            return None

        avg = np.mean(samples, axis=0)
        std = np.std(samples, axis=0)

        self.get_logger().info("")
        self.get_logger().info("="*60)
        self.get_logger().info("📋 校准结果")
        self.get_logger().info("="*60)
        self.get_logger().info("")
        self.get_logger().info(f"  平均零漂 (rad/s):")
        self.get_logger().info(f"    x: {avg[0]:.8f}")
        self.get_logger().info(f"    y: {avg[1]:.8f}")
        self.get_logger().info(f"    z: {avg[2]:.8f}")
        self.get_logger().info("")
        self.get_logger().info(f"  标准差:")
        self.get_logger().info(f"    x: {std[0]:.8f}")
        self.get_logger().info(f"    y: {std[1]:.8f}")
        self.get_logger().info(f"    z: {std[2]:.8f}")
        self.get_logger().info("")
        self.get_logger().info("="*60)
        self.get_logger().info("✏️  请将以下值填入 config.yaml:")
        self.get_logger().info("")
        self.get_logger().info("  bias:")
        self.get_logger().info(f"    - {avg[0]:.8f}")
        self.get_logger().info(f"    - {avg[1]:.8f}")
        self.get_logger().info(f"    - {avg[2]:.8f}")
        self.get_logger().info("")
        self.get_logger().info("="*60)

        return avg

    def run(self):
        if not self.wait_for_data(timeout=5.0):
            self.get_logger().error("❌ 未收到 IMU 数据")
            return False

        self.get_logger().info("✅ IMU 数据已接收")
        self.collect_bias()
        return True


def main():
    args = parse_args()
    rclpy.init()
    calibrator = IMUCalibrator(args.topic, args.samples)
    try:
        calibrator.run()
    except KeyboardInterrupt:
        pass
    finally:
        rclpy.shutdown()


if __name__ == "__main__":
    main()