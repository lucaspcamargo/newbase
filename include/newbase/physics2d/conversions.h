#pragma once

#include <newbase/components/body2d.h>
#include <newbase/components/spatial.h>
#include <box2d/box2d.h>

namespace nb 
{

    inline b2BodyType body2d_type_to_box2d(body2d_type type)
    {
        switch(type)
        {
            case body2d_type::STATIC:
                return b2BodyType::b2_staticBody;
            case body2d_type::KINEMATIC:
                return b2BodyType::b2_kinematicBody;
            case body2d_type::DYNAMIC:
                return b2BodyType::b2_dynamicBody;
        }

        return b2BodyType::b2_staticBody;
    }

    inline bool cbody2d_to_body_def(b2BodyDef &def, const cbody2d &comp, cspatial *spatial = nullptr)
    {
        def = b2DefaultBodyDef();
        def.type = body2d_type_to_box2d(comp.type);
        if(spatial)
        {
            def.position = b2Vec2{spatial->pos.x, spatial->pos.y};
            def.rotation = b2MakeRot(glm::radians(spatial->rot.z));
        }

        def.isEnabled = comp.enabled;
        def.enableSleep = comp.enable_sleep;
        def.isAwake = comp.awake;
        def.motionLocks.angularZ = comp.fix_rotation;
        def.isBullet = comp.bullet;

        def.gravityScale = comp.gravity_scale;
        if(comp.angular_damping != -1.0f)
            def.angularDamping = comp.angular_damping;
        if(comp.linear_damping != -1.0f)
            def.linearDamping = comp.linear_damping;

        return true;
    }

    inline bool shape2d_create(b2BodyId id, const shape2d &shape)
    {
        b2ShapeDef def = b2DefaultShapeDef();

        if(shape.density != -1.0f)
            def.density = shape.density;

        if(shape.friction != -1.0f)
            def.material.friction = shape.friction;

        if(shape.restitution != -1.0f)
            def.material.restitution = shape.restitution;

        if(shape.rolling_resistance != -1.0f)
            def.material.rollingResistance = shape.rolling_resistance;

        if(shape.tangent_speed != -1.0f)
            def.material.tangentSpeed = shape.tangent_speed;

        def.filter.categoryBits = shape.category_bits;
        def.filter.maskBits = shape.mask_bits;
        def.filter.groupIndex = shape.group;

        def.isSensor = shape.sensor;
        def.enableSensorEvents = shape.sensor_events;

        switch(shape.shape_type)
        {
            case shape2d_type::BOX:
            {
                if(shape.shape_data.size() != 2)
                    return false;
                b2Polygon box = b2MakeBox(50.0f, 10.0f);
                b2CreatePolygonShape(id, &def, &box);
            }
            break;

            case shape2d_type::CIRCLE:
            {
                if(shape.shape_data.size() != 1 && shape.shape_data.size() != 3)
                    return false;
                b2Circle circle {};
                circle.radius = shape.shape_data[0];
                if(shape.shape_data.size() == 3)
                    circle.center = b2Vec2{shape.shape_data[1], shape.shape_data[2]};
                b2CreateCircleShape(id, &def, &circle);
                return true;
            }
            break;

            case shape2d_type::POLY:
            {
                if(shape.shape_data.size() % 2)
                    return false;
                std::vector<b2Vec2> points;
                for(int i = 0; i < shape.shape_data.size(); i += 2)
                {
                    points.push_back(b2Vec2{shape.shape_data[i], shape.shape_data[i+1]});
                }
                b2Hull hull = b2ComputeHull(points.data(), static_cast<int>(points.size()));
                b2Polygon poly = b2MakePolygon(&hull, 0.0);
                b2CreatePolygonShape(id, &def, &poly);
                return true;
            }
            break;

            default:
                return false;
        }

        return true;
    }

}