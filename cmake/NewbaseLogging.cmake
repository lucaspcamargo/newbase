# setup configuration variables necessary for statically-compiled log pruning support

set(NEWBASE_ALL_LOG_LEVELS
    "INVALID"
    "TRACE"
    "VERBOSE"
    "DEBUG"
    "INFO"
    "WARN"
    "ERROR"
    "CRITICAL"
    )

list(FIND NEWBASE_ALL_LOG_LEVELS "${NEWBASE_LOG_LEVEL}" NEWBASE_LOG_LEVEL_INT)
if(NEWBASE_LOG_LEVEL_INT LESS 0)
    message( FATAL_ERROR "Invalid log level specified. Must be one of: ${NEWBASE_ALL_LOG_LEVELS}")
else()
    message("[newbase_logging] Building with log level ${NEWBASE_LOG_LEVEL} (${NEWBASE_LOG_LEVEL_INT})")
endif()