#include <newbase/res/writers.hpp>
#include <newbase/res/manager.hpp>
#include <newbase/res/texture.hpp>
#include <newbase/graphplan/plan.hpp>
#include <newbase/yaml/meta_any.hpp>
#include <newbase/log.hpp>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL.h>
#include <stb_image_write.h>
#include <ryml_std.hpp>
#include <vector>
#include <string>

namespace nb {

// stbi_write callback: appends written bytes to a std::vector<char>
static void stbi_write_to_vec(void* ctx, void* data, int size)
{
    auto* buf = static_cast<std::vector<char>*>(ctx);
    buf->insert(buf->end(), static_cast<char*>(data), static_cast<char*>(data) + size);
}

bool rwriter_texture(nb::resource* res)
{
    auto* rt = static_cast<rtexture*>(res);

    // Ensure we have a CPU surface to encode
    SDL_Surface* surf = rt->surf;
    if (!surf && rt->reload_surface)
        surf = rt->reload_surface(rt->id());
    if (!surf)
    {
        log::error("[rwriter_texture] no surface available for asset %x", rt->id());
        return false;
    }

    // Convert to RGBA32 for encoding if needed
    SDL_Surface* rgba = (surf->format == SDL_PIXELFORMAT_RGBA32)
                        ? surf
                        : SDL_ConvertSurface(surf, SDL_PIXELFORMAT_RGBA32);
    if (!rgba)
    {
        log::error("[rwriter_texture] surface conversion failed for asset %x", rt->id());
        if (surf != rt->surf) SDL_DestroySurface(surf);
        return false;
    }

    std::vector<char> png;
    int ok = stbi_write_png_to_func(stbi_write_to_vec, &png,
                                    rgba->w, rgba->h, 4,
                                    rgba->pixels, rgba->pitch);

    if (rgba != surf) SDL_DestroySurface(rgba);
    if (surf != rt->surf) SDL_DestroySurface(surf);

    if (!ok || png.empty())
    {
        log::error("[rwriter_texture] PNG encoding failed for asset %x", rt->id());
        return false;
    }

    return rman().write_all_sync(rt->id(), png.data(), png.size());
}

bool write_graphplan_plan(const graphplan::plan& plan, const char* path)
{
    ryml::Tree tree;
    ryml::NodeRef root = tree.rootref();
    root |= ryml::MAP;

    // Domain id
    root.append_child() << ryml::key("domain")
                        << c4::to_csubstr(plan.dom().id);

    // Nodes
    auto nodes_seq = root.append_child();
    nodes_seq << ryml::key("nodes");
    nodes_seq |= ryml::SEQ;

    for (const auto& [nid, nd] : plan.nodes)
    {
        const auto* tdef = plan.dom().find_type(nd.type);

        auto item = nodes_seq.append_child();
        item |= ryml::MAP;
        item.append_child() << ryml::key("id")   << nid;
        item.append_child() << ryml::key("type")
                            << c4::to_csubstr(tdef ? tdef->name : "UNKNOWN");

        auto pos = item.append_child();
        pos << ryml::key("pos");
        pos |= ryml::SEQ;
        pos.append_child() << nd.pos_x;
        pos.append_child() << nd.pos_y;

        if (!nd.properties.empty())
        {
            auto props = item.append_child();
            props << ryml::key("properties");
            props |= ryml::MAP;
            for (const auto& [pname, pval] : nd.properties)
                prop_to_yaml(props, c4::to_csubstr(pname), pval);
        }
    }

    // Links — stored as [from_node_id, from_pin_idx] / [to_node_id, to_pin_idx]
    auto links_seq = root.append_child();
    links_seq << ryml::key("links");
    links_seq |= ryml::SEQ;

    for (const auto& [lid, ld] : plan.links)
    {
        auto out_pin_it = plan.pins.find(ld.output_pin);
        auto in_pin_it  = plan.pins.find(ld.input_pin);
        if (out_pin_it == plan.pins.end() || in_pin_it == plan.pins.end()) continue;

        const auto& out_pin = out_pin_it->second;
        const auto& in_pin  = in_pin_it->second;
        auto from_nd_it = plan.nodes.find(out_pin.node_id);
        auto to_nd_it   = plan.nodes.find(in_pin.node_id);
        if (from_nd_it == plan.nodes.end() || to_nd_it == plan.nodes.end()) continue;

        int from_pin_idx = 0;
        for (size_t i = 0; i < from_nd_it->second.output_pins.size(); i++)
            if (from_nd_it->second.output_pins[i] == ld.output_pin) { from_pin_idx = (int)i; break; }

        int to_pin_idx = 0;
        for (size_t i = 0; i < to_nd_it->second.input_pins.size(); i++)
            if (to_nd_it->second.input_pins[i] == ld.input_pin) { to_pin_idx = (int)i; break; }

        auto link_item = links_seq.append_child();
        link_item |= ryml::MAP;

        auto from_seq = link_item.append_child();
        from_seq << ryml::key("from");
        from_seq |= ryml::SEQ;
        from_seq.append_child() << out_pin.node_id;
        from_seq.append_child() << from_pin_idx;

        auto to_seq = link_item.append_child();
        to_seq << ryml::key("to");
        to_seq |= ryml::SEQ;
        to_seq.append_child() << in_pin.node_id;
        to_seq.append_child() << to_pin_idx;
    }

    std::string yaml_str = ryml::emitrs_yaml<std::string>(tree);

    SDL_IOStream* io = SDL_IOFromFile(path, "wb");
    if (!io)
    {
        log::error("[write_graphplan_plan] cannot open for writing: %s", path);
        return false;
    }
    size_t written = SDL_WriteIO(io, yaml_str.data(), yaml_str.size());
    SDL_CloseIO(io);

    if (written != yaml_str.size())
    {
        log::error("[write_graphplan_plan] write failed: %s", path);
        return false;
    }
    log::info("[write_graphplan_plan] saved %zu bytes to %s", written, path);
    return true;
}

} // namespace nb
