#include <newbase/clock/clock.hpp>
#include <sol/sol.hpp>

using namespace nb;

void clock::bind(void *state)
{
    sol::state_view lua{reinterpret_cast<lua_State*>(state)};
    lua["system_clock"] = this->metatype_id();
    lua["clock_update_add"] = [this](sol::protected_function func) -> int {
        auto cb_id = this->update_add([func](float delta){
            func(delta);
        });
        // TODO remove callback when script is destroyed
        // idea: have a table of destructors in script environment
        // add a callable to the destructor list that removes the callback
        // the above can be made into an utility template
        return cb_id;
    };
}