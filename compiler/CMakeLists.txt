cmake_minimum_required(VERSION 3.16)

# QBIN Compiler (OpenQASM -> QBIN) - MVP standalone build
project(qbin-compiler LANGUAGES CXX)

# ---- Options ----
option(QBIN_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(QBIN_ENABLE_LTO "Enable link-time optimization if supported" ON)
option(QBIN_USE_LIBQBIN "Link against qbin::qbin if available" OFF)

# ---- C++ Standard ----
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- Sources ----
set(QBIN_COMPILER_SOURCES
  src/main.cpp
  src/compiler.cpp
  src/qasm_frontend.cpp
)

set(QBIN_COMPILER_HEADERS
  include/qbin_compiler/compiler.hpp
  include/qbin_compiler/qasm_frontend.hpp
)

add_executable(qbin-compile ${QBIN_COMPILER_SOURCES} ${QBIN_COMPILER_HEADERS})

target_include_directories(qbin-compile
  PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/include
)

# ---- Optional libqbin linkage (default OFF) ----
if(QBIN_USE_LIBQBIN)
  if(TARGET qbin::qbin)
    target_link_libraries(qbin-compile PRIVATE qbin::qbin)
  else()
    message(WARNING "QBIN_USE_LIBQBIN=ON but target qbin::qbin not found. Skipping linkage.")
  endif()
endif()

# ---- Warnings ----
if(MSVC)
  target_compile_options(qbin-compile PRIVATE /W4 $<$<BOOL:${QBIN_WARNINGS_AS_ERRORS}>:/WX>)
else()
  target_compile_options(qbin-compile PRIVATE -Wall -Wextra -Wpedantic $<$<BOOL:${QBIN_WARNINGS_AS_ERRORS}>:-Werror>)
endif()

# ---- LTO ----
include(CheckIPOSupported)
if(QBIN_ENABLE_LTO)
  check_ipo_supported(RESULT IPO_SUPPORTED OUTPUT IPO_MSG)
  if(IPO_SUPPORTED)
    set_property(TARGET qbin-compile PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
  else()
    message(STATUS "IPO/LTO not supported: ${IPO_MSG}")
  endif()
endif()

# ---- RPATH (for install on UNIX) ----
if(UNIX AND NOT APPLE)
  set_target_properties(qbin-compile PROPERTIES
    BUILD_WITH_INSTALL_RPATH OFF
    INSTALL_RPATH "$ORIGIN/../lib"
  )
endif()

# ---- Install ----
include(GNUInstallDirs)

install(TARGETS qbin-compile
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/qbin_compiler
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  FILES_MATCHING PATTERN "*.hpp"
)
