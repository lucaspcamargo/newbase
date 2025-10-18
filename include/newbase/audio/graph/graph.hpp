#pragma once

#include <newbase/audio/types.hpp>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <stdexcept>
#include <algorithm>
#include <queue>
#include <string>

namespace nb::audio_graph 
{

    using node_id = int;

    class node 
    {
    public:
        explicit node(node_id id) : id_(std::move(id)) {}
        virtual ~node() = default;

        const node_id& id() const { return id_; }

        // Add audio processing interface here as needed

    private:
        node_id id_;
    };

    // Operation types for modifying the graph
    struct op 
    {
        enum class type { add_node, remove_node, connect, disconnect };

        type op_type;
        node_id src;
        node_id dst;
        std::shared_ptr<node> node_ptr; // Only used for add_node

        static op add_node_op(std::shared_ptr<node> n) {
            return {type::add_node, n->id(), 0, n};
        }
        static op remove_node_op(const node_id& id) {
            return {type::remove_node, id, 0, nullptr};
        }
        static op connect_op(const node_id& src, const node_id& dst) {
            return {type::connect, src, dst, nullptr};
        }
        static op disconnect_op(const node_id& src, const node_id& dst) {
            return {type::disconnect, src, dst, nullptr};
        }
    };

    class graph
    {
    public:
        graph() {
            // Create a default output node with id 0
            auto output = std::make_shared<node>(0);
            nodes_[output->id()] = output;
            edges_[output->id()] = {};
        }

        void produce(uint8_t* buffer, size_t n) const {
            assert(buffer);
            for (size_t i = 0; i < n; ++i) {
                buffer[i] = 0x00;
            }
        }

        // Apply an operation to the graph
        void apply(const op& operation) {
            switch (operation.op_type) {
                case op::type::add_node:
                    add_node(operation.node_ptr);
                    break;
                case op::type::remove_node:
                    remove_node(operation.src);
                    break;
                case op::type::connect:
                    connect(operation.src, operation.dst);
                    break;
                case op::type::disconnect:
                    disconnect(operation.src, operation.dst);
                    break;
            }
        }

        void add_node(std::shared_ptr<node> n) {
            if (!n) throw std::invalid_argument("Null node");
            if (nodes_.count(n->id()))
                throw std::runtime_error("Node already exists: " + n->id());
            nodes_[n->id()] = std::move(n);
            edges_[n->id()] = {};
        }

        void remove_node(const node_id& id) {
            if (!nodes_.count(id))
                throw std::runtime_error("Node does not exist: " + id);
            nodes_.erase(id);
            edges_.erase(id);
            // Remove all incoming edges
            for (auto& [src, dsts] : edges_) {
                dsts.erase(std::remove(dsts.begin(), dsts.end(), id), dsts.end());
            }
        }

        void connect(const node_id& src, const node_id& dst) {
            if (!nodes_.count(src) || !nodes_.count(dst))
                throw std::runtime_error("Invalid node id(s) for connect");
            if (src == dst)
                throw std::runtime_error("Cannot connect node to itself");
            if (std::find(edges_[src].begin(), edges_[src].end(), dst) != edges_[src].end())
                throw std::runtime_error("Connection already exists");
            edges_[src].push_back(dst);
            if (has_cycle())
            {
                edges_[src].pop_back();
                throw std::runtime_error("Connection would create a cycle");
            }
        }

        void disconnect(const node_id& src, const node_id& dst) {
            if (!nodes_.count(src) || !nodes_.count(dst))
                throw std::runtime_error("Invalid node id(s) for disconnect");
            auto& dsts = edges_[src];
            auto it = std::remove(dsts.begin(), dsts.end(), dst);
            if (it == dsts.end())
                throw std::runtime_error("Connection does not exist");
            dsts.erase(it, dsts.end());
        }

        // Topological sort for processing order
        std::vector<node_id> topological_sort() const {
            std::unordered_map<node_id, int> in_degree;
            for (const auto& [id, _] : nodes_) in_degree[id] = 0;
            for (const auto& [src, dsts] : edges_)
                for (const auto& dst : dsts)
                    ++in_degree[dst];

            std::queue<node_id> q;
            for (const auto& [id, deg] : in_degree)
                if (deg == 0) q.push(id);

            std::vector<node_id> order;
            while (!q.empty()) {
                auto id = q.front(); q.pop();
                order.push_back(id);
                for (const auto& dst : edges_.at(id)) {
                    if (--in_degree[dst] == 0)
                        q.push(dst);
                }
            }
            if (order.size() != nodes_.size())
                throw std::runtime_error("Graph has a cycle");
            return order;
        }

        // Accessors
        const std::unordered_map<node_id, std::shared_ptr<node>>& nodes() const { return nodes_; }
        const std::unordered_map<node_id, std::vector<node_id>>& edges() const { return edges_; }

    private:
        std::unordered_map<node_id, std::shared_ptr<node>> nodes_;
        std::unordered_map<node_id, std::vector<node_id>> edges_;

        bool has_cycle() const {
            try {
                topological_sort();
                return false;
            } catch (...) {
                return true;
            }
        }
    };

}