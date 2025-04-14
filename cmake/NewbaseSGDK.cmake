# Functions and variables for SGDK APi support

if(NEWBASE_SGDK)

    # ymfm library
    # we build only what we need for SGDK support (core and OPN)
    add_library(ymfm STATIC EXCLUDE_FROM_ALL
        vendored/ymfm/src/ymfm_adpcm.cpp
        vendored/ymfm/src/ymfm_fm.ipp
        vendored/ymfm/src/ymfm_opn.cpp
        vendored/ymfm/src/ymfm_pcm.cpp
        vendored/ymfm/src/ymfm_ssg.cpp
    )
    target_link_directories(ymfm PUBLIC vendored/ymfm/src )
    export(TARGETS ymfm NAMESPACE newbase FILE newbase-ymfm-export.cmake)

    set(SGDK_SUPPORT_LIBS ymfm)
    set(SGDK_SUPPORT_SOURCES 
        src/sgdk/sgdk.cpp
    )

endif()