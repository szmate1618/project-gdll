# Small original meshes are built offline with standard-library Python.
set(VIEWER_DOODAD_ASSETS "${CMAKE_CURRENT_BINARY_DIR}/assets/doodads")
set(VIEWER_DOODAD_MODELS)
foreach(doodad branch stone beer_bottle)
    list(APPEND VIEWER_DOODAD_MODELS "${VIEWER_DOODAD_ASSETS}/${doodad}.glb")
endforeach()
add_custom_command(
    OUTPUT ${VIEWER_DOODAD_MODELS}
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/create_doodad_assets.py"
        --output-dir "${VIEWER_DOODAD_ASSETS}"
    DEPENDS tools/create_doodad_assets.py src/doodad_geometry.py src/doodad_assets.py src/glb.py
    COMMENT "Building the original branch, stone and beer bottle (offline)"
    VERBATIM
)
add_custom_target(viewer_doodad_assets DEPENDS ${VIEWER_DOODAD_MODELS})

add_library(viewer_doodads STATIC src/doodad_placement.cpp src/doodad_state.cpp src/doodad_layer.cpp)
target_link_libraries(viewer_doodads PUBLIC viewer_forests viewer_objects)
target_include_directories(viewer_doodads SYSTEM PRIVATE "${tinygltf_SOURCE_DIR}")
viewer_warnings(viewer_doodads)

if(BUILD_TESTING)
    foreach(suite doodad_placement doodad_state doodad_layer)
        add_executable(test_${suite} tests/test_${suite}.cpp)
        target_link_libraries(test_${suite} PRIVATE viewer_doodads)
        viewer_warnings(test_${suite})
        add_test(NAME viewer_${suite} COMMAND test_${suite})
    endforeach()
    target_include_directories(test_doodad_state SYSTEM PRIVATE "${tinygltf_SOURCE_DIR}")
endif()
