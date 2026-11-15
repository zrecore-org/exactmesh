# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 zrecore-org contributors
#
# GPU parts (optional): the exact-sign orient3d filter in soft binary64 and the deterministic LBVH with box-pair emission, with their host programs.
# Needs glslc and the Vulkan headers + volk that third_party/fetch.sh vulkan puts into EXACTMESH_DEPS (or a Vulkan SDK via -DVULKAN_INCLUDE_DIR / -DVOLK_DIR).
# The host programs find their SPIR-V in the directory CMake compiled it into (override: EXACTMESH_SHADER_DIR):
#   build/mesh/mp09_orient_filter device 400000      build/mesh/mp10_lbvh device 0.1      (tools/run_gpu.sh sets the Vulkan driver)
find_program(GLSLC glslc HINTS "$ENV{HOME}/VulkanSDK/1.3.283.0/macOS/bin" REQUIRED)
set(VULKAN_INCLUDE_DIR "${EXACTMESH_DEPS}/Vulkan-Headers-1.3.283/include" CACHE PATH "Vulkan headers")
set(VOLK_DIR "${EXACTMESH_DEPS}/volk-vulkan-sdk-1.3.283.0" CACHE PATH "Directory holding volk.h and volk.c")
if(NOT EXISTS "${VOLK_DIR}/volk.c" OR NOT EXISTS "${VULKAN_INCLUDE_DIR}/vulkan/vulkan.h")
  message(FATAL_ERROR "Vulkan headers / volk not found: run third_party/fetch.sh vulkan")
endif()
set(SHADER_OUT "${CMAKE_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${SHADER_OUT}")

add_library(volk STATIC "${VOLK_DIR}/volk.c")
target_include_directories(volk PUBLIC "${VOLK_DIR}" "${VULKAN_INCLUDE_DIR}")
target_compile_definitions(volk PUBLIC VK_NO_PROTOTYPES)
target_compile_options(volk PRIVATE -w)

set(GPU_SRC "${CMAKE_CURRENT_SOURCE_DIR}")
set(SPVS "")
function(add_spv out_name src)   # extra args: glslc flags
  add_custom_command(OUTPUT "${SHADER_OUT}/${out_name}.spv"
    COMMAND "${GLSLC}" --target-env=vulkan1.2 -O0 ${ARGN} -I "${GPU_SRC}/shaders" -I "${GPU_SRC}" -o "${SHADER_OUT}/${out_name}.spv" "${src}"
    DEPENDS "${src}" "${GPU_SRC}/shaders/f64soft.inc" "${GPU_SRC}/orient_filter.inc" "${GPU_SRC}/lbvh_common.glsl" "${GPU_SRC}/pip_filter.inc" "${GPU_SRC}/pip_diag.inc" VERBATIM)
  set(SPVS ${SPVS} "${SHADER_OUT}/${out_name}.spv" PARENT_SCOPE)
endfunction()
add_spv(orient_filter "${GPU_SRC}/orient_filter.comp")
add_spv(tri_tri_filter "${GPU_SRC}/tri_tri_filter.comp")
# planted mutants of the MP11 triangle filter (mp11_hybrid device m2|m3|m4); m1 (MAYBE treated as NO) is a host-side mutant
foreach(M 2 3 4)
  add_spv(tri_tri_filter_m${M} "${GPU_SRC}/tri_tri_filter.comp" -DTTMUT=${M})
endforeach()
add_spv(pip_ray "${GPU_SRC}/pip_ray.comp")
add_spv(pip_bvh "${GPU_SRC}/pip_bvh.comp")          # MP12c: point-in-solid through the LBVH
add_spv(pip_diag "${GPU_SRC}/pip_diag.comp")
add_spv(f64_check "${GPU_SRC}/f64_check.comp")
add_spv(pip_bvh_s2 "${GPU_SRC}/pip_bvh.comp" -DSTK=2)
add_spv(pip_bvh_b1 "${GPU_SRC}/pip_bvh.comp" -DBMUT=1)
add_spv(pip_bvh_b2 "${GPU_SRC}/pip_bvh.comp" -DBMUT=2 -DSTK=4)     # overflow needs a small stack to happen at all
add_spv(pip_bvh_b5 "${GPU_SRC}/pip_bvh.comp" -DBMUT=5)
foreach(M 1 2 3 4 5)
  add_spv(pip_bvh_p${M} "${GPU_SRC}/pip_bvh.comp" -DPMUT=${M})
endforeach()
add_spv(volume_reduce "${GPU_SRC}/volume_reduce.comp")
foreach(M 1 2 3 4)
  add_spv(volume_reduce_m${M} "${GPU_SRC}/volume_reduce.comp" -DVMUT=${M})
endforeach()
foreach(M 1 2 3 4 5)
  add_spv(pip_ray_m${M} "${GPU_SRC}/pip_ray.comp" -DPMUT=${M})
endforeach()
foreach(S lbvh_morton lbvh_build lbvh_fit lbvh_query)
  add_spv(${S} "${GPU_SRC}/${S}.comp")
endforeach()
foreach(S sort_count sort_scan sort_scatter)       # the radix sort kernels the LBVH build uses
  add_spv(${S} "${GPU_SRC}/shaders/${S}.comp")
endforeach()
# planted mutants of the LBVH kernels (MP10_MUT=1|2|3 selects them at run time; every mutant must make the comparison with the CPU reference fail)
add_spv(lbvh_build_m2 "${GPU_SRC}/lbvh_build.comp" -DMUT=2)
add_spv(lbvh_query_m1 "${GPU_SRC}/lbvh_query.comp" -DMUT=1)
add_spv(lbvh_query_m3 "${GPU_SRC}/lbvh_query.comp" -DMUT=3)
add_custom_target(gpu_shaders ALL DEPENDS ${SPVS})

add_executable(mp09_orient_filter mp09_orient_filter.cpp)
target_include_directories(mp09_orient_filter PRIVATE "${BOOST_ROOT_DIR}" "${CMAKE_SOURCE_DIR}/harness" "${GPU_SRC}")
target_link_libraries(mp09_orient_filter PRIVATE volk shewchuk_predicates)
target_compile_definitions(mp09_orient_filter PRIVATE EXACTMESH_SHADER_DIR_DEFAULT="${SHADER_OUT}")
add_dependencies(mp09_orient_filter gpu_shaders)

find_package(Threads REQUIRED)
add_executable(mp10_lbvh mp10_lbvh.cpp "${CMAKE_SOURCE_DIR}/cpu/bvh_pairs.cpp")
target_include_directories(mp10_lbvh PRIVATE "${CMAKE_SOURCE_DIR}/harness" "${CMAKE_SOURCE_DIR}/cpu" "${GPU_SRC}")
target_link_libraries(mp10_lbvh PRIVATE volk Threads::Threads)
target_compile_definitions(mp10_lbvh PRIVATE EXACTMESH_SHADER_DIR_DEFAULT="${SHADER_OUT}")
add_dependencies(mp10_lbvh gpu_shaders)

add_executable(mp11_hybrid mp11_hybrid.cpp "${CMAKE_SOURCE_DIR}/cpu/bvh_pairs.cpp")
target_include_directories(mp11_hybrid PRIVATE "${BOOST_ROOT_DIR}" "${CMAKE_SOURCE_DIR}/harness" "${CMAKE_SOURCE_DIR}/cpu" "${GPU_SRC}")
target_link_libraries(mp11_hybrid PRIVATE volk shewchuk_predicates Threads::Threads)
target_compile_definitions(mp11_hybrid PRIVATE EXACTMESH_SHADER_DIR_DEFAULT="${SHADER_OUT}")
add_dependencies(mp11_hybrid gpu_shaders)

add_executable(mp12_pip mp12_pip.cpp)
target_include_directories(mp12_pip PRIVATE "${BOOST_ROOT_DIR}" "${CMAKE_SOURCE_DIR}/harness" "${CMAKE_SOURCE_DIR}/cpu" "${GPU_SRC}")
target_link_libraries(mp12_pip PRIVATE volk shewchuk_predicates Threads::Threads)
target_compile_definitions(mp12_pip PRIVATE EXACTMESH_SHADER_DIR_DEFAULT="${SHADER_OUT}")
add_dependencies(mp12_pip gpu_shaders)

add_executable(mp12c_bvh_pip mp12c_bvh_pip.cpp "${CMAKE_SOURCE_DIR}/cpu/bvh_pairs.cpp")
target_include_directories(mp12c_bvh_pip PRIVATE "${BOOST_ROOT_DIR}" "${CMAKE_SOURCE_DIR}/harness" "${CMAKE_SOURCE_DIR}/cpu" "${GPU_SRC}")
target_link_libraries(mp12c_bvh_pip PRIVATE volk shewchuk_predicates Threads::Threads)
target_compile_definitions(mp12c_bvh_pip PRIVATE EXACTMESH_SHADER_DIR_DEFAULT="${SHADER_OUT}")
add_dependencies(mp12c_bvh_pip gpu_shaders)

add_executable(mp12_vol mp12_vol.cpp)
target_include_directories(mp12_vol PRIVATE "${BOOST_ROOT_DIR}" "${CMAKE_SOURCE_DIR}/harness" "${GPU_SRC}")
target_link_libraries(mp12_vol PRIVATE volk Threads::Threads)
target_compile_definitions(mp12_vol PRIVATE EXACTMESH_SHADER_DIR_DEFAULT="${SHADER_OUT}")
add_dependencies(mp12_vol gpu_shaders)

add_executable(mp13_service mp13_service.cpp)
target_include_directories(mp13_service PRIVATE "${BOOST_ROOT_DIR}" "${CMAKE_SOURCE_DIR}/harness" "${CMAKE_SOURCE_DIR}/cpu" "${GPU_SRC}")
target_link_libraries(mp13_service PRIVATE volk shewchuk_predicates Threads::Threads)
target_compile_definitions(mp13_service PRIVATE EXACTMESH_SHADER_DIR_DEFAULT="${SHADER_OUT}")
add_dependencies(mp13_service gpu_shaders)

if(EXACTMESH_TESTS)
  exactmesh_test(gpu_tests "${CMAKE_SOURCE_DIR}/tests/gpu/test_gpu.cpp")
  target_compile_definitions(gpu_tests PRIVATE EXACTMESH_BIN_DIR="${CMAKE_RUNTIME_OUTPUT_DIRECTORY}" EXACTMESH_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  add_dependencies(gpu_tests mp09_orient_filter mp10_lbvh)
  add_test(NAME gpu_tests COMMAND gpu_tests "[gpu]" WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
  set_tests_properties(gpu_tests PROPERTIES LABELS "gpu" TIMEOUT 1500)
  # not in the lavapipe CI job (a mutant of the tree build can hang a kernel); run with: ctest -L lbvhmut
  add_test(NAME gpu_mutants COMMAND gpu_tests "[gpu-mutant]" WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
  set_tests_properties(gpu_mutants PROPERTIES LABELS "lbvhmut" TIMEOUT 600)
  add_test(NAME gpu_host_orient_filter COMMAND mp09_orient_filter host 200000 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
  set_tests_properties(gpu_host_orient_filter PROPERTIES LABELS "fast")
  add_test(NAME gpu_host_mp12 COMMAND mp12_pip host WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
  set_tests_properties(gpu_host_mp12 PROPERTIES LABELS "fast")
  add_test(NAME gpu_host_mp12_vol COMMAND mp12_vol host WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
  set_tests_properties(gpu_host_mp12_vol PROPERTIES LABELS "fast")
  add_test(NAME gpu_host_mp11 COMMAND mp11_hybrid host WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
  set_tests_properties(gpu_host_mp11 PROPERTIES LABELS "fast")
  # m1 (MAYBE treated as NO) must be caught: the program exits non-zero when the mutant is NOT caught
  add_test(NAME gpu_host_mp11_m1 COMMAND mp11_hybrid host m1 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
  set_tests_properties(gpu_host_mp11_m1 PROPERTIES LABELS "mutant")
endif()
