cmake_minimum_required(VERSION 3.8)
project(drivers)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============ 定义宏，解决 serial.h 问题 ============
add_compile_definitions(HAVE_STDINT_H)

# ============ 使用 apt 安装的 libcxx-serial ============
set(SERIAL_INCLUDE_DIR /usr/include)
set(SERIAL_LIBRARY /usr/lib/aarch64-linux-gnu/libcxx-serial.so)

message(STATUS "SERIAL_INCLUDE_DIR: ${SERIAL_INCLUDE_DIR}")
message(STATUS "SERIAL_LIBRARY: ${SERIAL_LIBRARY}")

# ============ 编译你的库 ============
add_library(${PROJECT_NAME}_core
  src/drivers/controller_board/controller_comm.cpp
  src/drivers/controller_board/controller_board.cpp
)

target_include_directories(${PROJECT_NAME}_core
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include/${PROJECT_NAME}>
    ${SERIAL_INCLUDE_DIR}
)

target_link_libraries(${PROJECT_NAME}_core
  ${SERIAL_LIBRARY}
  pthread
)

# ============ 编译测试程序 ============
add_executable(test_controller
  test.cpp
)

target_link_libraries(test_controller
  ${PROJECT_NAME}_core
)

target_include_directories(test_controller
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
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
  TARGETS test_controller
  DESTINATION lib/${PROJECT_NAME}
)

install(
  DIRECTORY include/
  DESTINATION include/${PROJECT_NAME}
)

ament_export_include_directories(include)
ament_export_libraries(${PROJECT_NAME}_core)

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  set(ament_cmake_copyright_FOUND TRUE)
  set(ament_cmake_cpplint_FOUND TRUE)
  ament_lint_auto_find_test_dependencies()
endif()

ament_package()