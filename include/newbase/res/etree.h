#pragma once

#include <ryml.hpp>
#include <vector>

namespace nb {

    struct retree {
        ryml::Tree tree;
        std::vector<uint8_t> data;
    };

};