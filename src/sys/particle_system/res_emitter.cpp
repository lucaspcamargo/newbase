#include <newbase/sys/particle_system/particle_emitter.hpp>
#include <newbase/res/manager.hpp>
#include <newbase/yaml/glm.hpp>
#include <newbase/log.hpp>


using namespace nb;

// particle emitter loader
bool rparticle_emitter::do_load()
{
    log::info("[rloader_particle_emitter] loading: %x", id());

    std::vector<char> data;
    if (!rman().read_all_sync(id(), data, true))
    {
        log::error("[rloader_particle_emitter] cannot read: %x", id());
        return false;
    }

    auto tree = ryml::parse_in_place(c4::to_substr(data.data()));
    auto root = tree.rootref();

    if (root.has_child("max_particles"))
    {
        int mp = max_particles;
        root["max_particles"] >> mp;
        max_particles = mp;
    }

    if (root.has_child("pre_simulate_frames"))
    {
        int psf = 0;
        root["pre_simulate_frames"] >> psf;
        pre_simulate_frames = psf;
    }

    if (root.has_child("initial_burst"))
    {
        int ib = 0;
        root["initial_burst"] >> ib;
        initial_burst = ib;
    }

    if (root.has_child("blend_mode"))
    {
        std::string bm;
        root["blend_mode"] >> bm;
        if (bm == "add") blend_mode = particle_blend_mode::ADD;
    }

    if (root.has_child("texture"))
    {
        std::string tex_path;
        root["texture"] >> tex_path;
        auto tex_id = entt::hashed_string{tex_path.c_str()}.value();
        tex = rman().load_sync<rtexture>(tex_id);
        if (!tex)
            log::warn("[rloader_particle_emitter] cannot load texture '%s'", tex_path.c_str());
    }

    if (root.has_child("emitter"))
    {
        auto em  = root["emitter"];
        auto& cfg = emitter;
        if (em.has_child("rate"))               try_load_float(em["rate"],               cfg.rate);
        if (em.has_child("lifetime"))           try_load_float(em["lifetime"],            cfg.lifetime);
        if (em.has_child("lifetime_variance"))  try_load_float(em["lifetime_variance"],   cfg.lifetime_variance);
        if (em.has_child("pos_variance"))       try_load_vec2 (em["pos_variance"],        cfg.pos_variance);
        if (em.has_child("vel"))                try_load_vec2 (em["vel"],                 cfg.vel);
        if (em.has_child("vel_variance"))       try_load_vec2 (em["vel_variance"],        cfg.vel_variance);
        if (em.has_child("vel_angle_variance")) try_load_float(em["vel_angle_variance"],  cfg.vel_angle_variance);
        if (em.has_child("color"))              try_load_vec4 (em["color"],               cfg.color);
        if (em.has_child("color_variance"))     try_load_vec4 (em["color_variance"],      cfg.color_variance);
        if (em.has_child("scale"))              try_load_float(em["scale"],               cfg.scale);
        if (em.has_child("scale_variance"))     try_load_float(em["scale_variance"],      cfg.scale_variance);
        if (em.has_child("rotation"))           try_load_float(em["rotation"],            cfg.rotation);
        if (em.has_child("rotation_variance"))  try_load_float(em["rotation_variance"],   cfg.rotation_variance);
    }

    if (root.has_child("affectors"))
    {
        for (auto aff_node : root["affectors"])
        {
            particle_affector_config aff;

            std::string type_str;
            if (aff_node.has_child("type")) aff_node["type"] >> type_str;

            using AT = particle_affector_config::type;
            if      (type_str == "acceleration") aff.affector_type = AT::ACCELERATION;
            else if (type_str == "drag")         aff.affector_type = AT::DRAG;
            else if (type_str == "color_fade")   aff.affector_type = AT::COLOR_FADE;
            else if (type_str == "scale_fade")   aff.affector_type = AT::SCALE_FADE;
            else if (type_str == "attractor")    aff.affector_type = AT::ATTRACTOR;
            else
            {
                log::warn("[rloader_particle_emitter] unknown affector type '%s'", type_str.c_str());
                continue;
            }

            if (aff_node.has_child("value"))        try_load_vec2 (aff_node["value"],        aff.vec2_val);
            if (aff_node.has_child("rgba_per_sec")) try_load_vec4 (aff_node["rgba_per_sec"], aff.vec4_val);
            if (aff_node.has_child("per_sec"))      try_load_float(aff_node["per_sec"],       aff.float_val);
            if (aff_node.has_child("gain"))         try_load_float(aff_node["gain"],          aff.float_val);
            if (aff_node.has_child("kill_on_zero")) try_load_bool (aff_node["kill_on_zero"],  aff.kill_on_zero);

            affectors.push_back(aff);
        }
    }

    return true;
}
