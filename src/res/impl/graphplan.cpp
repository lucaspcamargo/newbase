#include <newbase/res/graphplan.hpp>
#include <newbase/res/manager.hpp>
#include <newbase/graphplan/domain_registry.hpp>
#include <newbase/yaml/meta_any.hpp>
#include <newbase/log.hpp>

#include <SDL3/SDL_iostream.h>


using namespace nb;

bool rgraphplan::_parse_graphplan(std::vector<char>& data)
{
    auto tree = ryml::parse_in_place(c4::to_substr(data.data()));
    auto root = tree.rootref();

    if (!root.has_child("domain"))
    {
        log::error("[rgraphplan] missing 'domain' field");
        return false;
    }

    root["domain"] >> domain_id;

    const graphplan::domain* dom = graphplan::find_domain(domain_id.c_str());
    if (!dom)
        log::warn("[rgraphplan] domain '%s' not registered — properties will be untyped",
                    domain_id.c_str());

    if (root.has_child("nodes"))
    {
        for (auto node_ref : root["nodes"])
        {
            rgraphplan::node_desc nd;
            node_ref["id"]   >> nd.id;
            node_ref["type"] >> nd.type_name;
            if (node_ref.has_child("pos"))
            {
                node_ref["pos"][0] >> nd.pos_x;
                node_ref["pos"][1] >> nd.pos_y;
            }
            if (node_ref.has_child("properties"))
            {
                const graphplan::node_type_def* tdef =
                dom ? dom->find_type_by_name(nd.type_name.c_str()) : nullptr;

                for (auto prop_ref : node_ref["properties"])
                {
                    std::string pname;
                    c4::from_chars(prop_ref.key(), &pname);

                    entt::meta_any hint;
                    if (tdef)
                    {
                        for (const auto& pd : tdef->props)
                            if (pname == pd.name) { hint = pd.default_value; break; }
                    }

                    entt::meta_any val;
                    if (prop_from_yaml(prop_ref, val, hint))
                        nd.properties[pname] = std::move(val);
                }
            }
            nodes.push_back(std::move(nd));
        }
    }

    if (root.has_child("links"))
    {
        for (auto link_ref : root["links"])
        {
            rgraphplan::link_desc ld;
            link_ref["from"][0] >> ld.from_node;
            link_ref["from"][1] >> ld.from_pin;
            link_ref["to"][0]   >> ld.to_node;
            link_ref["to"][1]   >> ld.to_pin;
            links.push_back(ld);
        }
    }

    log::info("[rgraphplan] parsed %zu nodes, %zu links",
                nodes.size(), links.size());
    return true;
}

bool rgraphplan::do_load()
{
    log::info("[rgraphplan] loading: %x", id());

    std::vector<char> data;
    if (!rman().read_all_sync(id(), data, true))
    {
        log::error("[rgraphplan] cannot read: %x", id());
        return false;
    }

    return _parse_graphplan(data);
}

std::shared_ptr<rgraphplan> rgraphplan::from_path(const char* path)
{
    log::info("[rgraphplan] loading from path: %s", path);

    SDL_IOStream* io = SDL_IOFromFile(path, "rb");
    if (!io)
    {
        log::error("[rgraphplan] cannot open: %s", path);
        return nullptr;
    }
    Sint64 sz = SDL_GetIOSize(io);
    if (sz <= 0) { SDL_CloseIO(io); return nullptr; }

    std::vector<char> data(static_cast<size_t>(sz) + 1, '\0');
    SDL_ReadIO(io, data.data(), static_cast<size_t>(sz));
    SDL_CloseIO(io);

    auto ret = std::make_shared<rgraphplan>(0);
    ret->_parse_graphplan(data);
    return ret;
}

