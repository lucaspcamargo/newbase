#pragma once

#include <newbase/res/resource.hpp>
#include <entt/core/hashed_string.hpp>
#include <entt/meta/meta.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

namespace nb {

struct rgraphplan : public resource
{
    using hashed_string = entt::hashed_string;
    explicit rgraphplan(entt::id_type id = 0)
        : resource(id, hashed_string{"rgraphplan"}.value()) {}

    std::string domain_id;

    struct node_desc
    {
        uint64_t    id      {0};
        std::string type_name;
        float       pos_x   {0.f};
        float       pos_y   {0.f};
        // Properties keyed by name; type-hinted during load from domain prop_defs.
        std::unordered_map<std::string, entt::meta_any> properties;
    };

    // Link stored as node-relative pin indices so IDs can be remapped freely.
    struct link_desc
    {
        uint64_t from_node  {0};
        int      from_pin   {0}; // output pin index on from_node
        uint64_t to_node    {0};
        int      to_pin     {0}; // input pin index on to_node
    };

    std::vector<node_desc> nodes;
    std::vector<link_desc> links;
};

} // namespace nb
