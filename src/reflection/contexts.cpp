#include <newbase/reflection/contexts.h>

namespace nb {
namespace rtti {   

entt::meta_ctx& ctx_systems() {
    static entt::meta_ctx ctx;
    return ctx;
}

}
}
