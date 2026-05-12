#include <newbase/reflection/coercions.hpp>
#include <entt/meta/factory.hpp>
#include <entt/entity/entity.hpp>
#include <lua.h>

namespace nb::rtti {

    static lua_Integer conv_entity_to_lua_int(entt::entity v) { return static_cast<lua_Integer>(entt::to_integral(v)); }
    static entt::entity conv_lua_int_to_entity(lua_Integer v) { return static_cast<entt::entity>(v); }

    void register_coercions()
    {
        // we use float throughout the apis, let's ensure lua number types can cleanly convert to floats

        // lua numbers -> C numeric types
        entt::meta_factory<double>{}.conv<float>();
        entt::meta_factory<lua_Integer>{}.conv<float>();
        entt::meta_factory<lua_Integer>{}.conv<double>();
        entt::meta_factory<lua_Integer>{}.conv<int>();
        entt::meta_factory<lua_Integer>{}.conv<unsigned int>();

        // entt::entity <-> lua_Integer
        entt::meta_factory<lua_Integer>{}.conv<&conv_lua_int_to_entity>();
        entt::meta_factory<entt::entity>{}.conv<&conv_entity_to_lua_int>();
    }

}
