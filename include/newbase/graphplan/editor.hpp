#pragma once

#include <newbase/graphplan/plan.hpp>

namespace nb::graphplan {

    struct editor_p;

    class editor
    {
    public:
        editor(plan &plan);
        ~editor();

        void draw();

    private:
        editor_p *_d;
    };

}