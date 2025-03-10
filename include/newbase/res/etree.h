#pragma once

#include <ryml.hpp>
#include <vector>

namespace nb {

    struct retree {
        bool valid {false};
        std::vector<char> data;
        ryml::Tree tree;
    };

};