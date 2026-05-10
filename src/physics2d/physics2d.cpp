#include <newbase/physics2d/physics2d.hpp>
#include <newbase/physics2d/debug_draw.hpp>
#include <newbase/physics2d/conversions.hpp>
#include <newbase/components/body2d.hpp>
#include <newbase/components/character2d.hpp>
#include <newbase/components/spatial.hpp>
#include <newbase/clock/clock.hpp>
#include <newbase/engine.hpp>
#include <newbase/scene.hpp>
#include <newbase/log.hpp>
#include <newbase/services/renderer_service.hpp>
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

    bool debug_draw_enabled {false};
    b2DebugDraw debug_draw {};

    // contact events — populated during PHYSICS_UPDATE, read by scripts during PREPARE
    struct contact_pair { entt::entity a; entt::entity b; };
    std::vector<contact_pair> contact_begins {};

    // signal receiver: called synchronously before cbody2d component data is freed
    void on_body_destroy(entt::registry &reg, entt::entity eid)
    {
        cbody2d &body = reg.get<cbody2d>(eid);
        if(B2_IS_NON_NULL(body._body_id))
        {
            body_entt.erase(body._body_id);
            b2DestroyBody(body._body_id);
            body._body_id = b2_nullBodyId;
        }
    }
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

    int argc = nb::engine::instance().argc();
    char** argv = nb::engine::instance().argv();
    for (int i = 1; i < argc; ++i)
    {
        if (!strcmp(argv[i], "--physics2d-debug-draw"))
        {
            _d->debug_draw_enabled = true;
            break;
        }
    }

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

    engine::instance().debug_action_register("Physics2D Tools", [](){
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
    reg.on_destroy<cbody2d>().connect<&physics2d_p::on_body_destroy>(_d);

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
                if (cbody.initial_linear_velocity  != glm::vec2{0.f})
                    b2Body_SetLinearVelocity(id, {cbody.initial_linear_velocity.x, cbody.initial_linear_velocity.y});
                if (cbody.initial_angular_velocity != 0.f)
                    b2Body_SetAngularVelocity(id, cbody.initial_angular_velocity);

                int idx = 0;
                for(auto &shape: cbody.shapes)
                {
                    ok = shape2d_create(id, shape);
                    if(!ok)
                        log::warn("[physics2d] could not create shape: %x: #%d", entity, idx);

                    ++idx;
                }
                _d->body_entt.emplace(id, entity);
            }
        }
        on_construct_upd.clear();

        const int substeps = 4;
        const float dt = entt::locator<nb::clock*>::has_value()
            ? entt::locator<nb::clock*>::value()->get_dt() : (1.0f/60.0f);

        if(B2_IS_NON_NULL(_d->world_id))
        {
            b2World_Step(_d->world_id, dt, substeps);

            // collect contact begin events for scripts to read next frame
            _d->contact_begins.clear();
            b2ContactEvents contacts = b2World_GetContactEvents(_d->world_id);
            for(int i = 0; i < contacts.beginCount; ++i)
            {
                const b2ContactBeginTouchEvent *ev = &contacts.beginEvents[i];
                b2BodyId ba = b2Shape_GetBody(ev->shapeIdA);
                b2BodyId bb = b2Shape_GetBody(ev->shapeIdB);
                auto ita = _d->body_entt.find(ba);
                auto itb = _d->body_entt.find(bb);
                if(ita != _d->body_entt.end() && itb != _d->body_entt.end())
                    _d->contact_begins.push_back({ita->second, itb->second});
            }

            b2BodyEvents events = b2World_GetBodyEvents(_d->world_id);
            for (int i = 0; i < events.moveCount; ++i)
            {
                const b2BodyMoveEvent* event = events.moveEvents + i;
                auto it = _d->body_entt.find(event->bodyId);
                if(it != _d->body_entt.end())
                {
                    cspatial *spatial = reg.try_get<cspatial>(it->second);
                    if(spatial)
                    {
                        spatial->pos = {event->transform.p.x, event->transform.p.y, spatial->pos.z};
                        spatial->rot.z = glm::degrees(b2Rot_GetAngle(event->transform.q));
                        spatial->apply();
                    }
                }
            }
        }

        // character movers
        _step_characters(reg, dt);
    }
    else if(phase == step_phase::PRE_RENDER)
    {
        if(_d->debug_draw_enabled)
        {
            renderer_service* vg = entt::locator<renderer_service*>::value();
            if(vg)
            {
                renderer_service::extents_2d extents;
                if(vg->get_2d_extents(extents))
                {
                    float cx = (extents.right+extents.left)/2.0f;
                    float cy = (extents.top+extents.bottom)/2.0f;
                    float sx = extents.width/extents.xspan;
                    float sy = extents.height/extents.yspan;
                    const float scr_cx = extents.screen_x / extents.ui_scale + extents.width  / (2.f * extents.ui_scale);
                    const float scr_cy = extents.screen_y / extents.ui_scale + extents.height / (2.f * extents.ui_scale);
                    physics2d_pre_debug_draw(_d->debug_draw, cx, cy, sx, sy, _d->world_scale, extents.ui_scale, scr_cx, scr_cy);
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
    if (!ImGui::Begin(ICON_FK_SQUARE " Physics 2D", close))
    {
        ImGui::End();
        return;
    }

    // World stats
    ImGui::TextDisabled("Scale: %.3f px/m", _d->world_scale);
    if (B2_IS_NON_NULL(_d->world_id))
    {
        b2Counters counters = b2World_GetCounters(_d->world_id);
        ImGui::TextDisabled("Bodies: %d  Contacts: %d  Joints: %d",
            counters.bodyCount, counters.contactCount, counters.jointCount);
    }

    ImGui::Separator();

    // Gravity
    if (B2_IS_NON_NULL(_d->world_id))
    {
        b2Vec2 g = b2World_GetGravity(_d->world_id);
        float gv[2] = {g.x, g.y};
        ImGui::TextUnformatted("Gravity");
        ImGui::SameLine();
        float btn_w = ImGui::CalcTextSize("Reset").x + ImGui::GetStyle().FramePadding.x * 2;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - btn_w - ImGui::GetStyle().ItemSpacing.x);
        if (ImGui::DragFloat2("##gravity", gv, 1.0f))
            b2World_SetGravity(_d->world_id, {gv[0], gv[1]});
        ImGui::SameLine();
        if (ImGui::Button("Reset"))
            set_gravity({0.0f, 9.81f * _d->world_scale});
    }

    ImGui::Separator();

    // Debug draw
    ImGui::Checkbox("Debug draw", &_d->debug_draw_enabled);
    if (!_d->debug_draw_enabled) ImGui::BeginDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Draw features" ICON_FK_CARET_DOWN))
        ImGui::OpenPopup("##drawfeatures");
    if (ImGui::BeginPopup("##drawfeatures"))
    {
        ImGui::Checkbox("Shapes",            &_d->debug_draw.drawShapes);
        ImGui::Checkbox("Joints",            &_d->debug_draw.drawJoints);
        ImGui::Checkbox("Joint extras",      &_d->debug_draw.drawJointExtras);
        ImGui::Checkbox("Bounds",            &_d->debug_draw.drawBounds);
        ImGui::Checkbox("Mass",              &_d->debug_draw.drawMass);
        ImGui::Checkbox("Body names",        &_d->debug_draw.drawBodyNames);
        ImGui::Checkbox("Islands",           &_d->debug_draw.drawIslands);
        ImGui::Separator();
        ImGui::Checkbox("Contacts",          &_d->debug_draw.drawContacts);
        ImGui::Checkbox("Contact normals",   &_d->debug_draw.drawContactNormals);
        ImGui::Checkbox("Contact forces",    &_d->debug_draw.drawContactForces);
        ImGui::Checkbox("Contact features",  &_d->debug_draw.drawContactFeatures);
        ImGui::Checkbox("Friction impulses", &_d->debug_draw.drawFrictionForces);
        ImGui::Separator();
        ImGui::Checkbox("Graph colors",      &_d->debug_draw.drawGraphColors);
        ImGui::EndPopup();
    }
    if (!_d->debug_draw_enabled) ImGui::EndDisabled();

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

void physics2d::reset_gravity()
{
    set_gravity(glm::vec2{0.0f, 9.81f * _d->world_scale});
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
    auto &reg = engine::instance().default_scene().registry();
    auto *cbody = reg.try_get<cbody2d>(ent);
    if(!cbody)
    {
        log::warn("[physics2d] body_warp: no body2d component: %x", ent);
        return false;
    }
    if(!B2_IS_NON_NULL(cbody->_body_id))
    {
        // body not created yet — set cspatial so it is created at the right position
        auto *spatial = reg.try_get<cspatial>(ent);
        if(spatial)
        {
            spatial->pos.x = pos.x;
            spatial->pos.y = pos.y;
            spatial->apply();
        }
        return true;
    }
    b2Transform xf = b2Body_GetTransform(cbody->_body_id);
    xf.p.x = pos.x;
    xf.p.y = pos.y;
    b2Body_SetTransform(cbody->_body_id, xf.p, xf.q);
    log::verb("[physics2d] warped body %x to (%f, %f)", ent, pos.x, pos.y);
    return true;
}

bool physics2d::body_set_velocity(entt::entity ent, glm::vec2 vel)
{
    auto &reg = engine::instance().default_scene().registry();
    auto *cbody = reg.try_get<cbody2d>(ent);
    if(!cbody)
    {
        log::warn("[physics2d] body_set_velocity: no body2d component: %x", ent);
        return false;
    }
    if(!B2_IS_NON_NULL(cbody->_body_id))
    {
        cbody->initial_linear_velocity = vel;
        return true;
    }
    b2Body_SetLinearVelocity(cbody->_body_id, {vel.x, vel.y});
    return true;
}

bool physics2d::body_set_angular_velocity(entt::entity ent, float omega)
{
    auto &reg = engine::instance().default_scene().registry();
    auto *cbody = reg.try_get<cbody2d>(ent);
    if(!cbody)
    {
        log::warn("[physics2d] body_set_angular_velocity: no body2d component: %x", ent);
        return false;
    }
    if(!B2_IS_NON_NULL(cbody->_body_id))
    {
        cbody->initial_angular_velocity = omega;
        return true;
    }
    b2Body_SetAngularVelocity(cbody->_body_id, omega);
    return true;
}

unsigned int physics2d::contact_begins_count() const
{
    return static_cast<unsigned int>(_d->contact_begins.size());
}

entt::entity physics2d::contact_begin_a(unsigned int idx) const
{
    if(idx >= _d->contact_begins.size()) return entt::null;
    return _d->contact_begins[idx].a;
}

entt::entity physics2d::contact_begin_b(unsigned int idx) const
{
    if(idx >= _d->contact_begins.size()) return entt::null;
    return _d->contact_begins[idx].b;
}

struct character_plane_ctx
{
    static constexpr int MAX_PLANES = 64;
    b2CollisionPlane planes[MAX_PLANES];
    b2ShapeId        shape_ids[MAX_PLANES];
    int count = 0;
};

static bool collect_plane(b2ShapeId shapeId, const b2PlaneResult* result, void* ctx)
{
    if (!result->hit) return true;
    auto* pc = static_cast<character_plane_ctx*>(ctx);
    if (pc->count < character_plane_ctx::MAX_PLANES)
    {
        int i = pc->count++;
        b2CollisionPlane& cp = pc->planes[i];
        cp.plane        = result->plane;
        cp.pushLimit    = FLT_MAX;
        cp.push         = 0.0f;
        cp.clipVelocity = true;
        pc->shape_ids[i] = shapeId;
    }
    return true;
}

void physics2d::_step_characters(entt::registry& reg, float dt)
{
    if (!B2_IS_NON_NULL(_d->world_id)) return;

    const b2Vec2 world_gravity = b2World_GetGravity(_d->world_id);

    auto view = reg.view<ccharacter2d, cspatial>();
    for (auto [eid, ch, sp] : view.each())
    {
        // Apply gravity
        ch.velocity.y += world_gravity.y * ch.gravity_scale * dt;

        b2Capsule capsule;
        capsule.center1 = { sp.pos.x, sp.pos.y - ch.capsule_half_height };
        capsule.center2 = { sp.pos.x, sp.pos.y + ch.capsule_half_height };
        capsule.radius  = ch.capsule_radius;

        b2QueryFilter filter = b2DefaultQueryFilter();
        filter.categoryBits = ch.category_bits;
        filter.maskBits     = ch.mask_bits;

        character_plane_ctx plane_ctx;
        b2World_CollideMover(_d->world_id, &capsule, filter, collect_plane, &plane_ctx);

        const b2Vec2 pre_clip_vel { ch.velocity.x, ch.velocity.y };
        const b2Vec2 desired_delta { pre_clip_vel.x * dt, pre_clip_vel.y * dt };
        b2PlaneSolverResult solved = b2SolvePlanes(desired_delta, plane_ctx.planes, plane_ctx.count);

        b2Vec2 clipped = b2ClipVector(pre_clip_vel, plane_ctx.planes, plane_ctx.count);
        ch.velocity = { clipped.x, clipped.y };

        // Grounded when a plane with an upward-facing normal (y < 0 in y-down space) pushed us
        ch.grounded = false;
        for (int i = 0; i < plane_ctx.count; ++i)
        {
            if (plane_ctx.planes[i].plane.normal.y < -0.5f && plane_ctx.planes[i].push > 0.f)
            {
                ch.grounded = true;
                break;
            }
        }

        // Push dynamic bodies: transfer character velocity onto contacted body
        if (ch.push_force > 0.f)
        {
            for (int i = 0; i < plane_ctx.count; ++i)
            {
                b2BodyId body_id = b2Shape_GetBody(plane_ctx.shape_ids[i]);
                if (b2Body_GetType(body_id) != b2_dynamicBody) continue;

                const b2Vec2& n = plane_ctx.planes[i].plane.normal;
                float vel_into = -(pre_clip_vel.x * n.x + pre_clip_vel.y * n.y);
                if (vel_into <= 0.f) continue;

                b2Vec2 cur = b2Body_GetLinearVelocity(body_id);
                // current velocity of body in the push direction (-n)
                float cur_push = -(cur.x * n.x + cur.y * n.y);
                float target_push = vel_into * ch.push_force;
                if (target_push > cur_push)
                {
                    float delta = target_push - cur_push;
                    b2Body_SetLinearVelocity(body_id, b2Vec2{
                        cur.x - n.x * delta,
                        cur.y - n.y * delta
                    });
                }
            }
        }

        sp.pos.x += solved.translation.x;
        sp.pos.y += solved.translation.y;
        sp.apply();

        // Detect sensor overlaps: find sensor bodies the character capsule overlaps after movement
        struct sensor_overlap_ctx {
            entt::entity character_eid;
            const std::unordered_map<b2BodyId, entt::entity>& body_entt;
            std::vector<nb::physics2d_p::contact_pair>& contact_begins;
            static bool callback(b2ShapeId shape_id, void* raw) {
                if (!b2Shape_IsSensor(shape_id)) return true;
                auto* c = static_cast<sensor_overlap_ctx*>(raw);
                b2BodyId body_id = b2Shape_GetBody(shape_id);
                auto it = c->body_entt.find(body_id);
                if (it != c->body_entt.end())
                    c->contact_begins.push_back({c->character_eid, it->second});
                return true;
            }
        };
        b2Vec2 pts[2] = {
            {sp.pos.x, sp.pos.y - ch.capsule_half_height},
            {sp.pos.x, sp.pos.y + ch.capsule_half_height}
        };
        b2ShapeProxy proxy = b2MakeProxy(pts, 2, ch.capsule_radius);
        b2QueryFilter sensor_filter = b2DefaultQueryFilter();
        sensor_filter.categoryBits = ch.category_bits;
        sensor_filter.maskBits     = ch.mask_bits;
        sensor_overlap_ctx sctx { eid, _d->body_entt, _d->contact_begins };
        b2World_OverlapShape(_d->world_id, &proxy, sensor_filter, sensor_overlap_ctx::callback, &sctx);
    }
}

bool physics2d::character_set_velocity(entt::entity ent, glm::vec2 vel)
{
    auto& reg = engine::instance().default_scene().registry();
    auto* ch = reg.try_get<ccharacter2d>(ent);
    if (!ch) return false;
    ch->velocity = vel;
    return true;
}

glm::vec2 physics2d::character_get_velocity(entt::entity ent) const
{
    auto& reg = engine::instance().default_scene().registry();
    auto* ch = reg.try_get<ccharacter2d>(ent);
    return ch ? ch->velocity : glm::vec2{0.f};
}

bool physics2d::character_is_grounded(entt::entity ent) const
{
    auto& reg = engine::instance().default_scene().registry();
    auto* ch = reg.try_get<ccharacter2d>(ent);
    return ch && ch->grounded;
}

bool physics2d::character_warp(entt::entity ent, glm::vec2 pos)
{
    auto& reg = engine::instance().default_scene().registry();
    auto* sp = reg.try_get<cspatial>(ent);
    if (!sp) return false;
    sp->pos.x = pos.x;
    sp->pos.y = pos.y;
    sp->apply();
    auto* ch = reg.try_get<ccharacter2d>(ent);
    if (ch) ch->velocity = {0.f, 0.f};
    return true;
}

glm::vec4 physics2d::raycast(float x1, float y1, float x2, float y2, uint64_t mask) const
{
    if (B2_IS_NULL(_d->world_id)) return {-1.f, 0.f, 0.f, 0.f};
    b2QueryFilter filter = b2DefaultQueryFilter();
    filter.maskBits = mask;
    b2RayResult r = b2World_CastRayClosest(_d->world_id, {x1, y1}, {x2 - x1, y2 - y1}, filter);
    if (!r.hit) return {-1.f, 0.f, 0.f, 0.f};
    return {r.fraction, r.normal.x, r.normal.y, 0.f};
}

// RTTI metadata
extern "C" void _rtti_init_physics2d()
{
    entt::meta_factory<nb::physics2d>{}
        .type("physics2d"_hs)
        .custom<rtti::type_info>(rtti::type_info{"physics2d", rtti::TYPE_CLASS_SYSTEM})
        .base<nb::system>()
        .func<&nb::physics2d::set_gravity>("set_gravity"_hs)
        .custom<rtti::func_info>(rtti::func_info{"set_gravity"})
        .func<&nb::physics2d::reset_gravity>("reset_gravity"_hs)
        .custom<rtti::func_info>(rtti::func_info{"reset_gravity"})
        .func<&nb::physics2d::body_force>("body_force"_hs)
            .custom<rtti::func_info>(rtti::func_info{"body_force"})
        .func<&nb::physics2d::body_force_center>("body_force_center"_hs)
            .custom<rtti::func_info>(rtti::func_info{"body_force_center"})
        .func<&nb::physics2d::body_torque>("body_torque"_hs)
            .custom<rtti::func_info>(rtti::func_info{"body_torque"})
        .func<&nb::physics2d::body_warp>("body_warp"_hs)
            .custom<rtti::func_info>(rtti::func_info{"body_warp"})
        .func<&nb::physics2d::body_set_velocity>("body_set_velocity"_hs)
            .custom<rtti::func_info>(rtti::func_info{"body_set_velocity"})
        .func<&nb::physics2d::body_set_angular_velocity>("body_set_angular_velocity"_hs)
            .custom<rtti::func_info>(rtti::func_info{"body_set_angular_velocity"})
        .func<&nb::physics2d::contact_begins_count>("contact_begins_count"_hs)
            .custom<rtti::func_info>(rtti::func_info{"contact_begins_count"})
        .func<&nb::physics2d::contact_begin_a>("contact_begin_a"_hs)
            .custom<rtti::func_info>(rtti::func_info{"contact_begin_a"})
        .func<&nb::physics2d::contact_begin_b>("contact_begin_b"_hs)
            .custom<rtti::func_info>(rtti::func_info{"contact_begin_b"})
        .func<&nb::physics2d::character_set_velocity>("character_set_velocity"_hs)
            .custom<rtti::func_info>(rtti::func_info{"character_set_velocity"})
        .func<&nb::physics2d::character_get_velocity>("character_get_velocity"_hs)
            .custom<rtti::func_info>(rtti::func_info{"character_get_velocity"})
        .func<&nb::physics2d::character_is_grounded>("character_is_grounded"_hs)
            .custom<rtti::func_info>(rtti::func_info{"character_is_grounded"})
        .func<&nb::physics2d::character_warp>("character_warp"_hs)
            .custom<rtti::func_info>(rtti::func_info{"character_warp"})
        .func<&nb::physics2d::raycast>("raycast"_hs)
            .custom<rtti::func_info>(rtti::func_info{"raycast"});
    entt::meta_factory<std::shared_ptr<nb::physics2d>>{rtti::ctx_systems()}
        .type("physics2d_shared"_hs)
        .ctor<&rtti::shared_ptr_builder<nb::physics2d>>()
        .conv<std::shared_ptr<nb::system>>();

    cbody2d::_ensure_rtti();
    ccharacter2d::_ensure_rtti();
}
