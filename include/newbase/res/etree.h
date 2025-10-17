#pragma once

#include <newbase/res/yaml.h>
#include <vector>

namespace nb {

    struct retree : public ryaml
    {
        bool etree_valid {false};
    };

}