#include <newbase/physics2d/physics2d.h>
#include <newbase/physics2d/debug_draw.h>
#include <newbase/components/body2d.h>
#include <newbase/engine.h>
#include <newbase/log.h>
#include <newbase/reflection/contexts.h>
#include <newbase/reflection/data.h>
#include <entt/entt.hpp>
#include <box2d/box2d.h>
#include <imgui.h>
#include <IconsForkAwesome.h>

using namespace nb;
using entt::operator""_hs;

// TODO: right now there's only physics for the default scene
// we need per-scene physics worlds

struct nb::physics2d_p
{
    float world_scale {1.0f};

    // per scene
    b2WorldDef world_def;
    b2WorldId world_id {b2_nullWorldId};

    bool debug_draw_enabled {false};
    b2DebugDraw debug_draw {};
};

physics2d::physics2d()
{
    log::info("[physics2d] constructing");
    _d = new physics2d_p();
}

physics2d::~physics2d()
{
    log::info("[physics2d] destroying");

    if(B2_IS_NON_NULL(_d->world_id))
    {
        b2DestroyWorld(_d->world_id);
        _d->world_id = {};
    }

    delete _d;
}

bool physics2d::init(ryml::ConstNodeRef cfg)
{
    log::info("[physics2d] init");

    if(cfg.invalid() || cfg.empty())
    {
        log::info("[physics2d] no config, using defaults");
    }
    else
    {
        if(cfg.has_child("world_scale"))
        {
            cfg["world_scale"] >> _d->world_scale; 
        }
    }

    log::info("[physics2d] world scale: %f", _d->world_scale);
    b2SetLengthUnitsPerMeter(_d->world_scale);

    physics2d_setup_debug_draw(_d->debug_draw, this);
    engine::instance().debug_action_register("physics2d debug draw", [this](){
        _d->debug_draw_enabled = !_d->debug_draw_enabled;
    }, 8);

    // TODO: do this per scene in the future
    _d->world_def = b2DefaultWorldDef();
    _d->world_def.gravity.x = 0.0f;
    _d->world_def.gravity.y = 9.81f * _d->world_scale;
    _d->world_id = b2CreateWorld(&_d->world_def);
    
    return true;
}

bool physics2d::step(step_phase phase)
{
    if(phase == step_phase::PHYSICS_UPDATE)
    {
        // TODO do use fixed timestep, but maybe not every frame
        const int substeps = 4;
        const float time_step = 1.0f/60.0f/substeps;

        if(B2_IS_NON_NULL(_d->world_id))
        {
            b2World_Step(_d->world_id, time_step, substeps);
        }
    }
    else if(phase == step_phase::PRE_RENDER)
    {
        if(_d->debug_draw_enabled)
        {
            ImGui::Begin(ICON_FK_SQUARE " Physics 2D Debug Draw");
            ImGui::Text("World scale: %f", _d->world_scale);
            ImGui::Checkbox("useDrawingBounds", &_d->debug_draw.useDrawingBounds);
            ImGui::Checkbox("drawShapes", &_d->debug_draw.drawShapes);
            ImGui::Checkbox("drawJoints", &_d->debug_draw.drawJoints);
            ImGui::Checkbox("drawJointExtras", &_d->debug_draw.drawJointExtras);
            ImGui::Checkbox("drawBounds", &_d->debug_draw.drawBounds);
            ImGui::Checkbox("drawMass", &_d->debug_draw.drawMass);
            ImGui::Checkbox("drawBodyNames", &_d->debug_draw.drawBodyNames);
            ImGui::Checkbox("drawContacts", &_d->debug_draw.drawContacts);
            ImGui::Checkbox("drawGraphColors", &_d->debug_draw.drawGraphColors);
            ImGui::Checkbox("drawContactNormals", &_d->debug_draw.drawContactNormals);
            ImGui::Checkbox("drawContactImpulses", &_d->debug_draw.drawContactImpulses);
            ImGui::Checkbox("drawContactFeatures", &_d->debug_draw.drawContactFeatures);
            ImGui::Checkbox("drawFrictionImpulses", &_d->debug_draw.drawFrictionImpulses);
            ImGui::Checkbox("drawIslands", &_d->debug_draw.drawIslands);
            if(ImGui::Button("Hello Box2D!"))
            {
                static bool ground_added = false;
                if(!ground_added)
                {
                    b2BodyDef groundBodyDef = b2DefaultBodyDef();
                    groundBodyDef.position = (b2Vec2){0.0f, 10.0f};
                    b2BodyId groundId = b2CreateBody(_d->world_id, &groundBodyDef);
                    b2Polygon groundBox = b2MakeBox(50.0f, 10.0f);
                    b2ShapeDef groundShapeDef = b2DefaultShapeDef();
                    b2CreatePolygonShape(groundId, &groundShapeDef, &groundBox);
                    ground_added = true;
                }

                b2BodyDef bodyDef = b2DefaultBodyDef();
                bodyDef.type = b2_dynamicBody;
                bodyDef.position = (b2Vec2){0.0f, -20.0f};
                b2BodyId bodyId = b2CreateBody(_d->world_id, &bodyDef);
                b2Polygon dynamicBox = b2MakeBox(1.0f, 1.0f);
                b2ShapeDef shapeDef = b2DefaultShapeDef();
                shapeDef.density = 1.0f;
                shapeDef.material.friction = 0.3f;
                b2CreatePolygonShape(bodyId, &shapeDef, &dynamicBox);
            }
            ImGui::End();

            physics2d_pre_debug_draw(0.0f, 0.0f, _d->world_scale, _d->world_scale);
            b2World_Draw(_d->world_id, &_d->debug_draw);

        }
    }
    return true;
}

bool physics2d::event(SDL_Event*)
{
    return true;
}


// RTTI metadata
extern "C" void _rtti_init_physics2d()
{
    entt::meta_factory<nb::physics2d>{}
        .type("physics2d"_hs)
        .custom<rtti::system_info>(rtti::system_info{"physics2d"})
        .base<nb::system>();
    entt::meta_factory<std::shared_ptr<nb::physics2d>>{rtti::ctx_systems()}
        .type("physics2d_shared"_hs)
        .ctor<&rtti::shared_ptr_builder<nb::physics2d>>()
        .conv<std::shared_ptr<nb::system>>();

    cbody2d::_ensure_rtti();
}