#pragma once

#include <newbase/nb_config.h>
#include <SDL3/SDL_log.h>

namespace nb {
namespace log {

    enum class category : int {
        APPLICATION,
        ERROR,
        ASSERT,
        SYSTEM,
        AUDIO,
        VIDEO,
        RENDER,
        INPUT,
        TEST,
        GPU,
        _COUNT
    };

    enum class priority : int {
        INVALID,
        TRACE,
        VERBOSE,
        DBG,
        INFO,
        WARN,
        ERROR,
        CRITICAL,
        _COUNT
    };

// TODO: respect static log level in all logging function templates

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-security"
#endif

    template<typename... Params>
    inline void verb(const char *msg, Params... params)
    {
        if constexpr(NB_STATIC_LOG_LEVEL_INT <= static_cast<int>(priority::VERBOSE))
            SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, msg, params...);
    }

    template<typename... Params>
    inline void debug(const char *msg, Params... params)
    {
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, msg, params...);
    }

    template<typename... Params>
    inline void info(const char *msg, Params... params)
    {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, msg, params...);
    }

    template<typename... Params>
    inline void warn(const char *msg, Params... params)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, msg, params...);
    }

    template<typename... Params>
    inline void error(const char *msg, Params... params)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, msg, params...);
    }

    template<typename... Params>
    inline void critical(const char *msg, Params... params)
    {
        SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, msg, params...);
    }

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

}
}
