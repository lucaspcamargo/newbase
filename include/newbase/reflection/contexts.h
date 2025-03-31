#pragma once

#include <entt/meta/meta.hpp>
#include <unordered_map>

namespace nb {
namespace rtti {

// rtti info is generally provided when doing static initialiaztion
// since we need to ensure rtti contexts are ready at any time 
// during static initialization, we are using Meyer's Singletons

// if we start to use C++20 then maybe consinit can simplify this, see:
// https://www.cppstories.com/2023/ub-factory-constinit/

entt::meta_ctx& ctx_systems();

}
}