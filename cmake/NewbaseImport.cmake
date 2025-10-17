# This file is to be included by projects that want to use Newbase as a dependency.
# It import the necessary targets and sets up variables.
# It assumes Newbase has been built and exported to a known location.
# It also brings in the newbase_* family of CMake functions

if(NOT DEFINED NEWBASE_ROOT OR "${NEWBASE_ROOT}" STREQUAL "")
    message(FATAL_ERROR "[newbase_import] NEWBASE_ROOT variable must be set and not empty.")
endif()

if(NOT DEFINED NEWBASE_BUILD_ROOT OR "${NEWBASE_BUILD_ROOT}" STREQUAL "")
    message(FATAL_ERROR "[newbase_import] NEWBASE_BUILD_ROOT variable must be set and not empty.")
endif()

set(NEWBASE_IMPORTED TRUE)

include(${NEWBASE_BUILD_ROOT}/newbase-lua-export.cmake)
include(${NEWBASE_BUILD_ROOT}/newbase-ymfm-export.cmake)
include(${NEWBASE_BUILD_ROOT}/newbase-glm-export.cmake)
include(${NEWBASE_BUILD_ROOT}/newbase-box2d-export.cmake)
include(${NEWBASE_BUILD_ROOT}/vendored/SDL/SDL3headersTargets.cmake)
include(${NEWBASE_BUILD_ROOT}/vendored/SDL/SDL3staticTargets.cmake)
include(${NEWBASE_BUILD_ROOT}/vendored/rapidyaml/rymlTargets.cmake)
include(${NEWBASE_BUILD_ROOT}/vendored/entt/EnTTTargets.cmake)
include(${NEWBASE_BUILD_ROOT}/vendored/sol2/cmake/sol2-targets.cmake)
include(${NEWBASE_BUILD_ROOT}/newbase-export.cmake)
include(${NEWBASE_ROOT}/cmake/NewbaseFunctions.cmake)