cmake_minimum_required(VERSION 3.15)
project(qbin_roundtrip_tests)
enable_testing()

find_package(Python3 REQUIRED COMPONENTS Interpreter)

# Paths may be absolute, or generator expressions like $<TARGET_FILE:...>
set(QBIN_COMPILE   "${QBIN_COMPILE}"   CACHE STRING "Path or generator expression for qbin-compile")
set(QBIN_DECOMPILE "${QBIN_DECOMPILE}" CACHE STRING "Path or generator expression for qbin-decompile")

if(NOT QBIN_COMPILE)
  message(FATAL_ERROR "QBIN_COMPILE not set (expected path or generator expression).")
endif()
if(NOT QBIN_DECOMPILE)
  message(FATAL_ERROR "QBIN_DECOMPILE not set (expected path or generator expression).")
endif()

set(TEST_DATA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/data)
set(RUNNER ${CMAKE_CURRENT_SOURCE_DIR}/roundtrip.py)

function(add_qasm_roundtrip name qasm_path)
  add_test(
    NAME roundtrip_${name}
    COMMAND ${Python3_EXECUTABLE} ${RUNNER}
            --compiler ${QBIN_COMPILE}
            --decompiler ${QBIN_DECOMPILE}
            --qasm "${qasm_path}"
            --workdir "${CMAKE_BINARY_DIR}/rt_${name}"
            --exact
  )
endfunction()

file(GLOB QASM_FILES "${TEST_DATA_DIR}/*.qasm")
if(QASM_FILES)
  foreach(f ${QASM_FILES})
    get_filename_component(n "${f}" NAME_WE)
    add_qasm_roundtrip(${n} "${f}")
  endforeach()
else()
  message(WARNING "No .qasm files found in ${TEST_DATA_DIR}")
endif()
