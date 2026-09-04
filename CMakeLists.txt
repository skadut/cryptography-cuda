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

add_test(NAME cpu_baseline COMMAND test_cpu_baseline)
set_tests_properties(cpu_baseline PROPERTIES LABELS phase1)

add_test(NAME telemetry COMMAND test_telemetry)
set_tests_properties(telemetry PROPERTIES LABELS phase1 SKIP_RETURN_CODE 77)
