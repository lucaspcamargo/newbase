#pragma once

#include <box2d/box2d.h>
#include <vector>

namespace nb {

    enum class body2d_type
    {
        STATIC,
        DYNAMIC,
        KINEMATIC
    };

    enum class shape2d_type
    {
        CIRCLE,
        BOX,
        POLY
    };

    struct shape2d
    {
        shape2d_type shape_type {shape2d_type::CIRCLE};
        std::vector<float> shape_data {};

        float density {-1.0f};
        float friction {-1.0f};
        float restitution {-1.0f};
        float rolling_resistance {-1.0f};
        float tangent_speed {0.0f};

        uint64_t category_bits {0x0000000000000001};
        uint64_t mask_bits {0xffffffffffffffff};
        int group {0};

        bool sensor {false};
        bool sensor_events {false};
    };

    struct cbody2d {

        // pointer stability (easy user_data)
        static constexpr auto in_place_delete = true;
        
        body2d_type type {body2d_type::STATIC};
        float linear_damping {-1.0f};
        float angular_damping {-1.0f};
        float gravity_scale {1.0f};
        
        bool enabled {true};
        bool enable_sleep {true};
        bool awake {true};
        bool fix_rotation {false};
        bool bullet {false};
        
        std::vector<shape2d> shapes;

        bool dirty {true};
        b2BodyId _body_id {b2_nullBodyId};

        // for the meta system
        static void _ensure_rtti();
    };
}