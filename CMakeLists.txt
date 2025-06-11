# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)


# ---
# 1) Collect all source files in your component
file(GLOB_RECURSE ALL_SRCS CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_LIST_DIR}/*.c"
     "${CMAKE_CURRENT_LIST_DIR}/*.cpp")

set(FORBIDDEN_CALLS "")

# 2) For each file except ringbuf_flash.c, search for flash APIs
foreach(src ${ALL_SRCS})
  get_filename_component(fname ${src} NAME)
  if(NOT fname STREQUAL "ringbuf_flash.c")
    file(READ ${src} content)
    string(REGEX MATCHALL
      "esp_partition_write\\s*\\(|esp_partition_erase_range\\s*\\(|esp_flash_write\\s*\\(|esp_partition_find_first\\s*\\("
      calls
      "${content}"
    )
    if(calls)
      list(APPEND FORBIDDEN_CALLS "${src}")
    endif()
  endif()
endforeach()

# 3) If we found any, stop the build
if(FORBIDDEN_CALLS)
  message(FATAL_ERROR
    "\n🚫 Forbidden flash-write calls detected in:\n"
    "   ${FORBIDDEN_CALLS}\n"
    "Only ringbuf_flash.c may call the flash APIs."
  )
endif()
# --- 


include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(secure_log)
