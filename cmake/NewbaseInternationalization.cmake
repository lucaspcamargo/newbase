find_program(XGETTEXT_EXECUTABLE xgettext)
find_program(MSGMERGE_EXECUTABLE msgmerge)

# TODO complete this
#      since we should only ship po files in the resource dirs, to avoid pollution,
#      we could keep the pot files in the
function(newbase_register_i18n)

    message(FATAL_ERROR "[newbase_register_i18n] not done yet!")

    if(NOT XGETTEXT_EXECUTABLE OR NOT MSGMERGE_EXECUTABLE)
        message("[newbase_register_i18n] we could not find xgettext and/or msgmerge")
        message("[newbase_register_i18n] cannot update target po files")
    endif()

        set(PO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/po")
        set(POT_FILE "${PO_DIR}/messages.pot")
        set(POTFILES_IN "${PO_DIR}/POTFILES.in")

        file(GLOB PO_FILES "${PO_DIR}/*.po")

        # Target: cmake --build <build_dir> --target update-po
        add_custom_target(update-po
            COMMAND ${XGETTEXT_EXECUTABLE}
                --files-from=${POTFILES_IN}
                --directory=${CMAKE_CURRENT_SOURCE_DIR}
                --output=${POT_FILE}
                --keyword=_
                --keyword=N_
                --keyword=gettext
                --keyword=pgettext:1c,2
                --keyword=npgettext:1c,2,3
                --add-comments=TRANSLATORS
                --from-code=UTF-8
                --package-name=${PROJECT_NAME}
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            COMMENT "Extracting strings to po/messages.pot..."
        )

        foreach(PO_FILE ${PO_FILES})
            add_custom_command(
                TARGET update-po POST_BUILD
                COMMAND ${MSGMERGE_EXECUTABLE}
                    --update
                    --backup=none
                    ${PO_FILE}
                    ${POT_FILE}
                COMMENT "Merging template into ${PO_FILE}..."
            )
        endforeach()
    endif()
endfunction(newbase_register_i18n)
