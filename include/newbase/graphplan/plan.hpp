#pragma once

#include <newbase/graphplan/types.hpp>
#include <unordered_map>

namespace nb::graphplan {

    class graphplan_editor;

    // this data structure represents a node graph plan
    // this includes:
    // - the domain for thie plan (allowed node types and semantics)
    // - nodes with type and corresponding pins
    // - pins with ids
    // - links between input and output pins
    // To match the semantics of the editor (and for my convenience),
    // we use unique IDs even amongst different types of objects (nodes, pins and links all have different IDs)

    class plan
    {
    public:
        plan(const domain &dom);
        ~plan();

        const domain& dom() const { return dom_; }

        // Create a node of the given type at (x, y), allocating typed pins from the domain.
        // Returns the new node's unique id.
        uint64_t add_node_from_type(int type_id, float x = 0.f, float y = 0.f);

        // ensure our next id is unique
        // always ascending
        // kind of a dirty HACK :)
        uint64_t get_next_unique_id() const
        {
            uint64_t max_id = 0;
            for (const auto& [id, link] : links)
            {
                if (id > max_id)
                    max_id = id;
            }
            for (const auto& [id, pin] : pins)
            {
                if (id > max_id)
                    max_id = id;
            }
            for (const auto& [id, node] : nodes)
            {
                if (id > max_id)
                    max_id = id;
            }
            return max_id + 1;
        }

        std::unordered_map<uint64_t, node_data> nodes;
        std::unordered_map<uint64_t, pin_data> pins;
        std::unordered_map<uint64_t, link_data> links;

    private:
        const domain& dom_;
    };

} 