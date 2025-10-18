#pragma once

#include <newbase/audio/types.hpp>

namespace nb {

struct rwav {
    bool valid {false};
    audio_spec spec {};
    uint8_t *buf {nullptr};
    uint32_t len {0};

    ~rwav();
};

}