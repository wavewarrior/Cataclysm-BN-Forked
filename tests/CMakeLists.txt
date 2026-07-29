if (BUILD_TESTING)
    # CONFIGURE_DEPENDS mirrors the .cpp glob in src/CMakeLists.txt: without it a
    # newly added tests/*.cpp is silently absent from the binary until someone
    # re-runs cmake by hand, and the missing test looks like a filter typo.
    file(GLOB CATACLYSM_BN_TEST_SOURCES CONFIGURE_DEPENDS
            ${CMAKE_SOURCE_DIR}/tests/*.cpp)

    # coop_integration_test.cpp talks raw BSD sockets (arpa/inet.h, netinet/in.h,
    # sys/select.h, unistd.h) and coordinates the two processes through /tmp
    # control files.  None of that exists on Windows, so drop it there rather
    # than failing the whole test binary to compile.
    if (WIN32)
        list(REMOVE_ITEM CATACLYSM_BN_TEST_SOURCES
                ${CMAKE_SOURCE_DIR}/tests/coop_integration_test.cpp)
    endif ()

    # Enabling benchmarks
    add_definitions(-DCATCH_CONFIG_ENABLE_BENCHMARKING -DCATCH_AMALGAMATED_CUSTOM_MAIN)

    # TODO: build MO files required for tests

    # Catch2 v3 amalgamated source must be compiled
    set(CATCH_V3_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/catch/catch_amalgamated.cpp)
    set_source_files_properties(${CATCH_V3_SOURCE} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

    # test_main.cpp includes catch with CATCH_CONFIG_RUNNER - skip PCH
    set_source_files_properties(test_main.cpp PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

    add_executable(cata_test-tiles ${CATACLYSM_BN_TEST_SOURCES} ${CATCH_V3_SOURCE})
    target_include_directories(cata_test-tiles PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(cata_test-tiles PRIVATE cataclysm-bn-tiles-common)

    # Mirror the game target's Windows DLL staging (src/CMakeLists.txt): without
    # dxcompiler.dll / dxil.dll / spirv-cross-c-shared.dll beside the binary the
    # loader kills it before main() runs, so the suite exits non-zero with no
    # output at all.
    if (WIN32)
        add_custom_command(TARGET cata_test-tiles POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_RUNTIME_DLLS:cata_test-tiles>
                    $<TARGET_FILE_DIR:cata_test-tiles>
            COMMAND_EXPAND_LISTS
            VERBATIM)

        foreach(_t IN ITEMS dxcompiler dxildll spirv-cross-c-shared)
            if(TARGET ${_t})
                get_target_property(_type ${_t} TYPE)
                if(_type STREQUAL "SHARED_LIBRARY" OR _type STREQUAL "MODULE_LIBRARY")
                    add_custom_command(TARGET cata_test-tiles POST_BUILD
                        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                                $<TARGET_FILE:${_t}>
                                $<TARGET_FILE_DIR:cata_test-tiles>
                        VERBATIM)
                endif()
            endif()
        endforeach()
    endif ()

    add_test(
        NAME test-tiles
        COMMAND sh -c "$<TARGET_FILE:cata_test-tiles> --rng-seed time"
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

    if (NOT "${CMAKE_EXPORT_COMPILE_COMMANDS}")
        target_precompile_headers(cata_test-tiles PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/pch/tests-pch.hpp)
    endif ()

    set_target_properties( cata_test-tiles PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" )
endif ()
