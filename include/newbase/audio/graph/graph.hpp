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
#include <unordered_set>

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
    // Starts a pull chain from the output node, which recursively pulls from its
    // inputs. Requires set_spec() to have been called.
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

        // Ensure output buffer.
        if (!out_buf_ || out_buf_->spec() != spec_ || out_buf_->frames() != frames)
            out_buf_ = std::make_unique<audio_buffer>(spec_, frames, nullptr);

        std::fill(out_buf_->data().begin(), out_buf_->data().end(), std::byte{0});

        // Pull from output node — it recursively pulls the whole graph.
        auto span = out_buf_->as_span();
        nodes_.at(0)->pull(span, ++generation_);

        // Explicitly pull feedback nodes (e.g. visualizers) that have no outgoing
        // edges and are therefore not reachable from the output node's chain.
        // Reuses the same generation so fan_out caches are shared with the main pull.
        for (auto id : always_pull_)
        {
            auto edge_it = edges_.find(id);
            if (edge_it != edges_.end() && !edge_it->second.empty())
                continue; // already pulled by normal chain
            auto node_it = nodes_.find(id);
            if (node_it == nodes_.end()) continue;

            if (!fb_buf_ || fb_buf_->spec() != spec_ || fb_buf_->frames() != frames)
                fb_buf_ = std::make_unique<audio_buffer>(spec_, frames, nullptr);
            std::fill(fb_buf_->data().begin(), fb_buf_->data().end(), std::byte{0});
            auto fb_span = fb_buf_->as_span();
            node_it->second->pull(fb_span, generation_);
        }

        // Copy result to device buffer.
        if (out_buf_->bytes() >= n_bytes)
            std::memcpy(buffer, out_buf_->data().data(), n_bytes);
        else
            std::memset(buffer, 0, n_bytes);
    }

    void apply(const op& operation)
    {
        switch (operation.op_type)
        {
            case op::type::ADD_NODE:    add_node(operation.node_ptr); return;
            case op::type::REMOVE_NODE: remove_node(operation.src);   return;
            case op::type::CONNECT:     connect(operation.src, operation.dst); return;
            case op::type::DISCONNECT:  disconnect(operation.src, operation.dst); return;
        }
    }

    void add_node(std::shared_ptr<node> n)
    {
        assert(n);
        const node_id id = n->id();
        assert(nodes_.count(id) == 0 && "Node already exists");
        nodes_[id] = std::move(n);
        edges_[id] = {};
        _wire_inputs();
    }

    void remove_node(const node_id& id)
    {
        assert(nodes_.count(id));
        nodes_.erase(id);
        edges_.erase(id);
        for (auto& [src, dsts] : edges_)
            dsts.erase(std::remove(dsts.begin(), dsts.end(), id), dsts.end());
        always_pull_.erase(id);
        _wire_inputs();
    }

    // Mark a node to be explicitly pulled every cycle even when it has no
    // outgoing edges (i.e. is not reachable from the output node).
    // Used for side-effect nodes like visualizers.
    void mark_always_pull(node_id id) { always_pull_.insert(id); }

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
        _wire_inputs();
        return true;
    }

    void disconnect(const node_id& src, const node_id& dst)
    {
        assert(nodes_.count(src) && nodes_.count(dst));
        auto& dsts = edges_[src];
        auto  it   = std::remove(dsts.begin(), dsts.end(), dst);
        assert(it != dsts.end());
        dsts.erase(it, dsts.end());
        _wire_inputs();
    }

    // Topological sort (Kahn's algorithm) — sources come first.
    // Used for cycle detection and debug logging; not required for produce().
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
    std::unordered_set<node_id>                             always_pull_;
    std::unique_ptr<audio_buffer>                           out_buf_;
    std::unique_ptr<audio_buffer>                           fb_buf_;
    uint64_t                                                generation_ {0};

    bool has_cycle() const
    {
        bool ret;
        topological_sort(&ret);
        return ret;
    }

    // Rebuild each node's inputs_ vector from the current edge topology.
    // Called after every structural change so pull() can traverse the graph.
    void _wire_inputs()
    {
        // Build reverse adjacency: for each node, which nodes feed into it.
        std::unordered_map<node_id, std::vector<node_id>> node_inputs;
        for (const auto& [id, _] : nodes_)
            node_inputs[id] = {};
        for (const auto& [src, dsts] : edges_)
            for (auto dst : dsts)
                node_inputs[dst].push_back(src);

        for (auto& [id, node_ptr] : nodes_)
        {
            std::vector<node*> inputs;
            for (auto in_id : node_inputs.at(id))
            {
                auto it = nodes_.find(in_id);
                if (it != nodes_.end())
                    inputs.push_back(it->second.get());
            }
            node_ptr->set_inputs(std::move(inputs));
        }
    }
};

} // namespace nb::audio_graph
