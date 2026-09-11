cmake_minimum_required(VERSION 3.20)
project(godollo_viewer VERSION 0.1.0 LANGUAGES C CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "Use an out-of-source build: cmake -S . -B build")
endif()
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "This viewer currently targets Linux OpenGL drivers.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build configuration" FORCE)
endif()

include(FetchContent)
include(CTest)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()
find_package(OpenGL REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

# X11 also works in Wayland sessions through XWayland. Native Wayland support
# can be enabled explicitly once the additional development packages exist.
option(GLFW_BUILD_X11 "Build the GLFW X11 backend" ON)
option(GLFW_BUILD_WAYLAND "Build the GLFW native Wayland backend" OFF)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_LIBRARY OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_INSTALL OFF CACHE BOOL "" FORCE)
set(TINYGLTF_HEADER_ONLY ON CACHE BOOL "" FORCE)
set(TINYGLTF_BUILD_LOADER_EXAMPLE OFF CACHE BOOL "" FORCE)
set(TINYGLTF_BUILD_GL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(TINYGLTF_BUILD_VALIDATOR_EXAMPLE OFF CACHE BOOL "" FORCE)
set(TINYGLTF_BUILD_BUILDER_EXAMPLE OFF CACHE BOOL "" FORCE)
set(TINYGLTF_INSTALL OFF CACHE BOOL "" FORCE)

# Pinned, hash-checked archives and all dependency outputs stay in the build
# directory. CMake reuses matching archives when configuring without a network.
FetchContent_Declare(glfw
    URL https://codeload.github.com/glfw/glfw/tar.gz/refs/tags/3.4
    URL_HASH SHA256=c038d34200234d071fae9345bc455e4a8f2f544ab60150765d7704e08f3dac01
    DOWNLOAD_DIR "${CMAKE_BINARY_DIR}/_archives"
    DOWNLOAD_NAME glfw-3.4.tar.gz
)
FetchContent_Declare(glm
    URL https://codeload.github.com/g-truc/glm/tar.gz/refs/tags/1.0.1
    URL_HASH SHA256=9f3174561fd26904b23f0db5e560971cbf9b3cbda0b280f04d5c379d03bf234c
    DOWNLOAD_DIR "${CMAKE_BINARY_DIR}/_archives"
    DOWNLOAD_NAME glm-1.0.1.tar.gz
)
FetchContent_Declare(tinygltf
    URL https://codeload.github.com/syoyo/tinygltf/tar.gz/refs/tags/v2.9.7
    URL_HASH SHA256=bf92fe53b86302f9415e77504ecd81a58659292ed04dffec873f5c71665046a0
    DOWNLOAD_DIR "${CMAKE_BINARY_DIR}/_archives"
    DOWNLOAD_NAME tinygltf-2.9.7.tar.gz
)
FetchContent_MakeAvailable(glfw glm tinygltf)

function(viewer_warnings target)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

add_library(viewer_model STATIC src/model.cpp)
target_include_directories(viewer_model PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_include_directories(viewer_model SYSTEM PRIVATE "${tinygltf_SOURCE_DIR}")
target_link_libraries(viewer_model PUBLIC glm::glm)
viewer_warnings(viewer_model)

add_library(viewer_camera STATIC src/camera.cpp)
target_include_directories(viewer_camera PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(viewer_camera PUBLIC glm::glm)
viewer_warnings(viewer_camera)

add_library(viewer_collision STATIC src/collision_world.cpp)
target_link_libraries(viewer_collision PUBLIC viewer_model)
viewer_warnings(viewer_collision)

add_library(viewer_fps STATIC src/fps_controller.cpp src/camera_rig.cpp)
target_link_libraries(viewer_fps PUBLIC viewer_collision viewer_camera)
viewer_warnings(viewer_fps)

set(VIEWER_TEST_ASSET "${CMAKE_CURRENT_SOURCE_DIR}/assets/test.glb")
add_custom_command(
    OUTPUT "${VIEWER_TEST_ASSET}"
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/create_test_asset.py"
        --output "${VIEWER_TEST_ASSET}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/create_test_asset.py"
    COMMENT "Generating the small textured GLB test scene"
    VERBATIM
)
add_custom_target(viewer_test_asset DEPENDS "${VIEWER_TEST_ASSET}")

add_executable(godollo_viewer src/main.cpp src/renderer.cpp src/collision_debug.cpp)
target_link_libraries(godollo_viewer PRIVATE
    viewer_model viewer_camera viewer_fps glfw OpenGL::GL
)
target_compile_definitions(godollo_viewer PRIVATE
    GLFW_INCLUDE_NONE GL_GLEXT_PROTOTYPES
    VIEWER_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders"
    VIEWER_DEFAULT_ASSET="${VIEWER_TEST_ASSET}"
)
add_dependencies(godollo_viewer viewer_test_asset)
viewer_warnings(godollo_viewer)

if(BUILD_TESTING)
    add_executable(test_model tests/test_model.cpp)
    target_link_libraries(test_model PRIVATE viewer_model)
    target_include_directories(test_model SYSTEM PRIVATE "${tinygltf_SOURCE_DIR}")
    add_dependencies(test_model viewer_test_asset)
    viewer_warnings(test_model)
    add_test(NAME viewer_model COMMAND test_model "${VIEWER_TEST_ASSET}")

    add_executable(test_camera tests/test_camera.cpp)
    target_link_libraries(test_camera PRIVATE viewer_camera)
    viewer_warnings(test_camera)
    add_test(NAME viewer_camera COMMAND test_camera)

    foreach(suite collision fps camera_rig)
        add_executable(test_${suite} tests/test_${suite}.cpp)
        target_link_libraries(test_${suite} PRIVATE viewer_fps)
        viewer_warnings(test_${suite})
        add_test(NAME viewer_${suite} COMMAND test_${suite})
    endforeach()

    add_executable(test_fps_integration tests/test_fps_integration.cpp)
    target_link_libraries(test_fps_integration PRIVATE viewer_fps)
    viewer_warnings(test_fps_integration)
    add_dependencies(test_fps_integration viewer_test_asset)
    add_test(NAME viewer_fps_test_scene COMMAND test_fps_integration "${VIEWER_TEST_ASSET}")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/output/godollo.glb")
        add_test(NAME viewer_fps_godollo COMMAND test_fps_integration
            "${CMAKE_CURRENT_SOURCE_DIR}/output/godollo.glb")
    endif()

    # Keep test assertions active even when checking an optimized viewer build.
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(test_model PRIVATE -UNDEBUG)
        target_compile_options(test_camera PRIVATE -UNDEBUG)
    endif()
endif()
