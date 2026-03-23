#include <newbase/reflection/coercions.hpp>
#include <entt/meta/factory.hpp>
#include <lua.h>

namespace nb::rtti {

    void register_coercions()
    {
        // we use float throughout the apis, let's ensure lua number types can cleanly convert to floats

        entt::meta_factory<double>{}.conv<float>();
        entt::meta_factory<lua_Integer>{}.conv<float>();
        entt::meta_factory<lua_Integer>{}.conv<double>();
    }

}
