cmake_minimum_required(VERSION 3.20)
if(NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "Package the Linux client on Linux")
endif()
get_filename_component(source_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
if(NOT DEFINED BUILD_DIR)
    set(BUILD_DIR "${source_root}/build")
endif()
get_filename_component(BUILD_DIR "${BUILD_DIR}" ABSOLUTE)
set(client "${BUILD_DIR}/multiplayer/godollo_client")
if(NOT EXISTS "${client}")
    message(FATAL_ERROR "Build godollo_client and viewer_test_asset first")
endif()

# Keep system runtimes and graphics libraries together with the host's drivers.
file(GET_RUNTIME_DEPENDENCIES EXECUTABLES "${client}"
    RESOLVED_DEPENDENCIES_VAR libraries UNRESOLVED_DEPENDENCIES_VAR missing
    PRE_EXCLUDE_REGEXES "^ld-linux" "^lib(c|m|dl|rt|pthread|resolv|util|anl)\\.so"
        "^lib(stdc\\+\\+|gcc_s)\\.so"
        "^lib(GL|GLX|GLdispatch|OpenGL|EGL|GLESv[12])\\.so")
if(missing)
    message(FATAL_ERROR "Unresolved client libraries: ${missing}")
endif()

string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef suffix)
set(staging "${BUILD_DIR}/linux-client-${suffix}")
set(package "${staging}/godollo-client-linux-x86_64")
file(MAKE_DIRECTORY "${package}/assets" "${package}/multiplayer/client" "${package}/lib" "${package}/licenses")
file(COPY "${client}" DESTINATION "${package}")
file(COPY "${source_root}/shaders" "${source_root}/items" "${source_root}/credits" DESTINATION "${package}")
file(COPY "${source_root}/inventory.toml" DESTINATION "${package}")
file(COPY "${source_root}/assets/test.glb" "${source_root}/assets/test.navmesh"
    DESTINATION "${package}/assets")
foreach(weapon pistol submachine_gun shotgun sniper he_grenade)
    file(COPY "${BUILD_DIR}/assets/weapons/${weapon}.glb" DESTINATION "${package}/assets/weapons")
endforeach()
file(COPY "${source_root}/assets/weapons/README.md" DESTINATION "${package}/assets/weapons")
file(COPY "${BUILD_DIR}/audio/" DESTINATION "${package}/assets/audio")
file(COPY "${source_root}/multiplayer/client/players.vert" "${source_root}/multiplayer/client/players.frag"
    DESTINATION "${package}/multiplayer/client")
file(COPY "${CMAKE_CURRENT_LIST_DIR}/run-client.sh" DESTINATION "${package}"
    FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
configure_file("${CMAKE_CURRENT_LIST_DIR}/README.md" "${package}/README.md" COPYONLY)

foreach(library IN LISTS libraries)
    file(COPY "${library}" DESTINATION "${package}/lib" FOLLOW_SYMLINK_CHAIN)
    get_filename_component(real_library "${library}" REALPATH)
    if(real_library MATCHES "/libopenal[.]")
        # This is the pinned build-tree OpenAL, not a Debian-owned library.
        continue()
    endif()
    execute_process(COMMAND dpkg-query -S "${real_library}" RESULT_VARIABLE result
        OUTPUT_VARIABLE owner ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT result EQUAL 0)
        execute_process(COMMAND dpkg-query -S "${library}" RESULT_VARIABLE result
            OUTPUT_VARIABLE owner ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
    endif()
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Cannot locate Debian copyright for ${library}")
    endif()
    string(REGEX MATCH "^[^:\n]+" owner "${owner}")
    configure_file("/usr/share/doc/${owner}/copyright" "${package}/licenses/${owner}.txt" COPYONLY)
endforeach()

file(COPY "${source_root}/credits/software/nlohmann-json.txt" "${source_root}/credits/software/stb.txt"
    DESTINATION "${package}/licenses")

set(projects GameNetworkingSockets GLFW glm box3d tinygltf tomlplusplus OpenAL)
set(license_names LICENSE LICENSE.md copying.txt LICENSE LICENSE LICENSE COPYING)
foreach(project IN LISTS projects)
    load_cache("${BUILD_DIR}" READ_WITH_PREFIX dependency_ "${project}_SOURCE_DIR")
endforeach()
configure_file("${dependency_OpenAL_SOURCE_DIR}/LICENSE-pffft" "${package}/licenses/OpenAL-pffft.txt" COPYONLY)
configure_file("${dependency_OpenAL_SOURCE_DIR}/fmt-11.1.1/LICENSE" "${package}/licenses/OpenAL-fmt.txt" COPYONLY)
foreach(project license IN ZIP_LISTS projects license_names)
    configure_file("${dependency_${project}_SOURCE_DIR}/${license}" "${package}/licenses/${project}.txt" COPYONLY)
endforeach()
if(NOT DEFINED PROTOBUF_LICENSE)
    set(PROTOBUF_LICENSE "/usr/share/doc/libprotobuf-dev/copyright")
endif()
configure_file("${PROTOBUF_LICENSE}" "${package}/licenses/Protobuf.txt" COPYONLY)

set(archive "${BUILD_DIR}/godollo-client-linux-x86_64.tar.gz")
execute_process(COMMAND "${CMAKE_COMMAND}" -E tar czf "${staging}/client.tar.gz" --format=gnutar
    godollo-client-linux-x86_64 WORKING_DIRECTORY "${staging}" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Cannot create Linux client archive; staging is in ${staging}")
endif()
file(RENAME "${staging}/client.tar.gz" "${archive}")
file(REMOVE_RECURSE "${staging}")
message(STATUS "Linux client package: ${archive}")
