# Detect Python 3.x interpreter
# Priority: PYTHON_INTERPRETER env var > venv/bin/python > system python

if(DEFINED ENV{PYTHON_INTERPRETER} AND NOT "$ENV{PYTHON_INTERPRETER}" STREQUAL "")
    set(NEWBASE_PYTHON_INTERPRETER "$ENV{PYTHON_INTERPRETER}")
else()
    # Try venv/bin/python
    if(WIN32)
        set(_venv_python "${CMAKE_CURRENT_SOURCE_DIR}/venv/Scripts/python.exe")
    else()
        set(_venv_python "${CMAKE_CURRENT_SOURCE_DIR}/venv/bin/python")
    endif()
    
    if(EXISTS "${_venv_python}")
        set(NEWBASE_PYTHON_INTERPRETER "${_venv_python}")
    else()
        # Fallback to system python
        find_program(NEWBASE_PYTHON_INTERPRETER NAMES python3 python)
    endif()
endif()

if(NOT NEWBASE_PYTHON_INTERPRETER)
    message(FATAL_ERROR "Python 3.x interpreter not found")
endif()

message(STATUS "[newbase_python] Python interpreter: ${NEWBASE_PYTHON_INTERPRETER}")

