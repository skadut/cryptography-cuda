cmake_minimum_required(VERSION 3.20)
project(gpuseal C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# Phase 1 is CPU-only. CUDA is enabled from phase 2 onward.
option(GPUSEAL_ENABLE_CUDA "Build CUDA kernels (phase 2+)" OFF)

if(MSVC)
  add_compile_options(/W4 /WX)
  # fopen/strncpy are flagged as deprecated by MSVC only; they are standard C11.
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
  add_compile_options(-Wall -Wextra -Werror)
endif()

find_package(OpenSSL 3.0 REQUIRED)
find_package(Threads REQUIRED)
find_package(OpenMP)

# --- NVML / NVTX: optional in phase 1 ---------------------------------------
find_path(NVML_INCLUDE_DIR nvml.h
  PATHS "$ENV{CUDA_PATH}/include" /usr/local/cuda/include)
find_library(NVML_LIBRARY NAMES nvidia-ml nvml
  PATHS "$ENV{CUDA_PATH}/lib/x64" /usr/local/cuda/lib64/stubs /usr/lib/x86_64-linux-gnu)

# NVTX3 is header-only, so an include path is the whole dependency.
find_path(NVTX_INCLUDE_DIR nvtx3/nvToolsExt.h
  PATHS "$ENV{CUDA_PATH}/include" /usr/local/cuda/include /opt/cuda/include)

# --- Core library ------------------------------------------------------------
add_library(gpuseal_core STATIC
  src/common.c
  src/cpu_baseline.c
  src/test_vectors.c
  src/telemetry.c
)
target_include_directories(gpuseal_core PUBLIC include)
target_link_libraries(gpuseal_core PUBLIC OpenSSL::Crypto Threads::Threads)

if(NVML_INCLUDE_DIR AND NVML_LIBRARY)
  target_compile_definitions(gpuseal_core PUBLIC GPUSEAL_HAVE_NVML)
  target_include_directories(gpuseal_core PUBLIC ${NVML_INCLUDE_DIR})
  target_link_libraries(gpuseal_core PUBLIC ${NVML_LIBRARY})
  message(STATUS "gpuseal: NVML found, telemetry enabled")
else()
  message(STATUS "gpuseal: NVML not found, telemetry builds in stub mode")
endif()

if(NVTX_INCLUDE_DIR)
  target_compile_definitions(gpuseal_core PUBLIC GPUSEAL_HAVE_NVTX)
  target_include_directories(gpuseal_core PUBLIC ${NVTX_INCLUDE_DIR})
  message(STATUS "gpuseal: NVTX3 found, range annotation enabled")
else()
  message(STATUS "gpuseal: NVTX3 not found, gpuseal_nvtx_* compile to no-ops "
                 "and gate P1.5 will report SKIP")
endif()

if(OpenMP_C_FOUND)
  target_link_libraries(gpuseal_core PUBLIC OpenMP::OpenMP_C)
else()
  message(WARNING "gpuseal: OpenMP not found. The CPU baseline will run single-threaded, "
                  "which violates the section 3 fair-baseline requirement. "
                  "Do not record P1.10 results from this build.")
endif()

# --- Benchmarks and tests ----------------------------------------------------
add_executable(bench_cpu bench/bench_cpu.c)
target_link_libraries(bench_cpu PRIVATE gpuseal_core)

enable_testing()

add_executable(test_cpu_baseline tests/test_cpu_baseline.c)
target_link_libraries(test_cpu_baseline PRIVATE gpuseal_core)

add_executable(test_telemetry tests/test_telemetry.c)
target_link_libraries(test_telemetry PRIVATE gpuseal_core)

add_executable(test_openssl_cli tests/test_openssl_cli.c)
target_link_libraries(test_openssl_cli PRIVATE gpuseal_core)

add_executable(test_thread_scaling tests/test_thread_scaling.c)
target_link_libraries(test_thread_scaling PRIVATE gpuseal_core)

# P1.5 probe. Only useful under `nsys profile`; scripts/verify_nvtx.* drives it.
add_executable(nvtx_probe tests/nvtx_probe.c)
target_link_libraries(nvtx_probe PRIVATE gpuseal_core)

# --- Phase 1 gates -----------------------------------------------------------
# P1.1: toolchain present.
if(WIN32)
  add_test(NAME prereqs COMMAND powershell -NoProfile -ExecutionPolicy Bypass
           -File "${CMAKE_SOURCE_DIR}/scripts/check_prereqs.ps1")
else()
  add_test(NAME prereqs COMMAND bash "${CMAKE_SOURCE_DIR}/scripts/check_prereqs.sh")
endif()
set_tests_properties(prereqs PROPERTIES LABELS phase1)

# P1.2, plus G2 correctness and P1.8/P1.9 vector loading.
add_test(NAME cpu_baseline COMMAND test_cpu_baseline)
set_tests_properties(cpu_baseline PROPERTIES LABELS phase1)

# P1.3, P1.4. Skips without NVML.
add_test(NAME telemetry COMMAND test_telemetry)
set_tests_properties(telemetry PROPERTIES LABELS phase1 SKIP_RETURN_CODE 77)

# P1.5. Skips without nsys or without NVTX headers.
if(WIN32)
  add_test(NAME nvtx_trace COMMAND powershell -NoProfile -ExecutionPolicy Bypass
           -File "${CMAKE_SOURCE_DIR}/scripts/verify_nvtx.ps1"
           -Probe "$<TARGET_FILE:nvtx_probe>")
else()
  add_test(NAME nvtx_trace COMMAND bash "${CMAKE_SOURCE_DIR}/scripts/verify_nvtx.sh"
           "$<TARGET_FILE:nvtx_probe>")
endif()
set_tests_properties(nvtx_trace PROPERTIES LABELS phase1 SKIP_RETURN_CODE 77)

# P1.6. Skips without the openssl CLI. Allow room for a 1 GiB run.
add_test(NAME openssl_cli_oracle COMMAND test_openssl_cli)
set_tests_properties(openssl_cli_oracle PROPERTIES
  LABELS phase1 SKIP_RETURN_CODE 77 TIMEOUT 900)

# P1.7. Skips on hosts with fewer than 8 cores.
add_test(NAME thread_scaling COMMAND test_thread_scaling)
set_tests_properties(thread_scaling PROPERTIES
  LABELS phase1 SKIP_RETURN_CODE 77 TIMEOUT 600
  RUN_SERIAL TRUE)   # a scaling measurement must not share cores with other tests

# `cmake --build build --target verify-phase1` -- referenced by docs/phases/.
add_custom_target(verify-phase1
  COMMAND ${CMAKE_CTEST_COMMAND} -L phase1 --output-on-failure
  DEPENDS test_cpu_baseline test_telemetry test_openssl_cli
          test_thread_scaling nvtx_probe
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  COMMENT "Running phase 1 gates P1.1-P1.10")
