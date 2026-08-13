# generated from rosidl_cmake/cmake/rosidl_cmake_aggregate_target-extras.cmake.in

# Create a convenience aggregate target interfaces::interfaces
# that links all generated interface targets, so downstream packages can use
# a single modern CMake target name instead of ${interfaces_TARGETS}.
if(interfaces_TARGETS AND NOT TARGET interfaces::interfaces)
  add_library(interfaces::interfaces INTERFACE IMPORTED)
  set_target_properties(interfaces::interfaces PROPERTIES
    INTERFACE_LINK_LIBRARIES "${interfaces_TARGETS}")
endif()
