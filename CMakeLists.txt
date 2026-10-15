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
find_package(OpenSSL REQUIRED COMPONENTS Crypto)
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
FetchContent_Declare(box3d
    GIT_REPOSITORY https://github.com/erincatto/box3d.git
    GIT_TAG f555ee42084e0b43cbffa863f40bff8117c08896
)
set(BOX3D_SAMPLES OFF CACHE BOOL "" FORCE)
set(BOX3D_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(BOX3D_DOCS OFF CACHE BOOL "" FORCE)
set(BOX3D_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(BOX3D_VALIDATE OFF CACHE BOOL "" FORCE)
set(MESHOPT_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(meshoptimizer
    URL https://codeload.github.com/zeux/meshoptimizer/tar.gz/refs/tags/v1.3
    URL_HASH SHA256=ca4f47d451201593904b9e3067fa54b65db94bc34ab5173864950862b748a145
    DOWNLOAD_DIR "${CMAKE_BINARY_DIR}/_archives"
    DOWNLOAD_NAME meshoptimizer-v1.3.tar.gz
)
FetchContent_Declare(tomlplusplus
    URL https://codeload.github.com/marzer/tomlplusplus/tar.gz/refs/tags/v3.4.0
    URL_HASH SHA256=8517f65938a4faae9ccf8ebb36631a38c1cadfb5efa85d9a72e15b9e97d25155
    DOWNLOAD_DIR "${CMAKE_BINARY_DIR}/_archives"
    DOWNLOAD_NAME tomlplusplus-3.4.0.tar.gz
)
FetchContent_MakeAvailable(glfw glm tinygltf box3d meshoptimizer tomlplusplus)

# Offline asset preparation; never runs during viewer startup.
add_executable(zombie_mesh_simplify tools/zombie_mesh_simplify.cpp)
target_link_libraries(zombie_mesh_simplify PRIVATE meshoptimizer)

function(viewer_warnings target)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()
viewer_warnings(zombie_mesh_simplify)

add_library(viewer_model STATIC src/model.cpp src/model_merge.cpp)
target_include_directories(viewer_model PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_include_directories(viewer_model SYSTEM PRIVATE "${tinygltf_SOURCE_DIR}")
target_link_libraries(viewer_model PUBLIC glm::glm)
viewer_warnings(viewer_model)

add_library(viewer_visibility STATIC src/visibility.cpp src/model_visibility.cpp)
target_link_libraries(viewer_visibility PUBLIC viewer_model)
viewer_warnings(viewer_visibility)

add_library(viewer_zombie_visibility STATIC src/zombie_visibility.cpp)
target_link_libraries(viewer_zombie_visibility PUBLIC viewer_visibility)
viewer_warnings(viewer_zombie_visibility)

add_library(viewer_roof_batches STATIC src/roof_batch.cpp)
target_link_libraries(viewer_roof_batches PUBLIC viewer_model)
viewer_warnings(viewer_roof_batches)

add_library(viewer_fingerprints STATIC src/file_fingerprint.cpp)
target_include_directories(viewer_fingerprints PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(viewer_fingerprints PRIVATE OpenSSL::Crypto)
viewer_warnings(viewer_fingerprints)

add_library(viewer_trees STATIC src/tree_layer.cpp src/tree_visibility.cpp src/tree_color.cpp src/tree_canopy.cpp)
target_link_libraries(viewer_trees PUBLIC viewer_model viewer_visibility viewer_fingerprints)
target_include_directories(viewer_trees SYSTEM PRIVATE "${tinygltf_SOURCE_DIR}")
viewer_warnings(viewer_trees)

add_library(viewer_poles STATIC src/pole_layer.cpp)
target_link_libraries(viewer_poles PUBLIC viewer_model PRIVATE viewer_fingerprints)
target_include_directories(viewer_poles SYSTEM PRIVATE "${tinygltf_SOURCE_DIR}")
viewer_warnings(viewer_poles)

add_library(viewer_guardrails STATIC src/guardrail_layer.cpp)
target_link_libraries(viewer_guardrails PUBLIC viewer_model PRIVATE viewer_fingerprints)
target_include_directories(viewer_guardrails SYSTEM PRIVATE "${tinygltf_SOURCE_DIR}")
viewer_warnings(viewer_guardrails)

add_library(viewer_camera STATIC src/camera.cpp)
target_include_directories(viewer_camera PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(viewer_camera PUBLIC glm::glm)
viewer_warnings(viewer_camera)

add_library(viewer_collision STATIC src/collision_world.cpp src/door_collision.cpp src/character_step.cpp)
target_link_libraries(viewer_collision PUBLIC viewer_model)
viewer_warnings(viewer_collision)

add_library(viewer_noise STATIC src/noise.cpp src/player_noise.cpp)
target_include_directories(viewer_noise PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(viewer_noise PUBLIC glm::glm)
viewer_warnings(viewer_noise)

add_library(viewer_physics STATIC src/physics_world.cpp src/door_geometry.cpp src/door_layer.cpp src/box_layer.cpp)
target_link_libraries(viewer_physics PUBLIC viewer_model box3d::box3d)
viewer_warnings(viewer_physics)
target_link_libraries(viewer_collision PUBLIC viewer_physics)

add_library(viewer_forests STATIC src/forest_layer.cpp src/forest_geometry.cpp src/forest_placement.cpp
    src/forest_tree_visibility.cpp src/forest_volume_batch.cpp)
target_link_libraries(viewer_forests PUBLIC viewer_trees viewer_collision)
target_include_directories(viewer_forests SYSTEM PRIVATE "${tinygltf_SOURCE_DIR}")
viewer_warnings(viewer_forests)

add_library(viewer_zombies STATIC src/animated_model.cpp src/zombie_placement.cpp src/zombie_park.cpp
    src/zombie_layer.cpp src/zombie_lod.cpp src/zombie_lod_assets.cpp src/zombie_hit.cpp src/zombie_shooting.cpp src/zombie_chase.cpp src/zombie_attack.cpp src/zombie_behavior.cpp src/zombie_animation.cpp
    src/zombie_ragdoll.cpp src/zombie_ragdoll_pose.cpp src/zombie_spatial_index.cpp src/zombie_crowd.cpp src/zombie_movement.cpp src/zombie_door_collision.cpp src/zombie_perception.cpp src/zombie_render_updates.cpp)
target_link_libraries(viewer_zombies PUBLIC viewer_model viewer_collision viewer_noise viewer_physics viewer_fingerprints)
target_include_directories(viewer_zombies SYSTEM PRIVATE "${tinygltf_SOURCE_DIR}")
viewer_warnings(viewer_zombies)

add_library(viewer_fps STATIC src/fps_controller.cpp src/camera_rig.cpp)
target_link_libraries(viewer_fps PUBLIC viewer_collision viewer_camera viewer_noise)
viewer_warnings(viewer_fps)

add_library(viewer_interaction STATIC src/door_interaction.cpp src/object_interaction.cpp src/player_interaction.cpp)
target_link_libraries(viewer_interaction PUBLIC viewer_camera viewer_collision viewer_physics)
viewer_warnings(viewer_interaction)

add_library(viewer_weapons STATIC src/weapons.cpp)
target_include_directories(viewer_weapons PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(viewer_weapons PUBLIC glm::glm PRIVATE tomlplusplus::tomlplusplus)
viewer_warnings(viewer_weapons)

add_library(viewer_objects STATIC src/interior_objects.cpp src/physics_scene.cpp)
target_link_libraries(viewer_objects PUBLIC viewer_collision viewer_physics)
viewer_warnings(viewer_objects)

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

add_library(viewer_sun_lighting STATIC src/sun_lighting.cpp src/day_night.cpp src/flashlight.cpp)
target_include_directories(viewer_sun_lighting PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(viewer_sun_lighting PUBLIC glm::glm viewer_camera)
viewer_warnings(viewer_sun_lighting)

add_library(viewer_renderer STATIC src/renderer.cpp src/renderer_shadows.cpp
    src/box_renderer.cpp
    src/weapon_overlay.cpp
    src/roof_renderer.cpp
    src/tree_renderer.cpp src/forest_renderer.cpp
    src/zombie_renderer.cpp src/zombie_palette_cache.cpp src/shadow_map.cpp src/rain_renderer.cpp src/shader_program.cpp)
target_link_libraries(viewer_renderer PUBLIC
    viewer_model viewer_roof_batches viewer_visibility viewer_zombie_visibility viewer_camera viewer_trees viewer_forests viewer_zombies viewer_sun_lighting OpenGL::GL)
target_compile_definitions(viewer_renderer PRIVATE GL_GLEXT_PROTOTYPES)
viewer_warnings(viewer_renderer)

add_executable(godollo_viewer src/main.cpp src/collision_debug.cpp
    src/reticle_overlay.cpp src/gpu_timer.cpp)
target_link_libraries(godollo_viewer PRIVATE
    viewer_renderer viewer_fps viewer_interaction viewer_objects viewer_poles viewer_guardrails viewer_weapons glfw
)
target_compile_definitions(godollo_viewer PRIVATE
    GLFW_INCLUDE_NONE GL_GLEXT_PROTOTYPES
    VIEWER_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders"
    VIEWER_DEFAULT_ASSET="${VIEWER_TEST_ASSET}"
    VIEWER_ZOMBIE_ASSETS="${CMAKE_CURRENT_SOURCE_DIR}/assets/zombies/models"
    VIEWER_ZOMBIE_PARK="${CMAKE_CURRENT_SOURCE_DIR}/assets/zombies/grassalkovich_park.json"
    VIEWER_FOREST_CONFIG="${CMAKE_CURRENT_SOURCE_DIR}/forest_patches.json"
    VIEWER_ITEMS="${CMAKE_CURRENT_SOURCE_DIR}/items"
    VIEWER_INVENTORY="${CMAKE_CURRENT_SOURCE_DIR}/inventory.toml"
)
add_dependencies(godollo_viewer viewer_test_asset)
viewer_warnings(godollo_viewer)

if(BUILD_TESTING)
    add_executable(test_weapons tests/test_weapons.cpp)
    target_link_libraries(test_weapons PRIVATE viewer_weapons)
    target_compile_definitions(test_weapons PRIVATE
        VIEWER_ITEMS="${CMAKE_CURRENT_SOURCE_DIR}/items"
        VIEWER_INVENTORY="${CMAKE_CURRENT_SOURCE_DIR}/inventory.toml")
    viewer_warnings(test_weapons)
    add_test(NAME viewer_weapons COMMAND test_weapons)

    add_executable(test_weapon_overlay tests/test_weapon_overlay.cpp)
    target_link_libraries(test_weapon_overlay PRIVATE viewer_renderer glfw)
    target_compile_definitions(test_weapon_overlay PRIVATE GLFW_INCLUDE_NONE GL_GLEXT_PROTOTYPES
        VIEWER_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders")
    viewer_warnings(test_weapon_overlay)
    add_test(NAME viewer_weapon_overlay COMMAND test_weapon_overlay)
    set_tests_properties(viewer_weapon_overlay PROPERTIES SKIP_RETURN_CODE 77)
    add_executable(test_object_interaction tests/test_object_interaction.cpp)
    target_link_libraries(test_object_interaction PRIVATE viewer_interaction viewer_objects)
    viewer_warnings(test_object_interaction)
    add_test(NAME viewer_object_interaction COMMAND test_object_interaction)
    add_executable(test_interior_objects tests/test_interior_objects.cpp)
    target_link_libraries(test_interior_objects PRIVATE viewer_objects viewer_interaction)
    viewer_warnings(test_interior_objects)
    add_test(NAME viewer_interior_objects COMMAND test_interior_objects)
    add_executable(test_physics_boxes tests/test_physics_boxes.cpp)
    target_link_libraries(test_physics_boxes PRIVATE viewer_objects viewer_fps)
    viewer_warnings(test_physics_boxes)
    add_test(NAME viewer_physics_boxes COMMAND test_physics_boxes)
    add_executable(test_box_renderer tests/test_box_renderer.cpp)
    target_link_libraries(test_box_renderer PRIVATE viewer_objects viewer_renderer glfw)
    target_compile_definitions(test_box_renderer PRIVATE GLFW_INCLUDE_NONE GL_GLEXT_PROTOTYPES
        VIEWER_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders")
    viewer_warnings(test_box_renderer)
    add_test(NAME viewer_box_renderer COMMAND test_box_renderer)
    set_tests_properties(viewer_box_renderer PROPERTIES SKIP_RETURN_CODE 77)
    add_executable(test_guardrail_layer tests/test_guardrail_layer.cpp)
    target_link_libraries(test_guardrail_layer PRIVATE viewer_guardrails viewer_collision viewer_fingerprints)
    target_include_directories(test_guardrail_layer SYSTEM PRIVATE "${tinygltf_SOURCE_DIR}")
    target_compile_definitions(test_guardrail_layer PRIVATE VIEWER_TEST_ASSET="${VIEWER_TEST_ASSET}")
    add_dependencies(test_guardrail_layer viewer_test_asset)
    viewer_warnings(test_guardrail_layer)
    add_test(NAME viewer_guardrail_layer COMMAND test_guardrail_layer)

    add_executable(test_guardrail_renderer tests/test_guardrail_renderer.cpp)
    target_link_libraries(test_guardrail_renderer PRIVATE viewer_guardrails viewer_renderer viewer_collision glfw)
    target_compile_definitions(test_guardrail_renderer PRIVATE
        GLFW_INCLUDE_NONE GL_GLEXT_PROTOTYPES
        VIEWER_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders")
    viewer_warnings(test_guardrail_renderer)
    add_test(NAME viewer_guardrail_renderer COMMAND test_guardrail_renderer)
    set_tests_properties(viewer_guardrail_renderer PROPERTIES SKIP_RETURN_CODE 77)

    add_executable(test_pole_layer tests/test_pole_layer.cpp)
    target_link_libraries(test_pole_layer PRIVATE viewer_poles viewer_collision viewer_visibility)
    target_include_directories(test_pole_layer SYSTEM PRIVATE "${tinygltf_SOURCE_DIR}")
    viewer_warnings(test_pole_layer)
    add_test(NAME viewer_pole_layer COMMAND test_pole_layer)

    add_executable(test_pole_renderer tests/test_pole_renderer.cpp)
    target_link_libraries(test_pole_renderer PRIVATE viewer_poles viewer_renderer glfw)
    target_compile_definitions(test_pole_renderer PRIVATE
        GLFW_INCLUDE_NONE GL_GLEXT_PROTOTYPES
        VIEWER_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders")
    viewer_warnings(test_pole_renderer)
    add_test(NAME viewer_pole_renderer COMMAND test_pole_renderer)
    set_tests_properties(viewer_pole_renderer PROPERTIES SKIP_RETURN_CODE 77)

    add_executable(test_roof_batch tests/test_roof_batch.cpp)
    target_link_libraries(test_roof_batch PRIVATE viewer_roof_batches)
    viewer_warnings(test_roof_batch)
    add_test(NAME viewer_roof_batch COMMAND test_roof_batch)

    add_executable(test_roof_renderer tests/test_roof_renderer.cpp)
    target_link_libraries(test_roof_renderer PRIVATE viewer_renderer glfw)
    target_compile_definitions(test_roof_renderer PRIVATE
        GLFW_INCLUDE_NONE GL_GLEXT_PROTOTYPES
        VIEWER_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders")
    viewer_warnings(test_roof_renderer)
    add_test(NAME viewer_roof_renderer COMMAND test_roof_renderer)
    set_tests_properties(viewer_roof_renderer PROPERTIES SKIP_RETURN_CODE 77)

    add_executable(test_tree_color tests/test_tree_color.cpp)
    target_link_libraries(test_tree_color PRIVATE viewer_trees)
    viewer_warnings(test_tree_color)
    add_test(NAME viewer_tree_color COMMAND test_tree_color)

    add_executable(test_tree_canopy tests/test_tree_canopy.cpp)
    target_link_libraries(test_tree_canopy PRIVATE viewer_trees)
    viewer_warnings(test_tree_canopy)
    add_test(NAME viewer_tree_canopy COMMAND test_tree_canopy)

    add_executable(test_tree_canopy_renderer tests/test_tree_canopy_renderer.cpp)
    target_link_libraries(test_tree_canopy_renderer PRIVATE viewer_renderer glfw)
    target_compile_definitions(test_tree_canopy_renderer PRIVATE
        GLFW_INCLUDE_NONE GL_GLEXT_PROTOTYPES
        VIEWER_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders")
    viewer_warnings(test_tree_canopy_renderer)
    add_test(NAME viewer_tree_canopy_renderer COMMAND test_tree_canopy_renderer)
    set_tests_properties(viewer_tree_canopy_renderer PROPERTIES SKIP_RETURN_CODE 77)

    add_executable(test_door_interaction tests/test_door_interaction.cpp)
    target_link_libraries(test_door_interaction PRIVATE viewer_interaction)
    viewer_warnings(test_door_interaction)
    add_test(NAME viewer_door_interaction COMMAND test_door_interaction)
    add_executable(test_forests tests/test_forests.cpp)
    target_link_libraries(test_forests PRIVATE viewer_forests)
    target_include_directories(test_forests SYSTEM PRIVATE "${tinygltf_SOURCE_DIR}")
    viewer_warnings(test_forests)
    add_test(NAME viewer_forests COMMAND test_forests)

    add_executable(test_forest_render_data tests/test_forest_render_data.cpp)
    target_link_libraries(test_forest_render_data PRIVATE viewer_forests)
    viewer_warnings(test_forest_render_data)
    add_test(NAME viewer_forest_render_data COMMAND test_forest_render_data)

    add_executable(test_forest_renderer tests/test_forest_renderer.cpp)
    target_link_libraries(test_forest_renderer PRIVATE viewer_renderer glfw)
    target_compile_definitions(test_forest_renderer PRIVATE GLFW_INCLUDE_NONE GL_GLEXT_PROTOTYPES
        VIEWER_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders")
    viewer_warnings(test_forest_renderer)
    add_test(NAME viewer_forest_renderer COMMAND test_forest_renderer)
    set_tests_properties(viewer_forest_renderer PROPERTIES SKIP_RETURN_CODE 77)

    add_executable(test_doors tests/test_doors.cpp)
    target_link_libraries(test_doors PRIVATE viewer_physics viewer_visibility viewer_zombies)
    viewer_warnings(test_doors)
    add_test(NAME viewer_doors COMMAND test_doors)
    add_executable(test_door_walking tests/test_door_walking.cpp)
    target_link_libraries(test_door_walking PRIVATE viewer_fps viewer_physics)
    viewer_warnings(test_door_walking)
    add_test(NAME viewer_door_walking COMMAND test_door_walking)

    foreach(suite animated_model zombie_lod zombie_placement zombie_chase zombie_crowd zombie_movement zombie_motion zombie_door_collision zombie_attack zombie_behavior zombie_shooting zombie_spatial_index zombie_perception zombie_ragdoll zombie_transition zombie_render_updates)
        add_executable(test_${suite} tests/test_${suite}.cpp)
        target_link_libraries(test_${suite} PRIVATE viewer_zombies)
        target_include_directories(test_${suite} SYSTEM PRIVATE "${tinygltf_SOURCE_DIR}")
        target_compile_definitions(test_${suite} PRIVATE
            VIEWER_ZOMBIE_PARK="${CMAKE_CURRENT_SOURCE_DIR}/assets/zombies/grassalkovich_park.json")
        viewer_warnings(test_${suite})
        add_test(NAME viewer_${suite} COMMAND test_${suite})
    endforeach()

    target_link_libraries(test_zombie_movement PRIVATE viewer_poles)

    add_executable(test_character_step tests/test_character_step.cpp)
    target_link_libraries(test_character_step PRIVATE viewer_fps viewer_zombies)
    viewer_warnings(test_character_step)
    add_test(NAME viewer_character_step COMMAND test_character_step)

    add_executable(test_zombie_visibility tests/test_zombie_visibility.cpp)
    target_link_libraries(test_zombie_visibility PRIVATE viewer_zombie_visibility)
    viewer_warnings(test_zombie_visibility)
    add_test(NAME viewer_zombie_visibility COMMAND test_zombie_visibility)

    # Exercise real vertex attributes and skinning palette lookups. Machines
    # without an accessible OpenGL display skip this graphics-only regression.
    add_executable(test_zombie_renderer tests/test_zombie_renderer.cpp)
    target_link_libraries(test_zombie_renderer PRIVATE
        viewer_renderer glfw)
    target_compile_definitions(test_zombie_renderer PRIVATE
        GLFW_INCLUDE_NONE GL_GLEXT_PROTOTYPES
        VIEWER_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders")
    viewer_warnings(test_zombie_renderer)
    add_test(NAME viewer_zombie_renderer COMMAND test_zombie_renderer)
    set_tests_properties(viewer_zombie_renderer PROPERTIES SKIP_RETURN_CODE 77)

    add_executable(test_sun_lighting tests/test_sun_lighting.cpp)
    target_link_libraries(test_sun_lighting PRIVATE viewer_sun_lighting viewer_visibility)
    viewer_warnings(test_sun_lighting)
    add_test(NAME viewer_sun_lighting COMMAND test_sun_lighting)

    add_executable(test_flashlight tests/test_flashlight.cpp)
    target_link_libraries(test_flashlight PRIVATE viewer_sun_lighting viewer_camera viewer_visibility)
    viewer_warnings(test_flashlight)
    add_test(NAME viewer_flashlight COMMAND test_flashlight)

    add_executable(test_day_night tests/test_day_night.cpp)
    target_link_libraries(test_day_night PRIVATE viewer_sun_lighting)
    viewer_warnings(test_day_night)
    add_test(NAME viewer_day_night COMMAND test_day_night)

    add_executable(test_shadow_renderer tests/test_shadow_renderer.cpp)
    target_link_libraries(test_shadow_renderer PRIVATE viewer_renderer glfw)
    target_compile_definitions(test_shadow_renderer PRIVATE
        GLFW_INCLUDE_NONE GL_GLEXT_PROTOTYPES
        VIEWER_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders")
    viewer_warnings(test_shadow_renderer)
    add_test(NAME viewer_shadow_renderer COMMAND test_shadow_renderer)
    set_tests_properties(viewer_shadow_renderer PROPERTIES SKIP_RETURN_CODE 77)

    add_executable(test_atmosphere_renderer tests/test_atmosphere_renderer.cpp)
    target_link_libraries(test_atmosphere_renderer PRIVATE viewer_renderer glfw)
    target_compile_definitions(test_atmosphere_renderer PRIVATE
        GLFW_INCLUDE_NONE GL_GLEXT_PROTOTYPES
        VIEWER_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders")
    viewer_warnings(test_atmosphere_renderer)
    add_test(NAME viewer_atmosphere_renderer COMMAND test_atmosphere_renderer)
    set_tests_properties(viewer_atmosphere_renderer PROPERTIES SKIP_RETURN_CODE 77)

    add_executable(test_fence_renderer tests/test_fence_renderer.cpp)
    target_link_libraries(test_fence_renderer PRIVATE viewer_renderer glfw)
    target_compile_definitions(test_fence_renderer PRIVATE
        GLFW_INCLUDE_NONE GL_GLEXT_PROTOTYPES
        VIEWER_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders")
    viewer_warnings(test_fence_renderer)
    add_test(NAME viewer_fence_renderer COMMAND test_fence_renderer)
    set_tests_properties(viewer_fence_renderer PROPERTIES SKIP_RETURN_CODE 77)

    add_executable(test_building_renderer tests/test_building_renderer.cpp)
    target_link_libraries(test_building_renderer PRIVATE viewer_renderer glfw)
    target_compile_definitions(test_building_renderer PRIVATE
        GLFW_INCLUDE_NONE GL_GLEXT_PROTOTYPES
        VIEWER_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders")
    viewer_warnings(test_building_renderer)
    add_test(NAME viewer_building_renderer COMMAND test_building_renderer)
    set_tests_properties(viewer_building_renderer PROPERTIES SKIP_RETURN_CODE 77)

    add_executable(test_rain_renderer tests/test_rain_renderer.cpp)
    target_link_libraries(test_rain_renderer PRIVATE viewer_renderer glfw)
    target_compile_definitions(test_rain_renderer PRIVATE
        GLFW_INCLUDE_NONE GL_GLEXT_PROTOTYPES
        VIEWER_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders")
    viewer_warnings(test_rain_renderer)
    add_test(NAME viewer_rain_renderer COMMAND test_rain_renderer)
    set_tests_properties(viewer_rain_renderer PROPERTIES SKIP_RETURN_CODE 77)

    add_executable(test_tree_layer tests/test_tree_layer.cpp)
    target_link_libraries(test_tree_layer PRIVATE viewer_trees OpenSSL::Crypto)
    target_include_directories(test_tree_layer SYSTEM PRIVATE "${tinygltf_SOURCE_DIR}")
    viewer_warnings(test_tree_layer)
    add_test(NAME viewer_tree_layer COMMAND test_tree_layer)

    add_executable(test_tree_visibility tests/test_tree_visibility.cpp)
    target_link_libraries(test_tree_visibility PRIVATE viewer_trees viewer_camera)
    viewer_warnings(test_tree_visibility)
    add_test(NAME viewer_tree_visibility COMMAND test_tree_visibility)

    add_executable(test_model_visibility tests/test_model_visibility.cpp)
    target_link_libraries(test_model_visibility PRIVATE viewer_visibility viewer_camera)
    viewer_warnings(test_model_visibility)
    add_test(NAME viewer_model_visibility COMMAND test_model_visibility)

    add_executable(test_tree_collision tests/test_tree_collision.cpp)
    target_link_libraries(test_tree_collision PRIVATE viewer_fps)
    viewer_warnings(test_tree_collision)
    add_test(NAME viewer_tree_collision COMMAND test_tree_collision)

    # Fake GL entry points exercise delayed query results without a display.
    add_executable(test_gpu_timer tests/test_gpu_timer.cpp src/gpu_timer.cpp)
    target_include_directories(test_gpu_timer PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${OPENGL_INCLUDE_DIR}")
    target_compile_definitions(test_gpu_timer PRIVATE GL_GLEXT_PROTOTYPES)
    viewer_warnings(test_gpu_timer)
    add_test(NAME viewer_gpu_timer COMMAND test_gpu_timer)

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

    foreach(suite collision fps camera_rig player_noise)
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
        add_test(NAME viewer_door_walking_godollo COMMAND test_door_walking
            "${CMAKE_CURRENT_SOURCE_DIR}/output/godollo.glb")
    endif()

    # Keep test assertions active even when checking an optimized viewer build.
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(test_model PRIVATE -UNDEBUG)
        target_compile_options(test_model_visibility PRIVATE -UNDEBUG)
        target_compile_options(test_camera PRIVATE -UNDEBUG)
    endif()
endif()
