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

# ============ imu_tools 库 ============
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

# ============ imu_tools_node ============
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

# ============ 安装 ============
install(
  TARGETS imu_tools
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
)

# 添加 imu_tools_node 的安装
install(
  TARGETS imu_tools_node
  DESTINATION lib/${PROJECT_NAME}
)

install(
  DIRECTORY include/
  DESTINATION include/${PROJECT_NAME}
)

ament_export_include_directories(include)
ament_export_libraries(imu_tools)

ament_package()