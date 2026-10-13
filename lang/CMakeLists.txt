# English only: the repository ships no translation sources, so nothing is compiled. The target
# and the empty lang/mo directory stay because the build presets, the macOS bundle step and the
# release install all refer to them.
add_custom_target(translations ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/lang/mo
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

if (RELEASE)
    if (USE_PREFIX_DATA_DIR)
        install(DIRECTORY ${CMAKE_SOURCE_DIR}/lang/mo
            DESTINATION ${CMAKE_INSTALL_LOCALEDIR})
    else()
        install(DIRECTORY ${CMAKE_SOURCE_DIR}/lang/mo
            DESTINATION lang)
    endif()
endif ()
