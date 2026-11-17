cmake_minimum_required(VERSION 3.24...4.3)

project(MPMCQueue
  VERSION 1.0.0
  HOMEPAGE_URL "https://github.com/rigtorp/MPMCQueue"
  LANGUAGES CXX
)

# -----------------------------------------------------------------------------
# Library Target
# -----------------------------------------------------------------------------
add_library(${PROJECT_NAME} INTERFACE)
add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

target_compile_features(${PROJECT_NAME} INTERFACE cxx_std_20)
target_sources(${PROJECT_NAME} INTERFACE
  FILE_SET HEADERS
  BASE_DIRS include
  FILES include/rigtorp/MPMCQueue.h
)

# -----------------------------------------------------------------------------
# Tests & Examples (Top-Level Only)
# -----------------------------------------------------------------------------
include(CTest)

if(PROJECT_IS_TOP_LEVEL AND BUILD_TESTING)
  find_package(Threads REQUIRED)

  function(mpmcqueue_executable target source)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE
      ${PROJECT_NAME}::${PROJECT_NAME}
      Threads::Threads
    )
    set_target_properties(${target} PROPERTIES
      CXX_EXTENSIONS OFF
      COMPILE_WARNING_AS_ERROR ON
    )
    target_compile_options(${target} PRIVATE
      "$<$<CXX_COMPILER_ID:MSVC>:/permissive-;/W4;/wd4324;/w14242>"
      "$<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic;-Wshadow;-Wconversion;-Wsign-conversion;-Wold-style-cast;-Wundef;-Wcast-align;-Wzero-as-null-pointer-constant;-Wformat=2>"
    )
  endfunction()

  mpmcqueue_executable(MPMCQueueExample src/MPMCQueueExample.cpp)
  mpmcqueue_executable(MPMCQueueTest src/MPMCQueueTest.cpp)
  add_test(NAME MPMCQueueTest COMMAND MPMCQueueTest)
endif()

# -----------------------------------------------------------------------------
# Installation & Package Export
# -----------------------------------------------------------------------------
if(PROJECT_IS_TOP_LEVEL)
  include(GNUInstallDirs)
  include(CMakePackageConfigHelpers)

  export(
    TARGETS ${PROJECT_NAME}
    NAMESPACE ${PROJECT_NAME}::
    FILE "${PROJECT_NAME}Config.cmake"
  )

  write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake"
    COMPATIBILITY SameMajorVersion
    ARCH_INDEPENDENT
  )

  install(TARGETS ${PROJECT_NAME} EXPORT ${PROJECT_NAME}Config FILE_SET HEADERS)

  install(
    EXPORT ${PROJECT_NAME}Config
    NAMESPACE ${PROJECT_NAME}::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}"
  )

  install(
    FILES "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}"
  )
endif()
