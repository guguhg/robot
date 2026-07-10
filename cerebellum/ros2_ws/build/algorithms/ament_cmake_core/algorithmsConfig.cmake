# generated from ament/cmake/core/templates/nameConfig.cmake.in

# prevent multiple inclusion
if(_algorithms_CONFIG_INCLUDED)
  # ensure to keep the found flag the same
  if(NOT DEFINED algorithms_FOUND)
    # explicitly set it to FALSE, otherwise CMake will set it to TRUE
    set(algorithms_FOUND FALSE)
  elseif(NOT algorithms_FOUND)
    # use separate condition to avoid uninitialized variable warning
    set(algorithms_FOUND FALSE)
  endif()
  return()
endif()
set(_algorithms_CONFIG_INCLUDED TRUE)

# output package information
if(NOT algorithms_FIND_QUIETLY)
  message(STATUS "Found algorithms: 0.0.0 (${algorithms_DIR})")
endif()

# warn when using a deprecated package
if(NOT "" STREQUAL "")
  set(_msg "Package 'algorithms' is deprecated")
  # append custom deprecation text if available
  if(NOT "" STREQUAL "TRUE")
    set(_msg "${_msg} ()")
  endif()
  # optionally quiet the deprecation message
  if(NOT ${algorithms_DEPRECATED_QUIET})
    message(DEPRECATION "${_msg}")
  endif()
endif()

# flag package as ament-based to distinguish it after being find_package()-ed
set(algorithms_FOUND_AMENT_PACKAGE TRUE)

# include all config extra files
set(_extras "ament_cmake_export_include_directories-extras.cmake;ament_cmake_export_libraries-extras.cmake")
foreach(_extra ${_extras})
  include("${algorithms_DIR}/${_extra}")
endforeach()
