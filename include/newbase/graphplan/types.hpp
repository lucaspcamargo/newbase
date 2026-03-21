#pragma once

#include <vector>
#include <cstdint>

namespace nb::graphplan {

    // a graphplan domain specifies which types of nodes can be used, the types of input/output that they have, 
    // the topology constraints for the graph, and fixed, global input/output definitions
    // it specifies, basically, a type of graph that can be used by a system to generate logic
    
    struct domain
    {
        // some kind of ID
        const char *id;
        
        // a list of possible node types
        std::vector<int> node_types;

        // some graph constraints
        bool acyclic {true};
    };

    struct link_data
    {
        uint64_t id;
        uint64_t input_pin;
        uint64_t output_pin;
    };

    struct pin_data
    {
        uint64_t id;
        uint64_t node_id;
    };

    struct node_data
    {
        uint64_t id;
        int type;
        std::vector<uint64_t> input_pins;
        std::vector<uint64_t> output_pins;

        float pos_x;
        float pos_y;
    };

    // a pure virtual class that represents the interface for a node
    class node_interface
    {
        
    };

}