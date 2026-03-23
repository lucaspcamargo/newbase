#pragma once

#include <newbase/res/resource.hpp>
#include <newbase/audio/types.hpp>
#include <vector>

namespace nb {

struct rvorbis : public resource {
    explicit rvorbis(entt::id_type id = 0) : resource(id, entt::hashed_string{"rvorbis"}.value()) {}

    bool valid {false};
    bool decoded {false};
    std::vector<char> data {};
    audio_spec spec {};
    std::vector<char> frames {};
};

}
