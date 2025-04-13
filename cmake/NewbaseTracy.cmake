if(NEWBASE_TRACING)
    if(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
        set(TRACY_SOURCES "vendored/tracy/public/TracyClient.cpp")
        set(TRACY_DEFINES "-DTRACY_ENABLE")
    endif()
endif()
set(TRACY_INCLUDES "vendored/tracy/public/") # should always be available to avoid mess of ifdefs