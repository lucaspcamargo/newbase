#pragma once

#include <newbase/sys/textext/rtexfont.hpp>
#include <newbase/utility/glm.hpp>
#include <string>
#include <memory>

namespace nb {

// TODO move component to textext system, after we have component building via RTTI in place
struct ctextext
{
    std::shared_ptr<rtexfont> font;
    std::string               text;
    glm::vec4                 color { 1.f, 1.f, 1.f, 1.f };
    bool                      dirty { true };

    static void _ensure_rtti();
};

}
