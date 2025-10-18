#include <newbase/reflection/contexts.hpp>

namespace nb {
namespace rtti {   

entt::meta_ctx& ctx_systems() {
    static entt::meta_ctx ctx;
    return ctx;
}

}
}
