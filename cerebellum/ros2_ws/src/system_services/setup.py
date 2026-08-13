from setuptools import find_packages, setup
import os
from glob import glob

package_name = 'system_services'

setup(
    name=package_name,
    version='0.0.0',
    packages=find_packages(exclude=['test']),
    data_files=[
        ('share/ament_index/resource_index/packages',
            ['resource/' + package_name]),
        ('share/' + package_name, ['package.xml']),
        # 安装 scripts 目录中的所有 Python 脚本
        (os.path.join('share', package_name, 'scripts'),
            glob('system_services/scripts/*.py')),
    ],
    install_requires=['setuptools'],
    zip_safe=True,
    maintainer='root',
    maintainer_email='3357697374@qq.com',
    description='System services: calibration, diagnostics, tools',
    license='Apache-2.0',
    tests_require=['pytest'],
    entry_points={
        'console_scripts': [
            'imu_calibrator = system_services.scripts.imu_calibrator:main',
        ],
    },
)