cmake_minimum_required(VERSION 3.8)
project(drivers)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============ 查找 yaml-cpp ============
# drivers 间接依赖 yaml-cpp（通过 common_config）
# 需要显式查找并链接，否则会出现 YAML 相关的未定义引用错误
find_package(yaml-cpp REQUIRED)

# ============ 定义宏，解决 serial.h 问题 ============
# serial.h 中条件编译需要 HAVE_STDINT_H 宏
# 定义后使用 <stdint.h> 而非 <serial/v8stdint.h>
add_compile_definitions(HAVE_STDINT_H)

# ============ 串口库配置 ============
# 使用 apt 安装的 libcxx-serial（wjwwood/serial 的 Debian 打包版本）
# 头文件位于 /usr/include，库位于 /usr/lib/aarch64-linux-gnu/
set(SERIAL_INCLUDE_DIR /usr/include)
set(SERIAL_LIBRARY /usr/lib/aarch64-linux-gnu/libcxx-serial.so)

# ============ common 包路径 ============
# 使用相对路径，保证在不同工作区下的移植性
# common 包位于 drivers 的上一级目录
set(COMMON_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../common/include")
set(COMMON_LIB "${CMAKE_CURRENT_SOURCE_DIR}/../../install/common/lib/libcommon_config.a")

message(STATUS "COMMON_INCLUDE_DIR: ${COMMON_INCLUDE_DIR}")
message(STATUS "COMMON_LIB: ${COMMON_LIB}")

# ============ 编译驱动库 ============
add_library(${PROJECT_NAME}_core
  # Controller Board
  src/drivers/controller_board/controller_comm.cpp
  src/drivers/controller_board/controller_board.cpp
  # BMS
  src/drivers/bms/bms.cpp
  # IMU
  src/drivers/imu/imu.cpp
  # Motor
  src/drivers/motor/motor.cpp
)

# 头文件搜索路径
target_include_directories(${PROJECT_NAME}_core
  PUBLIC
    # 本包头文件
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include/${PROJECT_NAME}>
    # 串口库头文件
    ${SERIAL_INCLUDE_DIR}
    # common 包头文件
    ${COMMON_INCLUDE_DIR}
)

# 链接依赖库
target_link_libraries(${PROJECT_NAME}_core
  ${SERIAL_LIBRARY}   # 串口库
  pthread             # 线程库（std::thread）
  ${COMMON_LIB}       # common 配置库
  yaml-cpp            # YAML 解析库（common_config 的依赖）
)

# ============ 编译测试程序 ============
add_executable(test_controller
  test.cpp
)

target_include_directories(test_controller
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${COMMON_INCLUDE_DIR}
)

target_link_libraries(test_controller
  ${PROJECT_NAME}_core
)

# ============ 安装 ============
# 安装驱动库
install(
  TARGETS ${PROJECT_NAME}_core
  EXPORT ${PROJECT_NAME}_export
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)

# 安装测试程序
install(
  TARGETS test_controller
  DESTINATION lib/${PROJECT_NAME}
)

# 安装头文件
install(
  DIRECTORY include/
  DESTINATION include/${PROJECT_NAME}
)

# 导出依赖信息，供其他包使用
ament_export_include_directories(include)
ament_export_libraries(${PROJECT_NAME}_core)

# ============ 测试 ============
if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  set(ament_cmake_copyright_FOUND TRUE)
  set(ament_cmake_cpplint_FOUND TRUE)
  ament_lint_auto_find_test_dependencies()
endif()

ament_package()