#pragma once

#include <newbase/res/texfont.hpp>
#include <newbase/utility/glm.hpp>
#include <string>
#include <memory>

namespace nb {

struct ctextext
{
    std::shared_ptr<rtexfont> font;
    std::string               text;
    glm::vec4                 color { 1.f, 1.f, 1.f, 1.f };
    bool                      dirty { true };

    static void _ensure_rtti();
};

}
