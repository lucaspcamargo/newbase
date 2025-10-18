#pragma once

#include <newbase/res/yaml.hpp>
#include <vector>

namespace nb {

    struct retree : public ryaml
    {
        bool etree_valid {false};
    };

}