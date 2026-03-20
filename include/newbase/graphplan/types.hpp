#pragma once

namespace nb::graphplan {

    // a graphplan domain specifies which types of nodes can be used, the types of input/output that they have, 
    // the topology constraints for the graph, and fixed, global input/output definitions
    // it specifies, basically, a type of graph that can be used by a system to generate logic
    
    struct domain
    {
        // some kind of ID
        // a list of possible node types
        // some graph constraints
    };

    // a pure virtual class that represents the interface for a node
    class node_interface
    {
        

    }

}