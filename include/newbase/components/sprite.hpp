#pragma once

#include <newbase/res/sprite.hpp>
#include <glm/vec4.hpp>
#include <memory>
#include <string>

namespace nb {
    struct csprite {
        std::shared_ptr<rsprite> spr;
        bool      visible { true };
        glm::vec4 color   { 1.f, 1.f, 1.f, 1.f };

        // Animation state — managed by sprite_anim system
        std::string sequence;        // empty = first sequence
        int         frame          { 0   };
        float       time_in_frame  { 0.f };
        bool        animating      { false };

        // Cached by sprite_anim; recomputed when sequence/frame differ from cached values
        glm::vec4   current_source_rect { -1.f, -1.f, -1.f, -1.f };
        std::string _cached_sequence    { "\x01" }; // sentinel: guaranteed mismatch on first tick
        int         _cached_frame       { -1 };

        static void _ensure_rtti();
    };

}