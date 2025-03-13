#pragma once

#include <functional>
#include <map>

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
        DEBUG,
        INFO,
        WARN,
        ERROR,
        CRITICAL,
        _COUNT
    };

    typedef std::function<void(int category, int prio, const char *msg)> observer_t;

    int register_observer(observer_t observer);
    bool unregister_observer(int handle);

    // to be called on engine initialization
    void setup_handler();

    // string helpers
    const char * category_str(category);
    const char * priority_str(priority);

    // get chars to output to terminal before and after log message (ansi escape sequences)
    // may return pair of nullptr for no decoration!
    std::pair<const char *, const char *> priority_ansi_decor(priority);
}
}