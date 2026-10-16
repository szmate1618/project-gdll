find_package(GameNetworkingSockets CONFIG QUIET)
if(NOT TARGET GameNetworkingSockets::GameNetworkingSockets AND NOT TARGET GameNetworkingSockets::static)
    find_package(Protobuf REQUIRED)
    # These dependency options are scoped here, away from the viewer build.
    set(BUILD_SHARED_LIB OFF)
    set(BUILD_STATIC_LIB ON)
    set(BUILD_EXAMPLES OFF)
    set(BUILD_TESTS OFF)
    set(BUILD_TOOLS OFF)
    set(ENABLE_ICE OFF)
    set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
    FetchContent_Declare(gamenetworkingsockets
        URL https://codeload.github.com/ValveSoftware/GameNetworkingSockets/tar.gz/505c697d0abef5da2ff3be35aa4ea3687597c3e9
        URL_HASH SHA256=5c138d89185113d4deb67ad359f6245a77af654326e6eb50923b1d732ca8ad25
        DOWNLOAD_DIR "${CMAKE_BINARY_DIR}/_archives"
        DOWNLOAD_NAME gamenetworkingsockets-1.4.1.tar.gz)
    FetchContent_MakeAvailable(gamenetworkingsockets)
endif()
if(TARGET GameNetworkingSockets::static)
    set(mp_transport GameNetworkingSockets::static)
else()
    set(mp_transport GameNetworkingSockets::GameNetworkingSockets)
endif()

add_library(multiplayer_core STATIC protocol.cpp scene.cpp resources.cpp network.cpp fragments.cpp
    server/world.cpp client/prediction.cpp client/snapshots.cpp)
target_include_directories(multiplayer_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(multiplayer_core PUBLIC viewer_fps viewer_zombies viewer_interaction
    viewer_objects viewer_forests viewer_poles viewer_guardrails viewer_weapons ${mp_transport})
target_compile_definitions(multiplayer_core PUBLIC
    MP_ROOT="${PROJECT_SOURCE_DIR}")
viewer_warnings(multiplayer_core)

add_executable(godollo_server server/main.cpp)
target_link_libraries(godollo_server PRIVATE multiplayer_core)
viewer_warnings(godollo_server)

add_executable(godollo_client client/main.cpp)
target_link_libraries(godollo_client PRIVATE multiplayer_core)
if(GD_VIEWER)
    target_sources(godollo_client PRIVATE client/players.cpp "${PROJECT_SOURCE_DIR}/src/reticle_overlay.cpp")
    target_link_libraries(godollo_client PRIVATE viewer_renderer glfw)
    target_compile_definitions(godollo_client PRIVATE MP_GRAPHICS GLFW_INCLUDE_NONE GL_GLEXT_PROTOTYPES)
endif()
viewer_warnings(godollo_client)

if(BUILD_TESTING)
    add_executable(test_multiplayer tests/test_multiplayer.cpp)
    target_link_libraries(test_multiplayer PRIVATE multiplayer_core)
    viewer_warnings(test_multiplayer)
    add_dependencies(test_multiplayer viewer_test_asset)
    add_test(NAME multiplayer_core COMMAND test_multiplayer "${VIEWER_TEST_ASSET}")
    add_executable(test_server_handshake tests/test_server_handshake.cpp)
    target_link_libraries(test_server_handshake PRIVATE multiplayer_core)
    viewer_warnings(test_server_handshake)
    add_dependencies(test_server_handshake viewer_test_asset)
    add_test(NAME multiplayer_handshake COMMAND test_server_handshake "${VIEWER_TEST_ASSET}")
    add_test(NAME multiplayer_loopback COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/loopback.py"
        "$<TARGET_FILE:godollo_server>" "$<TARGET_FILE:godollo_client>" "${VIEWER_TEST_ASSET}")
    set_tests_properties(multiplayer_loopback PROPERTIES TIMEOUT 45)
endif()
