cmake_minimum_required(VERSION 3.16)

# QBIN Decompiler (QBIN -> OpenQASM) - MVP standalone build
project(qbin-decompiler LANGUAGES CXX)

# ---- Options ----
option(QBIN_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(QBIN_ENABLE_LTO "Enable link-time optimization if supported" ON)
option(QBIN_USE_LIBQBIN "Link against qbin::qbin if available" OFF)

# ---- C++ Standard ----
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- Sources ----
set(QBIN_DECOMPILER_SOURCES
  src/main.cpp
  src/decompiler.cpp
)

set(QBIN_DECOMPILER_HEADERS
  include/qbin_decompiler/decompiler.hpp
)

add_executable(qbin-decompile ${QBIN_DECOMPILER_SOURCES} ${QBIN_DECOMPILER_HEADERS})

target_include_directories(qbin-decompile
  PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/include
)

# ---- Optional libqbin linkage (default OFF) ----
if(QBIN_USE_LIBQBIN)
  if(TARGET qbin::qbin)
    target_link_libraries(qbin-decompile PRIVATE qbin::qbin)
  else()
    message(WARNING "QBIN_USE_LIBQBIN=ON but target qbin::qbin not found. Skipping linkage.")
  endif()
endif()

# ---- Warnings ----
if(MSVC)
  target_compile_options(qbin-decompile PRIVATE /W4 $<$<BOOL:${QBIN_WARNINGS_AS_ERRORS}>:/WX>)
else()
  target_compile_options(qbin-decompile PRIVATE -Wall -Wextra -Wpedantic $<$<BOOL:${QBIN_WARNINGS_AS_ERRORS}>:-Werror>)
endif()

# ---- LTO ----
include(CheckIPOSupported)
if(QBIN_ENABLE_LTO)
  check_ipo_supported(RESULT IPO_SUPPORTED OUTPUT IPO_MSG)
  if(IPO_SUPPORTED)
    set_property(TARGET qbin-decompile PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
  else()
    message(STATUS "IPO/LTO not supported: ${IPO_MSG}")
  endif()
endif()

# ---- RPATH (for install on UNIX) ----
if(UNIX AND NOT APPLE)
  set_target_properties(qbin-decompile PROPERTIES
    BUILD_WITH_INSTALL_RPATH OFF
    INSTALL_RPATH "$ORIGIN/../lib"
  )
endif()

# ---- Install ----
include(GNUInstallDirs)

install(TARGETS qbin-decompile
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/qbin_decompiler
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  FILES_MATCHING PATTERN "*.hpp"
)

