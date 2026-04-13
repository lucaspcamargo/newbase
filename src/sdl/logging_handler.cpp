#include <newbase/sdl/logging_handler.hpp>
#include <SDL3/SDL_log.h>
#include <unordered_set>

namespace nb {
namespace log {

    static std::unordered_map<int, observer_t> _observers;
    static int _handle_counter {0}; 

    void _dispatch(void *userdata, int category, SDL_LogPriority prio, const char *msg)
    {
        // can be called from multiple threads (but only one at once)
        for(auto &pair: _observers)
        {
            pair.second(category, static_cast<int>(prio), msg);
        }
    }

    int register_observer(observer_t observer)
    {
        _observers[_handle_counter] = observer;
        return _handle_counter++;
    }

    bool unregister_observer(int handle)
    {
        if(auto it = _observers.find(handle); it != _observers.end())
        {
            _observers.erase(it);
            return true;
        }
        return false;
    }

    void setup_handler()
    {
        SDL_SetLogOutputFunction(_dispatch, nullptr);
    }

    const char * category_str(category cat)
    {
        static std::unordered_map<category, const char *> strings {
            {category::APPLICATION, "APPLICATION"},
            {category::ERROR, "ERROR"},
            {category::ASSERT, "ASSERT"},
            {category::SYSTEM, "SYSTEM"},
            {category::AUDIO, "AUDIO"},
            {category::VIDEO, "VIDEO"},
            {category::RENDER, "RENDER"},
            {category::INPUT, "INPUT"},
            {category::TEST, "TEST"},
            {category::GPU, "GPU"},
        };
        auto it = strings.find(cat);
        return it != strings.end()? it->second : "UNKNOWN";
    }

    const char * priority_str(priority prio)
    {
        static std::unordered_map<priority, const char *> strings {
            {priority::INVALID, "INVALID"},
            {priority::TRACE, "TRACE"},
            {priority::VERBOSE, "VERBOSE"},
            {priority::DBG, "DEBUG"},
            {priority::INFO, "INFO"},
            {priority::WARN, "WARN"},
            {priority::ERROR, "ERROR"},
            {priority::CRITICAL, "CRITICAL"},
        };
        auto it = strings.find(prio);
        return it != strings.end()? it->second : "UNKNOWN";
    }

    std::pair<const char *, const char *> priority_ansi_decor(priority prio)
    {
        int color = -1;
        switch (prio)
        {
        case priority::TRACE:
        [[fallthrough]];
        case priority::VERBOSE:
            color = 1;
            break;
        case priority::WARN:
            color = 2;
            break;
        case priority::ERROR:
            color = 3;
            break;
        case priority::CRITICAL:
            color = 4;
            break;
        default:;
        }

        if(color == 1)
            return {"\033[90m", "\033[0m"};
        else if(color == 2)
            return {"\033[93m", "\033[0m"};
        else if(color == 3)
            return {"\033[91m", "\033[0m"};
        else if(color == 4)
            return {"\033[91;1m", "\033[0m"};
        else 
            return {nullptr, nullptr};
        
    }
}
}