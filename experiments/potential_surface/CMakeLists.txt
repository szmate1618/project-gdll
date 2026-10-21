cmake_minimum_required(VERSION 3.16)
project(potential_surface_experiment LANGUAGES CXX)

# Standalone build: no viewer, navmesh, Detour, downloads or asset generation.
include(CTest)
find_package(OpenSSL REQUIRED COMPONENTS Crypto)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(PSS_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
find_path(PSS_JSON_INCLUDE_DIR json.hpp
    HINTS "${PSS_ROOT}/build/_deps/tinygltf-src"
    DOC "Existing directory containing nlohmann json.hpp (for the benchmark only)")
if(NOT PSS_JSON_INCLUDE_DIR)
    message(FATAL_ERROR "Set PSS_JSON_INCLUDE_DIR to the existing tinygltf/json.hpp dependency directory")
endif()

add_library(potential_surface STATIC "${PSS_ROOT}/src/potential_surface.cpp")
target_include_directories(potential_surface PUBLIC "${PSS_ROOT}/src")
target_compile_options(potential_surface PRIVATE -Wall -Wextra -Wpedantic)
add_library(potential_surface_simulation STATIC
    "${PSS_ROOT}/tools/potential_surface_simulation.cpp")
target_include_directories(potential_surface_simulation PUBLIC "${PSS_ROOT}/tools")
target_link_libraries(potential_surface_simulation PUBLIC potential_surface)
target_compile_options(potential_surface_simulation PRIVATE -Wall -Wextra -Wpedantic)
add_executable(benchmark_potential_surface
    "${PSS_ROOT}/tools/benchmark_potential_surface.cpp"
    "${PSS_ROOT}/src/file_fingerprint.cpp")
target_include_directories(benchmark_potential_surface SYSTEM PRIVATE "${PSS_JSON_INCLUDE_DIR}")
target_link_libraries(benchmark_potential_surface PRIVATE potential_surface_simulation OpenSSL::Crypto)
target_compile_options(benchmark_potential_surface PRIVATE -Wall -Wextra -Wpedantic)

if(BUILD_TESTING)
    foreach(suite potential_surface potential_surface_simulation)
        add_executable(test_${suite} "${PSS_ROOT}/tests/test_${suite}.cpp")
        target_link_libraries(test_${suite} PRIVATE potential_surface_simulation)
        target_compile_options(test_${suite} PRIVATE -Wall -Wextra -Wpedantic)
        add_test(NAME ${suite} COMMAND test_${suite})
    endforeach()
endif()
