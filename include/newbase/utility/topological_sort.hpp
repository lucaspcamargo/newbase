#pragma once

#include <vector>
#include <unordered_map>
#include <iterator>
#include <entt/entt.hpp>

namespace nb::util {

class topological_sorter {
public:
    // Templated to accept whatever graph type entt::flow produces
    template <typename Graph>
    bool build(const Graph& graph)
    {
        _reset();

        // calculate initial in-degrees and find root nodes
        for (const auto &vertex : graph.vertices()) {
            auto in_edges = graph.in_edges(vertex);
            size_t count = std::distance(in_edges.begin(), in_edges.end());
            _in_degree[vertex] = count;

            if (count == 0) {
                _ready_queue.push_back(vertex);
            }
        }

        // traverse the graph using the vector as a queue
        size_t head = 0;
        while (head < _ready_queue.size()) {
            auto current = _ready_queue[head++];
            _execution_order.push_back(current);

            // graph.out_edges yields std::pair<source, target>
            for (auto&& edge : graph.out_edges(current)) {
                auto target_vertex = edge.second; // Get the target node from the edge pair

                if (--_in_degree[target_vertex] == 0) {
                    _ready_queue.push_back(target_vertex);
                }
            }
        }

        // return true if successful, false if a cycle is detected
        return _execution_order.size() == _in_degree.size();
    }

    const std::vector<entt::id_type>& execution_order() const
    {
        return _execution_order;
    }

private:
    void _reset()
    {
        _execution_order.clear();
        _in_degree.clear();
        _ready_queue.clear();
    }

    std::vector<entt::id_type> _execution_order;
    std::unordered_map<entt::id_type, size_t> _in_degree;
    std::vector<entt::id_type> _ready_queue;
};

}
