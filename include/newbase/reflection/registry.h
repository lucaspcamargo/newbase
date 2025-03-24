#pragma once

#include <entt/entt.hpp>
#include <functional>
#include <unordered_map>

namespace nb {
namespace rtti {

// a paremeterized registry with template storage
// meant to be used for storing ent::meta_factory instances
// seems unnecessary since entt reflection does the same thing
template <typename Key_t, typename Value_t>
class registry
{
public:
    using map_t = std::unordered_map<Key_t, Value_t>;

    inline static bool registrate(Key_t key, Value_t val) // should be "register" but is reserved, oh well
    {
        auto &map = get_map();
        if(map.contains(key))
            return false;
        get_map[key] = val;
        return true;
    }

private:
    static map_t& get_map();
};

template<typename Key_t, typename Value_t>
typename registry<Key_t, Value_t>::map_t& registry<Key_t, Value_t>::get_map()
{
    static map_t map {};
    return &map;
}

template<typename Concrete_t>
bool standard_registration(std::string key, typename entt::meta_factory<script_lua> factory)
{
    return registry<std::string, decltype(factory)>::registrate(key, factory);
}

}
}