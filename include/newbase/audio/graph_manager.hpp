#pragma once
#include <newbase/audio/types.hpp>
#include <cstdint>

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
    void init(audio_spec spec, float bgm_gain_db, float sfx_gain_db);

    // Call from audio::~audio before closing the audio device (destroys GPU textures, etc).
    void shutdown();

    // Rebuild the live audio_graph from the current graphplan state.
    void rebuild(audio_graph::graph& live_graph, SDL_Mutex* mtx);

    // Draw the graphplan editor widget. Returns true if the plan was modified.
    bool draw_editor();

    // Immediately apply a gain_db value to a live gain node (also updates the plan property).
    void apply_gain_db(uint64_t node_id, float db);

    uint64_t bgm_gain_node_id() const { return _bgm_gain_node_id; }
    uint64_t sfx_gain_node_id() const { return _sfx_gain_node_id; }

    graphplan::plan* plan() const;

private:
    struct impl;
    impl* _d {nullptr};
    uint64_t _bgm_gain_node_id {0};
    uint64_t _sfx_gain_node_id {0};
};

} // namespace nb
