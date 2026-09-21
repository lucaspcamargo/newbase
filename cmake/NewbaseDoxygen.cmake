find_package(Doxygen OPTIONAL_COMPONENTS dot)

if(DOXYGEN_FOUND)

    if(DOXYGEN_DOT_FOUND)
        set(HAVE_DOT YES)
    else()
        set(HAVE_DOT NO)
    endif()

    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/doc/Doxyfile.in"
        "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile"
        @ONLY
    )

    # 3. Add a custom target excluded from default 'all' build
    add_custom_target(
        doc
        COMMAND ${DOXYGEN_EXECUTABLE} "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile"
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        COMMENT "Generating API documentation with Doxygen"
        VERBATIM
    )

    message(STATUS "Doxygen found: 'doc' target available.")
else()
    message(STATUS "Doxygen not found: 'doc' target will not be created.")
endif()
