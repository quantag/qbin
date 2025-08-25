cmake_minimum_required(VERSION 3.15)
project(qbin_roundtrip_tests)
enable_testing()

# You can pass paths via env vars or -D cache entries:
#   cmake -DQBIN_COMPILE=/path/to/qbin-compile -DQBIN_DECOMPILE=/path/to/qbin-decompile ..
# or
#   export QBIN_COMPILE=... ; export QBIN_DECOMPILE=... ; cmake ..

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(QBIN_COMPILE $ENV{QBIN_COMPILE} CACHE FILEPATH "Path to qbin-compile executable")
set(QBIN_DECOMPILE $ENV{QBIN_DECOMPILE} CACHE FILEPATH "Path to qbin-decompile executable")

if(NOT QBIN_COMPILE OR NOT EXISTS "${QBIN_COMPILE}")
  message(FATAL_ERROR "QBIN_COMPILE not set or not found. Set -DQBIN_COMPILE=/path/to/qbin-compile or export QBIN_COMPILE.")
endif()
if(NOT QBIN_DECOMPILE OR NOT EXISTS "${QBIN_DECOMPILE}")
  message(FATAL_ERROR "QBIN_DECOMPILE not set or not found. Set -DQBIN_DECOMPILE=/path/to/qbin-decompile or export QBIN_DECOMPILE.")
endif()

set(TEST_DATA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/data)
set(RUNNER ${CMAKE_CURRENT_SOURCE_DIR}/roundtrip.py)

function(add_qasm_roundtrip name qasm_path)
  add_test(
    NAME roundtrip_${name}
    COMMAND ${Python3_EXECUTABLE} ${RUNNER}
            --compiler "${QBIN_COMPILE}"
            --decompiler "${QBIN_DECOMPILE}"
            --qasm "${qasm_path}"
            --workdir "${CMAKE_BINARY_DIR}/rt_${name}"
            --exact
  )
endfunction()

# Add all .qasm in tests/data automatically
file(GLOB QASM_FILES "${TEST_DATA_DIR}/*.qasm")
if(QASM_FILES)
  foreach(f ${QASM_FILES})
    get_filename_component(n "${f}" NAME_WE)
    add_qasm_roundtrip(${n} "${f}")
  endforeach()
else()
  message(WARNING "No .qasm files found in ${TEST_DATA_DIR}")
endif()
