#pragma once

#include <newbase/res/resource.hpp>
#include <newbase/audio/types.hpp>
#include <vector>

namespace nb {

struct rvorbis : public resource {
    explicit rvorbis(entt::id_type id = 0) : resource(id, entt::hashed_string{"rvorbis"}.value()) {}

    bool valid {false};
    audio_spec spec {};
    std::size_t total_frames {0};   // total PCM frames in the stream

    // Set for short samples (≤ 2s): fully decoded S16 interleaved PCM ready to play.
    // Not set for longer files; the producer streams from storage on demand.
    bool cached {false};
    std::vector<char> frames {};
};

}
