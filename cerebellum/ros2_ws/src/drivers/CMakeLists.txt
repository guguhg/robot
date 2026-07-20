cmake_minimum_required(VERSION 3.8)
project(drivers)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic -fPIC)
endif()

find_package(ament_cmake REQUIRED)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============ 查找依赖 ============
find_package(PkgConfig REQUIRED)
pkg_check_modules(YAML_CPP REQUIRED yaml-cpp)
pkg_check_modules(SERIAL REQUIRED cxx-serial)

# ============ 查找 common 包 ============
find_package(common REQUIRED)

# ============ 定义宏，解决 serial.h 问题 ============
add_compile_definitions(HAVE_STDINT_H)

# ============ 编译驱动库 ============
add_library(${PROJECT_NAME}_core
  src/drivers/controller_board/controller_comm.cpp
  src/drivers/controller_board/controller_board.cpp
  src/drivers/bms/bms.cpp
  src/drivers/imu/imu.cpp
  src/drivers/motor/motor.cpp
)

target_include_directories(${PROJECT_NAME}_core
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include/${PROJECT_NAME}>
    ${SERIAL_INCLUDE_DIRS}
    ${common_INCLUDE_DIRS}
    ${YAML_CPP_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME}_core
  ${common_LIBRARIES}
  ${SERIAL_LIBRARIES}
  ${YAML_CPP_LIBRARIES}
  pthread
)

# ============ 编译测试程序 ============
add_executable(test_controller
  test.cpp
)

target_include_directories(test_controller
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${common_INCLUDE_DIRS}
)

target_link_libraries(test_controller
  ${PROJECT_NAME}_core
)

# ============ 安装 ============
install(
  TARGETS ${PROJECT_NAME}_core
  EXPORT ${PROJECT_NAME}_export
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)

install(
  DIRECTORY include/${PROJECT_NAME}/
  DESTINATION include/${PROJECT_NAME}
)

ament_export_include_directories(include)
ament_export_libraries(${PROJECT_NAME}_core)
ament_export_dependencies(common)

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  ament_lint_auto_find_test_dependencies()
endif()

ament_package()