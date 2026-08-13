cmake_minimum_required(VERSION 3.8)
project(dri_interfaces)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(std_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(interfaces REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(YAML_CPP REQUIRED yaml-cpp)
pkg_check_modules(SERIAL REQUIRED cxx-serial)

# ============ Eigen3 ============
find_package(PkgConfig REQUIRED)
pkg_check_modules(EIGEN3 REQUIRED eigen3)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============ 查找库 ============
find_library(COMMON_LIB common_config
  PATHS ${CMAKE_CURRENT_SOURCE_DIR}/../../install/common/lib
  REQUIRED
)

find_library(DRIVERS_LIB drivers_core
  PATHS ${CMAKE_CURRENT_SOURCE_DIR}/../../install/drivers/lib
  REQUIRED
)

# ============ 小脑综合驱动节点 ============
add_executable(cerebellum_dri
  src/cerebellum_dri.cpp
  src/chassis_dri.cpp
  src/imu_dri.cpp
  src/bms_dri.cpp
)

target_include_directories(cerebellum_dri
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include/${PROJECT_NAME}>
    ${CMAKE_CURRENT_SOURCE_DIR}/../drivers/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/include
    ${YAML_CPP_INCLUDE_DIRS}
    ${SERIAL_INCLUDE_DIRS}
    ${EIGEN3_INCLUDE_DIRS}      
)

ament_target_dependencies(cerebellum_dri
  rclcpp
  sensor_msgs
  std_msgs
  interfaces
)

target_link_libraries(cerebellum_dri
  ${DRIVERS_LIB}
  ${COMMON_LIB}
  ${YAML_CPP_LIBRARIES}
  ${SERIAL_LIBRARIES}
  ${EIGEN3_LIBRARIES}           
  pthread
)

# ============ 安装 ============
install(
  TARGETS cerebellum_dri
  DESTINATION lib/${PROJECT_NAME}
)

ament_package()