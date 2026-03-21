#include <newbase/physics2d/physics2d.hpp>
#include <newbase/physics2d/debug_draw.hpp>
#include <newbase/physics2d/conversions.hpp>
#include <newbase/components/body2d.hpp>
#include <newbase/components/spatial.hpp>
#include <newbase/engine.hpp>
#include <newbase/scene.hpp>
#include <newbase/log.hpp>
#include <newbase/services/viewport_geometry.hpp>
#include <newbase/services/ui_manager.hpp>
#include <newbase/reflection/contexts.hpp>
#include <newbase/reflection/data.hpp>
#include <entt/entt.hpp>
#include <box2d/box2d.h>
#include <imgui.h>
#include <IconsForkAwesome.h>
#include <unordered_map>

using namespace nb;
using entt::operator""_hs;

constexpr auto BODY2D_CONSTRUCT_UPDATE_STORAGE = "body2d_on_construct_update"_hs;
constexpr auto BODY2D_DESTROY_STORAGE = "body2d_on_destroy"_hs; 

// TODO: right now there's only physics for the default scene
// we need per-scene physics worlds

// For scaling forces and torques etc according to by world scale, this guy knows what's up: https://stackoverflow.com/a/46898740

template<> 
struct std::hash<b2BodyId>
{
    std::size_t operator()(const b2BodyId &id) const
    {
        return static_cast<size_t>(b2StoreBodyId(id));
    }
};

bool operator ==(const b2BodyId &a, const b2BodyId &b)
{
    return b2StoreBodyId(a) == b2StoreBodyId(b);
}

struct nb::physics2d_p
{
    float world_scale {1.0f};

    // per scene
    b2WorldDef world_def;
    b2WorldId world_id {b2_nullWorldId};
    std::unordered_map<b2BodyId, entt::entity> body_entt {};
    std::unordered_map<b2BodyId, cbody2d*> body_comp {};
    std::unordered_map<b2BodyId, cspatial*> body_spatial {}; // TODO spatial has no pointer stability, this is wrong and dangerous

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

    ui_manager* ui_mgr = entt::locator<ui_manager*>::value();
    if(ui_mgr)
    {
        ui_mgr->register_tool_window("physics2d", [this](bool *open){
            _draw_tool_window(open);
        });
    }

    engine::instance().debug_action_register("toggle physics2d tools", [](){
        ui_manager* ui_mgr = entt::locator<ui_manager*>::value();
        if(ui_mgr)
            ui_mgr->toggle_tool_window("physics2d");
    }, 8);


    // TODO: do this per scene in the future
    _d->world_def = b2DefaultWorldDef();
    _d->world_def.gravity.x = 0.0f;
    _d->world_def.gravity.y = 9.81f * _d->world_scale;
    _d->world_id = b2CreateWorld(&_d->world_def);

    // setup reactive storage on scene registry
    auto &reg = engine::instance().default_scene().registry();
    auto &on_construct_upd = reg.storage<entt::reactive>(BODY2D_CONSTRUCT_UPDATE_STORAGE);
    on_construct_upd.on_construct<cbody2d>().on_update<cbody2d>();
    auto &on_destroy = reg.storage<entt::reactive>(BODY2D_DESTROY_STORAGE);
    on_destroy.on_destroy<cbody2d>();

    return true;
}

bool physics2d::step(step_phase phase)
{
    if(phase == step_phase::PHYSICS_UPDATE)
    {
        // construct or update bodies
        auto &reg = engine::instance().default_scene().registry();
        auto &on_construct_upd = reg.storage<entt::reactive>(BODY2D_CONSTRUCT_UPDATE_STORAGE);
        for(auto entity: on_construct_upd)
        {
            cbody2d &cbody = reg.get<cbody2d>(entity);
            cspatial *spatial = reg.try_get<cspatial>(entity);

            if(B2_IS_NON_NULL(cbody._body_id))
            {
                // destroy previous body
                // TODO: maybe just update what can be updated if possible
                b2DestroyBody(cbody._body_id);
                cbody._body_id = b2_nullBodyId;
            }

            b2BodyDef bodyDef;
            bool ok = cbody2d_to_body_def(bodyDef, cbody, spatial);
            bodyDef.userData = reinterpret_cast<void*>(entity);
            if(!ok)
                log::warn("[physics2d] invalid body def: %x", entity);
            else
            {
                b2BodyId id = b2CreateBody(_d->world_id, &bodyDef);
                cbody._body_id = id;

                int idx = 0;
                for(auto &shape: cbody.shapes)
                {
                    ok = shape2d_create(id, shape);
                    if(!ok)
                        log::warn("[physics2d] could not create shape: %x: #%d", entity, idx);

                    ++idx;
                }
                _d->body_entt.emplace(id, entity);
                _d->body_comp.emplace(id, &cbody);
                _d->body_spatial.emplace(id, spatial);
            }
        }
        on_construct_upd.clear();

        // TODO do use fixed timestep, but maybe not every frame
        const int substeps = 4;
        const float time_step = 1.0f/60.0f/substeps;

        if(B2_IS_NON_NULL(_d->world_id))
        {
            b2World_Step(_d->world_id, time_step, substeps);

            b2BodyEvents events = b2World_GetBodyEvents(_d->world_id);
            for (int i = 0; i < events.moveCount; ++i)
            {
                const b2BodyMoveEvent* event = events.moveEvents + i;
                auto it = _d->body_spatial.find(event->bodyId);
                if(it != _d->body_spatial.end())
                {
                    cspatial *spatial = it->second;
                    if(spatial)
                    {
                        spatial->pos = {event->transform.p.x, event->transform.p.y, spatial->pos.z};
                        spatial->rot.z = glm::degrees(b2Rot_GetAngle(event->transform.q));
                        spatial->apply();
                    }
                    else
                        log::warn("[p2d] no spatial: body_id=%lx", b2StoreBodyId(event->bodyId));
                }
            }
        }

        // check all bodies - unneeded?
        //auto view = reg.view<cbody2d>();
        //for(auto [id, body]:view.each())
        //{
        //}
    }
    else if(phase == step_phase::PRE_RENDER)
    {
        if(_d->debug_draw_enabled)
        {
            viewport_geometry* vg = entt::locator<viewport_geometry*>::value();
            if(vg)
            {
                viewport_geometry::extents_2d extents;
                if(vg->get_2d_extents(extents))
                {
                    float cx = (extents.right+extents.left)/2.0f;
                    float cy = (extents.top+extents.bottom)/2.0f;
                    float sx = extents.width/extents.xspan;
                    float sy = extents.height/extents.yspan;
                    physics2d_pre_debug_draw(_d->debug_draw, cx, cy, sx, sy, _d->world_scale, extents.ui_scale);
                    b2World_Draw(_d->world_id, &_d->debug_draw);
                }
            }

        }
    }
    return true;
}

bool physics2d::event(SDL_Event*)
{
    return true;
}

void physics2d::_draw_tool_window(bool *close)
{
    ImGui::Begin(ICON_FK_SQUARE " Physics 2D Tools");
    ImGui::Checkbox("debug draw", &_d->debug_draw_enabled);
    ImGui::Text("World scale: %f", _d->world_scale);
    ImGui::Checkbox("drawShapes", &_d->debug_draw.drawShapes);
    ImGui::Checkbox("drawJoints", &_d->debug_draw.drawJoints);
    ImGui::Checkbox("drawJointExtras", &_d->debug_draw.drawJointExtras);
    ImGui::Checkbox("drawBounds", &_d->debug_draw.drawBounds);
    ImGui::Checkbox("drawMass", &_d->debug_draw.drawMass);
    ImGui::Checkbox("drawBodyNames", &_d->debug_draw.drawBodyNames);
    ImGui::Checkbox("drawContacts", &_d->debug_draw.drawContacts);
    ImGui::Checkbox("drawGraphColors", &_d->debug_draw.drawGraphColors);
    ImGui::Checkbox("drawContactNormals", &_d->debug_draw.drawContactNormals);
    ImGui::Checkbox("drawContactForces", &_d->debug_draw.drawContactForces);
    ImGui::Checkbox("drawContactFeatures", &_d->debug_draw.drawContactFeatures);
    ImGui::Checkbox("drawFrictionImpulses", &_d->debug_draw.drawFrictionForces);
    ImGui::Checkbox("drawIslands", &_d->debug_draw.drawIslands);
    if(ImGui::Button("Hello Box2D!"))
    {
        static bool ground_added = false;
        if(!ground_added)
        {
            b2BodyDef groundBodyDef = b2DefaultBodyDef();
            groundBodyDef.position.x = 0.0f;
            groundBodyDef.position.y = 10.0f;
            b2BodyId groundId = b2CreateBody(_d->world_id, &groundBodyDef);
            b2Polygon groundBox = b2MakeBox(50.0f, 10.0f);
            b2ShapeDef groundShapeDef = b2DefaultShapeDef();
            b2CreatePolygonShape(groundId, &groundShapeDef, &groundBox);
            ground_added = true;
        }

        b2BodyDef bodyDef = b2DefaultBodyDef();
        bodyDef.type = b2_dynamicBody;
        bodyDef.position.x = 0.0f;
        bodyDef.position.y = -75.0f;
        b2BodyId bodyId = b2CreateBody(_d->world_id, &bodyDef);
        b2Polygon dynamicBox = b2MakeBox(10.0f, 10.0f);
        b2ShapeDef shapeDef = b2DefaultShapeDef();
        shapeDef.density = 1.0f;
        shapeDef.material.friction = 0.3f;
        b2CreatePolygonShape(bodyId, &shapeDef, &dynamicBox);
    }
    ImGui::End();
}

void physics2d::set_gravity(glm::vec2 grav)
{
    if(B2_IS_NON_NULL(_d->world_id))
    {
        b2Vec2 gravity;
        gravity.x = grav.x;
        gravity.y = grav.y;
        b2World_SetGravity(_d->world_id, gravity);
    }
}

bool physics2d::body_force(entt::entity ent, glm::vec2 force, glm::vec2 world_point, bool awake)
{
    // apply force to body at point
    auto &reg = engine::instance().default_scene().registry();
    auto *cbody = reg.try_get<cbody2d>(ent);
    if(!cbody)
    {
        log::warn("[physics2d] body_force: no body2d component: %x", ent);
        return false;
    }
    if(!B2_IS_NON_NULL(cbody->_body_id))
    {
        log::warn("[physics2d] body_force: body has no physics body: %x", ent);
        return false;
    }
    b2Vec2 f;
    f.x = force.x;
    f.y = force.y;
    b2Vec2 p;
    p.x = world_point.x;
    p.y = world_point.y;
    log::verb("[physics2d] applying force (%f, %f) at point (%f, %f) to entity body %x", f.x, f.y, p.x, p.y, ent);
    b2Body_ApplyForce(cbody->_body_id, f, p, awake);
    return true;
}

bool physics2d::body_force_center(entt::entity ent, glm::vec2 force, bool awake)
{
    // apply force to body center

    // find entity's body id
    auto &reg = engine::instance().default_scene().registry();
    auto *cbody = reg.try_get<cbody2d>(ent);
    if(!cbody)
    {
        log::warn("[physics2d] body_force_center: no body2d component: %x", ent);
        return false;
    }
    if(!B2_IS_NON_NULL(cbody->_body_id))
    {
        log::warn("[physics2d] body_force_center: body has no physics body: %x", ent);
        return false;
    }
    b2Vec2 f;
    f.x = force.x;
    f.y = force.y;
    log::verb("[physics2d] applying center force (%f, %f) to entity body %x", f.x, f.y, ent);
    b2Body_ApplyForceToCenter(cbody->_body_id, f, true);
    return true;
}

bool physics2d::body_torque(entt::entity ent, float torque, bool awake)
{
    // apply torque to body

    // find entity's body id
    auto &reg = engine::instance().default_scene().registry();
    auto *cbody = reg.try_get<cbody2d>(ent);
    if(!cbody)
    {
        log::warn("[physics2d] body_torque: no body2d component: %x", ent);
        return false;
    }
    if(!B2_IS_NON_NULL(cbody->_body_id))
    {
        log::warn("[physics2d] body_torque: body has no physics body: %x", ent);
        return false;
    }
    log::verb("[physics2d] applying torque %f to entity body %x", torque, ent);
    b2Body_ApplyTorque(cbody->_body_id, torque, true);
    return true;
}

bool physics2d::body_warp(entt::entity ent, glm::vec2 pos)
{
    // instantly set body position, keep current angle
    auto &reg = engine::instance().default_scene().registry();
    auto *cbody = reg.try_get<cbody2d>(ent);
    if(!cbody)
    {
        log::warn("[physics2d] body_warp: no body2d component: %x", ent);
        return false;
    }
    if(!B2_IS_NON_NULL(cbody->_body_id))
    {
        log::warn("[physics2d] body_warp: body has no physics body: %x", ent);
        return false;
    }
    b2Transform xf = b2Body_GetTransform(cbody->_body_id);
    xf.p.x = pos.x;
    xf.p.y = pos.y;
    b2Body_SetTransform(cbody->_body_id, xf.p, xf.q);
    log::verb("[physics2d] warped body %x to (%f, %f)", ent, pos.x, pos.y);
    return true;
}

// RTTI metadata
extern "C" void _rtti_init_physics2d()
{
    entt::meta_factory<nb::physics2d>{}
        .type("physics2d"_hs)
        .custom<rtti::type_info>(rtti::type_info{"physics2d", rtti::TYPE_CLASS_SYSTEM})
        .base<nb::system>();
    entt::meta_factory<std::shared_ptr<nb::physics2d>>{rtti::ctx_systems()}
        .type("physics2d_shared"_hs)
        .ctor<&rtti::shared_ptr_builder<nb::physics2d>>()
        .conv<std::shared_ptr<nb::system>>();

    cbody2d::_ensure_rtti();
}