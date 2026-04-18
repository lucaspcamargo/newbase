#pragma once

#include <newbase/res/fwd.hpp>
#include <entt/core/ident.hpp>
#include <memory>

namespace nb {

struct rloader_etree {
    using result_type = std::shared_ptr<retree>;
    result_type operator()(entt::id_type) const;
};

struct rloader_sprite {
    using result_type = std::shared_ptr<rsprite>;
    result_type operator()(entt::id_type) const;
};

struct rloader_texture {
    using result_type = std::shared_ptr<rtexture>;
    result_type operator()(entt::id_type) const;
};

struct rloader_script {
    using result_type = std::shared_ptr<rscript>;
    result_type operator()(entt::id_type) const;
};

struct rloader_vorbis {
    using result_type = std::shared_ptr<rvorbis>;
    result_type operator()(entt::id_type) const;
};

struct rloader_wav {
    using result_type = std::shared_ptr<rwav>;
    result_type operator()(entt::id_type) const;
};

struct rloader_yaml {
    using result_type = std::shared_ptr<ryaml>;
    result_type operator()(entt::id_type) const;
};

struct rloader_particle_emitter {
    using result_type = std::shared_ptr<rparticle_emitter>;
    result_type operator()(entt::id_type) const;
};

}