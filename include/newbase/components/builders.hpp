#pragma once

#include <newbase/components/fwd.hpp>
#include <ryml.hpp>

namespace nb
{
    bool build_spatial(ryml::ConstNodeRef def, cspatial &dst);
    bool build_sprite(ryml::ConstNodeRef def, csprite &dst);
    bool build_script(ryml::ConstNodeRef def, cscript &dst);
    bool build_body2d(ryml::ConstNodeRef def, cbody2d &dst);
}