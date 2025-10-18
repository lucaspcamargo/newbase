#pragma once

#include <ryml.hpp>
#include <vector>

namespace nb {

    struct ryaml {
        bool yaml_valid {false};
        std::vector<char> data;
        ryml::Tree tree;
    };

}