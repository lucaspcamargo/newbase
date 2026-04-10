#pragma once

#include <vector>
#include <string>
#include <unordered_map>
#include <cstdint>
#include <functional>
#include <memory>
#include <entt/meta/meta.hpp>

namespace nb::graphplan {

    // a graphplan domain specifies which types of nodes can be used, the types of input/output that they have,
    // the topology constraints for the graph, and fixed, global input/output definitions
    // it specifies, basically, a type of graph that can be used by a system to generate logic

    // Describes the data type flowing through a pin.
    // is_opaque = true for domain-specific "fake" types whose semantics the graphplan
    // system treats as opaque (e.g. an audio stream handle).
    struct pin_type_def
    {
        int         type_id;
        const char* name;
        bool        is_opaque {false};
    };

    // A configurable property slot on a node type, with a typed default value.
    struct prop_def
    {
        const char*     name;
        entt::meta_any  default_value {};
        bool            hide_when_custom_ui {false}; // suppress from standard property list when draw_fn is set
        entt::id_type   res_type_id {0}; // non-zero: property is an entt::id_type resource reference of this type
    };

    struct node_data; // forward declaration for draw_fn signature

    // Describes a node type: its pins are listed as type_id sequences.
    // The length of input_types / output_types is the fixed pin count for that kind.
    struct node_type_def
    {
        int              type_id;
        const char*      name;
        std::vector<int>      input_types;   // type_id for each input pin
        std::vector<int>      output_types;  // type_id for each output pin
        std::vector<prop_def> props;         // configurable properties
        bool             user_addable {true}; // false = never shown in the add-node menu
        int              category_id  {0};   // 0 = uncategorised
        float            header_color[4] {0.25f, 0.25f, 0.25f, 1.f}; // RGBA header background color

        // Optional extra ImGui controls drawn above all pins.
        // Receives the live node_data so the function can read/write properties.
        std::function<bool(node_data&)> draw_fn {};
    };

    struct category_def
    {
        int         id;
        const char* name;
    };

    struct domain
    {
        const char* id;

        // pin types known to this domain
        std::vector<pin_type_def> pin_types;

        // node categories (optional grouping for the add-node menu)
        std::vector<category_def> categories;

        // node types available in this domain
        std::vector<node_type_def> node_types;

        bool acyclic {true};

        const node_type_def* find_type(int type_id) const
        {
            for (const auto& ntd : node_types)
                if (ntd.type_id == type_id) return &ntd;
            return nullptr;
        }

        const pin_type_def* find_pin_type(int type_id) const
        {
            for (const auto& ptd : pin_types)
                if (ptd.type_id == type_id) return &ptd;
            return nullptr;
        }

        // Returns true if an output pin of output_type_id may connect to
        // an input pin of input_type_id. Currently: same type = compatible.
        bool can_connect(int output_type_id, int input_type_id) const
        {
            return output_type_id == input_type_id;
        }
    };

    struct link_data
    {
        uint64_t id;
        uint64_t input_pin;   // pin with PinKind::Input  (receives the signal)
        uint64_t output_pin;  // pin with PinKind::Output (sends the signal)
    };

    struct pin_data
    {
        uint64_t id;
        uint64_t node_id;
        int      type_id {0}; // matches a pin_type_def in the domain
    };

    struct node_data
    {
        uint64_t id;
        int type;
        std::vector<uint64_t> input_pins;
        std::vector<uint64_t> output_pins;

        float pos_x;
        float pos_y;

        std::unordered_map<std::string, entt::meta_any> properties;

        // Optional per-node user data (e.g. feedback structs for draw_fn).
        // Set by the domain owner, read by draw_fn. Thread-safety is the owner's concern.
        std::shared_ptr<void> user_data;
    };

    // a pure virtual class that represents the interface for a node
    class node_interface
    {

    };

}
