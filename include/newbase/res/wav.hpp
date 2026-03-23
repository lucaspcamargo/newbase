#pragma once

#include <newbase/res/resource.hpp>
#include <newbase/audio/types.hpp>

namespace nb {

struct rwav : public resource {
    explicit rwav(entt::id_type id = 0) : resource(id, entt::hashed_string{"rwav"}.value()) {}
    ~rwav();

    bool valid {false};
    audio_spec spec {};
    uint8_t *buf {nullptr};
    uint32_t len {0};
};

}
