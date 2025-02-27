#pragma once

#include <newbase/res/fwd.h>
#include <entt/core/ident.hpp>
#include <memory>

namespace nb {

struct rloader_sprite {
    using result_type = std::shared_ptr<rsprite>;
    result_type operator()(entt::id_type) const;
};

struct rloader_texture {
    using result_type = std::shared_ptr<rtexture>;
    result_type operator()(entt::id_type) const;
};

}