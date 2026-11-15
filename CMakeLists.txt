# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 zrecore-org contributors
#
# Developer build: the oracle and corpus tools, the unit tests (fast suite), the acceptance tests (need the pinned, patched Geogram pipeline) and the optional GPU parts.
# The pinned Geogram build and the pipeline CLI are built by ci/geogram_xplat.sh, not by this file (their inputs are hash-checked there).
#   third_party/fetch.sh all                       # Boost headers, Shewchuk predicates, Catch2, Vulkan headers and volk, into build/deps
#   cmake -S . -B build && cmake --build build -j && ctest --test-dir build -L fast --output-on-failure
cmake_minimum_required(VERSION 3.20)
project(exactmesh VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
# The exact predicates and the generated corpora rely on exact IEEE operations: no FMA contraction anywhere (the recorded hashes were produced this way).
add_compile_options(-ffp-contract=off)
set(CMAKE_CXX_FLAGS_RELEASE "-O2")
set(CMAKE_C_FLAGS_RELEASE "-O2")

option(EXACTMESH_TESTS "Build the unit and acceptance tests (needs Catch2 in EXACTMESH_DEPS)" ON)
option(EXACTMESH_GPU "Build the Vulkan GPU parts (needs glslc, Vulkan headers and volk)" OFF)
set(EXACTMESH_DEPS "${CMAKE_SOURCE_DIR}/build/deps" CACHE PATH "Where third_party/fetch.sh put the pinned inputs")
set(BOOST_ROOT_DIR "${EXACTMESH_DEPS}/boost_1_88_0")
set(PREDICATES_C "${EXACTMESH_DEPS}/shewchuk/predicates.c")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/mesh")

if(NOT EXISTS "${BOOST_ROOT_DIR}/boost/multiprecision/cpp_int.hpp" OR NOT EXISTS "${PREDICATES_C}")
  message(FATAL_ERROR "Missing pinned inputs in ${EXACTMESH_DEPS}: run third_party/fetch.sh boost shewchuk")
endif()

add_library(shewchuk_predicates STATIC "${PREDICATES_C}")
set_source_files_properties("${PREDICATES_C}" PROPERTIES LANGUAGE C)
target_compile_options(shewchuk_predicates PRIVATE -w)

# Tools (same sources and flags as the scripts in ci/ use).
function(exactmesh_tool name)
  add_executable(${name} ${ARGN})
  target_include_directories(${name} PRIVATE "${BOOST_ROOT_DIR}")
endfunction()
exactmesh_tool(predicates_check oracle/predicates_check.cpp)
target_link_libraries(predicates_check shewchuk_predicates)
exactmesh_tool(mesh_compare oracle/compare.cpp)
target_link_libraries(mesh_compare shewchuk_predicates)
target_compile_options(mesh_compare PRIVATE -O3 -DNDEBUG)   # as in ci/geogram_xplat.sh
exactmesh_tool(gen_corpus corpus/gen_corpus.cpp)
exactmesh_tool(corpus_check corpus/corpus_check.cpp)
exactmesh_tool(gen_touch_set3 corpus/touch/gen_touch_set3.cpp)
target_link_libraries(gen_touch_set3 shewchuk_predicates)
exactmesh_tool(gen_touch_set5 corpus/touch/gen_touch_set5.cpp)
target_link_libraries(gen_touch_set5 shewchuk_predicates)
exactmesh_tool(gen_fidelity corpus/fidelity/gen_fidelity.cpp)
exactmesh_tool(gen_fidelity2 corpus/fidelity2/gen_fidelity2.cpp)
exactmesh_tool(gen_v1 corpus/v1/gen_v1.cpp)
target_include_directories(gen_v1 PRIVATE "${CMAKE_SOURCE_DIR}/harness")
exactmesh_tool(check_v1 corpus/v1/check_v1.cpp)
target_include_directories(check_v1 PRIVATE "${CMAKE_SOURCE_DIR}/harness")

exactmesh_tool(bson_sizes research/integration/bson_sizes.cpp)
exactmesh_tool(bbox_index research/integration/bbox_index.cpp)

if(EXACTMESH_TESTS)
  enable_testing()
  add_subdirectory(tests)
  add_test(NAME mp14_bson_sizes COMMAND bson_sizes WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
  add_test(NAME mp14_bbox_index COMMAND bbox_index WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
  set_tests_properties(mp14_bson_sizes mp14_bbox_index PROPERTIES LABELS "fast")
  foreach(M 1 2 3)   # each planted mutant must be caught: the program exits 0 when it is, 3 when it is not
    add_test(NAME mp14_bson_sizes_m${M} COMMAND bson_sizes ${M} WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_test(NAME mp14_bbox_index_m${M} COMMAND bbox_index ${M} WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    set_tests_properties(mp14_bson_sizes_m${M} mp14_bbox_index_m${M} PROPERTIES LABELS "mutant")
  endforeach()
endif()
if(EXACTMESH_GPU)
  add_subdirectory(gpu)
endif()
