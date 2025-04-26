#pragma once

#include <newbase/audio/types.h>
#include <vector>

namespace nb {

struct rvorbis {
    bool valid {false};
    bool decoded {false};
    std::vector<char> data {};
    audio_spec spec {};
    std::vector<char> frames {};
};

}