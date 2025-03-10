#pragma once

#include <newbase/components/fwd.h>
#include <ryml.hpp>

namespace nb
{
    bool build_spatial(ryml::ConstNodeRef data, cspatial &dst);
    bool build_sprite(ryml::ConstNodeRef data, csprite &dst);
}