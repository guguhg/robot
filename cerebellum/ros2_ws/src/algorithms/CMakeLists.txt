cmake_minimum_required(VERSION 3.8)
project(algorithms)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(std_msgs REQUIRED)
find_package(std_srvs REQUIRED) 
find_package(interfaces REQUIRED)

# ============ common 依赖 ============
find_package(common REQUIRED)

# ============ Eigen3 ============
find_package(PkgConfig REQUIRED)
pkg_check_modules(EIGEN3 REQUIRED eigen3)

# ============ yaml-cpp 和 serial ============
pkg_check_modules(YAML_CPP REQUIRED yaml-cpp)
pkg_check_modules(SERIAL REQUIRED cxx-serial)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============ 查找 common 库 ============
set(COMMON_LIB "${CMAKE_CURRENT_SOURCE_DIR}/../../install/common/lib/libcommon_config.a")
set(COMMON_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/../common/include")

# ============================================================
# 1. IMU Tools 库 - 姿态解算核心
# ============================================================
add_library(imu_tools
  src/imu_tools.cpp
)

target_include_directories(imu_tools
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include/${PROJECT_NAME}>
    ${EIGEN3_INCLUDE_DIRS}
    ${COMMON_INCLUDE}
    ${YAML_CPP_INCLUDE_DIRS}
    ${SERIAL_INCLUDE_DIRS}
)

target_link_libraries(imu_tools
  ${EIGEN3_LIBRARIES}
  ${YAML_CPP_LIBRARIES}
  ${SERIAL_LIBRARIES}
  ${COMMON_LIB}
  pthread
)

# ============================================================
# 2. IMU Tools 节点 - 发布 /imu/data
# ============================================================
add_executable(imu_tools_node
  src/nodes/imu_tools_node.cpp
)

target_include_directories(imu_tools_node
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${EIGEN3_INCLUDE_DIRS}
    ${COMMON_INCLUDE}
    ${YAML_CPP_INCLUDE_DIRS}
    ${SERIAL_INCLUDE_DIRS}
)

ament_target_dependencies(imu_tools_node
  rclcpp
  sensor_msgs
  geometry_msgs
  std_srvs  
)

target_link_libraries(imu_tools_node
  imu_tools
  ${COMMON_LIB}
  ${YAML_CPP_LIBRARIES}
  ${SERIAL_LIBRARIES}
  pthread
)

# ============================================================
# 3. Twist Handler 库 - 速度限幅 + 平滑滤波 + 超时保护
# ============================================================
add_library(twist_handler
  src/twist_handler.cpp
)

target_include_directories(twist_handler
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include/${PROJECT_NAME}>
    ${EIGEN3_INCLUDE_DIRS}
)

target_link_libraries(twist_handler
  ${EIGEN3_LIBRARIES}
)

# ============================================================
# 4. Twist 节点 - 订阅 /cmd_vel → 发布 /cmd_vel_limited
# ============================================================
add_executable(twist_node
  src/nodes/twist_node.cpp
)

target_include_directories(twist_node
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${EIGEN3_INCLUDE_DIRS}
    ${COMMON_INCLUDE}
    ${YAML_CPP_INCLUDE_DIRS}
)

ament_target_dependencies(twist_node
  rclcpp
  geometry_msgs
)

target_link_libraries(twist_node
  twist_handler
  ${COMMON_LIB}
  ${YAML_CPP_LIBRARIES}
  pthread
)

# ============================================================
# 5. Attitude Compensator 库 - IMU 姿态补偿（坡道重力补偿）
# ============================================================
add_library(attitude_compensator
  src/attitude_compensator.cpp
)

target_include_directories(attitude_compensator
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include/${PROJECT_NAME}>
    ${EIGEN3_INCLUDE_DIRS}
)

target_link_libraries(attitude_compensator
  ${EIGEN3_LIBRARIES}
)

# ============================================================
# 6. Attitude Compensator 节点 - 订阅 /cmd_vel_limited + /imu/data
#                                    发布 /cmd_vel_compensated
# ============================================================
add_executable(attitude_comp_node
  src/nodes/attitude_comp_node.cpp
)

target_include_directories(attitude_comp_node
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${EIGEN3_INCLUDE_DIRS}
    ${COMMON_INCLUDE}
    ${YAML_CPP_INCLUDE_DIRS}
)

ament_target_dependencies(attitude_comp_node
  rclcpp
  geometry_msgs
  sensor_msgs
)

target_link_libraries(attitude_comp_node
  attitude_compensator
  ${COMMON_LIB}
  ${YAML_CPP_LIBRARIES}
  pthread
)

# ============================================================
# 7. Inverse Kinematics 库 - 逆运动学（麦轮/差速/普通四轮）
# ============================================================
add_library(inverse_kinematics
  src/inverse_kinematics.cpp
)

target_include_directories(inverse_kinematics
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include/${PROJECT_NAME}>
    ${EIGEN3_INCLUDE_DIRS}
)

target_link_libraries(inverse_kinematics
  ${EIGEN3_LIBRARIES}
)

# ============================================================
# 8. IK 节点 - 订阅 /cmd_vel_compensated → 发布 /chassis/motor_cmd
# ============================================================
add_executable(ik_node
  src/nodes/ik_node.cpp
)

target_include_directories(ik_node
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${EIGEN3_INCLUDE_DIRS}
    ${COMMON_INCLUDE}
    ${YAML_CPP_INCLUDE_DIRS}
)

ament_target_dependencies(ik_node
  rclcpp
  geometry_msgs
  interfaces
)

target_link_libraries(ik_node
  inverse_kinematics
  ${COMMON_LIB}
  ${YAML_CPP_LIBRARIES}
  pthread
)

# ============================================================
# 安装
# ============================================================
install(
  TARGETS imu_tools
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
)

install(
  TARGETS imu_tools_node
  DESTINATION lib/${PROJECT_NAME}
)

install(
  TARGETS twist_node
  DESTINATION lib/${PROJECT_NAME}
)

install(
  TARGETS attitude_comp_node
  DESTINATION lib/${PROJECT_NAME}
)

install(
  TARGETS ik_node
  DESTINATION lib/${PROJECT_NAME}
)

install(
  DIRECTORY include/
  DESTINATION include/${PROJECT_NAME}
)

ament_export_include_directories(include)

ament_package()