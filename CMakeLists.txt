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

  option(MPMCQueue_COVERAGE "Check MC/DC coverage with gcov" OFF)
  if(MPMCQueue_COVERAGE)
    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_VERSION VERSION_LESS 14)
      message(FATAL_ERROR "MPMCQueue_COVERAGE requires GCC 14 or newer")
    endif()
    if(CMAKE_CONFIGURATION_TYPES)
      message(FATAL_ERROR "MPMCQueue_COVERAGE requires a single-config generator")
    endif()

    target_compile_options(MPMCQueueTest PRIVATE
      -O0 --coverage -fcondition-coverage -fprofile-update=atomic
    )
    target_link_options(MPMCQueueTest PRIVATE --coverage)

    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    get_filename_component(compiler_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    if(CMAKE_CXX_COMPILER MATCHES "g[+][+](-[0-9]+)$")
      set(gcov_name "gcov${CMAKE_MATCH_1}")
    else()
      set(gcov_name gcov)
    endif()
    find_program(GCOV_EXECUTABLE NAMES "${gcov_name}" HINTS "${compiler_dir}" REQUIRED)
    set(profile "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/MPMCQueueTest.dir/src/MPMCQueueTest.cpp")
    add_test(NAME MPMCQueueCoverageReset COMMAND "${CMAKE_COMMAND}" -E rm -f "${profile}.gcda")
    add_test(NAME MPMCQueueMCDC
      COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/src/check_mcdc.py"
        --gcno "${profile}.gcno"
        --gcov "${GCOV_EXECUTABLE}"
    )
    set_tests_properties(MPMCQueueCoverageReset PROPERTIES FIXTURES_SETUP queue_coverage_reset)
    set_tests_properties(MPMCQueueTest PROPERTIES
      FIXTURES_REQUIRED queue_coverage_reset
      FIXTURES_SETUP queue_coverage
    )
    set_tests_properties(MPMCQueueMCDC PROPERTIES FIXTURES_REQUIRED queue_coverage DEPENDS MPMCQueueTest)
  endif()
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
  install(FILES LICENSE DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/${PROJECT_NAME}")

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
