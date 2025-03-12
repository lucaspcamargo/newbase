#include <newbase/sdl/logging_handler.h>
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

}
}