cmake_minimum_required(VERSION 3.16)
project(qbin LANGUAGES CXX)

# Global defaults
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(QBIN_BUILD_TESTS "Build and run QBIN round-trip tests" ON)

# Subprojects
add_subdirectory(compiler)
add_subdirectory(decompiler)

if(QBIN_BUILD_TESTS)
  enable_testing()
  # Let tests reference just-built binaries via generator expressions
  set(QBIN_COMPILE   $<TARGET_FILE:qbin-compile>   CACHE STRING "Path or generator expression for qbin-compile")
  set(QBIN_DECOMPILE $<TARGET_FILE:qbin-decompile> CACHE STRING "Path or generator expression for qbin-decompile")
  add_subdirectory(tests)
endif()
