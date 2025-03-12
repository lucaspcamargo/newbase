#pragma once

#include <functional>

namespace nb {
namespace log {

    typedef std::function<void(int category, int prio, const char *msg)> observer_t;

    int register_observer(observer_t observer);
    bool unregister_observer(int handle);

    // to be called on engine initialization
    void setup_handler();

}
}