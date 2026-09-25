#include <newbase/sdl/logging_handler.hpp>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_init.h>
#include <unordered_map>
#include <string>

namespace nb::log {

    // listeners with handles
    static std::unordered_map<int, observer_t> _observers;
    static int _handle_counter {0}; 

    // for thread-safe handling
    // if something is logged not on the main thread, the message is
    // serialized and stored for later dispatch on the main thread
    // note that this incurs heap allocation, so it's best to avoid
    // excessive logging from other threads
    struct canned_message
    {
        int category;
        SDL_LogPriority prio;
        std::string msg;
    };


    void _dispatch_now(int category, SDL_LogPriority prio, const char *msg)
    {
        for(auto &pair: _observers)
        {
            pair.second(category, static_cast<int>(prio), msg);
        }

    }


    void _dispatch(void *, int category, SDL_LogPriority prio, const char *msg)
    {
        // can be called from multiple threads (but only one at once)

        if(SDL_IsMainThread())
        {
            _dispatch_now(category, prio, msg);
        }
        else
        {
            // send log to main thread via SDL's event loop instead, with a thread identifier prepended
            auto newmsg = new canned_message(category, prio, "[thread " + std::to_string(SDL_GetCurrentThreadID()) + "] ");
            newmsg->msg += msg;
            SDL_RunOnMainThread(+[](void* msg) {
                canned_message *cm = static_cast<canned_message*>(msg);
                _dispatch_now(cm->category, cm->prio, cm->msg.c_str());
                delete cm;

            }, newmsg, false);

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
