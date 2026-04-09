#pragma once

#include <newbase/audio/graph/node.hpp>
#include <newbase/audio/graph/nodes.hpp>
#include <newbase/log.hpp>
#include <vector>
#include <unordered_map>
#include <memory>
#include <algorithm>
#include <deque>
#include <queue>
#include <cassert>
#include <cstring>

namespace nb::audio_graph
{

// Operation types for modifying the graph
struct op
{
    enum class type {
        ADD_NODE,
        REMOVE_NODE,
        CONNECT,
        DISCONNECT
    };

    type                  op_type;
    node_id               src;
    node_id               dst;
    std::shared_ptr<node> node_ptr; // only used for ADD_NODE

    static op add_node_op(std::shared_ptr<node> n)
    {
        return {type::ADD_NODE, n->id(), 0, n};
    }
    static op remove_node_op(const node_id& id)
    {
        return {type::REMOVE_NODE, id, 0, nullptr};
    }
    static op connect_op(const node_id& src, const node_id& dst)
    {
        return {type::CONNECT, src, dst, nullptr};
    }
    static op disconnect_op(const node_id& src, const node_id& dst)
    {
        return {type::DISCONNECT, src, dst, nullptr};
    }
};

class graph
{
public:
    graph()
    {
        auto output = std::make_shared<output_node>(0);
        nodes_[output->id()] = output;
        edges_[output->id()] = {};
    }

    // Must be called before produce() — sets the audio format used for processing.
    void set_spec(audio_spec spec) { spec_ = spec; }
    const audio_spec& spec() const { return spec_; }

    // Pull `n_bytes` of audio into buffer.
    // Processes all graph nodes in topological order, then copies the output
    // node's buffer to the destination. Requires set_spec() to have been called.
    void produce(uint8_t* buffer, size_t n_bytes)
    {
        assert(buffer);
        if (spec_.format == audio_format::UNKNOWN || !n_bytes)
        {
            std::memset(buffer, 0, n_bytes);
            return;
        }

        const size_t frame_stride =
            audio_format_size(spec_.format) * static_cast<size_t>(spec_.channels);
        if (!frame_stride)
        {
            std::memset(buffer, 0, n_bytes);
            return;
        }
        const size_t frames = n_bytes / frame_stride;

        // Reverse adjacency: for each node, which nodes feed into it.
        std::unordered_map<node_id, std::vector<node_id>> node_inputs;
        for (const auto& [id, _] : nodes_)
            node_inputs[id] = {};
        for (const auto& [src, dsts] : edges_)
            for (auto dst : dsts)
                node_inputs[dst].push_back(src);

        // Process nodes sources-first.
        auto order = topological_sort(nullptr);
        for (auto id : order)
        {
            std::vector<audio_buffer*> input_bufs;
            for (auto in_id : node_inputs.at(id))
                if (auto* b = nodes_.at(in_id)->output_buffer())
                    input_bufs.push_back(b);
            nodes_.at(id)->process(spec_, frames, input_bufs);
        }

        // Copy output node's result to device buffer.
        auto* out_buf = nodes_.at(0)->output_buffer();
        if (out_buf && out_buf->bytes() >= n_bytes)
            std::memcpy(buffer, out_buf->data().data(), n_bytes);
        else
            std::memset(buffer, 0, n_bytes);
    }

    void apply(const op& operation)
    {
        switch (operation.op_type)
        {
            case op::type::ADD_NODE:    add_node(operation.node_ptr); break;
            case op::type::REMOVE_NODE: remove_node(operation.src);   break;
            case op::type::CONNECT:     connect(operation.src, operation.dst); break;
            case op::type::DISCONNECT:  disconnect(operation.src, operation.dst); break;
        }
    }

    void add_node(std::shared_ptr<node> n)
    {
        assert(n);
        const node_id id = n->id();
        assert(nodes_.count(id) == 0 && "Node already exists");
        nodes_[id] = std::move(n);
        edges_[id] = {};
    }

    void remove_node(const node_id& id)
    {
        assert(nodes_.count(id));
        nodes_.erase(id);
        edges_.erase(id);
        for (auto& [src, dsts] : edges_)
            dsts.erase(std::remove(dsts.begin(), dsts.end(), id), dsts.end());
    }

    bool connect(const node_id& src, const node_id& dst)
    {
        assert(nodes_.count(src) && nodes_.count(dst) && "Invalid node id(s) for connect");
        assert(src != dst && "Cannot connect node to itself");
        assert(std::find(edges_[src].begin(), edges_[src].end(), dst) == edges_[src].end()
               && "Connection already exists");
        edges_[src].push_back(dst);
        if (has_cycle())
        {
            edges_[src].pop_back();
            log::error("Connection from node {} to node {} would create a cycle", src, dst);
            return false;
        }
        return true;
    }

    void disconnect(const node_id& src, const node_id& dst)
    {
        assert(nodes_.count(src) && nodes_.count(dst));
        auto& dsts = edges_[src];
        auto  it   = std::remove(dsts.begin(), dsts.end(), dst);
        assert(it != dsts.end());
        dsts.erase(it, dsts.end());
    }

    // Topological sort (Kahn's algorithm) — sources come first.
    std::vector<node_id> topological_sort(bool* has_cycle_out) const
    {
        std::unordered_map<node_id, int> in_degree;
        for (const auto& [id, _] : nodes_) in_degree[id] = 0;
        for (const auto& [src, dsts] : edges_)
            for (auto dst : dsts)
                ++in_degree[dst];

        std::queue<node_id> q;
        for (const auto& [id, deg] : in_degree)
            if (deg == 0) q.push(id);

        std::vector<node_id> order;
        while (!q.empty())
        {
            auto id = q.front(); q.pop();
            order.push_back(id);
            for (auto dst : edges_.at(id))
                if (--in_degree[dst] == 0)
                    q.push(dst);
        }

        if (has_cycle_out)
            *has_cycle_out = order.size() != nodes_.size();
        return order;
    }

    const std::unordered_map<node_id, std::shared_ptr<node>>& nodes() const { return nodes_; }
    const std::unordered_map<node_id, std::vector<node_id>>& edges() const { return edges_; }

private:
    audio_spec spec_;
    std::unordered_map<node_id, std::shared_ptr<node>>     nodes_;
    std::unordered_map<node_id, std::vector<node_id>>      edges_;

    bool has_cycle() const
    {
        bool ret;
        topological_sort(&ret);
        return ret;
    }
};

} // namespace nb::audio_graph
