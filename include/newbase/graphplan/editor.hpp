#pragma once

#include <newbase/graphplan/plan.hpp>

namespace nb::graphplan {

    struct editor_p;

    class editor
    {
    public:
        editor(plan &plan);
        ~editor();

        // Returns true if any structural change was made to the plan this frame.
        bool draw();

    private:
        editor_p *_d;
    };

}