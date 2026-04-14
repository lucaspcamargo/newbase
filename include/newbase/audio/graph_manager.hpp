#pragma once
#include <newbase/audio/types.hpp>
#include <entt/core/fwd.hpp>
#include <cstdint>
#include <string>

struct SDL_Mutex;

namespace nb {

namespace graphplan { class plan; }
namespace audio_graph { class graph; }

class audio_graph_manager {
public:
    audio_graph_manager();
    ~audio_graph_manager();

    audio_graph_manager(const audio_graph_manager&) = delete;
    audio_graph_manager& operator=(const audio_graph_manager&) = delete;

    // Call from audio::init after the audio device is open.
    // Creates the graphplan and the main OUTPUT node.
    void init(audio_spec spec);

    // Call from audio::~audio before closing the audio device (destroys GPU textures, etc).
    void shutdown();

    // Rebuild the live audio_graph from the current graphplan state.
    void rebuild(audio_graph::graph& live_graph, SDL_Mutex* mtx);

    // Draw the graphplan editor widget. Returns true if the plan was modified.
    bool draw_editor();

    // Immediately apply a gain_db value to a live gain node (also updates the plan property).
    void apply_gain_db(uint64_t node_id, float db);

    // Add a named bus to the plan (BUS_OUTPUT → GAIN → OUTPUT chain).
    // Returns the graphplan node id of the GAIN node (for use with apply_gain_db).
    uint64_t add_managed_bus(const std::string& name, float gain_db);

    struct player_nodes {
        uint64_t vorbis_node_id   {0};
        uint64_t gain_node_id     {0};
        uint64_t bus_input_node_id{0};
    };

    // Add a one-shot or looping vorbis player on the given bus.
    // Topology: VORBIS → GAIN(0 dB) → BUS_INPUT.
    // Returns the three graphplan node ids for later manipulation / removal.
    player_nodes add_player(const std::string& bus_name, entt::id_type res_id, bool loop);

    // Remove a previously added player from the plan (all three nodes + links).
    void remove_player(const player_nodes& nodes);

    graphplan::plan* plan() const;

private:
    void _link(uint64_t from_node, uint64_t to_node);

    struct impl;
    impl* _d {nullptr};
};

} // namespace nb
